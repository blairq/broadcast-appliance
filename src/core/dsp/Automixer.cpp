// Automixer.cpp
//
// Implementacion del automixer de mesa redonda (spec 7.3, estilo Dugan).
// Objeto COMPARTIDO de niveles: setLevel() (thread de audio, un escritor)
// escribe las energias suavizadas y gainFor() (thread de control/UI, lectores)
// las lee en paralelo. Los niveles viven en std::atomic<float> para que la
// lectura sea lock-free y no haya data race.
//
// RT-safe: setLevel()/gainFor() no asignan, no bloquean ni hacen I/O. Todo el
// estado vive en arrays de tamano fijo (capacidad fija en la construccion, sin
// crecimiento de heap despues).
//
// Modelo Dugan:
//   gain[ch] = env[ch] / sum(env[*])   (normalizacion de energias suavizadas)
// con:
//   * piso en el denominador (todos en silencio -> ganancias 0, nunca NaN);
//   * piso de ganancia minima del "ultimo micro abierto" sostenido durante una
//     ventana de hold (ms) despues de que el canal cae bajo el umbral;
//   * atenuacion por cantidad de micros abiertos (mas abiertos -> a cada uno
//     le toca menos), suavizada con un one-pole para no producir clicks.
//
// El "tiempo" del hold se mide en ticks de setLevel() (proxy de muestra en el
// dominio del thread de audio), no con un reloj de sistema: asi no hay I/O ni
// dependencias de reloj en el camino de audio.

#include "Automixer.hpp"

#include <algorithm>
#include <cmath>

namespace ba::dsp {

namespace {

constexpr double kDefaultFs = 48000.0;
constexpr double kMinDb = -120.0; // piso para dB -> lineal (evita 0/underflow)
constexpr double kEps = 1e-9;
// Piso del denominador (lineal): por debajo de esto se considera "todo en
// silencio" y las ganancias salen a 0 (evita division por cero / NaN).
constexpr float kDenomFloor = 1e-6f;

} // namespace

double Automixer::dbToLin(double db) {
    if (db < kMinDb) {
        db = kMinDb;
    }
    return std::pow(10.0, db / 20.0);
}

float Automixer::onePole(double ms, double fs) {
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

float Automixer::attenuationForOpenCount(int openCount) const {
    // Atenuacion por cantidad de micros abiertos: acota la salida SUMADA.
    //
    // La normalizacion Dugan ya hace que la suma de las ganancias base sea
    // ~1.0 cuando toda la energia esta en micros abiertos. La atenuacion es un
    // factor ADICIONAL que entra en juego cuando hay mas micros abiertos que
    // `maxOpenMics`: a partir de ahi, a cada uno le toca menos, de modo que la
    // salida sumada no crezca con la cantidad de hablantes simultaneos.
    //
    //   openCount <= maxOpenMics  -> 1.0 (sin atenuacion extra)
    //   openCount >  maxOpenMics  -> targetSummedGain * maxOpenMics / openCount
    //
    // Con openCount = maxOpenMics el factor es targetSummedGain (por defecto
    // 1.0, continuo). Con openCount > maxOpenMics cae como 1/openCount, lo que
    // mantiene `openCount * gain` acotado a ~targetSummedGain * maxOpenMics.
    if (openCount <= 0) {
        return 1.0f;
    }
    const int maxOpen = maxOpenMics_.load(std::memory_order_relaxed);
    if (openCount <= maxOpen) {
        return 1.0f;
    }
    double a = targetSummedGain_.load(std::memory_order_relaxed) * static_cast<double>(maxOpen) /
               static_cast<double>(openCount);
    if (a < 0.0) {
        a = 0.0;
    }
    if (a > 1.0) {
        a = 1.0;
    }
    return static_cast<float>(a);
}

Automixer::Automixer(int maxChannels) {
    // Capacidad fija: se reserva una sola vez, sin crecimiento de heap
    // despues. maxChannels <= 0 se toma como 1.
    int cap = (maxChannels > 0) ? maxChannels : 1;
    maxChannels_ = cap;
    env_ = std::make_unique<std::atomic<float>[]>(static_cast<std::size_t>(cap));
    holdUntil_ = std::make_unique<std::atomic<long long>[]>(static_cast<std::size_t>(cap));
    holdLeft_ = std::make_unique<std::atomic<long long>[]>(static_cast<std::size_t>(cap));
    for (int i = 0; i < cap; ++i) {
        env_[static_cast<std::size_t>(i)].store(0.0f);
        holdUntil_[static_cast<std::size_t>(i)].store(-1);
        holdLeft_[static_cast<std::size_t>(i)].store(0);
    }
    tick_.store(0);
    // Coeficientes con los defaults (setParams los recalcula).
    const double fs = fs_.load(std::memory_order_relaxed);
    atk_.store(onePole(attackMs_, fs), std::memory_order_relaxed);
    rel_.store(onePole(releaseMs_, fs), std::memory_order_relaxed);
    attFade_.store(onePole(5.0, fs), std::memory_order_relaxed);
}

void Automixer::setParams(const AutomixerParams& p, double smoothMs) {
    // --- Clamps defensivos de parámetros ---
    const double fs = (p.fs > 0.0) ? p.fs : kDefaultFs;
    fs_.store(fs, std::memory_order_relaxed);

    openThresholdDb_.store(p.openThresholdDb, std::memory_order_relaxed);
    attackMs_ = (p.attackMs >= 0.0) ? p.attackMs : 0.0;
    releaseMs_ = (p.releaseMs >= 0.0) ? p.releaseMs : 0.0;
    holdMs_.store((p.holdMs >= 0.0) ? p.holdMs : 0.0, std::memory_order_relaxed);
    holdFloorDb_.store(p.holdFloorDb, std::memory_order_relaxed);
    maxOpenMics_.store((p.maxOpenMics >= 1) ? p.maxOpenMics : 1, std::memory_order_relaxed);
    targetSummedGain_.store((p.targetSummedGain > 0.0) ? p.targetSummedGain : 1.0, std::memory_order_relaxed);

    // --- Coeficientes one-pole (una vez por cambio de preset) ---
    atk_.store(onePole(attackMs_, fs), std::memory_order_relaxed);
    rel_.store(onePole(releaseMs_, fs), std::memory_order_relaxed);
    attFade_.store(onePole((smoothMs > 0.0) ? smoothMs : 0.0, fs), std::memory_order_relaxed);

    // El objetivo de atenuacion se recalcula cada muestra en setLevel(); no se
    // toca el estado de envolventes para no introducir un click al cambiar de
    // preset.
    attenuationTarget_ = 1.0f;
}

void Automixer::reset() {
    for (int i = 0; i < maxChannels_; ++i) {
        env_[static_cast<std::size_t>(i)].store(0.0f);
        holdUntil_[static_cast<std::size_t>(i)].store(-1);
        holdLeft_[static_cast<std::size_t>(i)].store(0);
    }
    tick_.store(0);
    attenuation_.store(1.0f, std::memory_order_relaxed);
    attenuationTarget_ = 1.0f;
}

int Automixer::maxChannels() const { return maxChannels_; }

void Automixer::setLevel(int ch, float level) {
    if (ch < 0 || ch >= maxChannels_) {
        return; // fuera de rango: no-op
    }
    const std::size_t idx = static_cast<std::size_t>(ch);

    // Nivel lineal (valor absoluto; un nivel negativo se trata como su
    // amplitud). Se clampa a [0, 1] para que la envolvente no crezca sin
    // acotacion.
    float lin = std::fabs(level);
    if (lin > 1.0f) {
        lin = 1.0f;
    }

    // Envolvente one-pole: ataque rapido (sube), liberacion lenta (baja).
    // Mismo estilo que ChannelStrip.
    // Snapshot de los parametros atomicos al entrar: se usan varias veces mas
    // abajo y asi el valor es coherente dentro de esta llamada aunque el thread
    // de control cambie de preset a mitad.
    const float atk = atk_.load(std::memory_order_relaxed);
    const float rel = rel_.load(std::memory_order_relaxed);
    const double fs = fs_.load(std::memory_order_relaxed);
    const double holdMs = holdMs_.load(std::memory_order_relaxed);
    const double openThrDb = openThresholdDb_.load(std::memory_order_relaxed);
    const float attFade = attFade_.load(std::memory_order_relaxed);

    const float prev = env_[idx].load();
    float next;
    if (lin > prev) {
        next = prev + (lin - prev) * atk;
    } else {
        next = prev + (lin - prev) * rel;
    }
    env_[idx].store(next);

    // --- Hold del "ultimo micro abierto" ---
    // El tiempo se mide en ticks de setLevel() (proxy de muestra). holdTicks es
    // la ventana de hold (ms) expresada en ticks.
    const long long tick = tick_.fetch_add(1);
    const long long holdTicks = static_cast<long long>((holdMs / 1000.0) * fs + 0.5);
    const double thrLin = dbToLin(openThrDb);
    if (next > static_cast<float>(thrLin)) {
        // El canal esta abierto: refresca la ventana de hold (se extiende
        // mientras siga abierto).
        holdUntil_[idx].store(tick + holdTicks);
    }
    // Si cae bajo el umbral, holdUntil_ se queda en el ultimo valor: el hold
    // sigue activo hasta que tick supere holdUntil_.

    // --- Atenuacion por cantidad de micros abiertos ---
    // Cuenta los canales abiertos (env > umbral) y suaviza la atenuacion hacia
    // el objetivo con un one-pole (smoothMs) para no producir clicks.
    int openCount = 0;
    for (int i = 0; i < maxChannels_; ++i) {
        if (env_[static_cast<std::size_t>(i)].load() > static_cast<float>(thrLin)) {
            ++openCount;
        }
    }
    attenuationTarget_ = attenuationForOpenCount(openCount);
    float att = attenuation_.load(std::memory_order_relaxed);
    att += (attenuationTarget_ - att) * attFade;
    if (att < 0.0f) {
        att = 0.0f;
    }
    if (att > 1.0f) {
        att = 1.0f;
    }
    attenuation_.store(att, std::memory_order_relaxed);
}

float Automixer::gainFor(int ch) const {
    if (ch < 0 || ch >= maxChannels_) {
        return 0.0f; // fuera de rango
    }
    const std::size_t idx = static_cast<std::size_t>(ch);

    // Lee todas las energias (lock-free) y suma.
    float sum = 0.0f;
    for (int i = 0; i < maxChannels_; ++i) {
        sum += env_[static_cast<std::size_t>(i)].load();
    }

    // Normalizacion Dugan: energia del canal / energia total. Con piso en el
    // denominador: todo en silencio (sum < kDenomFloor) -> gain 0 (nunca NaN).
    float gain = 0.0f;
    if (sum >= kDenomFloor) {
        const float envCh = env_[idx].load();
        gain = envCh / sum;
        if (gain < 0.0f) {
            gain = 0.0f;
        }
        if (gain > 1.0f) {
            gain = 1.0f;
        }
        // Atenuacion por cantidad de micros abiertos (suavizada).
        gain *= attenuation_.load(std::memory_order_relaxed);
    }

    // Piso de ganancia minima del "ultimo micro abierto": si el canal esta en
    // hold (recientemente abierto), no baje de holdFloor. Se aplica AUN cuando
    // el denominador esta en silencio (sum < kDenomFloor): el hold existe para
    // que el micro que acaba de hablar no se corte de golpe a 0 al terminar una
    // frase (su energia cae bajo el piso del denominador justo en ese momento).
    // Sin esto, la normalizacion Dugan lo mandaba a 0 y el hold quedaba inutil.
    const long long tick = tick_.load();
    const long long holdUntil = holdUntil_[idx].load();
    if (holdUntil > tick || holdLeft_[idx].load() > 0) {
        const float floor = static_cast<float>(dbToLin(holdFloorDb_.load(std::memory_order_relaxed)));
        if (gain < floor) {
            gain = floor;
        }
    }

    if (gain < 0.0f) {
        gain = 0.0f;
    }
    if (gain > 1.0f) {
        gain = 1.0f;
    }
    return gain;
}

void Automixer::setLevelBlock(int ch, float rms, int frames) {
    if (ch < 0 || ch >= maxChannels_ || frames <= 0) {
        return;
    }
    const std::size_t idx = static_cast<std::size_t>(ch);
    float lin = std::fabs(rms);
    if (lin > 1.0f) {
        lin = 1.0f;
    }
    // El one-pole aplicado `frames` veces seguidas con la misma entrada:
    // coeficiente equivalente 1 - (1 - c)^frames.
    const auto enBloque = [frames](float c) {
        return static_cast<float>(1.0 - std::pow(1.0 - static_cast<double>(c), static_cast<double>(frames)));
    };
    const float prev = env_[idx].load();
    const float c = enBloque(lin > prev ? atk_.load(std::memory_order_relaxed) : rel_.load(std::memory_order_relaxed));
    const float next = prev + (lin - prev) * c;
    env_[idx].store(next);

    const double fs = fs_.load(std::memory_order_relaxed);
    const double thrLin = dbToLin(openThresholdDb_.load(std::memory_order_relaxed));
    if (next > static_cast<float>(thrLin)) {
        holdLeft_[idx].store(static_cast<long long>((holdMs_.load(std::memory_order_relaxed) / 1000.0) * fs + 0.5));
    } else {
        const long long q = holdLeft_[idx].load() - frames;
        holdLeft_[idx].store(q > 0 ? q : 0);
    }

    int openCount = 0;
    for (int i = 0; i < maxChannels_; ++i) {
        if (env_[static_cast<std::size_t>(i)].load() > static_cast<float>(thrLin)) {
            ++openCount;
        }
    }
    // Varios escritores: el objetivo es local (no `attenuationTarget_`) y la
    // atenuación compartida se acerca a él; si dos la escriben a la vez gana
    // uno, sin consecuencia audible (el siguiente bloque corrige).
    const float target = attenuationForOpenCount(openCount);
    float att = attenuation_.load(std::memory_order_relaxed);
    att += (target - att) * enBloque(attFade_.load(std::memory_order_relaxed));
    attenuation_.store(std::clamp(att, 0.0f, 1.0f), std::memory_order_relaxed);
}

} // namespace ba::dsp
