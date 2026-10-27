// src/app/sala/VotacionSala.cpp — la votación de la sala (ver el .hpp)

#include "sala/VotacionSala.hpp"

#include "i18n/I18n.hpp"


namespace ba::app::sala {

VotacionSala::VotacionSala(ConferenceClient& sala, Votantes votantes) : sala_(sala), votantes_(std::move(votantes)) {}

std::string VotacionSala::abrir(const std::string& pregunta, const std::vector<std::string>& opcionesPedidas,
                                const std::string& modo, bool secreta, bool enVivo, Listo listo) {
    nlohmann::json opciones = nlohmann::json::array();
    for (const std::string& o : opcionesPedidas) {
        std::string t = o;
        t.erase(0, t.find_first_not_of(" \t"));
        t.erase(t.find_last_not_of(" \t") + 1);
        if (!t.empty()) opciones.push_back(t);
    }
    if (opciones.size() < 2)
        return ba::i18n::traducir("sala", "Hacen falta al menos dos opciones: con una sola no hay nada que decidir.");
    if (opciones.size() > 10) return ba::i18n::traducir("sala", "Van hasta diez opciones.");
    if (modo != "encuesta" && modo != "formal") return ba::i18n::traducir("sala", "modo: encuesta o formal");
    nlohmann::json cuerpo{{"accion", "abrir"}, {"pregunta", pregunta}, {"opciones", opciones},
                          {"modo", modo},      {"secreta", secreta},   {"enVivo", enVivo}};
    if (modo == "formal") {
        const auto v = votantes_ ? votantes_() : std::nullopt;
        if (!v) return ba::i18n::traducir("sala", "Una votación formal necesita el padrón, y el padrón vive en el evento: abrí uno.");
        if (v->habilitados.empty())
            return ba::i18n::traducir("sala", "No hay nadie con derecho a voto y presente en el padrón (hace falta «vota» y "
                                              "estado «llegó»). Una votación formal donde nadie puede votar da un resultado "
                                              "que parece válido.");
        nlohmann::json hs = nlohmann::json::array();
        for (const auto& h : v->habilitados) hs.push_back({{"codigo", h.codigo}, {"nombre", h.nombre}});
        cuerpo["habilitados"] = hs;
        cuerpo["podian"] = v->podian;
    }
    pedir(sala_, "POST", "/api/v1/votacion", cuerpo, std::move(listo));
    return {};
}

void VotacionSala::cerrar(Listo listo) { pedir(sala_, "POST", "/api/v1/votacion", {{"accion", "cerrar"}}, std::move(listo)); }

void VotacionSala::setEnVivo(bool si, Listo listo) {
    pedir(sala_, "POST", "/api/v1/votacion", {{"accion", "en_vivo"}, {"si", si}}, std::move(listo));
}

void VotacionSala::ver(bool historial, Listo listo) {
    pedir(sala_, "GET", historial ? "/api/v1/votacion?historial=1" : "/api/v1/votacion", {}, std::move(listo));
}

} // namespace ba::app::sala
