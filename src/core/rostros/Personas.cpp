// ============================================================================
// src/core/rostros/Personas.cpp — ver Personas.hpp
// ============================================================================
#include "rostros/Personas.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>

#include <nlohmann/json.hpp>

#include "rostros/Rostros.hpp"

namespace ba::rostros {
namespace {

using json = nlohmann::json;

/// Dos vectores tan parecidos que el segundo no aporta nada.
///
/// Muy alto a propósito: se descarta solo lo que es la misma toma. Con un
/// umbral más bajo se tiraría justamente lo que sirve —la misma persona con
/// otra luz o de otro ángulo, que es lo que hace que el reconocimiento aguante
/// el día real—.
constexpr float kYaLaTengo = 0.92f;

std::string idNuevo() {
    static std::mt19937_64 rng{std::random_device{}()};
    static const char* hex = "0123456789abcdef";
    std::string s = "p";
    for (int i = 0; i < 12; ++i) s += hex[rng() & 0xF];
    return s;
}

} // namespace

const Persona* Personas::porId(const std::string& id) const {
    for (const Persona& p : personas_)
        if (p.id == id) return &p;
    return nullptr;
}

std::string Personas::guardarFicha(const Persona& p) {
    for (Persona& q : personas_) {
        if (q.id != p.id || p.id.empty()) continue;
        // Las CARAS no se pisan desde la ficha: la edita alguien escribiendo
        // un nombre, y perder los vectores por corregir una tilde sería una
        // sorpresa cara —hay que volver a mostrarle la cara al sistema—.
        q.nombre = p.nombre;
        q.cargo = p.cargo;
        q.nota = p.nota;
        return q.id;
    }
    Persona nueva = p;
    if (nueva.id.empty()) nueva.id = idNuevo();
    personas_.push_back(std::move(nueva));
    return personas_.back().id;
}

bool Personas::borrar(const std::string& id) {
    const auto it = std::remove_if(personas_.begin(), personas_.end(),
                                   [&id](const Persona& p) { return p.id == id; });
    if (it == personas_.end()) return false;
    personas_.erase(it, personas_.end());
    return true;
}

bool Personas::sumarCara(const std::string& id, const std::vector<float>& vector) {
    if (vector.empty()) return false;
    for (Persona& p : personas_) {
        if (p.id != id) continue;
        for (const auto& v : p.caras)
            if (parecido(v, vector) >= kYaLaTengo) return true; // ya la tengo
        p.caras.push_back(vector);
        // Con la tabla llena se va la MÁS VIEJA: las primeras son de cuando se
        // la dio de alta (una foto, una luz) y las nuevas traen la variedad
        // que hace falta.
        if (p.caras.size() > kMaxCarasPorPersona) p.caras.erase(p.caras.begin());
        return true;
    }
    return false;
}

Coincidencia Personas::buscar(const std::vector<float>& vector) const {
    Coincidencia mejor;
    if (vector.empty()) return mejor;
    for (const Persona& p : personas_) {
        for (const auto& v : p.caras) {
            const float s = parecido(v, vector);
            if (s <= mejor.parecido) continue;
            mejor.parecido = s;
            mejor.id = p.id;
            mejor.nombre = p.nombre;
        }
    }
    return mejor;
}

void Personas::vistaAhora(const std::string& id, std::int64_t ahoraMs) {
    for (Persona& p : personas_)
        if (p.id == id) p.vistaMs = ahoraMs;
}

bool Personas::leer(const std::string& archivo) {
    personas_.clear();
    error_.clear();
    if (!std::filesystem::exists(archivo)) return true; // todavía no hay libreta
    std::ifstream f(archivo, std::ios::binary);
    const json j = json::parse(f, nullptr, false);
    if (!j.is_array()) {
        error_ = "el archivo de personas no es una lista";
        return false;
    }
    for (const json& e : j) {
        if (!e.is_object()) continue;
        Persona p;
        p.id = e.value("id", std::string());
        p.nombre = e.value("nombre", std::string());
        p.cargo = e.value("cargo", std::string());
        p.nota = e.value("nota", std::string());
        p.vistaMs = e.value("vistaMs", std::int64_t{0});
        if (p.id.empty()) p.id = idNuevo();
        if (const auto it = e.find("caras"); it != e.end() && it->is_array())
            for (const json& v : *it)
                if (v.is_array() && !v.empty()) p.caras.push_back(v.get<std::vector<float>>());
        personas_.push_back(std::move(p));
    }
    return true;
}

bool Personas::guardar(const std::string& archivo) const {
    json j = json::array();
    for (const Persona& p : personas_)
        j.push_back(json{{"id", p.id},
                         {"nombre", p.nombre},
                         {"cargo", p.cargo},
                         {"nota", p.nota},
                         {"vistaMs", p.vistaMs},
                         {"caras", p.caras}});
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(archivo).parent_path(), ec);
    // SE ESCRIBE AL LADO Y SE RENOMBRA: si se corta la luz en la mitad, queda
    // la libreta anterior entera y no media libreta. Volver a dar de alta a
    // todos no es algo que se le pueda pedir a nadie.
    const std::string tmp = archivo + ".part";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            error_ = "no se pudo escribir " + tmp;
            return false;
        }
        f << j.dump(1, '\t');
        if (!f) {
            error_ = "error al escribir " + tmp;
            return false;
        }
    }
    std::filesystem::rename(tmp, archivo, ec);
    if (ec) {
        error_ = "no se pudo reemplazar " + archivo + ": " + ec.message();
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

} // namespace ba::rostros
