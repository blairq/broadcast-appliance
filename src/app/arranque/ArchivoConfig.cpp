// src/app/arranque/ArchivoConfig.cpp — ver el .hpp

#include "arranque/ArchivoConfig.hpp"

#include "persist/ColaEscritura.hpp"

namespace ba::app::config {

nlohmann::json leerObjeto(const std::filesystem::path& archivo) {
    // Lo último: si hay algo encolado que todavía no está en el disco, eso.
    std::string bytes;
    if (!ba::persist::cola().leer(archivo, bytes)) return nlohmann::json::object();
    const nlohmann::json j = nlohmann::json::parse(bytes, nullptr, false);
    return j.is_object() ? j : nlohmann::json::object();
}

bool guardar(const std::filesystem::path& archivo, const nlohmann::json& j) {
    ba::persist::cola().escribir(archivo, j.dump(1, ' ', false, nlohmann::json::error_handler_t::replace));
    return true;
}

} // namespace ba::app::config
