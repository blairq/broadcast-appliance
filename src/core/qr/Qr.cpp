// ============================================================================
// src/core/qr/Qr.cpp — ver Qr.hpp
// ============================================================================
#include "qr/Qr.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

#include "qrcodegen.hpp"

namespace ba::qr {
namespace {

using qrcodegen::QrCode;

QrCode::Ecc nivel(Correccion c) {
    switch (c) {
        case Correccion::Baja: return QrCode::Ecc::LOW;
        case Correccion::Alta: return QrCode::Ecc::QUARTILE;
        case Correccion::Maxima: return QrCode::Ecc::HIGH;
        case Correccion::Media:
        default: return QrCode::Ecc::MEDIUM;
    }
}

/// CUANTO SE PUEDE REDONDEAR UN OJO SIN QUE DEJE DE LEERSE.
///
/// Un lector encuentra el código por los tres ojos: barre líneas y busca la
/// proporción 1:1:3:1:1 de negro y blanco. En un ojo cuadrado ESA proporción
/// sale por cualquier línea que lo cruce; en uno redondeado, solo por las del
/// medio, y cuanto más redondo, menos líneas sirven.
///
/// El límite no es opinión: se midió. Con un barrido de radios, decodificando
/// cada resultado, hasta 1.0 módulos lee y en 1.25 deja de leer. Acá se usa
/// 0.9, con margen. Estaba en 1.8 y NO se leía: se veía lindo y no servía.
constexpr double kRadioOjoMax = 0.9;

/// Los tres ojos ocupan 7x7 módulos en tres esquinas. Se dibujan aparte del
/// cuerpo (tienen su propia forma), así que el cuerpo tiene que saltearlos:
/// si no, quedarían dibujados dos veces y con formas distintas encimadas.
bool enOjo(int x, int y, int n) {
    const auto dentro = [&](int ox, int oy) { return x >= ox && x < ox + 7 && y >= oy && y < oy + 7; };
    return dentro(0, 0) || dentro(n - 7, 0) || dentro(0, n - 7);
}

/// Un cuadrado con las esquinas redondeadas `r` (en módulos), como camino SVG.
void redondeado(std::ostringstream& o, double x, double y, double w, double h, double r) {
    r = std::min(r, std::min(w, h) / 2.0);
    o << "M" << x + r << ',' << y << "h" << w - 2 * r << "a" << r << ',' << r << " 0 0 1 " << r << ',' << r
      << "v" << h - 2 * r << "a" << r << ',' << r << " 0 0 1 " << -r << ',' << r << "h" << -(w - 2 * r)
      << "a" << r << ',' << r << " 0 0 1 " << -r << ',' << -r << "v" << -(h - 2 * r) << "a" << r << ',' << r
      << " 0 0 1 " << r << ',' << -r << "z";
}

void circulo(std::ostringstream& o, double cx, double cy, double r) {
    // Dos arcos: un círculo completo no se puede con un solo arco SVG.
    o << "M" << cx - r << ',' << cy << "a" << r << ',' << r << " 0 1 0 " << 2 * r << ",0"
      << "a" << r << ',' << r << " 0 1 0 " << -2 * r << ",0z";
}

/// Un módulo del cuerpo, con la forma que pida el estilo.
void modulo(std::ostringstream& o, Puntos p, int x, int y) {
    const double fx = x;
    const double fy = y;
    switch (p) {
        case Puntos::Cuadrados:
            o << "M" << fx << ',' << fy << "h1v1h-1z";
            break;
        case Puntos::Redondeados:
            redondeado(o, fx + 0.04, fy + 0.04, 0.92, 0.92, 0.28);
            break;
        case Puntos::MuyRedondeados:
            redondeado(o, fx + 0.03, fy + 0.03, 0.94, 0.94, 0.47);
            break;
        case Puntos::Circulos:
            circulo(o, fx + 0.5, fy + 0.5, 0.46);
            break;
        case Puntos::Separados:
            // Separados de verdad: el hueco es lo que se ve.
            o << "M" << fx + 0.12 << ',' << fy + 0.12 << "h0.76v0.76h-0.76z";
            break;
    }
}

/// Un ojo: el marco de 7x7 y el punto de 3x3 del centro. Se dibujan como dos
/// figuras concéntricas, con el hueco del medio hecho por la regla de
/// relleno "evenodd" del SVG (no hay que pintar el fondo por dentro).
void ojo(std::ostringstream& o, Ojos e, int ox, int oy) {
    const double x = ox;
    const double y = oy;
    if (e == Ojos::Cuadrados) {
        o << "M" << x << ',' << y << "h7v7h-7z";
        o << "M" << x + 1 << ',' << y + 1 << "v5h5v-5z";  // hueco (sentido inverso)
        o << "M" << x + 2 << ',' << y + 2 << "h3v3h-3z";  // el punto del centro
        return;
    }
    // Redondeados, con el radio que se midió que todavía se lee.
    const double r = kRadioOjoMax;
    redondeado(o, x, y, 7, 7, r);
    redondeado(o, x + 1, y + 1, 5, 5, r * 0.66);
    redondeado(o, x + 2, y + 2, 3, 3, r * 0.42);
}

std::string colorSeguro(const std::string& c, const std::string& siVacio) {
    // Solo se dejan pasar colores que no puedan romper el SVG: #rgb, #rrggbb,
    // o un nombre de una palabra. Lo demás se cambia por el de fábrica, que
    // es mejor que generar un SVG inválido con lo que haya escrito alguien.
    if (c.empty()) return siVacio;
    if (c[0] == '#') {
        if (c.size() != 4 && c.size() != 7) return siVacio;
        for (std::size_t i = 1; i < c.size(); ++i)
            if (std::isxdigit(static_cast<unsigned char>(c[i])) == 0) return siVacio;
        return c;
    }
    for (const char ch : c)
        if (std::isalpha(static_cast<unsigned char>(ch)) == 0) return siVacio;
    return c;
}

void escapar(std::ostringstream& o, const std::string& s) {
    for (const char c : s) {
        switch (c) {
            case '&': o << "&amp;"; break;
            case '<': o << "&lt;"; break;
            case '>': o << "&gt;"; break;
            case '"': o << "&quot;"; break;
            case '\'': o << "&apos;"; break;
            default: o << c;
        }
    }
}

/// Lo que se escapa adentro de una carga: el punto y coma y la barra invertida
/// separan campos en WIFI: y en MECARD, así que un nombre de red con un ";"
/// partiría el código en dos si se copiara tal cual.
std::string escaparCampo(const std::string& s) {
    std::string o;
    for (const char c : s) {
        if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') o.push_back('\\');
        o.push_back(c);
    }
    return o;
}

} // namespace

int lado(const std::string& texto, Correccion c) {
    if (texto.empty()) return 0;
    try {
        return QrCode::encodeText(texto.c_str(), nivel(c)).getSize();
    } catch (const std::exception&) {
        return 0;
    }
}

std::string svg(const std::string& texto, const Estilo& e, std::string* error) {
    if (texto.empty()) {
        if (error != nullptr) *error = "No hay nada que codificar.";
        return {};
    }
    // Con logo se tapan módulos: la corrección sube sola a la máxima. No es
    // una preferencia, es lo que hace que el código siga leyéndose.
    const double logo = std::clamp(e.logo, 0.0, 0.30);
    const Correccion corr = logo > 0 ? Correccion::Maxima : e.correccion;

    QrCode qr = QrCode::encodeText("", QrCode::Ecc::LOW);
    try {
        qr = QrCode::encodeText(texto.c_str(), nivel(corr));
    } catch (const std::exception&) {
        if (error != nullptr)
            *error = "El texto es demasiado largo para un código QR (probá con menos corrección de "
                     "errores, o acortá el enlace).";
        return {};
    }

    const int n = qr.getSize();
    const int z = std::clamp(e.zonaSilenciosa, 0, 16);
    const int lado = n + z * 2;
    const std::string primario = colorSeguro(e.primario, "#000000");
    const std::string secundario = e.secundario.empty() ? "" : colorSeguro(e.secundario, primario);
    const bool degradado = !secundario.empty() && secundario != primario;
    const std::string fondo = e.fondo == "transparent" ? "" : colorSeguro(e.fondo, "#FFFFFF");

    std::ostringstream o;
    o.setf(std::ios::fixed);
    o.precision(3);
    o << "<svg xmlns='http://www.w3.org/2000/svg' xmlns:xlink='http://www.w3.org/1999/xlink' viewBox='0 0 "
      << lado << ' ' << lado << "' shape-rendering='geometricPrecision'>";
    if (degradado) {
        o << "<defs><linearGradient id='g' x1='0' y1='0' x2='1' y2='1'>"
          << "<stop offset='0' stop-color='" << primario << "'/>"
          << "<stop offset='1' stop-color='" << secundario << "'/></linearGradient></defs>";
    }
    if (!fondo.empty()) {
        if (e.marco == Marco::Redondeado)
            o << "<rect width='" << lado << "' height='" << lado << "' rx='" << z * 0.8 << "' fill='"
              << fondo << "'/>";
        else
            o << "<rect width='100%' height='100%' fill='" << fondo << "'/>";
    }

    const std::string pinturaStr = degradado ? "url(#g)" : primario;

    // El hueco del logo, en módulos: se saltean los del centro.
    const int huecoLado = logo > 0 ? static_cast<int>(n * logo) + 1 : 0;
    const int huecoDesde = (n - huecoLado) / 2;
    const int huecoHasta = huecoDesde + huecoLado;

    // --- El cuerpo -------------------------------------------------------
    o << "<path fill='" << pinturaStr << "' d='";
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            if (!qr.getModule(x, y) || enOjo(x, y, n)) continue;
            if (huecoLado > 0 && x >= huecoDesde && x < huecoHasta && y >= huecoDesde && y < huecoHasta)
                continue;
            modulo(o, e.puntos, x + z, y + z);
        }
    }
    o << "'/>";

    // --- Los tres ojos ---------------------------------------------------
    // `evenodd`: el anillo queda hueco sin tener que pintar el fondo adentro,
    // que es lo que permite que el QR sea transparente.
    o << "<path fill='" << pinturaStr << "' fill-rule='evenodd' d='";
    ojo(o, e.ojos, z, z);
    ojo(o, e.ojos, n - 7 + z, z);
    ojo(o, e.ojos, z, n - 7 + z);
    o << "'/>";

    // --- El logo ---------------------------------------------------------
    if (huecoLado > 0) {
        const double lx = huecoDesde + z;
        const double ancho = huecoLado;
        if (!fondo.empty())
            o << "<rect x='" << lx - 0.3 << "' y='" << lx - 0.3 << "' width='" << ancho + 0.6
              << "' height='" << ancho + 0.6 << "' rx='" << ancho * 0.15 << "' fill='" << fondo << "'/>";
        if (!e.logoSrc.empty()) {
            o << "<image x='" << lx << "' y='" << lx << "' width='" << ancho << "' height='" << ancho
              << "' preserveAspectRatio='xMidYMid meet' xlink:href='";
            escapar(o, e.logoSrc);
            o << "'/>";
        }
    }

    // --- El marco --------------------------------------------------------
    if (e.marco != Marco::Ninguno) {
        const double g = 0.6; // grosor, en módulos
        o << "<rect x='" << g / 2 << "' y='" << g / 2 << "' width='" << lado - g << "' height='"
          << lado - g << "' rx='" << (e.marco == Marco::Redondeado ? z * 0.8 : 0.0)
          << "' fill='none' stroke='" << pinturaStr << "' stroke-width='" << g << "'/>";
    }
    o << "</svg>";
    return o.str();
}

// --- Las cargas ---------------------------------------------------------------

std::string wifi(const std::string& red, const std::string& clave, const std::string& seguridad,
                 bool oculta) {
    std::string s = seguridad;
    if (s != "WPA" && s != "WEP" && s != "nopass") s = "WPA";
    std::string out = "WIFI:S:" + escaparCampo(red) + ";T:" + s + ";";
    if (s != "nopass") out += "P:" + escaparCampo(clave) + ";";
    if (oculta) out += "H:true;";
    return out + ";";
}

std::string contacto(const std::string& nombre, const std::string& telefono, const std::string& email,
                     const std::string& organizacion, const std::string& cargo, const std::string& web) {
    // vCard 3.0: es la que entienden Android y iPhone sin pensarlo.
    std::string out = "BEGIN:VCARD\nVERSION:3.0\nFN:" + nombre + "\n";
    // N (apellido;nombre) sale de partir FN por el último espacio: no es
    // perfecto, pero sin N algunas agendas no lo guardan.
    const auto esp = nombre.find_last_of(' ');
    if (esp != std::string::npos)
        out += "N:" + nombre.substr(esp + 1) + ";" + nombre.substr(0, esp) + ";;;\n";
    else
        out += "N:" + nombre + ";;;;\n";
    if (!organizacion.empty()) out += "ORG:" + organizacion + "\n";
    if (!cargo.empty()) out += "TITLE:" + cargo + "\n";
    if (!telefono.empty()) out += "TEL;TYPE=CELL:" + telefono + "\n";
    if (!email.empty()) out += "EMAIL:" + email + "\n";
    if (!web.empty()) out += "URL:" + web + "\n";
    return out + "END:VCARD";
}

std::string reunion(const std::string& plataforma, const std::string& codigo, const std::string& clave) {
    // Si ya pegaron el enlace entero, se respeta: es lo que hace la gente.
    if (codigo.rfind("http", 0) == 0) return codigo;
    if (plataforma == "zoom") {
        std::string u = "https://zoom.us/j/" + codigo;
        if (!clave.empty()) u += "?pwd=" + clave;
        return u;
    }
    if (plataforma == "teams") return codigo;
    return "https://meet.google.com/" + codigo;
}

std::string telefono(const std::string& numero) { return "tel:" + numero; }

std::string sms(const std::string& numero, const std::string& texto) {
    return texto.empty() ? "sms:" + numero : "sms:" + numero + "?body=" + texto;
}

std::string correo(const std::string& direccion, const std::string& asunto, const std::string& cuerpo) {
    std::string u = "mailto:" + direccion;
    std::string q;
    if (!asunto.empty()) q += "subject=" + asunto;
    if (!cuerpo.empty()) q += (q.empty() ? "" : "&") + std::string("body=") + cuerpo;
    return q.empty() ? u : u + "?" + q;
}

} // namespace ba::qr
