// ============================================================================
// src/core/ajustes/Ajustes.cpp — ver Ajustes.hpp
// ============================================================================
#include "ajustes/Ajustes.hpp"

#include <mutex>
#include <shared_mutex>

namespace ba::ajustes {

namespace {
std::shared_mutex& mu() {
    static std::shared_mutex m;
    return m;
}
nlohmann::json& valores() {
    static nlohmann::json v = nlohmann::json::object();
    return v;
}
/// El valor guardado o null (con el candado de lectura tomado).
nlohmann::json leer(const char* clave) {
    std::shared_lock<std::shared_mutex> l(mu());
    const auto& v = valores();
    const auto it = v.find(clave);
    return it == v.end() ? nlohmann::json() : *it;
}
} // namespace

void cargar(const nlohmann::json& nuevos) {
    std::unique_lock<std::shared_mutex> l(mu());
    valores() = nuevos.is_object() ? nuevos : nlohmann::json::object();
}

void poner(const std::string& clave, const nlohmann::json& valor) {
    std::unique_lock<std::shared_mutex> l(mu());
    if (valor.is_null()) valores().erase(clave);
    else valores()[clave] = valor;
}

nlohmann::json todos() {
    std::shared_lock<std::shared_mutex> l(mu());
    return valores();
}

double numero(const char* clave, double respaldo) {
    const nlohmann::json v = leer(clave);
    return v.is_number() ? v.get<double>() : respaldo;
}

int entero(const char* clave, int respaldo) {
    const nlohmann::json v = leer(clave);
    return v.is_number() ? static_cast<int>(v.get<double>()) : respaldo;
}

std::int64_t entero64(const char* clave, std::int64_t respaldo) {
    const nlohmann::json v = leer(clave);
    return v.is_number() ? static_cast<std::int64_t>(v.get<double>()) : respaldo;
}

bool siNo(const char* clave, bool respaldo) {
    const nlohmann::json v = leer(clave);
    return v.is_boolean() ? v.get<bool>() : respaldo;
}

std::string texto(const char* clave, const std::string& respaldo) {
    const nlohmann::json v = leer(clave);
    return v.is_string() ? v.get<std::string>() : respaldo;
}

} // namespace ba::ajustes
