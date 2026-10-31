// ============================================================================
// src/app/agent/ModeloPropio.cpp — ver ModeloPropio.hpp
// ============================================================================
#include "agent/ModeloPropio.hpp"
#include "arranque/ArchivoConfig.hpp"

#include <fstream>

namespace ba::app::agent {

using nlohmann::json;

ModeloPropio ModeloPropio::leer(const std::filesystem::path& archivo) {
    ModeloPropio m;
    // No tener uno configurado no es un error; uno roto se lee como «no hay ninguno».
    const json j = config::leerObjeto(archivo);
    if (j.empty()) return m;
    m.url = j.value("url", std::string());
    m.modelo = j.value("modelo", std::string());
    if (j.contains("canales") && j["canales"].is_array()) {
        for (const json& jc : j["canales"]) {
            if (!jc.is_object()) continue;
            Canal c;
            c.id = jc.value("id", std::string());
            c.nombre = jc.value("nombre", std::string());
            if (jc.contains("extra") && jc["extra"].is_object()) c.extra = jc["extra"];
            if (jc.contains("limite") && jc["limite"].is_object()) c.limite = jc["limite"];
            c.instrucciones = jc.value("instrucciones", std::string());
            c.piensa = jc.value("piensa", true);
            // Sin id no se puede nombrar el proveedor: se descarta en vez de
            // generar uno vacío que opencode no sabría elegir.
            if (!c.id.empty()) m.canales.push_back(std::move(c));
        }
    }
    return m;
}

bool ModeloPropio::guardar(const std::filesystem::path& archivo) const {
    json cs = json::array();
    for (const Canal& c : canales)
        cs.push_back(json{{"id", c.id},
                          {"nombre", c.nombre},
                          {"extra", c.extra.is_object() ? c.extra : json::object()},
                          {"limite", c.limite.is_object() ? c.limite : json::object()},
                          {"instrucciones", c.instrucciones},
                          {"piensa", c.piensa}});
    return config::guardar(archivo, {{"url", url}, {"modelo", modelo}, {"canales", cs}});
}

std::vector<std::string> ModeloPropio::comoLosLlamaOpencode() const {
    std::vector<std::string> out;
    for (const Canal& c : canales)
        out.push_back("propio-" + c.id + "/" + modelo);
    return out;
}

} // namespace ba::app::agent
