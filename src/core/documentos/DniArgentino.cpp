// ============================================================================
// src/core/documentos/DniArgentino.cpp — ver DniArgentino.hpp
// ============================================================================
#include "documentos/DniArgentino.hpp"

#include <algorithm>

namespace ba::documentos {
namespace {

bool soloDigitos(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

/// El número del DNI: sin espacios ni puntos, sin una letra adelante (algunos
/// viejos la traen) y sin ceros a la izquierda. "" si no son de 6 a 9 dígitos.
std::string numeroDni(const std::string& s) {
    std::string d;
    for (char c : s)
        if (c != ' ' && c != '.') d.push_back(c);
    if (!d.empty() && ((d[0] >= 'A' && d[0] <= 'Z') || (d[0] >= 'a' && d[0] <= 'z'))) d.erase(0, 1);
    if (!soloDigitos(d)) return {};
    d.erase(0, std::min(d.find_first_not_of('0'), d.size()));
    if (d.size() < 6 || d.size() > 9) return {};
    return d;
}

std::string sexoDni(const std::string& s) {
    const std::string x = recortar(s);
    if (x == "M" || x == "F" || x == "X") return x;
    if (x == "m" || x == "f" || x == "x") return std::string(1, static_cast<char>(x[0] - 'a' + 'A'));
    return {};
}

/// Un apellido o nombre del código: letras (también las de UTF-8), espacios,
/// apóstrofos, guiones y puntos. Sin dígitos: un campo corrido de lugar trae números.
bool pareceNombre(const std::string& s) {
    if (recortar(s).empty()) return false;
    return std::none_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || c == '/'; });
}

std::string ejemplarDni(const std::string& s) {
    const std::string x = recortar(s);
    return x.size() == 1 && x[0] >= 'A' && x[0] <= 'Z' ? x : std::string();
}

} // namespace

bool DniArgentino::leer(const Codigo& c, Datos& out) const {
    const std::string texto = recortar(c.texto);
    if (texto.find('@') == std::string::npos) return false;
    const std::vector<std::string> f = partir(texto, '@');
    Datos d;
    d.pais = "AR";
    if (texto[0] == '@') {
        // El viejo: @DNI@ejemplar@?@apellido@nombres@nacionalidad@nacimiento@sexo@emisión@trámite@?@vencimiento@…
        if (f.size() < 13) return false;
        d.numero = numeroDni(f[1]);
        d.ejemplar = ejemplarDni(f[2]);
        if (!pareceNombre(f[4])) return false;
        d.apellido = capitalizar(recortar(f[4]));
        if (pareceNombre(f[5])) d.nombres = capitalizar(recortar(f[5]));
        if (pareceNombre(f[6])) d.nacionalidad = capitalizar(recortar(f[6]));
        d.nacimiento = fechaIso(f[7]);
        d.sexo = sexoDni(f[8]);
        d.emision = fechaIso(f[9]);
        if (soloDigitos(recortar(f[10]))) d.tramite = recortar(f[10]);
        d.vencimiento = fechaIso(f[12]);
    } else {
        // El nuevo: trámite@apellido@nombres@sexo@DNI@ejemplar@nacimiento@emisión[@cuil]
        if (f.size() < 8) return false;
        const std::string tramite = recortar(f[0]);
        if (!soloDigitos(tramite)) return false;
        d.tramite = tramite;
        if (!pareceNombre(f[1])) return false;
        d.apellido = capitalizar(recortar(f[1]));
        if (pareceNombre(f[2])) d.nombres = capitalizar(recortar(f[2]));
        d.sexo = sexoDni(f[3]);
        d.numero = numeroDni(f[4]);
        d.ejemplar = ejemplarDni(f[5]);
        d.nacimiento = fechaIso(f[6]);
        d.emision = fechaIso(f[7]);
        // El noveno: prefijo y dígito verificador del CUIL. Solo si cierra la
        // cuenta: si no, es otra cosa y no se inventa un CUIL.
        if (f.size() >= 9) {
            const std::string x = recortar(f[8]);
            if (x.size() == 3 && soloDigitos(x) && !d.numero.empty() && d.numero.size() <= 8) {
                const std::string ocho = std::string(8 - d.numero.size(), '0') + d.numero;
                const int dv = digitoCuil(x.substr(0, 2) + ocho);
                if (dv >= 0 && dv == x[2] - '0') d.cuil = x.substr(0, 2) + "-" + ocho + "-" + x.substr(2, 1);
            }
        }
    }
    // Lo esencial: el número y una fecha de nacimiento que sea fecha. Con eso
    // se descarta un código de otra cosa que casualmente tenga arrobas.
    if (d.numero.empty() || d.nacimiento.empty()) return false;
    d.crudo = c.texto;
    out = std::move(d);
    return true;
}

} // namespace ba::documentos
