// ============================================================================
// src/app/agent/ModeloPropio.cpp — ver ModeloPropio.hpp
// ============================================================================
#include "agent/ModeloPropio.hpp"

#include <fstream>

namespace ba::app::agent {

using nlohmann::json;

ModeloPropio ModeloPropio::leer(const std::filesystem::path& archivo) {
    ModeloPropio m;
    std::ifstream f(archivo);
    if (!f) return m; // no tener uno configurado no es un error
    json j;
    try {
        f >> j;
    } catch (const json::exception&) {
        return m; // un archivo roto se lee como «no hay ninguno»
    }
    if (!j.is_object()) return m;
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
    std::error_code ec;
    std::filesystem::create_directories(archivo.parent_path(), ec);
    // Por un archivo aparte y renombrando: si se corta la luz en el medio, lo
    // que queda es la configuración vieja entera y no media nueva.
    const std::filesystem::path parcial = archivo.string() + ".part";
    {
        std::ofstream f(parcial, std::ios::trunc);
        if (!f) return false;
        json cs = json::array();
        for (const Canal& c : canales)
            cs.push_back(json{{"id", c.id},
                              {"nombre", c.nombre},
                              {"extra", c.extra.is_object() ? c.extra : json::object()},
                              {"limite", c.limite.is_object() ? c.limite : json::object()},
                              {"instrucciones", c.instrucciones},
                              {"piensa", c.piensa}});
        f << json{{"url", url}, {"modelo", modelo}, {"canales", cs}}.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
        if (!f) return false;
    }
    std::filesystem::rename(parcial, archivo, ec);
    return !ec;
}

std::vector<std::string> ModeloPropio::comoLosLlamaOpencode() const {
    std::vector<std::string> out;
    for (const Canal& c : canales)
        out.push_back("propio-" + c.id + "/" + modelo);
    return out;
}

} // namespace ba::app::agent
