// SPDX-License-Identifier: GPL-2.0-or-later
#include "legislatura/Cuerpo.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <fstream>
#include <sstream>
#include <vector>

namespace ba::app::legislatura {

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

/// Minúsculas y sin tildes (UTF-8 de las vocales acentuadas y la ñ), para comparar.
std::string plano(std::string s) {
    static const std::pair<const char*, const char*> kTildes[] = {
        {"á", "a"}, {"é", "e"}, {"í", "i"}, {"ó", "o"}, {"ú", "u"}, {"ü", "u"}, {"ñ", "n"},
        {"Á", "a"}, {"É", "e"}, {"Í", "i"}, {"Ó", "o"}, {"Ú", "u"}, {"Ü", "u"}, {"Ñ", "n"}};
    for (const auto& [a, b] : kTildes)
        for (size_t i = 0; (i = s.find(a, i)) != std::string::npos;) s.replace(i, std::string(a).size(), b);
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    // Sin espacios de los costados.
    const auto b = s.find_first_not_of(" \t\r\n"), e = s.find_last_not_of(" \t\r\n");
    return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

std::string ahoraIso() {
    const std::time_t t = std::time(nullptr);
    char b[32];
    std::strftime(b, sizeof b, "%Y-%m-%d %H:%M", std::localtime(&t));
    return b;
}

/// Una línea de CSV en campos (comillas dobles, separador `sep`).
std::vector<std::string> campos(const std::string& linea, char sep) {
    std::vector<std::string> out;
    std::string cur;
    bool entre = false;
    for (size_t i = 0; i < linea.size(); ++i) {
        const char c = linea[i];
        if (entre) {
            if (c == '"' && i + 1 < linea.size() && linea[i + 1] == '"') cur += '"', ++i;
            else if (c == '"') entre = false;
            else cur += c;
        } else if (c == '"') {
            entre = true;
        } else if (c == sep) {
            out.push_back(cur);
            cur.clear();
        } else if (c != '\r') {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

const char* kCampos[] = {"persona_id", "nombre", "cargo_oficial", "bloque", "distrito",
                         "banca",      "foto",   "preset_ptz",    "microfono"};

} // namespace

Cuerpo::Cuerpo(fs::path config) : archivo_(std::move(config)) {
    std::ifstream f(archivo_);
    const json j = json::parse(f, nullptr, false);
    datos_ = j.is_object() ? j : json{{"nombre", ""}, {"integrantes", json::array()}};
    if (!datos_.contains("integrantes") || !datos_["integrantes"].is_array()) datos_["integrantes"] = json::array();
}

void Cuerpo::guardarArchivo() const {
    std::error_code ec;
    fs::create_directories(archivo_.parent_path(), ec);
    const fs::path tmp = archivo_.string() + ".tmp";
    {
        std::ofstream o(tmp);
        o << datos_.dump(1);
    }
    fs::rename(tmp, archivo_, ec);
}

void Cuerpo::setNombre(const std::string& n) {
    datos_["nombre"] = n;
    guardarArchivo();
}

json Cuerpo::buscar(const std::string& clave) const {
    const std::string k = plano(clave);
    for (const auto& i : datos_["integrantes"])
        if (i.value("id", std::string()) == clave || plano(i.value("banca", std::string())) == k ||
            plano(i.value("nombre", std::string())) == k)
            return i;
    return json::object();
}

std::string Cuerpo::guardar(const json& c, std::string* error) {
    std::string id = c.value("id", std::string());
    json* x = nullptr;
    for (auto& i : datos_["integrantes"])
        if (!id.empty() && i.value("id", std::string()) == id) x = &i;
    if (x == nullptr) {
        if (c.value("nombre", std::string()).empty()) {
            if (error) *error = "falta el nombre";
            return {};
        }
        if (id.empty()) {
            int n = static_cast<int>(datos_["integrantes"].size()) + 1;
            const auto usado = [&](const std::string& cand) { return !buscar(cand).empty(); };
            while (usado("l" + std::to_string(n))) ++n;
            id = "l" + std::to_string(n);
        }
        datos_["integrantes"].push_back(json{{"id", id}});
        x = &datos_["integrantes"].back();
    }
    // La banca es única: si otra la tiene, error.
    if (c.contains("banca") && !c.value("banca", std::string()).empty()) {
        const json otro = buscar(c.value("banca", std::string()));
        if (!otro.empty() && otro.value("id", std::string()) != id &&
            plano(otro.value("banca", std::string())) == plano(c.value("banca", std::string()))) {
            if (error) *error = "la banca «" + c.value("banca", std::string()) + "» ya es de " + otro.value("nombre", std::string());
            return {};
        }
    }
    bool cambioVisible = false;
    for (const char* k : kCampos)
        if (c.contains(k)) {
            if ((std::string(k) == "nombre" || std::string(k) == "cargo_oficial") && (*x).value(k, json()) != c[k])
                cambioVisible = true;
            (*x)[k] = c[k];
        }
    // Lo que sale en un zócalo cambió: hay que volver a validarlo.
    if (cambioVisible && !c.contains("validado_por")) {
        x->erase("validado_por");
        x->erase("validado_en");
    }
    guardarArchivo();
    return id;
}

bool Cuerpo::borrar(const std::string& id) {
    auto& v = datos_["integrantes"];
    const auto antes = v.size();
    for (auto it = v.begin(); it != v.end(); ++it)
        if (it->value("id", std::string()) == id) {
            v.erase(it);
            break;
        }
    if (v.size() == antes) return false;
    guardarArchivo();
    return true;
}

bool Cuerpo::validar(const std::string& id, const std::string& quien) {
    for (auto& i : datos_["integrantes"])
        if (i.value("id", std::string()) == id) {
            i["validado_por"] = quien.empty() ? "operador" : quien;
            i["validado_en"] = ahoraIso();
            guardarArchivo();
            return true;
        }
    return false;
}

json Cuerpo::importarCsv(const fs::path& archivo, bool reemplazar) {
    json res{{"importados", 0}, {"actualizados", 0}, {"errores", json::array()}};
    std::ifstream f(archivo);
    if (!f) {
        res["errores"].push_back("no se pudo abrir " + archivo.string());
        return res;
    }
    std::string cab;
    std::getline(f, cab);
    if (cab.size() >= 3 && static_cast<unsigned char>(cab[0]) == 0xEF) cab = cab.substr(3); // BOM de Excel
    const char sep = std::count(cab.begin(), cab.end(), ';') > std::count(cab.begin(), cab.end(), ',') ? ';' : ',';
    std::vector<std::string> cols;
    for (const auto& c : campos(cab, sep)) {
        const std::string p = plano(c);
        cols.push_back(p == "cargo" || p == "cargo oficial" ? "cargo_oficial"
                       : p == "camara" || p == "cámara"     ? "camara"
                       : p == "posicion" || p == "preset"   ? "posicion"
                       : p == "microfono" || p == "mic"     ? "microfono"
                                                            : p);
    }
    if (std::find(cols.begin(), cols.end(), "nombre") == cols.end()) {
        res["errores"].push_back("la planilla no tiene la columna «nombre»");
        return res;
    }
    if (reemplazar) datos_["integrantes"] = json::array();
    std::string linea;
    int nro = 1;
    while (std::getline(f, linea)) {
        ++nro;
        if (plano(linea).empty()) continue;
        const auto v = campos(linea, sep);
        json c = json::object();
        std::string camara, posicion;
        for (size_t i = 0; i < cols.size() && i < v.size(); ++i) {
            const std::string val = v[i];
            if (cols[i] == "camara") camara = val;
            else if (cols[i] == "posicion") posicion = val;
            else if (cols[i] == "microfono") { if (!val.empty()) c["microfono"] = {{"fuente", val}}; }
            else if (cols[i] == "nombre" || cols[i] == "cargo_oficial" || cols[i] == "bloque" ||
                     cols[i] == "distrito" || cols[i] == "banca" || cols[i] == "foto")
                c[cols[i]] = val;
        }
        if (!camara.empty()) c["preset_ptz"] = {{"camara", camara}, {"posicion", posicion}};
        const json existe = !c.value("banca", std::string()).empty() ? buscar(c.value("banca", std::string()))
                                                                       : buscar(c.value("nombre", std::string()));
        if (!existe.empty()) c["id"] = existe["id"];
        std::string err;
        if (guardar(c, &err).empty())
            res["errores"].push_back("fila " + std::to_string(nro) + ": " + err);
        else
            res[existe.empty() ? "importados" : "actualizados"] = res.value(existe.empty() ? "importados" : "actualizados", 0) + 1;
    }
    return res;
}

bool Cuerpo::escribirPlantilla(const fs::path& archivo) {
    std::error_code ec;
    fs::create_directories(archivo.parent_path(), ec);
    std::ofstream o(archivo);
    o << "banca;nombre;cargo;bloque;distrito;camara;posicion;microfono\n"
         "1;Nombre Apellido;Diputada;Bloque A;Distrito 1;Cámara sala;banca 1;Mic banca 1\n"
         "2;Otro Nombre;Diputado;Bloque B;Distrito 2;Cámara sala;banca 2;Mic banca 2\n";
    return static_cast<bool>(o);
}

} // namespace ba::app::legislatura
