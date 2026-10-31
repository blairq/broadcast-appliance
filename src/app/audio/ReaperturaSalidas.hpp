// src/app/audio/ReaperturaSalidas.hpp — las salidas virtuales de la Matriz y su placa
//
// Una salida virtual sale a un dispositivo del equipo (Matriz::Bus::dispositivo).
// Si la placa se desenchufa, su QAudioSink queda parado y nadie lo volvía a abrir:
// el bus seguía mudo aunque la placa volviera. Con cada cambio de dispositivos
// (EquipoAudio::cambio) la Matriz pregunta acá qué hacer con cada una:
//   - abrir: tiene placa elegida, la placa está y el sink no anda (volvió);
//   - soltar: el sink está abierto pero la placa ya no está (se fue: queda
//     esperándola, con su dispositivo guardado).
// Sin Qt: se prueba con placas inventadas (test_placa_que_se_va).
#pragma once

#include <set>
#include <string>
#include <vector>

namespace ba::app::audio {

struct SalidaVirtual {
    std::string id;
    std::string dispositivo; ///< "" = sin placa elegida
    bool abierta = false;    ///< su sink anda (abierto y sin error)
};

struct PlanReapertura {
    std::vector<std::string> abrir, soltar;
};

inline PlanReapertura reaperturaSalidas(const std::vector<SalidaVirtual>& salidas, const std::set<std::string>& hay) {
    PlanReapertura p;
    for (const auto& s : salidas) {
        if (s.dispositivo.empty()) continue;
        const bool esta = hay.count(s.dispositivo) > 0;
        if (esta && !s.abierta) p.abrir.push_back(s.id);
        if (!esta && s.abierta) p.soltar.push_back(s.id);
    }
    return p;
}

} // namespace ba::app::audio
