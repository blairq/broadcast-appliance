// src/app/arranque/ArchivoConfig.cpp — ver el .hpp

#include "arranque/ArchivoConfig.hpp"

#include <fstream>

namespace ba::app::config {

nlohmann::json leerObjeto(const std::filesystem::path& archivo) {
    std::ifstream f(archivo);
    if (!f) return nlohmann::json::object();
    const nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
    return j.is_object() ? j : nlohmann::json::object();
}

bool guardar(const std::filesystem::path& archivo, const nlohmann::json& j) {
    std::error_code ec;
    std::filesystem::create_directories(archivo.parent_path(), ec);
    std::ofstream o(archivo);
    o << j.dump(1, ' ', false, nlohmann::json::error_handler_t::replace);
    return static_cast<bool>(o);
}

} // namespace ba::app::config
