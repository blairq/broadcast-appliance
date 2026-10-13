// SPDX-License-Identifier: GPL-2.0-or-later
// src/app/evento/Documentos.cpp — ver Documentos.hpp

#include "evento/Documentos.hpp"

#include <algorithm>
#include <fstream>
#include <random>

namespace ba::evento {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
fs::path deU8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string aU8(const fs::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::string idNuevo() {
    static std::mt19937_64 r{std::random_device{}()};
    static const char* h = "0123456789abcdef";
    std::string s;
    for (int i = 0; i < 10; ++i) s += h[r() % 16];
    return s;
}

/// Los campos que se pueden cambiar, con su tipo (lo demás se ignora).
void aplicar(json& d, const json& c) {
    for (const char* k : {"titulo", "descripcion"})
        if (c.contains(k) && c[k].is_string()) d[k] = c[k].get<std::string>().substr(0, k[0] == 't' ? 300 : 4000);
    if (c.contains("publico") && c["publico"].is_boolean()) d["publico"] = c["publico"];
    if (c.contains("paginas") && c["paginas"].is_number_integer()) d["paginas"] = c["paginas"];
    if (c.contains("puntos") && c["puntos"].is_array()) {
        json p = json::array();
        for (const auto& x : c["puntos"])
            if (x.is_number_integer() && x.get<int>() > 0) p.push_back(x.get<int>());
        d["puntos"] = p;
    }
    if (c.contains("secciones") && c["secciones"].is_array()) {
        json s = json::array();
        for (const auto& x : c["secciones"])
            if (x.is_string() && !x.get<std::string>().empty()) s.push_back(x.get<std::string>());
            else if (x.is_number_integer()) s.push_back(std::to_string(x.get<int>()));
        d["secciones"] = s;
    }
}
} // namespace

Documentos::Documentos(fs::path evento) : evento_(std::move(evento)), docs_(evento_ / "docs") {}

json Documentos::leer() const {
    std::ifstream f(docs_ / "documentos.json");
    json j = json::parse(f, nullptr, false);
    return j.is_object() && j.contains("documentos") && j["documentos"].is_array() ? j["documentos"] : json::array();
}

bool Documentos::guardar(const json& l) const {
    std::error_code ec;
    fs::create_directories(docs_, ec);
    const fs::path tmp = docs_ / "documentos.json.tmp";
    {
        std::ofstream o(tmp, std::ios::trunc);
        if (!o) return false;
        o << json{{"version", 1}, {"documentos", l}}.dump(1, ' ', false, json::error_handler_t::replace);
        if (!o) return false;
    }
    fs::rename(tmp, docs_ / "documentos.json", ec);
    return !ec;
}

json Documentos::lista() const {
    json l = leer();
    for (auto& d : l) {
        std::error_code ec;
        d["miniatura"] = fs::exists(miniatura(d.value("id", std::string())), ec);
    }
    return l;
}

json Documentos::documento(const std::string& id) const {
    for (const auto& d : lista())
        if (d.value("id", std::string()) == id) return d;
    return nullptr;
}

fs::path Documentos::ruta(const std::string& id) const {
    for (const auto& d : leer())
        if (d.value("id", std::string()) == id) return archivos() / deU8(d.value("archivo", std::string()));
    return {};
}

json Documentos::publicos() const {
    json out = json::array();
    for (const auto& d : lista()) {
        if (!d.value("publico", true)) continue;
        const std::string id = d.value("id", std::string());
        json p{{"id", id},
               {"titulo", d.value("titulo", std::string())},
               {"descripcion", d.value("descripcion", std::string())},
               {"paginas", d.value("paginas", 0)},
               {"bytes", d.value("bytes", 0)},
               {"puntos", d.value("puntos", json::array())},
               {"secciones", d.value("secciones", json::array())},
               {"pdf", "/evento/docs/" + id + ".pdf"}};
        if (d.value("miniatura", false)) p["miniatura"] = "/evento/docs/" + id + ".jpg";
        out.push_back(p);
    }
    return out;
}

std::string Documentos::agregar(const fs::path& origen, const json& datos, std::int64_t ahoraMs, std::string* error) {
    std::error_code ec;
    if (!fs::is_regular_file(origen, ec)) {
        if (error) *error = "no existe el archivo " + aU8(origen);
        return {};
    }
    fs::create_directories(archivos(), ec);
    fs::create_directories(miniaturas(), ec);
    // El nombre original; si ya hay uno igual, con un número (nunca se pisa).
    std::string base = aU8(origen.stem()), ext = aU8(origen.extension());
    std::string nombre = base + ext;
    for (int i = 2; fs::exists(archivos() / deU8(nombre), ec); ++i) nombre = base + " (" + std::to_string(i) + ")" + ext;
    fs::copy_file(origen, archivos() / deU8(nombre), ec);
    if (ec) {
        if (error) *error = "no se pudo copiar: " + ec.message();
        return {};
    }
    json l = leer();
    std::string id;
    do id = idNuevo();
    while (std::any_of(l.begin(), l.end(), [&](const json& d) { return d.value("id", std::string()) == id; }));
    json d{{"id", id},
           {"archivo", nombre},
           {"titulo", base},
           {"descripcion", ""},
           {"paginas", 0},
           {"bytes", static_cast<std::uint64_t>(fs::file_size(archivos() / deU8(nombre), ec))},
           {"agregado_ms", ahoraMs},
           {"publico", true},
           {"puntos", json::array()},
           {"secciones", json::array()}};
    aplicar(d, datos);
    l.push_back(d);
    if (!guardar(l)) {
        fs::remove(archivos() / deU8(nombre), ec);
        if (error) *error = "no se pudo guardar el índice de documentos";
        return {};
    }
    return id;
}

std::string Documentos::editar(const std::string& id, const json& cambios) {
    json l = leer();
    for (auto& d : l)
        if (d.value("id", std::string()) == id) {
            aplicar(d, cambios);
            return guardar(l) ? std::string() : "no se pudo guardar el índice de documentos";
        }
    return "no hay un documento «" + id + "»";
}

std::string Documentos::quitar(const std::string& id) {
    json l = leer();
    for (auto it = l.begin(); it != l.end(); ++it) {
        if (it->value("id", std::string()) != id) continue;
        std::error_code ec;
        const fs::path pap = evento_ / "Papelera" / "docs";
        fs::create_directories(pap, ec);
        const fs::path arch = archivos() / deU8(it->value("archivo", std::string()));
        if (fs::exists(arch, ec)) {
            fs::path dst = pap / arch.filename();
            for (int i = 2; fs::exists(dst, ec); ++i) dst = pap / (aU8(arch.stem()) + " (" + std::to_string(i) + ")" + aU8(arch.extension()));
            fs::rename(arch, dst, ec);
        }
        fs::remove(miniatura(id), ec);
        l.erase(it);
        return guardar(l) ? std::string() : "no se pudo guardar el índice de documentos";
    }
    return "no hay un documento «" + id + "»";
}

} // namespace ba::evento
