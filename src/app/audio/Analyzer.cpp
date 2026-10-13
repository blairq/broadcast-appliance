// ============================================================================
// src/app/audio/Analyzer.cpp — ver Analyzer.hpp
// ============================================================================

#include "audio/Analyzer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace ba::app::audio {

namespace {

constexpr double kPi = 3.14159265358979323846;
// Lleva el ruido rosa de Kellet (entrada uniforme) a RMS AES = amp.
constexpr double kPinkNorm = 0.384; // medido (test_audio_dsp)

double toDb(double power) { return power > 1e-14 ? 10.0 * std::log10(power) : -140.0; }
float ampDb(double amp) { return static_cast<float>(amp > 1e-7 ? 20.0 * std::log10(amp) : -140.0); }

// FFT compleja iterativa radix-2 (in place).
void fft(float* re, float* im, int n) {
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double ang = -2 * kPi / len;
        const float wr = static_cast<float>(std::cos(ang)), wi = static_cast<float>(std::sin(ang));
        for (int i = 0; i < n; i += len) {
            float cr = 1, ci = 0;
            for (int k = 0; k < len / 2; ++k) {
                const int a = i + k, b = i + k + len / 2;
                const float tr = re[b] * cr - im[b] * ci;
                const float ti = re[b] * ci + im[b] * cr;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
                const float ncr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = ncr;
            }
        }
    }
}

} // namespace

const std::array<float, Analyzer::kBands>& Analyzer::bandCenters() {
    static const std::array<float, kBands> c{25,   31.5f, 40,   50,   63,   80,   100,   125,   160,   200,
                                             250,  315,   400,  500,  630,  800,  1000,  1250,  1600,  2000,
                                             2500, 3150,  4000, 5000, 6300, 8000, 10000, 12500, 16000, 20000};
    return c;
}

Analyzer::Analyzer() : fftBuf_(kFft * 2, 0.f), window_(kFft), re_(kFft), im_(kFft) {
    for (int i = 0; i < kFft; ++i)
        window_[static_cast<size_t>(i)] = static_cast<float>(0.5 - 0.5 * std::cos(2 * kPi * i / (kFft - 1)));
    setSampleRate(48000);
}

void Analyzer::setSampleRate(double sr) {
    sr_ = sr;
    blockLen_ = static_cast<uint32_t>(sr / 10);
    // K-weighting (BS.1770-4) para cualquier frecuencia de muestreo.
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan(kPi * f0 / sr), Vh = std::pow(10.0, G / 20),
                     Vb = std::pow(Vh, 0.4996667741545416);
        const double a0 = 1 + K / Q + K * K;
        kShelf_.b0 = (Vh + Vb * K / Q + K * K) / a0;
        kShelf_.b1 = 2 * (K * K - Vh) / a0;
        kShelf_.b2 = (Vh - Vb * K / Q + K * K) / a0;
        kShelf_.a1 = 2 * (K * K - 1) / a0;
        kShelf_.a2 = (1 - K / Q + K * K) / a0;
    }
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan(kPi * f0 / sr);
        const double a0 = 1 + K / Q + K * K;
        kHp_.b0 = 1;
        kHp_.b1 = -2;
        kHp_.b2 = 1;
        kHp_.a1 = 2 * (K * K - 1) / a0;
        kHp_.a2 = (1 - K / Q + K * K) / a0;
    }
    reset();
}

void Analyzer::reset() {
    for (auto* b : {&kShelf_, &kHp_})
        for (int c = 0; c < 2; ++c)
            b->z1[c] = b->z2[c] = 0;
    blockPos_ = 0;
    blockK_ = blockXY_ = 0;
    for (int c = 0; c < 2; ++c) {
        blockSq_[c] = blockSum_[c] = 0;
        blockPeak_[c] = maxPeak_[c] = 0;
        sqRing_[c].fill(0);
    }
    kRing_.fill(0);
    xyRing_.fill(0);
    ringPos_ = ringCount_ = 0;
    gateHist_.fill(0);
    gateSum_.fill(0);
    history_.fill(-140);
    historyPos_ = historyCount_ = 0;
    fftPos_ = 0;
    std::fill(fftBuf_.begin(), fftBuf_.end(), 0.f);
    bandAvg_.fill(0);
    bandLong_.fill(0);
    bandLongN_ = 0;
    seconds_ = 0;
    clips_ = 0;
    publish();
}

void Analyzer::feed(const float* const* ch, int channels, uint32_t frames) {
    if (resetRequested_.exchange(false)) reset();
    const float* L = ch[0];
    const float* R = channels > 1 ? ch[1] : ch[0];
    const bool stereo = channels > 1;
    for (uint32_t i = 0; i < frames; ++i) {
        const float l = L[i], r = R[i];
        const double kl = kHp_.run(kShelf_.run(l, 0), 0);
        blockK_ += kl * kl;
        if (stereo) {
            const double kr = kHp_.run(kShelf_.run(r, 1), 1);
            blockK_ += kr * kr;
        }
        blockSq_[0] += static_cast<double>(l) * l;
        blockSq_[1] += static_cast<double>(r) * r;
        blockSum_[0] += l;
        blockSum_[1] += r;
        blockXY_ += static_cast<double>(l) * r;
        const float al = std::fabs(l), ar = std::fabs(r);
        blockPeak_[0] = std::max(blockPeak_[0], al);
        blockPeak_[1] = std::max(blockPeak_[1], ar);
        if (al >= 0.999f || ar >= 0.999f) ++clips_;
        // FFT sobre la mezcla (M).
        fftBuf_[static_cast<size_t>(fftPos_++)] = 0.5f * (l + r);
        if (fftPos_ == kFft) {
            if (bloquesSinLeer_ < 30) fftFrame();
            // Solape del 50 %: la segunda mitad pasa a ser la primera.
            std::memmove(fftBuf_.data(), fftBuf_.data() + kFft / 2, sizeof(float) * (kFft / 2));
            fftPos_ = kFft / 2;
        }
        if (++blockPos_ >= blockLen_) {
            const double n = blockLen_;
            kRing_[static_cast<size_t>(ringPos_)] = blockK_ / n;
            sqRing_[0][static_cast<size_t>(ringPos_)] = blockSq_[0] / n;
            sqRing_[1][static_cast<size_t>(ringPos_)] = blockSq_[1] / n;
            xyRing_[static_cast<size_t>(ringPos_ % 4)] = blockXY_ / n;
            ringPos_ = (ringPos_ + 1) % 30;
            ringCount_ = std::min(ringCount_ + 1, 30);
            // Bloque de 400 ms para la integrada (compuertas).
            if (ringCount_ >= 4) {
                double e = 0;
                for (int k = 1; k <= 4; ++k)
                    e += kRing_[static_cast<size_t>((ringPos_ - k + 30) % 30)];
                e /= 4;
                const double lk = -0.691 + toDb(e);
                if (lk > -70) {
                    const int idx = std::clamp(static_cast<int>((lk + 70) * 10), 0, kHist - 1);
                    ++gateHist_[static_cast<size_t>(idx)];
                    gateSum_[static_cast<size_t>(idx)] += e;
                }
            }
            // Historial: RMS (AES, como rmsDb) de la mezcla del bloque.
            history_[static_cast<size_t>(historyPos_)] =
                static_cast<float>(toDb((blockSq_[0] + blockSq_[1]) / (2 * n)) + 3.0103);
            historyPos_ = (historyPos_ + 1) % kHistory;
            historyCount_ = std::min(historyCount_ + 1, kHistory);
            maxPeak_[0] = std::max(maxPeak_[0], blockPeak_[0]);
            maxPeak_[1] = std::max(maxPeak_[1], blockPeak_[1]);
            seconds_ += n / sr_;
            publish();
            blockPos_ = 0;
            blockK_ = blockXY_ = 0;
            for (int c = 0; c < 2; ++c) {
                blockSq_[c] = blockSum_[c] = 0;
                blockPeak_[c] = 0;
            }
        }
    }
}

void Analyzer::fftFrame() {
    for (int i = 0; i < kFft; ++i) {
        re_[static_cast<size_t>(i)] = fftBuf_[static_cast<size_t>(i)] * window_[static_cast<size_t>(i)];
        im_[static_cast<size_t>(i)] = 0;
    }
    fft(re_.data(), im_.data(), kFft);
    // Potencia de una senoidal = 2 * sum|X|^2 / (N * sum w^2) (Parseval, un lado);
    // referencia 0 dB = senoidal de pico 1 (potencia 0.5).
    double s2 = 0;
    for (float w : window_)
        s2 += static_cast<double>(w) * w;
    const double norm = 2.0 / (kFft * s2) / 0.5;
    const auto& centers = bandCenters();
    const double binHz = sr_ / kFft;
    for (int b = 0; b < kBands; ++b) {
        const double lo = centers[static_cast<size_t>(b)] / std::pow(2.0, 1.0 / 6);
        const double hi = centers[static_cast<size_t>(b)] * std::pow(2.0, 1.0 / 6);
        const int k0 = std::max(1, static_cast<int>(std::ceil(lo / binHz)));
        const int k1 = std::min(kFft / 2 - 1, static_cast<int>(std::floor(hi / binHz)));
        double p = 0;
        for (int k = k0; k <= k1; ++k)
            p += static_cast<double>(re_[static_cast<size_t>(k)]) * re_[static_cast<size_t>(k)] +
                 static_cast<double>(im_[static_cast<size_t>(k)]) * im_[static_cast<size_t>(k)];
        // Bandas mas angostas que un bin (graves): el bin mas cercano.
        if (k1 < k0) {
            const int k = std::clamp(static_cast<int>(std::lround(centers[static_cast<size_t>(b)] / binHz)),
                                     1, kFft / 2 - 1);
            p = static_cast<double>(re_[static_cast<size_t>(k)]) * re_[static_cast<size_t>(k)] +
                static_cast<double>(im_[static_cast<size_t>(k)]) * im_[static_cast<size_t>(k)];
        }
        p *= norm;
        bandAvg_[static_cast<size_t>(b)] = bandAvg_[static_cast<size_t>(b)] * 0.9 + p * 0.1;
        bandLong_[static_cast<size_t>(b)] += p;
    }
    ++bandLongN_;
}

void Analyzer::publish() {
    // Cada 100 ms: ¿alguien leyó desde la última vez? (ver leido_).
    bloquesSinLeer_ = leido_.exchange(false, std::memory_order_relaxed) ? 0 : std::min(bloquesSinLeer_ + 1, 1000);
    Snapshot s;
    s.seconds = seconds_;
    for (int c = 0; c < 2; ++c) {
        s.peakDb[c] = ampDb(blockPeak_[c]);
        s.maxPeakDb[c] = ampDb(maxPeak_[c]);
        double sq = 0;
        const int n = std::min(ringCount_, 3);
        for (int k = 1; k <= n; ++k)
            sq += sqRing_[c][static_cast<size_t>((ringPos_ - k + 30) % 30)];
        s.rmsDb[c] = n > 0 ? static_cast<float>(toDb(sq / n) + 3.0103) : -140.f; // +3: RMS de seno = pico
        s.dc[c] = blockLen_ > 0 ? static_cast<float>(blockSum_[c] / std::max<uint32_t>(blockPos_, 1)) : 0.f;
    }
    auto lufs = [&](int blocks) -> float {
        const int n = std::min(ringCount_, blocks);
        if (n < blocks) return -120.f;
        double e = 0;
        for (int k = 1; k <= n; ++k)
            e += kRing_[static_cast<size_t>((ringPos_ - k + 30) % 30)];
        return static_cast<float>(-0.691 + toDb(e / n));
    };
    s.lufsM = lufs(4);
    s.lufsS = lufs(30);
    {
        uint64_t cnt = 0;
        double sum = 0;
        for (int i = 0; i < kHist; ++i) {
            cnt += gateHist_[static_cast<size_t>(i)];
            sum += gateSum_[static_cast<size_t>(i)];
        }
        if (cnt > 0) {
            const double rel = -0.691 + toDb(sum / static_cast<double>(cnt)) - 10.0;
            const int start = std::clamp(static_cast<int>((rel + 70) * 10), 0, kHist - 1);
            cnt = 0;
            sum = 0;
            for (int i = start; i < kHist; ++i) {
                cnt += gateHist_[static_cast<size_t>(i)];
                sum += gateSum_[static_cast<size_t>(i)];
            }
            if (cnt > 0) s.lufsI = static_cast<float>(-0.691 + toDb(sum / static_cast<double>(cnt)));
        }
    }
    {
        double xy = 0, xx = 0, yy = 0;
        const int n = std::min(ringCount_, 4);
        for (int k = 1; k <= n; ++k) {
            xy += xyRing_[static_cast<size_t>((ringPos_ - k + 4 * 8) % 4)];
            xx += sqRing_[0][static_cast<size_t>((ringPos_ - k + 30) % 30)];
            yy += sqRing_[1][static_cast<size_t>((ringPos_ - k + 30) % 30)];
        }
        s.correlation = (xx > 1e-12 && yy > 1e-12) ? static_cast<float>(xy / std::sqrt(xx * yy)) : 1.f;
    }
    s.clips = clips_;
    for (int b = 0; b < kBands; ++b) {
        s.bandsDb[b] = static_cast<float>(toDb(bandAvg_[static_cast<size_t>(b)]));
        s.bandsLongDb[b] = bandLongN_ > 0
                               ? static_cast<float>(toDb(bandLong_[static_cast<size_t>(b)] / bandLongN_))
                               : -140.f;
    }
    s.historyCount = historyCount_;
    for (int i = 0; i < historyCount_; ++i)
        s.history[i] = history_[static_cast<size_t>((historyPos_ - historyCount_ + i + kHistory) % kHistory)];

    seq_.fetch_add(1, std::memory_order_acq_rel); // impar: escribiendo
    pub_ = s;
    seq_.fetch_add(1, std::memory_order_release);
}

Analyzer::Snapshot Analyzer::snapshot() const {
    leido_.store(true, std::memory_order_relaxed);
    Snapshot s;
    for (int tries = 0; tries < 100; ++tries) {
        const uint32_t a = seq_.load(std::memory_order_acquire);
        if (a & 1u) continue;
        s = pub_;
        std::atomic_thread_fence(std::memory_order_acquire);
        if (seq_.load(std::memory_order_relaxed) == a) break;
    }
    return s;
}

nlohmann::json Analyzer::report(const Snapshot& s) {
    using json = nlohmann::json;
    auto r1 = [](double v) { return std::round(v * 10) / 10; };
    json out;
    out["segundos"] = r1(s.seconds);
    out["picoDbfs"] = r1(std::max(s.peakDb[0], s.peakDb[1]));
    out["picoMaximoDbfs"] = r1(std::max(s.maxPeakDb[0], s.maxPeakDb[1]));
    out["rmsDbfs"] = {r1(s.rmsDb[0]), r1(s.rmsDb[1])};
    out["lufs"] = {{"momentaneo", r1(s.lufsM)}, {"corto", r1(s.lufsS)}, {"integrado", r1(s.lufsI)}};
    out["recortes"] = s.clips;
    out["correlacionEstereo"] = r1(s.correlation);
    // Piso de ruido y nivel activo: percentiles del historial de 100 ms.
    std::vector<float> h(s.history, s.history + s.historyCount);
    double noise = -140, active = -140;
    if (h.size() >= 10) {
        std::sort(h.begin(), h.end());
        noise = h[h.size() / 10];
        active = h[h.size() * 9 / 10];
        out["pisoDeRuidoDbfs"] = r1(noise);
        out["nivelActivoDbfs"] = r1(active);
        out["rangoDinamicoDb"] = r1(active - noise);
    }
    // Espectro: bandas y zonas (graves, cuerpo, presencia, sibilancia, aire).
    json bands = json::object();
    const auto& c = bandCenters();
    for (int b = 0; b < kBands; ++b) {
        const float hz = c[static_cast<size_t>(b)];
        bands[hz >= 1000 ? std::to_string(static_cast<int>(hz / 100) / 10.0).substr(0, 4) + "k"
                         : std::to_string(static_cast<int>(hz))] = r1(s.bandsDb[b]);
    }
    out["espectroTercios"] = bands;
    auto zone = [&](double lo, double hi) {
        double p = 0;
        for (int b = 0; b < kBands; ++b)
            if (c[static_cast<size_t>(b)] >= lo && c[static_cast<size_t>(b)] <= hi)
                p += std::pow(10.0, s.bandsDb[b] / 10);
        return toDb(p);
    };
    const double rumble = zone(20, 80), body = zone(100, 315), mid = zone(400, 2500),
                 presence = zone(3150, 5000), sib = zone(6300, 10000), air = zone(12500, 20000);
    out["zonasDb"] = {{"graves_20-80Hz", r1(rumble)},  {"cuerpo_100-315Hz", r1(body)},
                      {"medios_400-2500Hz", r1(mid)},  {"presencia_3-5kHz", r1(presence)},
                      {"sibilancia_6-10kHz", r1(sib)}, {"aire_12-20kHz", r1(air)}};
    // Pistas (heuristicas simples; el asistente decide).
    json hints = json::array();
    const float peak = std::max(s.maxPeakDb[0], s.maxPeakDb[1]);
    if (s.seconds <= 0)
        hints.push_back(
            "No llega audio de la fuente: ¿esta en la escena al aire, reproduciendose o conectada?");
    else if (s.seconds < 3)
        hints.push_back("Poco audio analizado: esperar unos segundos mas.");
    if (s.clips > 0 || peak > -0.5f)
        hints.push_back("Satura (picos en 0 dBFS): bajar la ganancia o poner un limitador.");
    if (active > -140 && active < -35) hints.push_back("Nivel bajo: subir la ganancia de entrada.");
    if (noise > -50 && active - noise < 25)
        hints.push_back(
            "Ruido de fondo alto respecto de la señal: una compuerta (gate) o reducción de ruido ayudaría.");
    else if (noise > -140 && active - noise >= 25)
        hints.push_back("Para una compuerta: umbral entre el piso de ruido y el nivel activo (unos 6-10 dB "
                        "sobre el ruido).");
    if (mid > -140 && rumble - mid > -6)
        hints.push_back("Mucho grave (<80 Hz): un filtro pasa-altos (80-100 Hz) limpia retumbe.");
    if (mid > -140 && sib - mid > -8)
        hints.push_back("Sibilancia fuerte (6-10 kHz): un de-esser la controla.");
    if (mid > -140 && body - mid > 6)
        hints.push_back("Cuerpo/embarrado (100-315 Hz) alto: bajar 2-4 dB con un EQ en 200-300 Hz.");
    if (s.correlation < 0)
        hints.push_back("Correlación estéreo negativa: posible fase invertida en un canal.");
    if (std::fabs(s.dc[0]) > 0.01f || std::fabs(s.dc[1]) > 0.01f)
        hints.push_back("Hay DC: un pasa-altos lo quita.");
    out["pistas"] = hints;
    return out;
}

// --- Generador ------------------------------------------------------------------------

bool Generator::parseKind(const std::string& s, Kind& k) {
    if (s == "silencio" || s == "silence")
        k = Kind::Silence;
    else if (s == "seno" || s == "sine")
        k = Kind::Sine;
    else if (s == "blanco" || s == "white")
        k = Kind::White;
    else if (s == "rosa" || s == "pink")
        k = Kind::Pink;
    else if (s == "barrido" || s == "sweep")
        k = Kind::Sweep;
    else
        return false;
    return true;
}

const char* Generator::kindName(Kind k) {
    switch (k) {
        case Kind::Silence: return "silencio";
        case Kind::Sine: return "seno";
        case Kind::White: return "blanco";
        case Kind::Pink: return "rosa";
        case Kind::Sweep: return "barrido";
    }
    return "?";
}

void Generator::configure(const Config& c) {
    seq_.fetch_add(1, std::memory_order_acq_rel);
    pending_ = c;
    seq_.fetch_add(1, std::memory_order_release);
}

Generator::Config Generator::config() const { return pending_; }

void Generator::render(float* const* ch, int channels, uint32_t frames) {
    const uint32_t s = seq_.load(std::memory_order_acquire);
    if ((s & 1u) == 0 && s != seen_) {
        cur_ = pending_;
        if (seq_.load(std::memory_order_acquire) == s) {
            seen_ = s;
            t_ = 0;
        }
    }
    const double amp = std::pow(10.0, cur_.levelDb / 20);
    for (uint32_t i = 0; i < frames; ++i) {
        float v = 0;
        switch (cur_.kind) {
            case Kind::Silence: break;
            case Kind::Sine:
                v = static_cast<float>(amp * std::sin(phase_));
                phase_ += 2 * kPi * cur_.freq / sr_;
                break;
            case Kind::White: {
                rng_ = rng_ * 1664525u + 1013904223u;
                // Uniforme en [-1,1): RMS 1/sqrt(3). Nivel en dBFS RMS (AES: el
                // RMS de un seno es igual a su pico) -> RMS real = amp/sqrt(2).
                v = static_cast<float>(amp * std::sqrt(1.5) * ((rng_ >> 8) / 8388608.0 - 1.0));
                break;
            }
            case Kind::Pink: {
                rng_ = rng_ * 1664525u + 1013904223u;
                const double w = (rng_ >> 8) / 8388608.0 - 1.0;
                // Filtro de Paul Kellet (rosa, -3 dB/oct).
                pink_[0] = 0.99886 * pink_[0] + w * 0.0555179;
                pink_[1] = 0.99332 * pink_[1] + w * 0.0750759;
                pink_[2] = 0.96900 * pink_[2] + w * 0.1538520;
                pink_[3] = 0.86650 * pink_[3] + w * 0.3104856;
                pink_[4] = 0.55000 * pink_[4] + w * 0.5329522;
                pink_[5] = -0.7616 * pink_[5] - w * 0.0168980;
                const double p =
                    pink_[0] + pink_[1] + pink_[2] + pink_[3] + pink_[4] + pink_[5] + pink_[6] + w * 0.5362;
                pink_[6] = w * 0.115926;
                v = static_cast<float>(amp * p * kPinkNorm);
                break;
            }
            case Kind::Sweep: {
                const double T = std::max(0.5, cur_.seconds);
                const double f1 = std::max(1.0, cur_.freq), f2 = std::max(f1 + 1, cur_.freqEnd);
                const double f = f1 * std::pow(f2 / f1, std::fmod(t_, T) / T);
                v = static_cast<float>(amp * std::sin(phase_));
                phase_ += 2 * kPi * f / sr_;
                t_ += 1.0 / sr_;
                break;
            }
        }
        if (phase_ > 2 * kPi) phase_ -= 2 * kPi;
        for (int c = 0; c < channels; ++c)
            ch[c][i] = v;
    }
}

} // namespace ba::app::audio
