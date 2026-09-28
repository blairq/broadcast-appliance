// ============================================================================
// src/app/carrusel/CarruselStore.cpp — ver CarruselStore.hpp
// ============================================================================
#include "carrusel/CarruselStore.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace fs = std::filesystem;

namespace ba::carrusel {
namespace {

using nlohmann::json;

std::string bajo(const std::string& s) {
    std::string o = s;
    std::transform(o.begin(), o.end(), o.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return o;
}

} // namespace

Store::Store(fs::path archivo) : archivo_(std::move(archivo)) { releer(); }

void Store::releer() {
    todos_.clear();
    error_.clear();
    std::error_code ec;
    // Que no exista es lo normal hasta que alguien arma el primero.
    if (!fs::exists(archivo_, ec)) return;
    std::ifstream in(archivo_);
    if (!in) {
        error_ = "no se pudo abrir " + archivo_.string();
        return;
    }
    const json j = json::parse(in, nullptr, false);
    if (j.is_discarded()) {
        // Se dice, no se hace pasar por una biblioteca vacia: si no, un archivo
        // roto se ve igual que «todavia no armaste ninguno» y el operador
        // rearma todo sin saber que lo suyo sigue ahi.
        error_ = "la biblioteca de carruseles esta rota (JSON invalido): " + archivo_.string();
        return;
    }
    if (!j.is_object() || !j.contains("carruseles") || !j["carruseles"].is_array()) return;
    for (const json& jc : j["carruseles"]) {
        Carrusel c = Carrusel::deJson(jc);
        if (!c.nombre().empty()) todos_.push_back(std::move(c));
    }
}

Carrusel* Store::porNombre(const std::string& nombre) {
    const std::string n = bajo(nombre);
    for (Carrusel& c : todos_)
        if (bajo(c.nombre()) == n) return &c;
    return nullptr;
}

const Carrusel* Store::porNombre(const std::string& nombre) const {
    return const_cast<Store*>(this)->porNombre(nombre);
}

bool Store::guardar(const Carrusel& c) {
    // Sin nombre no se puede volver a encontrar, y la biblioteca se llenaria de
    // entradas anonimas indistinguibles.
    if (c.nombre().empty()) {
        error_ = "un carrusel necesita un nombre";
        return false;
    }
    if (Carrusel* ya = porNombre(c.nombre()); ya != nullptr) *ya = c;
    else todos_.push_back(c);
    return persistir();
}

bool Store::borrar(const std::string& nombre) {
    const std::string n = bajo(nombre);
    const auto it = std::remove_if(todos_.begin(), todos_.end(),
                                   [&n](const Carrusel& c) { return bajo(c.nombre()) == n; });
    if (it == todos_.end()) return false;
    todos_.erase(it, todos_.end());
    return persistir();
}

bool Store::persistir() {
    error_.clear();
    std::error_code ec;
    fs::create_directories(archivo_.parent_path(), ec);
    json cs = json::array();
    for (const Carrusel& c : todos_) cs.push_back(c.aJson());
    // Al lado y renombrar: si se corta la luz en medio, el que estaba sigue
    // entero. Un carrusel a medio escribir es una tira que ya no apunta a
    // ningun archivo.
    const fs::path tmp = archivo_.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) {
            error_ = "no se pudo escribir " + tmp.string();
            return false;
        }
        out << json{{"carruseles", cs}}.dump(2) << '\n';
        if (!out) {
            error_ = "no se pudo terminar de escribir " + tmp.string();
            return false;
        }
    }
    fs::rename(tmp, archivo_, ec);
    if (ec) {
        error_ = "no se pudo reemplazar la biblioteca: " + ec.message();
        return false;
    }
    return true;
}

} // namespace ba::carrusel
