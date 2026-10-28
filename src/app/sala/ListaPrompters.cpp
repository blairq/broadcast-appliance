// src/app/sala/ListaPrompters.cpp — ver el .hpp

#include "sala/ListaPrompters.hpp"

#include "i18n/I18n.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace ba::app::sala {

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

/// Como Prompters::idValido del gateway: minúsculas, números y guiones, hasta 32.
std::string idDe(const std::string& nombre) {
    std::string out;
    for (unsigned char c : nombre) {
        if (c >= 0x80) continue; // (las tildes se caen: el nombre de verdad queda en la lista)
        if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
        else if (!out.empty() && out.back() != '-') out += '-';
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out.substr(0, 28);
}

std::string recortar(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

} // namespace

void ListaPrompters::cargar(const fs::path& archivo) {
    archivo_ = archivo;
    lista_ = json::array();
    if (archivo.empty()) return;
    std::ifstream f(archivo);
    const json j = json::parse(f, nullptr, false);
    if (!j.is_object() || !j.contains("prompters") || !j["prompters"].is_array()) return;
    for (const auto& p : j["prompters"])
        if (p.is_object() && !p.value("id", std::string()).empty()) lista_.push_back(p);
}

json ListaPrompters::lista() const {
    json out = json::array({{{"id", kPrincipal}, {"nombre", nombreDe(kPrincipal)}}});
    for (const auto& p : lista_)
        if (p.value("id", std::string()) != kPrincipal) out.push_back({{"id", p["id"]}, {"nombre", p.value("nombre", std::string())}});
    return out;
}

bool ListaPrompters::existe(const std::string& id) const {
    if (id == kPrincipal) return true;
    return std::any_of(lista_.begin(), lista_.end(), [&](const json& p) { return p.value("id", std::string()) == id; });
}

std::string ListaPrompters::nombreDe(const std::string& id) const {
    for (const auto& p : lista_)
        if (p.value("id", std::string()) == id && !p.value("nombre", std::string()).empty()) return p["nombre"];
    return id == kPrincipal ? ba::i18n::traducir("sala", "Principal") : id;
}

std::string ListaPrompters::agregar(const std::string& nombre, std::string* error) {
    const std::string n = recortar(nombre);
    const std::string base = idDe(n);
    if (base.empty()) {
        if (error) *error = ba::i18n::traducir("sala", "Ponele un nombre al teleprompter (con alguna letra o número).");
        return {};
    }
    std::string id = base;
    for (int k = 2; existe(id); ++k) id = base + "-" + std::to_string(k);
    lista_.push_back({{"id", id}, {"nombre", n}});
    if (!guardar()) {
        lista_.erase(lista_.end() - 1);
        if (error) *error = ba::i18n::traducir("sala", "No se pudo guardar la lista de teleprompters (¿hay un evento abierto?).");
        return {};
    }
    return id;
}

std::string ListaPrompters::quitar(const std::string& id) {
    if (id == kPrincipal) return ba::i18n::traducir("sala", "El teleprompter principal no se saca.");
    const auto antes = lista_.size();
    lista_.erase(std::remove_if(lista_.begin(), lista_.end(), [&](const json& p) { return p.value("id", std::string()) == id; }),
                 lista_.end());
    if (lista_.size() == antes) return ba::i18n::llenar(ba::i18n::traducir("sala", "No hay un teleprompter «%1»."), {id});
    return guardar() ? std::string() : ba::i18n::traducir("sala", "No se pudo guardar la lista de teleprompters.");
}

std::string ListaPrompters::renombrar(const std::string& id, const std::string& nombre) {
    const std::string n = recortar(nombre);
    if (n.empty()) return ba::i18n::traducir("sala", "Falta el nombre.");
    if (!existe(id)) return ba::i18n::llenar(ba::i18n::traducir("sala", "No hay un teleprompter «%1»."), {id});
    bool hecho = false;
    for (auto& p : lista_)
        if (p.value("id", std::string()) == id) p["nombre"] = n, hecho = true;
    if (!hecho) lista_.push_back({{"id", id}, {"nombre", n}}); // el principal, renombrado por primera vez
    return guardar() ? std::string() : ba::i18n::traducir("sala", "No se pudo guardar la lista de teleprompters.");
}

bool ListaPrompters::guardar() const {
    if (archivo_.empty()) return false;
    std::error_code ec;
    fs::create_directories(archivo_.parent_path(), ec);
    const fs::path tmp = archivo_.string() + ".tmp";
    {
        std::ofstream o(tmp);
        o << json{{"prompters", lista_}}.dump(1);
        if (!o) return false;
    }
    fs::rename(tmp, archivo_, ec);
    return !ec;
}

} // namespace ba::app::sala
