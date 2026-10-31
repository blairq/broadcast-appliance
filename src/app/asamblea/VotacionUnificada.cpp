// src/app/asamblea/VotacionUnificada.cpp — la votación en curso, resuelta (ver el .hpp)

#include "asamblea/VotacionUnificada.hpp"

#include <string>

namespace ba::app::asamblea {

using json = nlohmann::json;

json votacionUnificada(const json& central, const json& cel, const std::function<json(const json&)>& combinar,
                       const std::function<json(const std::string&)>& opcionesDeMenu) {
    const std::string estCentral = central.value("estado", std::string());
    const bool mandaCentral = !estCentral.empty() && estCentral != "sin_preparar";
    json opciones = cel.value("opciones", json::array());
    if (opciones.empty() && opcionesDeMenu) opciones = opcionesDeMenu(central.value("menu", std::string()));
    json totales = json::object();
    std::string origen = mandaCentral ? "central" : "celulares";
    if (!mandaCentral) {
        totales = cel.value("totales", json::object());
    } else if (central.contains("nominal") && combinar) {
        totales = combinar(central.value("nominal", json::array())).value("totales", json::object());
        origen = "central + celulares";
    } else {
        // Sin la nominal de la central no se puede saber si alguien votó por los dos lados.
        const json tc = central.value("totales", json::object()), tr = cel.value("totales", json::object());
        for (const auto& o : opciones) {
            const std::string id = o.value("id", std::string());
            totales[id] = tc.value(id, 0) + tr.value(id, 0);
        }
        origen = "central + celulares (sumados)";
    }
    for (const auto& o : opciones) // todas las opciones, aunque estén en cero
        if (!totales.contains(o.value("id", std::string()))) totales[o.value("id", std::string())] = 0;
    std::string tema = cel.value("tema", std::string());
    if (tema.empty()) tema = central.value("tema", std::string());
    return {{"estado", mandaCentral ? estCentral : cel.value("estado", std::string("sin"))},
            {"tema", tema},
            {"opciones", opciones},
            {"totales", totales},
            {"hasta_ms", cel.value("hasta_ms", std::int64_t{0})},
            {"origen", origen}};
}

} // namespace ba::app::asamblea
