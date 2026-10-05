// SPDX-License-Identifier: GPL-2.0-or-later
//
// FTB (fade to black) y placa de seguridad: dos capas que van ENCIMA de todo
// lo que sale, en canales de salida propios de libobs (los canales se dibujan
// en orden: el 0 es el programa, el 30 la placa, el 31 el negro). No dependen
// de que el operador arme escenas: se crean solas, con su fundido (un filtro
// de opacidad que se anima en el tick de libobs, sin pasar por la interfaz).
#include "BroadcastEngine.hpp"

#include <obs.h>

#include <algorithm>
#include <map>
#include <mutex>

namespace ba::app {

namespace {
constexpr std::uint32_t kCanalPlaca = 30;
constexpr std::uint32_t kCanalFtb = 31;

std::mutex g_volMu;
std::map<std::string, float> g_volAntes; ///< volumen de cada fuente de audio antes del FTB

obs_source_t* fuenteDe(void* p) { return reinterpret_cast<obs_source_t*>(p); }

void ponerOpacidad(void* filtro, float op) {
    obs_data_t* d = obs_data_create();
    obs_data_set_double(d, "opacity", std::clamp(op, 0.0f, 1.0f));
    obs_source_update(reinterpret_cast<obs_source_t*>(filtro), d);
    obs_data_release(d);
}
} // namespace

void BroadcastEngine::asegurarVelos() {
    if (veloFtb_.fuente != nullptr) return;
    obs_video_info ovi = {};
    obs_get_video_info(&ovi);
    const int w = ovi.base_width > 0 ? static_cast<int>(ovi.base_width) : 1920;
    const int h = ovi.base_height > 0 ? static_cast<int>(ovi.base_height) : 1080;
    // El negro: un color de lienzo entero.
    {
        obs_data_t* d = obs_data_create();
        obs_data_set_int(d, "color", 0xFF000000);
        obs_data_set_int(d, "width", w);
        obs_data_set_int(d, "height", h);
        obs_source_t* s = obs_source_create_private("color_source_v3", "__ftb__", d);
        obs_data_release(d);
        obs_source_t* f = obs_source_create_private("color_filter_v2", "__ftb_opacidad__", nullptr);
        if (s != nullptr && f != nullptr) obs_source_filter_add(s, f);
        veloFtb_.fuente = s;
        veloFtb_.filtro = f;
        veloFtb_.canal = kCanalFtb;
    }
    // La placa: una escena privada con fondo y texto (y la imagen, si hay).
    {
        obs_scene_t* sc = obs_scene_create_private("__placa__");
        obs_source_t* s = obs_scene_get_source(sc);
        obs_source_t* f = obs_source_create_private("color_filter_v2", "__placa_opacidad__", nullptr);
        if (s != nullptr && f != nullptr) obs_source_filter_add(s, f);
        veloPlaca_.fuente = s;
        veloPlaca_.filtro = f;
        veloPlaca_.canal = kCanalPlaca;
        obs_data_t* d = obs_data_create();
        obs_data_set_int(d, "color", 0xFF15171C);
        obs_data_set_int(d, "width", w);
        obs_data_set_int(d, "height", h);
        obs_source_t* fondo = obs_source_create_private("color_source_v3", "__placa_fondo__", d);
        obs_data_release(d);
        if (fondo != nullptr) {
            obs_scene_add(sc, fondo);
            obs_source_release(fondo);
        }
    }
    for (Velo* v : {&veloFtb_, &veloPlaca_})
        if (v->filtro != nullptr) ponerOpacidad(v->filtro, 0.0f);
    if (!tickVelo_) {
        obs_add_tick_callback(&BroadcastEngine::tickVelos, this);
        tickVelo_ = true;
    }
}

void BroadcastEngine::soltarVelos() {
    if (tickVelo_) {
        obs_remove_tick_callback(&BroadcastEngine::tickVelos, this);
        tickVelo_ = false;
    }
    for (Velo* v : {&veloFtb_, &veloPlaca_, &dsk_[0], &dsk_[1], &dsk_[2], &dsk_[3]}) {
        if (v->fuente == nullptr) continue;
        obs_set_output_source(v->canal, nullptr);
        if (v->filtro != nullptr) obs_source_release(fuenteDe(v->filtro));
        obs_source_release(fuenteDe(v->fuente));
        *v = Velo{};
    }
}

void BroadcastEngine::tickVelos(void* data, float seg) {
    auto* e = static_cast<BroadcastEngine*>(data);
    for (Velo* v : {&e->veloFtb_, &e->veloPlaca_, &e->dsk_[0], &e->dsk_[1], &e->dsk_[2], &e->dsk_[3]}) {
        if (v->fuente == nullptr || v->actual == v->objetivo) continue;
        const float paso = v->porSeg * seg;
        v->actual = v->objetivo > v->actual ? std::min(v->objetivo, v->actual + paso)
                                            : std::max(v->objetivo, v->actual - paso);
        ponerOpacidad(v->filtro, v->actual);
        // Se saca del canal cuando terminó de irse (no cuesta nada dibujarla).
        if (v->actual <= 0.0f && v->objetivo <= 0.0f) obs_set_output_source(v->canal, nullptr);
        if (v == &e->veloFtb_ && e->ftbAudio_) {
            std::lock_guard<std::mutex> l(g_volMu);
            for (const auto& [n, vol] : g_volAntes)
                if (obs_source_t* s = obs_get_source_by_name(n.c_str())) {
                    obs_source_set_volume(s, vol * (1.0f - v->actual));
                    obs_source_release(s);
                }
            // Terminó de volver: el audio quedó como estaba; se olvida lo guardado.
            if (v->actual <= 0.0f && v->objetivo <= 0.0f) {
                g_volAntes.clear();
                e->ftbAudio_ = false;
            }
        }
    }
}

bool BroadcastEngine::ftb(bool activar, int ms, bool bajarAudio) {
    if (!started_) {
        lastError_ = "el motor no está andando";
        return false;
    }
    asegurarVelos();
    Velo& v = veloFtb_;
    if (v.fuente == nullptr) {
        lastError_ = "no se pudo crear la capa de negro";
        return false;
    }
    v.porSeg = ms <= 0 ? 1e6f : 1000.0f / static_cast<float>(ms);
    v.objetivo = activar ? 1.0f : 0.0f;
    if (activar) {
        obs_set_output_source(v.canal, fuenteDe(v.fuente));
        ftbAudio_ = bajarAudio;
        if (bajarAudio) {
            std::lock_guard<std::mutex> l(g_volMu);
            if (g_volAntes.empty())
                for (const std::string& n : audioSources())
                    if (obs_source_t* s = obs_get_source_by_name(n.c_str())) {
                        g_volAntes[n] = obs_source_get_volume(s);
                        obs_source_release(s);
                    }
        }
    } else if (ms <= 0) {
        v.actual = 0;
        ponerOpacidad(v.filtro, 0);
        obs_set_output_source(v.canal, nullptr);
    }
    if (!activar && ftbAudio_) {
        // El audio vuelve con el fundido (tick); al terminar se olvida lo guardado.
        if (ms <= 0) {
            std::lock_guard<std::mutex> l(g_volMu);
            for (const auto& [n, vol] : g_volAntes)
                if (obs_source_t* s = obs_get_source_by_name(n.c_str())) {
                    obs_source_set_volume(s, vol);
                    obs_source_release(s);
                }
            g_volAntes.clear();
            ftbAudio_ = false;
        }
    }
    return true;
}

bool BroadcastEngine::placa(bool activar, const std::string& texto, const std::string& imagen, int ms) {
    if (!started_) {
        lastError_ = "el motor no está andando";
        return false;
    }
    asegurarVelos();
    Velo& v = veloPlaca_;
    if (v.fuente == nullptr) {
        lastError_ = "no se pudo crear la placa";
        return false;
    }
    v.porSeg = ms <= 0 ? 1e6f : 1000.0f / static_cast<float>(ms);
    if (activar) {
        obs_scene_t* sc = obs_scene_from_source(fuenteDe(v.fuente));
        obs_video_info ovi = {};
        obs_get_video_info(&ovi);
        const float w = ovi.base_width > 0 ? static_cast<float>(ovi.base_width) : 1920.0f;
        const float h = ovi.base_height > 0 ? static_cast<float>(ovi.base_height) : 1080.0f;
        // Se rearman texto e imagen (el fondo queda).
        for (const char* n : {"__placa_texto__", "__placa_imagen__"})
            if (obs_sceneitem_t* it = obs_scene_find_source(sc, n)) obs_sceneitem_remove(it);
        placaImagen_ = imagen;
        if (!imagen.empty()) {
            obs_data_t* d = obs_data_create();
            obs_data_set_string(d, "file", imagen.c_str());
            obs_source_t* img = obs_source_create_private("image_source", "__placa_imagen__", d);
            obs_data_release(d);
            if (img != nullptr) {
                obs_sceneitem_t* it = obs_scene_add(sc, img);
                obs_transform_info ti;
                obs_sceneitem_get_info2(it, &ti);
                ti.bounds_type = OBS_BOUNDS_SCALE_INNER;
                ti.bounds = {w, h};
                ti.pos = {0, 0};
                obs_sceneitem_set_info2(it, &ti);
                obs_source_release(img);
            }
        }
        placaTexto_ = texto.empty() ? std::string("Volvemos enseguida") : texto;
        obs_data_t* d = obs_data_create();
        obs_data_set_string(d, "text", placaTexto_.c_str());
        obs_data_t* fuente = obs_data_create();
        obs_data_set_string(fuente, "face", "Helvetica");
        obs_data_set_int(fuente, "size", static_cast<int>(h / 12));
        obs_data_set_obj(d, "font", fuente);
        obs_data_release(fuente);
        obs_data_set_int(d, "color1", 0xFFFFFFFF);
        obs_data_set_int(d, "color2", 0xFFFFFFFF);
        obs_source_t* txt = obs_source_create_private("text_ft2_source_v2", "__placa_texto__", d);
        obs_data_release(d);
        if (txt != nullptr) {
            obs_sceneitem_t* it = obs_scene_add(sc, txt);
            obs_transform_info ti;
            obs_sceneitem_get_info2(it, &ti);
            ti.alignment = OBS_ALIGN_CENTER;
            ti.pos = {w / 2, imagen.empty() ? h / 2 : h * 0.85f};
            obs_sceneitem_set_info2(it, &ti);
            obs_source_release(txt);
        }
        obs_set_output_source(v.canal, fuenteDe(v.fuente));
    }
    v.objetivo = activar ? 1.0f : 0.0f;
    if (!activar && ms <= 0) {
        v.actual = 0;
        ponerOpacidad(v.filtro, 0);
        obs_set_output_source(v.canal, nullptr);
    }
    return true;
}

nlohmann::json BroadcastEngine::estadoPlaca() const {
    return {{"ftb", veloFtb_.objetivo > 0}, {"ftb_audio", ftbAudio_}, {"placa", veloPlaca_.objetivo > 0},
            {"placa_texto", placaTexto_}, {"placa_imagen", placaImagen_}};
}

} // namespace ba::app
