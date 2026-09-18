// ChannelStrip.cpp
//
// Implementacion del channel strip nativo (spec 7.2). Cadena fija:
//
//   HPF (cascada 1/2/3 high-pass) -> EQ 4 bandas (RBJ) -> de-esser
//   (banda partida) -> expander/gate -> compresor (peak, knee suave)
//   -> limitador (lookahead).
//
// RT-safe: process() no asigna, no bloquea ni hace I/O. Todo el estado vive
// en miembros fijos (arrays de biquads, buffers de lookahead de tamano fijo).
//
// Suavizado de preset: cada ganancia dinamica (de-esser, gate, comp, lim) se
// interpola con un one-pole hacia su objetivo a lo largo de smoothMs, de modo
// que un cambio de preset no produzca clicks. Los coeficientes de filtro se
// recalculan al instante (el transitorio de un cambio de coeficiente es
// despreciable frente al fade de ganancia).

#include "ChannelStrip.hpp"

#include <algorithm>
#include <cmath>

namespace ba::dsp {

namespace {

constexpr double kDefaultFs = 48000.0;
constexpr double kMinDb = -120.0; // piso para linToDb (evita -inf)
constexpr double kEps = 1e-9;

} // namespace

double ChannelStrip::dbToLin(double db) { return std::pow(10.0, db / 20.0); }

double ChannelStrip::linToDb(double lin) {
    if (lin < kEps) {
        return kMinDb;
    }
    return 20.0 * std::log10(lin);
}

float ChannelStrip::onePole(double ms, double fs) {
    if (ms <= 0.0) {
        return 1.0f; // cambio inmediato
    }
    const double tau = ms / 1000.0; // segundos
    const double dt = 1.0 / fs;
    // Coeficiente exacto de un one-pole: 1 - exp(-dt/tau), en [0,1).
    const double c = 1.0 - std::exp(-dt / tau);
    if (c < 0.0) {
        return 0.0f;
    }
    if (c > 1.0) {
        return 1.0f;
    }
    return static_cast<float>(c);
}

ChannelStrip::ChannelStrip() {
    // Estado inicial neutro: HPF 100 Hz 18 dB/oct (2 etapas), EQ desactivado,
    // de-esser/gate desactivados, comp y lim activos. Los biquads nacen
    // neutros (b0=1, resto 0) gracias a los defaults de Biquad, de modo que
    // antes del primer setParams() el strip es pasabanda.
    hpfStages_ = 2;
    fs_ = kDefaultFs;
}

void ChannelStrip::setParams(const StripParams& p, double smoothMs) {
    // --- Clamps defensivos de parámetros ---
    double fs = (p.fs > 0.0) ? p.fs : kDefaultFs;
    fs_ = fs;

    double hpfFreq = (p.hpfFreq > 0.0) ? p.hpfFreq : 100.0;
    int stages = 2; // 18 dB/oct por defecto
    if (p.hpfSlopeDbOct <= 12.0) {
        stages = 1;
    } else if (p.hpfSlopeDbOct >= 24.0) {
        stages = 3;
    } else {
        stages = 2;
    }
    hpfStages_ = stages;

    // --- HPF: cascada de high-pass Butterworth ---
    for (int i = 0; i < 3; ++i) {
        if (i < stages) {
            hpfL_[i].setParams(BiquadType::HighPass, fs, hpfFreq, 0.0, 0.7071);
            hpfR_[i].setParams(BiquadType::HighPass, fs, hpfFreq, 0.0, 0.7071);
        } else {
            // Etapa inactiva: la dejamos en un high-pass muy por debajo del
            // rango audible para que sea (casi) neutra. Con freq clamped a 1 Hz
            // el high-pass pasa practicamente todo.
            hpfL_[i].setParams(BiquadType::HighPass, fs, 1.0, 0.0, 0.7071);
            hpfR_[i].setParams(BiquadType::HighPass, fs, 1.0, 0.0, 0.7071);
        }
    }

    // --- EQ 4 bandas ---
    for (int i = 0; i < 4; ++i) {
        const auto& b = p.eq[i];
        eqEnabled_[i] = b.enabled;
        double q = (b.q > 0.0) ? b.q : 1.0;
        if (b.enabled) {
            eqL_[i].setParams(b.type, fs, b.freq, b.gainDb, q);
            eqR_[i].setParams(b.type, fs, b.freq, b.gainDb, q);
        } else {
            // Banda desactivada: ganancia 0 dB (neutro) para no alterar.
            eqL_[i].setParams(b.type, fs, b.freq, 0.0, q);
            eqR_[i].setParams(b.type, fs, b.freq, 0.0, q);
        }
    }

    // --- De-esser ---
    deEsserEnabled_ = p.deEsserEnabled;
    deEsserRatio_ = (p.deEsserRatio >= 1.0) ? p.deEsserRatio : 1.0;
    deEsserThresholdDb_ = p.deEsserThresholdDb;
    double deFreq = (p.deEsserFreq > 0.0) ? p.deEsserFreq : 6500.0;
    // Q de la banda de sidechain: banda estrecha (~octava) alrededor de la
    // frecuencia de sibilancia.
    deEsserBpL_.setParams(BiquadType::BandPass, fs, deFreq, 0.0, 1.4);
    deEsserBpR_.setParams(BiquadType::BandPass, fs, deFreq, 0.0, 1.4);

    // --- Gate ---
    gateEnabled_ = p.gateEnabled;
    gateThresholdDb_ = p.gateThresholdDb;
    gateRangeDb_ = (p.gateRangeDb >= 0.0) ? p.gateRangeDb : 0.0;
    gateAttackMs_ = (p.gateAttackMs >= 0.0) ? p.gateAttackMs : 0.0;
    gateReleaseMs_ = (p.gateReleaseMs >= 0.0) ? p.gateReleaseMs : 0.0;

    // --- Compresor ---
    compEnabled_ = p.compEnabled;
    compRatio_ = (p.compRatio >= 1.0) ? p.compRatio : 1.0;
    compThresholdDb_ = p.compThresholdDb;
    compKneeDb_ = (p.compKneeDb >= 0.0) ? p.compKneeDb : 0.0;
    compAttackMs_ = (p.compAttackMs >= 0.0) ? p.compAttackMs : 0.0;
    compReleaseMs_ = (p.compReleaseMs >= 0.0) ? p.compReleaseMs : 0.0;

    // --- Limitador ---
    limEnabled_ = p.limEnabled;
    limCeilingDb_ = p.limCeilingDb;
    int la = p.limLookaheadSamples;
    if (la < 0) {
        la = 0;
    }
    if (la > kMaxLookahead) {
        la = kMaxLookahead;
    }
    limLookahead_ = la;

    // --- Suavizado de ganancia (one-pole sobre smoothMs) ---
    // Con smoothMs<=0 el fade es inmediato (fade=1).
    fade_ = onePole((smoothMs > 0.0) ? smoothMs : 0.0, fs);

    // Los objetivos de ganancia se recalculan cada muestra en process(); no se
    // toca el estado de envolventes para no introducir un click al cambiar de
    // preset.
    deEsserGainTarget_ = 1.0f;
    gateGainTarget_ = 1.0f;
    compGainTarget_ = 1.0f;
}

void ChannelStrip::reset() {
    for (int i = 0; i < 3; ++i) {
        hpfL_[i].reset();
        hpfR_[i].reset();
    }
    for (int i = 0; i < 4; ++i) {
        eqL_[i].reset();
        eqR_[i].reset();
    }
    deEsserBpL_.reset();
    deEsserBpR_.reset();
    deEsserEnv_ = 0.0f;
    deEsserGain_ = 1.0f;
    deEsserGainTarget_ = 1.0f;
    gateEnv_ = 0.0f;
    gateGain_ = 1.0f;
    gateGainTarget_ = 1.0f;
    compEnv_ = 0.0f;
    compGain_ = 1.0f;
    compGainTarget_ = 1.0f;
    limGain_ = 1.0f;
    limBufPos_ = 0;
    limBufFill_ = 0;
    for (int i = 0; i < kMaxLookahead; ++i) {
        limBufL_[i] = 0.0f;
        limBufR_[i] = 0.0f;
    }
}

void ChannelStrip::process(float* interleaved, int n) {
    if (interleaved == nullptr || n <= 0) {
        return;
    }

    // Coeficientes one-pole de las envolventes (una vez por bloque).
    const float deAtk = onePole(1.0, fs_);  // ataque de-esser (rapido)
    const float deRel = onePole(10.0, fs_); // liberacion de-esser (lenta)
    const float gateAtk = onePole(gateAttackMs_, fs_);
    const float gateRel = onePole(gateReleaseMs_, fs_);
    const float compAtk = onePole(compAttackMs_, fs_);
    const float compRel = onePole(compReleaseMs_, fs_);

    // Umbrales/techo en lineal (una vez por bloque).
    const float deThrLin = static_cast<float>(dbToLin(deEsserThresholdDb_));
    const float gateThrLin = static_cast<float>(dbToLin(gateThresholdDb_));
    const float compThrLin = static_cast<float>(dbToLin(compThresholdDb_));
    const float limCeilLin = static_cast<float>(dbToLin(limCeilingDb_));
    const float ratio = static_cast<float>(compRatio_);
    const float knee = compKneeDb_;
    const float rangeDb = gateRangeDb_;
    const float thrDb = gateThresholdDb_;

    for (int i = 0; i < n; ++i) {
        float* L = interleaved + (2 * i);
        float* R = interleaved + (2 * i + 1);

        // === 1) HPF (cascada) ===
        for (int s = 0; s < hpfStages_; ++s) {
            *L = hpfL_[s].process(*L);
            *R = hpfR_[s].process(*R);
        }

        // === 2) EQ 4 bandas ===
        for (int b = 0; b < 4; ++b) {
            if (eqEnabled_[b]) {
                *L = eqL_[b].process(*L);
                *R = eqR_[b].process(*R);
            }
        }

        // === 3) De-esser (banda partida, sidechain) ===
        if (deEsserEnabled_) {
            // Sidechain: band-pass de cada canal (biquads separados por
            // estado), envolvente peak comun.
            const float bandL = deEsserBpL_.process(*L);
            const float bandR = deEsserBpR_.process(*R);
            const float absL = std::fabs(bandL);
            const float absR = std::fabs(bandR);
            const float peak = std::max(absL, absR);
            // Envolvente peak: ataque rapido, liberacion lenta.
            if (peak > deEsserEnv_) {
                deEsserEnv_ += (peak - deEsserEnv_) * deAtk;
            } else {
                deEsserEnv_ += (peak - deEsserEnv_) * deRel;
            }
            // Ganancia de compresion sobre la banda: si la envolvente supera
            // el umbral, se reduce segun el ratio.
            float g = 1.0f;
            if (deEsserEnv_ > deThrLin) {
                const float over = deEsserEnv_ / deThrLin; // > 1
                // gain = over^(1/ratio - 1)  (compresion de banda)
                g = std::pow(over, 1.0f / static_cast<float>(deEsserRatio_) - 1.0f);
                if (g < 0.01f) {
                    g = 0.01f;
                }
            }
            deEsserGainTarget_ = g;
            // Suaviza la ganancia de de-esser (one-pole) para click-free.
            deEsserGain_ += (deEsserGainTarget_ - deEsserGain_) * fade_;
            // Split-band: out = x - band + band*gain (solo se comprime la
            // banda de sibilancia, el resto de la senal no se toca).
            *L = *L - bandL + bandL * deEsserGain_;
            *R = *R - bandR + bandR * deEsserGain_;
        }

        // === 4) Expander / gate ===
        if (gateEnabled_) {
            const float absL = std::fabs(*L);
            const float absR = std::fabs(*R);
            const float peak = std::max(absL, absR);
            // Envolvente peak: ataque rapido, liberacion lenta.
            if (peak > gateEnv_) {
                gateEnv_ += (peak - gateEnv_) * gateAtk;
            } else {
                gateEnv_ += (peak - gateEnv_) * gateRel;
            }
            // Ganancia: si el nivel esta bajo el umbral, se atenua hasta
            // gateRangeDb. Lineal entre umbral y (umbral - range).
            float g = 1.0f;
            // rangeDb == 0 significa "gate sin atenuacion": no hay nada que
            // reducir. Hay que cortocircuitarlo ANTES de dividir: con la
            // envolvente justo en el umbral la division seria 0/0 = NaN, y los
            // clamps de abajo son ambos FALSOS para NaN, asi que el NaN llegaba
            // a gateGain_ y el one-pole lo propagaba para siempre (canal mudo
            // hasta un reset()).
            if (gateEnv_ < gateThrLin && rangeDb > static_cast<float>(kEps)) {
                const float envDb = linToDb(gateEnv_);
                float frac = (thrDb - envDb) / rangeDb;
                if (!(frac > 0.0f)) { // cubre tambien NaN
                    frac = 0.0f;
                }
                if (frac > 1.0f) {
                    frac = 1.0f;
                }
                g = static_cast<float>(dbToLin(-rangeDb * frac));
            }
            gateGainTarget_ = g;
            gateGain_ += (gateGainTarget_ - gateGain_) * fade_;
            *L *= gateGain_;
            *R *= gateGain_;
        }

        // === 5) Compresor (peak, knee suave) ===
        if (compEnabled_) {
            const float absL = std::fabs(*L);
            const float absR = std::fabs(*R);
            const float peak = std::max(absL, absR);
            // Envolvente peak: ataque/liberacion segun parametros.
            if (peak > compEnv_) {
                compEnv_ += (peak - compEnv_) * compAtk;
            } else {
                compEnv_ += (peak - compEnv_) * compRel;
            }
            // Ganancia de compresion con knee suave (soft knee).
            float g = 1.0f;
            if (compEnv_ > compThrLin) {
                const float x = linToDb(compEnv_) - compThresholdDb_; // dB sobre umbral
                float gainReductionDb;
                if (knee <= 0.0f) {
                    // Hard knee: compresion completa por encima del umbral.
                    gainReductionDb = x * (1.0f - 1.0f / ratio);
                } else if (x >= knee) {
                    // Por encima del knee: compresion completa.
                    gainReductionDb = (x - knee) * (1.0f - 1.0f / ratio);
                } else if (x <= -knee) {
                    // Por debajo del knee: sin compresion.
                    gainReductionDb = 0.0f;
                } else {
                    // Dentro del knee: parabolica suave.
                    gainReductionDb = ((x + knee) * (x + knee) / (4.0f * knee)) * (1.0f - 1.0f / ratio);
                }
                g = static_cast<float>(dbToLin(-gainReductionDb));
            }
            compGainTarget_ = g;
            compGain_ += (compGainTarget_ - compGain_) * fade_;
            *L *= compGain_;
            *R *= compGain_;
        }

        // === 6) Limitador (lookahead) ===
        if (limEnabled_) {
            // Lookahead: la muestra se escribe en el buffer circular y se
            // emite la muestra de `limLookahead_` pasos atras, cuyo pico en la
            // ventana de lookahead (las muestras que la siguen) ya se conoce.
            limBufL_[limBufPos_] = *L;
            limBufR_[limBufPos_] = *R;

            const int outIdx = (limBufPos_ - limLookahead_ + kMaxLookahead) % kMaxLookahead;
            // Pico en la ventana de lookahead (de outIdx a la muestra actual).
            float peak = 0.0f;
            for (int k = 0; k < limLookahead_; ++k) {
                const int idx = (outIdx + k) % kMaxLookahead;
                const float aL = std::fabs(limBufL_[idx]);
                const float aR = std::fabs(limBufR_[idx]);
                if (aL > peak) {
                    peak = aL;
                }
                if (aR > peak) {
                    peak = aR;
                }
            }
            // Ganancia de limitacion: si el pico supera el techo, reduce.
            float g = 1.0f;
            if (peak > limCeilLin) {
                g = limCeilLin / peak;
                if (g < 0.01f) {
                    g = 0.01f;
                }
            }
            // Suaviza la ganancia del limitador (one-pole) para click-free.
            limGain_ += (g - limGain_) * fade_;

            const float outL = limBufL_[outIdx] * limGain_;
            const float outR = limBufR_[outIdx] * limGain_;

            limBufPos_ = (limBufPos_ + 1) % kMaxLookahead;
            if (limBufFill_ < kMaxLookahead) {
                ++limBufFill_;
            }

            *L = outL;
            *R = outR;
        }
    }
}

float ChannelStrip::lastGainDb() const {
    // Suma en dB de las ganancias dinámicas aplicadas (gate + comp + lim).
    // El de-esser se excluye porque actua sobre la banda de sidechain, no
    // sobre la senal completa; el spec pide gate+comp+lim.
    double db = 0.0;
    db += linToDb(static_cast<double>(gateGain_));
    db += linToDb(static_cast<double>(compGain_));
    db += linToDb(static_cast<double>(limGain_));
    return static_cast<float>(db);
}

} // namespace ba::dsp
