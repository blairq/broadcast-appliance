// SPDX-License-Identifier: GPL-2.0-or-later
#include "asamblea/Cuerpo.hpp"

#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <chrono>
#include <ctime>
#include <fstream>
#include <sstream>
#include <vector>

namespace ba::app::asamblea {

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

// (preset_ptz no: es la toma de la banca, ver Cuerpo::guardar.)
const char* kCampos[] = {"persona_id", "nombre", "cargo_oficial", "bloque",    "distrito",
                         "banca",      "foto",   "microfono",     "dcn_unidad", "prioridad"};

} // namespace

std::string Cuerpo::normalizar(const std::string& s) { return plano(s); }

Cuerpo::Cuerpo(fs::path config) : archivo_(std::move(config)) {
    std::ifstream f(archivo_);
    const json j = json::parse(f, nullptr, false);
    datos_ = j.is_object() ? j : json{{"nombre", ""}, {"integrantes", json::array()}};
    if (!datos_.contains("integrantes") || !datos_["integrantes"].is_array()) datos_["integrantes"] = json::array();
    if (!datos_.contains("tomas") || !datos_["tomas"].is_object()) datos_["tomas"] = json::object();
    // Antes la toma era de la persona: pasa a su banca (una vez; la de la banca gana).
    bool migro = false;
    for (auto& i : datos_["integrantes"]) {
        if (!i.contains("preset_ptz")) continue;
        const json p = i["preset_ptz"];
        const std::string banca = i.value("banca", std::string());
        if (p.is_object() && !p.value("camara", std::string()).empty() && !banca.empty() && toma(banca).empty())
            datos_["tomas"][banca] = {{"camara", p.value("camara", std::string())}, {"posicion", p.value("posicion", std::string())}};
        i.erase("preset_ptz");
        migro = true;
    }
    if (migro) guardarArchivo();
}

std::string Cuerpo::claveToma(const std::string& banca) const {
    const std::string k = plano(banca);
    if (k.empty()) return {};
    const json tomas = datos_.value("tomas", json::object()); // (no recorrer un temporal)
    for (const auto& [clave, v] : tomas.items())
        if (plano(clave) == k) return clave;
    return {};
}

json Cuerpo::toma(const std::string& banca) const {
    const std::string k = claveToma(banca);
    if (k.empty()) return json::object();
    const json t = datos_["tomas"][k];
    return t.is_object() && !t.value("camara", std::string()).empty() ? t : json::object();
}

std::string Cuerpo::ponerToma(const std::string& banca, const std::string& camara, const std::string& posicion) {
    if (plano(banca).empty()) return "falta la banca";
    if (!camara.empty() && posicion.empty()) return "falta la posición de la cámara";
    const std::string ya = claveToma(banca);
    if (!ya.empty()) datos_["tomas"].erase(ya);
    if (!camara.empty()) datos_["tomas"][ya.empty() ? banca : ya] = {{"camara", camara}, {"posicion", posicion}};
    return {};
}

std::string Cuerpo::setToma(const std::string& banca, const std::string& camara, const std::string& posicion) {
    const std::string e = ponerToma(banca, camara, posicion);
    if (e.empty()) guardarArchivo();
    return e;
}

json Cuerpo::conToma(json i) const {
    if (!i.is_object()) return i;
    const json t = toma(i.value("banca", std::string()));
    if (!t.empty()) i["preset_ptz"] = t;
    return i;
}

json Cuerpo::integrantes() const {
    json out = json::array();
    for (const auto& i : datos_.value("integrantes", json::array())) out.push_back(conToma(i));
    return out;
}

void Cuerpo::guardarArchivo() const {
    std::error_code ec;
    fs::create_directories(archivo_.parent_path(), ec);
    const fs::path tmp = archivo_.string() + ".tmp";
    {
        std::ofstream o(tmp);
        o << datos_.dump(1, ' ', false, nlohmann::json::error_handler_t::replace);
    }
    fs::rename(tmp, archivo_, ec);
    // Todo cambio termina guardándose acá: es el lugar para avisar.
    if (alCambiar_ && !importando_) alCambiar_();
}

json Cuerpo::recinto(bool completo) const {
    json r = datos_.value("recinto", json::object());
    if (!r.is_object()) r = json::object();
    json out{{"filas", r.value("filas", 0)}, {"columnas", r.value("columnas", 0)}, {"bancas", json::array()},
             {"distribucion", r.value("distribucion", std::string("grilla"))}};
    std::vector<std::string> ubicadas;
    for (const auto& b : r.value("bancas", json::array())) {
        json lugar{{"banca", b.value("banca", std::string())}, {"fila", b.value("fila", 0)}, {"columna", b.value("columna", 0)}};
        if (const json t = toma(lugar["banca"].get<std::string>()); !t.empty()) lugar["toma"] = t;
        ubicadas.push_back(plano(lugar["banca"].get<std::string>()));
        if (completo) {
            const json i = buscar(lugar["banca"].get<std::string>());
            if (!i.empty() && plano(i.value("banca", std::string())) == ubicadas.back())
                lugar["integrante"] = {{"id", i.value("id", std::string())},
                                       {"nombre", i.value("nombre", std::string())},
                                       {"bloque", i.value("bloque", std::string())},
                                       {"preset_ptz", i.value("preset_ptz", json::object())},
                                       {"validado", i.contains("validado_por")}};
        }
        out["bancas"].push_back(lugar);
    }
    if (completo) {
        json sin = json::array();
        for (const auto& i : datos_["integrantes"]) {
            const std::string b = i.value("banca", std::string());
            if (!b.empty() && std::find(ubicadas.begin(), ubicadas.end(), plano(b)) == ubicadas.end()) sin.push_back(b);
        }
        out["sin_lugar"] = sin;
    }
    return out;
}

std::string Cuerpo::setRecinto(const json& c) {
    json r = recinto(false);
    if (c.contains("filas")) r["filas"] = c.value("filas", 0);
    if (c.contains("columnas")) r["columnas"] = c.value("columnas", 0);
    // Cómo se dibuja: la grilla de siempre, un hemiciclo (las filas son arcos y las
    // columnas se reparten de izquierda a derecha) o una lista.
    if (c.contains("distribucion")) {
        const std::string d = c.value("distribucion", std::string());
        if (d != "grilla" && d != "hemiciclo" && d != "lista") return "distribucion: grilla, hemiciclo o lista";
        r["distribucion"] = d;
        if (!c.contains("filas") && !c.contains("columnas") && !c.contains("bancas") && !c.contains("mover") &&
            !c.value("auto", false)) {
            datos_["recinto"] = r;
            guardarArchivo();
            return {};
        }
    }
    const int filas = r.value("filas", 0), columnas = r.value("columnas", 0);
    if (filas < 1 || filas > 40 || columnas < 1 || columnas > 60) return "filas entre 1 y 40 y columnas entre 1 y 60";
    if (c.contains("bancas")) {
        if (!c["bancas"].is_array()) return "bancas: una lista de {banca, fila, columna}";
        r["bancas"] = json::array();
        for (const auto& b : c["bancas"])
            r["bancas"].push_back({{"banca", b.value("banca", std::string())}, {"fila", b.value("fila", 0)}, {"columna", b.value("columna", 0)}});
    }
    if (c.value("auto", false)) {
        // Las bancas del cuerpo en orden (numérico si son números), de adelante
        // para atrás y de izquierda a derecha.
        std::vector<std::string> bancas;
        for (const auto& i : datos_["integrantes"])
            if (!i.value("banca", std::string()).empty()) bancas.push_back(i.value("banca", std::string()));
        std::sort(bancas.begin(), bancas.end(), [](const std::string& a, const std::string& b) {
            const bool na = !a.empty() && std::all_of(a.begin(), a.end(), ::isdigit), nb = !b.empty() && std::all_of(b.begin(), b.end(), ::isdigit);
            if (na && nb) return std::stoll(a) < std::stoll(b);
            if (na != nb) return na;
            return a < b;
        });
        if (static_cast<int>(bancas.size()) > filas * columnas)
            return "no entran " + std::to_string(bancas.size()) + " bancas en " + std::to_string(filas) + "×" + std::to_string(columnas);
        r["bancas"] = json::array();
        for (size_t k = 0; k < bancas.size(); ++k)
            r["bancas"].push_back({{"banca", bancas[k]}, {"fila", static_cast<int>(k) / columnas}, {"columna", static_cast<int>(k) % columnas}});
    }
    if (c.contains("mover")) {
        const json m = c["mover"];
        const std::string banca = m.value("banca", std::string());
        if (banca.empty()) return "mover: falta la banca";
        auto& lista = r["bancas"];
        for (auto it = lista.begin(); it != lista.end();)
            it = plano(it->value("banca", std::string())) == plano(banca) ? lista.erase(it) : it + 1;
        if (m.value("fila", 0) >= 0) lista.push_back({{"banca", banca}, {"fila", m.value("fila", 0)}, {"columna", m.value("columna", 0)}});
    }
    // Cada banca en un lugar válido, una sola vez, y un lugar por banca.
    std::vector<std::string> vistas, lugares;
    for (const auto& b : r["bancas"]) {
        const std::string banca = b.value("banca", std::string());
        const int f = b.value("fila", -1), col = b.value("columna", -1);
        if (banca.empty()) return "hay un lugar sin banca";
        if (f < 0 || f >= filas || col < 0 || col >= columnas)
            return "la banca «" + banca + "» queda fuera de la grilla (" + std::to_string(filas) + "×" + std::to_string(columnas) + ")";
        if (std::find(vistas.begin(), vistas.end(), plano(banca)) != vistas.end()) return "la banca «" + banca + "» está dos veces";
        const std::string lugar = std::to_string(f) + "," + std::to_string(col);
        if (std::find(lugares.begin(), lugares.end(), lugar) != lugares.end())
            return "dos bancas en la fila " + std::to_string(f + 1) + ", columna " + std::to_string(col + 1);
        vistas.push_back(plano(banca));
        lugares.push_back(lugar);
    }
    datos_["recinto"] = r;
    guardarArchivo();
    return {};
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
            return conToma(i);
    return json::object();
}

std::string Cuerpo::guardar(const json& c, std::string* error) {
    std::string id = c.value("id", std::string());
    json* x = nullptr;
    for (auto& i : datos_["integrantes"])
        if (!id.empty() && i.value("id", std::string()) == id) x = &i;
    // Todo lo que puede fallar, ANTES de crear: un integrante nuevo que no pasa la
    // validación no tiene que quedar a medias en la lista (se guardaba con el próximo cambio).
    if (c.contains("banca") && !c.value("banca", std::string()).empty()) {
        const json otro = buscar(c.value("banca", std::string()));
        if (!otro.empty() && otro.value("id", std::string()) != id &&
            plano(otro.value("banca", std::string())) == plano(c.value("banca", std::string()))) {
            if (error) *error = "la banca «" + c.value("banca", std::string()) + "» ya es de " + otro.value("nombre", std::string());
            return {};
        }
    }
    if (c.contains("preset_ptz") && c["preset_ptz"].is_object() && !c["preset_ptz"].value("camara", std::string()).empty()) {
        const std::string banca = c.contains("banca") ? c.value("banca", std::string())
                                  : x != nullptr      ? (*x).value("banca", std::string())
                                                      : std::string();
        if (banca.empty()) {
            if (error) *error = "sin banca no hay toma de cámara: poné primero su banca";
            return {};
        }
        if (c["preset_ptz"].value("posicion", std::string()).empty()) {
            if (error) *error = "falta la posición de la cámara";
            return {};
        }
    }
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
    // La toma (preset_ptz) es de la banca: la de este integrante, la nueva si la cambia.
    if (c.contains("preset_ptz")) {
        const std::string banca = c.contains("banca") ? c.value("banca", std::string()) : (*x).value("banca", std::string());
        const json p = c["preset_ptz"].is_object() ? c["preset_ptz"] : json::object();
        // (Validado arriba: no falla.) Se guarda abajo, con lo demás.
        if (!banca.empty()) ponerToma(banca, p.value("camara", std::string()), p.value("posicion", std::string()));
    }
    bool cambioVisible = false;
    // La prioridad (la grilla de personas ordena por ella: mayor, antes) es un número; de la
    // planilla o de una celda de texto llega escrita. Vacía = sin prioridad.
    json cn = c;
    if (cn.contains("prioridad") && cn["prioridad"].is_string()) {
        const std::string t = cn["prioridad"].get<std::string>();
        char* fin = nullptr;
        const long v = std::strtol(t.c_str(), &fin, 10);
        if (t.empty() || fin == t.c_str()) cn.erase("prioridad");
        else cn["prioridad"] = static_cast<int>(v);
        if (t.empty()) x->erase("prioridad");
    }
    for (const char* k : kCampos)
        if (cn.contains(k)) {
            if ((std::string(k) == "nombre" || std::string(k) == "cargo_oficial") && (*x).value(k, json()) != cn[k])
                cambioVisible = true;
            (*x)[k] = cn[k];
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
                       : p == "unidad dcn" || p == "unidad_dcn" || p == "dcn_unidad" || p == "unidad" ? "dcn_unidad"
                                                            : p);
    }
    if (std::find(cols.begin(), cols.end(), "nombre") == cols.end()) {
        res["errores"].push_back("la planilla no tiene la columna «nombre»");
        return res;
    }
    if (reemplazar) datos_["integrantes"] = json::array();
    importando_ = true;
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
                     cols[i] == "distrito" || cols[i] == "banca" || cols[i] == "foto" || cols[i] == "dcn_unidad" ||
                     cols[i] == "prioridad")
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
    importando_ = false;
    if (alCambiar_) alCambiar_();
    return res;
}

bool Cuerpo::escribirPlantilla(const fs::path& archivo) {
    std::error_code ec;
    fs::create_directories(archivo.parent_path(), ec);
    std::ofstream o(archivo);
    o << "banca;nombre;cargo;bloque;distrito;camara;posicion;microfono\n"
         "1;Nombre Apellido;Consejera;Bloque A;Distrito 1;Cámara sala;banca 1;Mic banca 1\n"
         "2;Otro Nombre;Consejero;Bloque B;Distrito 2;Cámara sala;banca 2;Mic banca 2\n";
    return static_cast<bool>(o);
}

} // namespace ba::app::asamblea
