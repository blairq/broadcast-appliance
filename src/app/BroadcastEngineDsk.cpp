// SPDX-License-Identifier: GPL-2.0-or-later
//
// DSK (downstream keyers): cuatro capas globales ENCIMA del programa, en los
// canales de salida 1..4 de libobs (el 0 es el programa; la placa va en el 30
// y el negro en el 31, así que el FTB las tapa). Cambiar de escena no las
// toca: un logo, la mosca, el recuadro del intérprete de LSA o un zócalo fijo
// quedan arriba hasta que se bajan. Cada capa es una escena privada con las
// escenas o fuentes que se le asignen y su fundido (el mismo mecanismo que la
// placa: un filtro de opacidad animado en el tick de libobs).
#include "BroadcastEngine.hpp"

#include <obs.h>

#include <algorithm>

namespace ba::app {

namespace {
obs_source_t* fuenteDe(void* p) { return reinterpret_cast<obs_source_t*>(p); }

void opacidad(void* filtro, float op) {
    obs_data_t* d = obs_data_create();
    obs_data_set_double(d, "opacity", std::clamp(op, 0.0f, 1.0f));
    obs_source_update(reinterpret_cast<obs_source_t*>(filtro), d);
    obs_data_release(d);
}
} // namespace

void BroadcastEngine::asegurarDsk(int i) {
    asegurarVelos(); // el tick de los fundidos
    Velo& v = dsk_[i];
    if (v.fuente != nullptr) return;
    const std::string nombre = "__dsk" + std::to_string(i + 1) + "__";
    obs_scene_t* sc = obs_scene_create_private(nombre.c_str());
    obs_source_t* s = obs_scene_get_source(sc);
    obs_source_t* f = obs_source_create_private("color_filter_v2", (nombre + "opacidad").c_str(), nullptr);
    if (s != nullptr && f != nullptr) obs_source_filter_add(s, f);
    v.fuente = s;
    v.filtro = f;
    v.canal = static_cast<std::uint32_t>(i + 1);
    if (f != nullptr) opacidad(f, 0.0f);
}

bool BroadcastEngine::dskArmar(int capa, const std::vector<std::string>& contenido, const nlohmann::json& recuadro) {
    if (!started_) {
        lastError_ = "el motor no está andando";
        return false;
    }
    if (capa < 1 || capa > 4) {
        lastError_ = "la capa va de 1 a 4";
        return false;
    }
    const int i = capa - 1;
    asegurarDsk(i);
    Velo& v = dsk_[i];
    obs_scene_t* sc = obs_scene_from_source(fuenteDe(v.fuente));
    if (sc == nullptr) {
        lastError_ = "no se pudo crear la capa";
        return false;
    }
    // Se vacía y se vuelve a llenar.
    std::vector<obs_sceneitem_t*> items;
    obs_scene_enum_items(
        sc,
        [](obs_scene_t*, obs_sceneitem_t* it, void* p) {
            static_cast<std::vector<obs_sceneitem_t*>*>(p)->push_back(it);
            return true;
        },
        &items);
    for (obs_sceneitem_t* it : items) obs_sceneitem_remove(it);

    obs_video_info ovi = {};
    obs_get_video_info(&ovi);
    const float w = ovi.base_width > 0 ? static_cast<float>(ovi.base_width) : 1920.0f;
    const float h = ovi.base_height > 0 ? static_cast<float>(ovi.base_height) : 1080.0f;
    std::vector<std::string> faltan;
    for (const std::string& n : contenido) {
        obs_source_t* src = obs_get_source_by_name(n.c_str());
        if (src == nullptr) {
            faltan.push_back(n);
            continue;
        }
        obs_sceneitem_t* it = obs_scene_add(sc, src);
        obs_source_release(src);
        if (it == nullptr) continue;
        if (recuadro.is_object() && !recuadro.empty()) {
            const float tam = std::clamp(recuadro.value("tamano", 0.28f), 0.05f, 1.0f);
            const float margen = std::clamp(recuadro.value("margen", 0.03f), 0.0f, 0.2f);
            const std::string pos = recuadro.value("posicion", std::string("abajo_derecha"));
            const float bw = w * tam, bh = bw * h / w;
            const float mx = w * margen, my = h * margen;
            const bool derecha = pos.find("derecha") != std::string::npos;
            const bool abajo = pos.find("abajo") != std::string::npos;
            obs_transform_info ti;
            obs_sceneitem_get_info2(it, &ti);
            ti.bounds_type = OBS_BOUNDS_SCALE_INNER;
            ti.bounds = {bw, bh};
            ti.alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;
            ti.bounds_alignment = OBS_ALIGN_CENTER;
            ti.pos = {derecha ? w - mx - bw : mx, abajo ? h - my - bh : my};
            obs_sceneitem_set_info2(it, &ti);
        }
    }
    dskContenido_[i] = contenido;
    if (!faltan.empty()) {
        lastError_ = "no existe: " + faltan.front() + (faltan.size() > 1 ? " (y otras)" : "");
        return false;
    }
    return true;
}

bool BroadcastEngine::dskMostrar(int capa, bool visible, int ms) {
    if (!started_) {
        lastError_ = "el motor no está andando";
        return false;
    }
    if (capa < 1 || capa > 4) {
        lastError_ = "la capa va de 1 a 4";
        return false;
    }
    const int i = capa - 1;
    asegurarDsk(i);
    Velo& v = dsk_[i];
    v.porSeg = ms <= 0 ? 1e6f : 1000.0f / static_cast<float>(ms);
    v.objetivo = visible ? 1.0f : 0.0f;
    if (visible) {
        obs_set_output_source(v.canal, fuenteDe(v.fuente));
    } else if (ms <= 0) {
        v.actual = 0;
        opacidad(v.filtro, 0);
        obs_set_output_source(v.canal, nullptr);
    }
    return true;
}

nlohmann::json BroadcastEngine::dskEstado() const {
    nlohmann::json out = nlohmann::json::array();
    for (int i = 0; i < 4; ++i)
        out.push_back({{"capa", i + 1}, {"visible", dsk_[i].objetivo > 0}, {"opacidad", dsk_[i].actual},
                       {"contenido", dskContenido_[i]}});
    return out;
}

std::vector<std::string> BroadcastEngine::fuentesDsk() const {
    std::vector<std::string> out;
    for (int i = 0; i < 4; ++i) {
        if (dsk_[i].objetivo <= 0) continue;
        for (const std::string& n : dskContenido_[i]) {
            // Una escena cuenta con todo lo que tiene adentro.
            const auto hijas = sceneSources(n);
            if (hijas.empty()) out.push_back(n);
            else out.insert(out.end(), hijas.begin(), hijas.end());
        }
    }
    return out;
}

bool BroadcastEngine::textoGlobal(const std::string& nombre, const std::string& texto) {
    if (!started_) {
        lastError_ = "el motor no está andando";
        return false;
    }
    obs_video_info ovi = {};
    obs_get_video_info(&ovi);
    const int h = ovi.base_height > 0 ? static_cast<int>(ovi.base_height) : 1080;
    obs_data_t* d = obs_data_create();
    obs_data_set_string(d, "text", texto.c_str());
    obs_data_t* fuente = obs_data_create();
    obs_data_set_string(fuente, "face", "Helvetica");
    obs_data_set_int(fuente, "size", h / 24);
    obs_data_set_obj(d, "font", fuente);
    obs_data_release(fuente);
    obs_data_set_int(d, "color1", 0xFFFFFFFF);
    obs_data_set_int(d, "color2", 0xFFFFFFFF);
    obs_data_set_bool(d, "outline", true);
    obs_data_set_bool(d, "drop_shadow", true);
    obs_source_t* s = obs_get_source_by_name(nombre.c_str());
    if (s != nullptr) {
        obs_source_update(s, d);
        obs_source_release(s);
    } else {
        s = obs_source_create("text_ft2_source_v2", nombre.c_str(), d, nullptr);
        if (s == nullptr) {
            obs_data_release(d);
            lastError_ = "no se pudo crear el rótulo";
            return false;
        }
        // Queda vivo mientras viva el motor (una capa DSK lo busca por nombre).
        rotulosGlobales_.push_back(s);
    }
    obs_data_release(d);
    return true;
}

} // namespace ba::app
