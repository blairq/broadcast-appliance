// ============================================================================
// src/app/audio/MonitorVoz.cpp — ver MonitorVoz.hpp
// ============================================================================
#include "audio/MonitorVoz.hpp"

#include <obs.h>
#include <util/platform.h>

#include <algorithm>

namespace ba::app::audio {

namespace {

constexpr const char* kTipo = "ba_voz_asistente";
constexpr std::uint32_t kBloque = 1024; // frames por paquete

const char* nombre(void*) { return "Voz del asistente (solo monitoreo)"; }
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
    if (fuente_ != nullptr) {
        obs_source_set_monitoring_type(fuente_, OBS_MONITORING_TYPE_NONE);
        obs_source_release(fuente_);
        fuente_ = nullptr;
    }
}

bool MonitorVoz::asegurar() {
    if (fuente_ != nullptr) return true;
    if (!obs_initialized()) return false;
    fuente_ = obs_source_create_private(kTipo, "ba-voz-asistente", nullptr);
    if (fuente_ == nullptr) return false;
    // Solo a los auriculares del operador: jamás a la mezcla (y además no
    // está en ningún canal de salida).
    obs_source_set_monitoring_type(fuente_, OBS_MONITORING_TYPE_MONITOR_ONLY);
    obs_source_set_muted(fuente_, false);
    return true;
}

void MonitorVoz::reproducir(const float* muestras, std::size_t n, std::uint32_t frecuencia) {
    if (muestras == nullptr || n == 0 || frecuencia == 0) return;
    std::lock_guard<std::mutex> l(lock_);
    if (!asegurar()) return;
    // Pegado a lo que ya está sonando; si no suena nada, un poco adelante de
    // ahora (lo que necesita libobs para no tirarlo por viejo).
    const std::uint64_t ahora = os_gettime_ns();
    std::uint64_t ts = std::max(proximo_, ahora + 50'000'000ULL);
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
    // Lo encolado se descarta rearmando la fuente.
    obs_source_set_monitoring_type(fuente_, OBS_MONITORING_TYPE_NONE);
    obs_source_release(fuente_);
    fuente_ = nullptr;
    proximo_ = 0;
}

} // namespace ba::app::audio
