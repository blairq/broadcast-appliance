// ============================================================================
// src/core/documentos/Documento.cpp — ver Documento.hpp
// ============================================================================
#include "documentos/Documento.hpp"

#include <algorithm>
#include <cctype>

#include "documentos/DniArgentino.hpp"

namespace ba::documentos {
namespace {

using nlohmann::json;

std::string textoDe(const json& j, const char* k) {
    return j.is_object() && j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string();
}

bool esDigito(char c) { return c >= '0' && c <= '9'; }

/// Una letra de un texto en mayúsculas pasada a minúscula. Las de un byte con
/// tolower de ASCII; las acentuadas de UTF-8 que trae un documento (Á É Í Ó Ú Ü
/// Ñ, todas C3 xx) sumando 0x20 al segundo byte, que es como están en la tabla.
void aMinuscula(const std::string& s, std::size_t& i, std::string& out) {
    const auto c = static_cast<unsigned char>(s[i]);
    if (c == 0xC3 && i + 1 < s.size()) {
        const auto d = static_cast<unsigned char>(s[i + 1]);
        out.push_back(s[i]);
        out.push_back(static_cast<char>(d >= 0x80 && d <= 0x9E && d != 0x97 ? d + 0x20 : d));
        i += 2;
        return;
    }
    out.push_back(c < 0x80 ? static_cast<char>(std::tolower(c)) : s[i]);
    ++i;
}

/// La letra en `i` (de uno o dos bytes) a mayúscula, al revés que `aMinuscula`.
void aMayusculaEn(std::string& s, std::size_t i) {
    if (i >= s.size()) return;
    const auto c = static_cast<unsigned char>(s[i]);
    if (c == 0xC3 && i + 1 < s.size()) {
        const auto d = static_cast<unsigned char>(s[i + 1]);
        if (d >= 0xA0 && d <= 0xBE && d != 0xB7) s[i + 1] = static_cast<char>(d - 0x20);
        return;
    }
    if (c < 0x80) s[i] = static_cast<char>(std::toupper(c));
}

} // namespace

// --- Datos -------------------------------------------------------------------

json Datos::aJson() const {
    json j = json::object();
    const auto poner = [&j](const char* k, const std::string& v) {
        if (!v.empty()) j[k] = v;
    };
    poner("tipo", tipo);
    poner("pais", pais);
    poner("numero", numero);
    poner("apellido", apellido);
    poner("nombres", nombres);
    poner("sexo", sexo);
    poner("nacimiento", nacimiento);
    poner("emision", emision);
    poner("vencimiento", vencimiento);
    poner("nacionalidad", nacionalidad);
    poner("tramite", tramite);
    poner("ejemplar", ejemplar);
    poner("cuil", cuil);
    poner("crudo", crudo);
    if (extra.is_object() && !extra.empty()) j["extra"] = extra;
    return j;
}

Datos Datos::deJson(const json& j) {
    Datos d;
    d.tipo = textoDe(j, "tipo");
    d.pais = textoDe(j, "pais");
    d.numero = textoDe(j, "numero");
    d.apellido = textoDe(j, "apellido");
    d.nombres = textoDe(j, "nombres");
    d.sexo = textoDe(j, "sexo");
    d.nacimiento = textoDe(j, "nacimiento");
    d.emision = textoDe(j, "emision");
    d.vencimiento = textoDe(j, "vencimiento");
    d.nacionalidad = textoDe(j, "nacionalidad");
    d.tramite = textoDe(j, "tramite");
    d.ejemplar = textoDe(j, "ejemplar");
    d.cuil = textoDe(j, "cuil");
    d.crudo = textoDe(j, "crudo");
    if (j.is_object() && j.contains("extra") && j["extra"].is_object()) d.extra = j["extra"];
    return d;
}

std::string Datos::nombreCompleto() const {
    if (nombres.empty()) return apellido;
    if (apellido.empty()) return nombres;
    return nombres + " " + apellido;
}

// --- Catálogo ----------------------------------------------------------------

Catalogo::Catalogo() { registrar(std::make_unique<DniArgentino>()); }
Catalogo::~Catalogo() = default;

void Catalogo::registrar(std::unique_ptr<TipoDocumento> t) {
    if (t == nullptr || porId(t->id()) != nullptr) return;
    tipos_.push_back(std::move(t));
}

const TipoDocumento* Catalogo::porId(const std::string& id) const {
    for (const auto& t : tipos_)
        if (t->id() == id) return t.get();
    return nullptr;
}

bool Catalogo::leer(const Codigo& c, const std::vector<std::string>& aceptados, Datos& out) const {
    const auto probar = [&c, &out](const TipoDocumento& t) {
        const auto fs = t.formatos();
        if (!fs.empty() && !c.formato.empty() && std::find(fs.begin(), fs.end(), c.formato) == fs.end()) return false;
        Datos d;
        if (!t.leer(c, d)) return false;
        d.tipo = t.id();
        if (d.crudo.empty()) d.crudo = c.texto;
        out = std::move(d);
        return true;
    };
    if (aceptados.empty()) {
        for (const auto& t : tipos_)
            if (probar(*t)) return true;
        return false;
    }
    for (const std::string& id : aceptados)
        if (const TipoDocumento* t = porId(id); t != nullptr && probar(*t)) return true;
    return false;
}

// --- Ayudas ------------------------------------------------------------------

std::vector<std::string> partir(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string actual;
    for (char c : s) {
        if (c == sep) {
            out.push_back(actual);
            actual.clear();
        } else {
            actual.push_back(c);
        }
    }
    out.push_back(actual);
    return out;
}

std::string recortar(const std::string& s) {
    const char* blancos = " \t\r\n";
    const auto a = s.find_first_not_of(blancos);
    if (a == std::string::npos) return {};
    const auto b = s.find_last_not_of(blancos);
    return s.substr(a, b - a + 1);
}

std::string fechaIso(const std::string& f) {
    const std::string s = recortar(f);
    if (s.size() != 10) return {};
    for (std::size_t i : {0u, 1u, 3u, 4u, 6u, 7u, 8u, 9u})
        if (!esDigito(s[i])) return {};
    if (s[2] != s[5] || (s[2] != '/' && s[2] != '-' && s[2] != '.')) return {};
    const int dia = std::stoi(s.substr(0, 2));
    const int mes = std::stoi(s.substr(3, 2));
    const int anio = std::stoi(s.substr(6, 4));
    if (mes < 1 || mes > 12 || dia < 1 || anio < 1900 || anio > 2200) return {};
    static const int kDias[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool bisiesto = (anio % 4 == 0 && anio % 100 != 0) || anio % 400 == 0;
    if (dia > kDias[mes - 1] || (mes == 2 && dia == 29 && !bisiesto)) return {};
    return s.substr(6, 4) + "-" + s.substr(3, 2) + "-" + s.substr(0, 2);
}

std::string capitalizar(const std::string& s) {
    // Palabra por palabra: todo en minúscula y la inicial en mayúscula. Las
    // partículas («de», «del», «la»…) quedan en minúscula salvo al principio.
    static const char* kParticulas[] = {"de", "del", "la", "las", "los", "y", "da", "di", "van", "von"};
    std::string out;
    bool primera = true;
    for (const std::string& w : partir(s, ' ')) {
        if (w.empty()) continue;
        std::string pal;
        for (std::size_t k = 0; k < w.size();) aMinuscula(w, k, pal);
        const bool particula = std::any_of(std::begin(kParticulas), std::end(kParticulas),
                                           [&pal](const char* p) { return pal == p; });
        if (!particula || primera) aMayusculaEn(pal, 0);
        // Apóstrofos y guiones: «D'ANGELO» -> «D'Angelo», «PEREZ-GARCIA» -> «Perez-Garcia».
        for (std::size_t p = 1; p + 1 < pal.size(); ++p)
            if (pal[p] == '\'' || pal[p] == '-') aMayusculaEn(pal, p + 1);
        if (!out.empty()) out += ' ';
        out += pal;
        primera = false;
    }
    return out;
}

std::string numeroConPuntos(const std::string& numero) {
    if (numero.empty() || !std::all_of(numero.begin(), numero.end(), esDigito)) return numero;
    std::string out;
    const std::size_t n = numero.size();
    for (std::size_t i = 0; i < n; ++i) {
        if (i > 0 && (n - i) % 3 == 0) out.push_back('.');
        out.push_back(numero[i]);
    }
    return out;
}

std::string paraLog(const Datos& d) {
    const std::string tipo = d.tipo.empty() ? std::string("documento") : d.tipo;
    if (d.numero.size() <= 3) return tipo;
    return tipo + " …" + d.numero.substr(d.numero.size() - 3);
}

int digitoCuil(const std::string& s) {
    if (s.size() != 10 || !std::all_of(s.begin(), s.end(), esDigito)) return -1;
    static const int kPesos[] = {5, 4, 3, 2, 7, 6, 5, 4, 3, 2};
    int suma = 0;
    for (std::size_t i = 0; i < 10; ++i) suma += (s[i] - '0') * kPesos[i];
    const int dv = 11 - suma % 11;
    if (dv == 11) return 0;
    if (dv == 10) return -1;
    return dv;
}

} // namespace ba::documentos
