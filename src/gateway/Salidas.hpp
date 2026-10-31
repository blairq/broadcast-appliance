// ============================================================================
// src/gateway/Salidas.hpp — la lista de salidas que manda la app
//
// El aparato tiene N salidas (docs/planes/PLAN-SALIDAS.md). La app es la que
// sabe cuáles hay y le pasa la lista al gateway (POST /api/v1/salidas):
//
//   {"lista": [{"id": "principal", "nombre": "Principal", "color": "#e53935",
//               "porOmision": true}, {"id": "b", "nombre": "Segunda", ...}]}
//
// Acá se sanea (ids que sirven para nombrar canales, una sola por omisión) y se
// saca lo que necesita el motor de medios (SalidasConocidas). Con una sola
// salida, o sin lista, todo baja como siempre: BA.Program.
// ============================================================================
#pragma once

#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include "MediaRoute.hpp"
#include "ipc/CanalesSalida.hpp"

namespace ba::gateway::salidas {

/// La lista válida: cada id una vez y que sirva para un canal, nombre y color recortados,
/// y exactamente una por omisión (la marcada primero; si ninguna, la primera).
inline nlohmann::json sanear(const nlohmann::json& lista) {
    nlohmann::json out = nlohmann::json::array();
    if (!lista.is_array()) return out;
    std::set<std::string> vistos;
    int porOmision = -1;
    for (const auto& s : lista) {
        if (!s.is_object()) continue;
        const std::string id = s.value("id", std::string());
        if (!ba::ipc::idDeSalidaValido(id) || !vistos.insert(id).second) continue;
        std::string nombre = s.value("nombre", std::string());
        if (nombre.empty()) nombre = id;
        if (nombre.size() > 64) { // sin partir una letra de UTF-8 al medio
            size_t corte = 64;
            while (corte > 0 && (static_cast<unsigned char>(nombre[corte]) & 0xC0) == 0x80) --corte;
            nombre.resize(corte);
        }
        std::string color = s.value("color", std::string());
        if (color.size() > 16) color.clear();
        const bool omision = s.value("porOmision", false) && porOmision < 0;
        if (omision) porOmision = static_cast<int>(out.size());
        out.push_back({{"id", id}, {"nombre", nombre}, {"color", color}, {"porOmision", omision}});
    }
    if (porOmision < 0 && !out.empty()) out[0]["porOmision"] = true;
    return out;
}

/// Lo que el motor de medios necesita de una lista ya saneada.
inline SalidasConocidas conocidas(const nlohmann::json& saneada) {
    SalidasConocidas c;
    for (const auto& s : saneada) {
        c.ids.push_back(s.value("id", std::string()));
        if (s.value("porOmision", false)) c.porOmision = c.ids.back();
    }
    return c;
}

} // namespace ba::gateway::salidas
