// ============================================================================
// src/app/audio/MonitorVoz.cpp — ver MonitorVoz.hpp
// ============================================================================
#include "audio/MonitorVoz.hpp"

#include <obs.h>
#include <util/platform.h>

#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

namespace ba::app::audio {

namespace {

constexpr const char* kTipo = "ba_voz_asistente";
constexpr std::uint32_t kBloque = 1024; // frames por paquete

const char* nombre(void*) { return "Voz del sistema"; }
// Su canal de salida (0 programa, 1-16 invitados, 17-24 micrófonos, 40-55
// entradas virtuales, 62-63 las voces): estar en un canal es lo que permite mezclarla.
// La fuente no tiene estado propio: el audio se lo empuja MonitorVoz.
void* crear(obs_data_t*, obs_source_t* s) { return s; }
void destruir(void*) {}

obs_source_info g_info = {};

} // namespace

void registrarMonitorVoz() {
    g_info.id = kTipo;
    g_info.type = OBS_SOURCE_TYPE_INPUT;
    // Sin OBS_SOURCE_CAP_*: no aparece para agregar a una escena.
    g_info.output_flags = OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE | OBS_SOURCE_CAP_DISABLED;
    g_info.get_name = nombre;
    g_info.create = crear;
    g_info.destroy = destruir;
    g_info.icon_type = OBS_ICON_TYPE_AUDIO_OUTPUT;
    obs_register_source(&g_info);
}

MonitorVoz::~MonitorVoz() {
    std::lock_guard<std::mutex> l(lock_);
    // Si libobs ya se apagó (un cierre temprano, p. ej. la interfaz no
    // cargó), la fuente ya no existe: tocarla es un acceso inválido.
    if (fuente_ != nullptr && obs_initialized()) {
        obs_set_output_source(canal_, nullptr);
        obs_source_set_monitoring_type(fuente_, OBS_MONITORING_TYPE_NONE);
        obs_source_release(fuente_);
        fuente_ = nullptr;
    }
}

bool MonitorVoz::asegurar() {
    if (fuente_ != nullptr) return true;
    if (!obs_initialized()) return false;
    fuente_ = obs_source_create(kTipo, nombre_.c_str(), nullptr, nullptr);
    if (fuente_ == nullptr) return false;
    // De fábrica: en los auriculares del operador y en ninguna pista (no va
    // al aire). La Matriz le restaura el ruteo que el operador le haya dado.
    obs_source_set_audio_mixers(fuente_, 0);
    // «Solo monitor» mientras no tenga pistas; la Matriz lo pasa a «monitor y
    // salida» si se la rutea (MONITOR_ONLY la saca de la mezcla).
    obs_source_set_monitoring_type(fuente_, OBS_MONITORING_TYPE_MONITOR_ONLY);
    obs_source_set_muted(fuente_, false);
    obs_set_output_source(canal_, fuente_);
    return true;
}

void MonitorVoz::preparar() {
    std::lock_guard<std::mutex> l(lock_);
    asegurar();
}

void MonitorVoz::reproducir(const float* muestras, std::size_t n, std::uint32_t frecuencia) {
    if (muestras == nullptr || n == 0 || frecuencia == 0) return;
    std::lock_guard<std::mutex> l(lock_);
    if (!asegurar()) return;
    // Pegado a lo que ya está sonando; si no suena nada, un poco adelante de
    // ahora (lo que necesita libobs para no tirarlo por viejo).
    const std::uint64_t ahora = os_gettime_ns();
    std::uint64_t ts = std::max(proximo_, ahora + 50'000'000ULL);
    // Si la callaron, lo nuevo va después de lo que quedó encolado (que suena
    // silenciado), y se le devuelve el sonido justo cuando termina.
    if (silencioHasta_ != 0) {
        if (ahora >= silencioHasta_) {
            obs_source_set_muted(fuente_, false);
            silencioHasta_ = 0;
        } else {
            ts = std::max(ts, silencioHasta_ + 20'000'000ULL);
            obs_source_t* ref = obs_source_get_ref(fuente_);
            const std::uint64_t hasta = silencioHasta_;
            std::thread([ref, hasta]() {
                const std::uint64_t t = os_gettime_ns();
                if (hasta > t) std::this_thread::sleep_for(std::chrono::nanoseconds(hasta - t));
                obs_source_set_muted(ref, false);
                obs_source_release(ref);
            }).detach();
            silencioHasta_ = 0;
        }
    }
    empujar(muestras, n, frecuencia, ts);
}

void MonitorVoz::reproducirEn(const float* muestras, std::size_t n, std::uint32_t frecuencia,
                              std::uint64_t inicioNs) {
    if (muestras == nullptr || n == 0 || frecuencia == 0) return;
    std::lock_guard<std::mutex> l(lock_);
    if (!asegurar()) return;
    const std::uint64_t ahora = os_gettime_ns();
    // Desde dónde sigue el audio: lo encolado, o un poco adelante de ahora.
    std::uint64_t desde = std::max(proximo_, ahora + 50'000'000ULL);
    if (silencioHasta_ != 0) {
        if (ahora >= silencioHasta_) {
            obs_source_set_muted(fuente_, false);
        } else {
            desde = std::max(desde, silencioHasta_ + 20'000'000ULL);
            obs_source_t* ref = obs_source_get_ref(fuente_);
            const std::uint64_t hasta = silencioHasta_;
            std::thread([ref, hasta]() {
                const std::uint64_t t = os_gettime_ns();
                if (hasta > t) std::this_thread::sleep_for(std::chrono::nanoseconds(hasta - t));
                obs_source_set_muted(ref, false);
                obs_source_release(ref);
            }).detach();
        }
        silencioHasta_ = 0;
    }
    // libobs toma un salto en los timestamps como un corte y lo pone a sonar
    // YA: para que empiece en `inicioNs`, el hueco se llena con silencio.
    if (inicioNs > desde) {
        const auto huecos = static_cast<std::size_t>((inicioNs - desde) * frecuencia / 1'000'000'000ULL);
        if (huecos > 0) {
            const std::vector<float> silencio(huecos, 0.0f);
            empujar(silencio.data(), silencio.size(), frecuencia, desde);
            desde = proximo_;
        }
    }
    empujar(muestras, n, frecuencia, desde);
}

void MonitorVoz::empujar(const float* muestras, std::size_t n, std::uint32_t frecuencia, std::uint64_t ts) {
    for (std::size_t i = 0; i < n; i += kBloque) {
        const auto frames = static_cast<std::uint32_t>(std::min<std::size_t>(kBloque, n - i));
        obs_source_audio a = {};
        a.data[0] = reinterpret_cast<const std::uint8_t*>(muestras + i);
        a.frames = frames;
        a.speakers = SPEAKERS_MONO;
        a.format = AUDIO_FORMAT_FLOAT;
        a.samples_per_sec = frecuencia;
        a.timestamp = ts;
        obs_source_output_audio(fuente_, &a);
        ts += static_cast<std::uint64_t>(frames) * 1'000'000'000ULL / frecuencia;
    }
    proximo_ = ts;
}

void MonitorVoz::callar() {
    std::lock_guard<std::mutex> l(lock_);
    if (fuente_ == nullptr) return;
    // Lo encolado ya está en libobs: se lo deja sonar silenciado.
    const std::uint64_t ahora = os_gettime_ns();
    if (proximo_ > ahora) {
        obs_source_set_muted(fuente_, true);
        silencioHasta_ = proximo_;
    }
}

} // namespace ba::app::audio
