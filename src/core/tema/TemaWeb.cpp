// ============================================================================
// src/core/tema/TemaWeb.cpp — ver TemaWeb.hpp
// ============================================================================
#include "tema/TemaWeb.hpp"

#include <charconv>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace ba::tema {

using json = nlohmann::json;

namespace {

int hexDig(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

double lineal(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }

// Con punto siempre: printf usa la coma del idioma del sistema («0,50») y el CSS la rechaza.
std::string conDosDecimales(double v) {
    char b[32];
    const auto r = std::to_chars(b, b + sizeof b, v, std::chars_format::fixed, 2);
    return std::string(b, r.ptr);
}

/// Lo que haya en `o[clave]` si es texto; si no, "".
std::string texto(const json& o, const char* clave) {
    if (!o.is_object()) return {};
    const auto it = o.find(clave);
    return it != o.end() && it->is_string() ? it->get<std::string>() : std::string();
}

/// El nombre de una familia tipográfica, sin nada que pueda romper la hoja: letras, números,
/// espacio, «-» y «_» (y lo que no es ASCII, para «Señal» o «思源»). Hasta 60 bytes, cortando
/// en el borde de una letra.
std::string familia(const std::string& s) {
    std::string out;
    for (const char ch : s) {
        const auto u = static_cast<unsigned char>(ch);
        const bool vale = u >= 0x80 || (u >= '0' && u <= '9') || (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') ||
                          u == ' ' || u == '-' || u == '_';
        if (!vale) continue;
        if (u == ' ' && (out.empty() || out.back() == ' ')) continue;
        out.push_back(ch);
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    if (out.size() > 60) {
        std::size_t n = 60;
        while (n > 0 && (static_cast<unsigned char>(out[n]) & 0xC0) == 0x80) --n; // no partir una letra
        out.resize(n);
    }
    return out;
}

/// La pila de letras de respaldo: si la fuente del evento no tiene un glifo (chino, árabe),
/// el navegador sigue por acá; la del perfil de idioma no se pierde.
const char* const kRespaldo = "system-ui, -apple-system, \"Segoe UI\", Roboto, \"Noto Sans\", sans-serif";

std::string pila(const std::string& fam) { return fam.empty() ? std::string() : "\"" + fam + "\", " + kRespaldo; }

const Rgb kBlanco{1, 1, 1};
const Rgb kNegro{0, 0, 0};
const Rgb kTintaOscura{11 / 255.0, 11 / 255.0, 12 / 255.0}; // la del sitio (--accent-ink)

/// El que más contrasta con `fondo` de los dos.
Rgb masLegible(const Rgb& fondo, const Rgb& claro, const Rgb& oscuro) {
    return contraste(claro, fondo) >= contraste(oscuro, fondo) ? claro : oscuro;
}

} // namespace

// --- Colores ------------------------------------------------------------------------

std::optional<Rgb> leerColor(const std::string& textoIn) {
    std::string t = textoIn;
    while (!t.empty() && t.front() == ' ') t.erase(t.begin());
    while (!t.empty() && t.back() == ' ') t.pop_back();
    if (!t.empty() && t.front() == '#') t.erase(t.begin());
    if (t.size() != 3 && t.size() != 6) return std::nullopt;
    int v[6];
    for (std::size_t i = 0; i < t.size(); ++i)
        if ((v[i] = hexDig(t[i])) < 0) return std::nullopt;
    if (t.size() == 3) return Rgb{v[0] * 17 / 255.0, v[1] * 17 / 255.0, v[2] * 17 / 255.0};
    return Rgb{(v[0] * 16 + v[1]) / 255.0, (v[2] * 16 + v[3]) / 255.0, (v[4] * 16 + v[5]) / 255.0};
}

std::string hex(const Rgb& c) {
    const auto canal = [](double x) { return static_cast<int>(std::lround(std::clamp(x, 0.0, 1.0) * 255)); };
    char b[8];
    std::snprintf(b, sizeof b, "#%02x%02x%02x", canal(c.r), canal(c.g), canal(c.b));
    return b;
}

std::string rgba(const Rgb& c, double alfa) {
    const auto canal = [](double x) { return static_cast<int>(std::lround(std::clamp(x, 0.0, 1.0) * 255)); };
    char b[48];
    std::snprintf(b, sizeof b, "rgba(%d, %d, %d, ", canal(c.r), canal(c.g), canal(c.b));
    return b + conDosDecimales(std::clamp(alfa, 0.0, 1.0)) + ")";
}

double luminancia(const Rgb& c) { return 0.2126 * lineal(c.r) + 0.7152 * lineal(c.g) + 0.0722 * lineal(c.b); }

double contraste(const Rgb& a, const Rgb& b) {
    const double la = luminancia(a), lb = luminancia(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

Rgb mezclar(const Rgb& a, const Rgb& b, double t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

// --- Alcance y archivos ---------------------------------------------------------------

Nivel nivelDe(const std::string& t, Nivel siNoVale) {
    if (t == "nada") return Nivel::Nada;
    if (t == "acento") return Nivel::Acento;
    if (t == "todo") return Nivel::Todo;
    return siNoVale;
}

const char* nombreDe(Nivel n) { return n == Nivel::Nada ? "nada" : n == Nivel::Acento ? "acento" : "todo"; }

bool archivoValido(const std::string& n) {
    if (n.empty() || n.size() > 80 || n.front() == '.') return false;
    int puntos = 0;
    for (const char ch : n) {
        if (ch == '.') { ++puntos; continue; }
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_')) return false;
    }
    return puntos == 1 && n.back() != '.';
}

namespace {
bool terminaEn(const std::string& n, const char* ext) {
    const std::string e(ext);
    return n.size() > e.size() && n.compare(n.size() - e.size(), e.size(), e) == 0;
}
} // namespace

bool esImagenDeMarca(const std::string& n) {
    return archivoValido(n) && (terminaEn(n, ".svg") || terminaEn(n, ".png") || terminaEn(n, ".webp"));
}

bool esFuente(const std::string& n) {
    return archivoValido(n) && (terminaEn(n, ".woff2") || terminaEn(n, ".woff") || terminaEn(n, ".ttf") || terminaEn(n, ".otf"));
}

// --- Resolver ---------------------------------------------------------------------------

json resolver(const json& crudo) {
    const json c = crudo.is_object() ? crudo : json::object();
    const json col = c.value("colores", json::object());
    const auto color = [&](const char* k) { return leerColor(texto(col, k)); };

    // El modo: el que diga; si no, el que sugiere el fondo; si no, oscuro (como el resto del sistema).
    const std::optional<Rgb> fondoDado = color("fondo");
    std::string modo = texto(c, "modo");
    if (modo != "claro" && modo != "oscuro") modo = fondoDado && luminancia(*fondoDado) > 0.4 ? "claro" : "oscuro";
    const bool claro = modo == "claro";

    const Rgb fondo = fondoDado.value_or(claro ? Rgb{246 / 255.0, 246 / 255.0, 247 / 255.0} : kTintaOscura);
    const Rgb acento = color("acento").value_or(*leerColor("#ff4d2e"));
    const Rgb texto_ = color("texto").value_or(masLegible(fondo, Rgb{244 / 255.0, 244 / 255.0, 245 / 255.0},
                                                          Rgb{17 / 255.0, 17 / 255.0, 20 / 255.0}));
    const Rgb superficie = color("superficie").value_or(mezclar(fondo, texto_, 0.06));
    const Rgb textoSuave = color("textoSuave").value_or(mezclar(texto_, fondo, 0.38));
    const Rgb acentoTexto = color("acentoTexto").value_or(masLegible(acento, kBlanco, kTintaOscura));

    json r;
    r["modo"] = modo;
    r["colores"] = {{"acento", hex(acento)},
                    {"acentoTexto", hex(acentoTexto)},
                    {"fondo", hex(fondo)},
                    {"superficie", hex(superficie)},
                    {"superficie2", hex(mezclar(superficie, texto_, 0.04))},
                    {"hundido", hex(mezclar(fondo, kNegro, claro ? 0.06 : 0.25))},
                    {"texto", hex(texto_)},
                    {"textoSuave", hex(textoSuave)},
                    {"textoTenue", hex(mezclar(textoSuave, fondo, 0.3))},
                    {"acento2", hex(mezclar(acento, kBlanco, 0.2))},
                    {"borde", rgba(texto_, 0.12)},
                    {"bordeFuerte", rgba(texto_, 0.22)},
                    {"foco", rgba(acento, 0.55)},
                    {"acentoSuave", rgba(acento, 0.16)},
                    {"acentoMedio", rgba(acento, 0.3)},
                    {"acentoFuerte", rgba(acento, 0.5)},
                    {"acentoNada", rgba(acento, 0)},
                    {"fondoVelo", rgba(fondo, 0.72)},
                    // Lo que las páginas tenían fijo pensado para fondo oscuro (blanco con poca
                    // opacidad para resaltar, negro para hundir y para las sombras): con un
                    // fondo claro, el texto con poca opacidad resalta y el negro casi no se ve.
                    {"tinte", rgba(texto_, 0.06)},
                    {"tinteFuerte", rgba(texto_, 0.12)},
                    {"campo", rgba(kNegro, claro ? 0.04 : 0.35)},
                    {"velo", rgba(kNegro, claro ? 0.06 : 0.55)},
                    {"sombra", rgba(kNegro, claro ? 0.14 : 0.45)}};

    // Lo que conviene avisar: con código y números, el texto lo pone quien lo muestra.
    const double cTexto = contraste(texto_, fondo), cAcento = contraste(acentoTexto, acento),
                 cAcentoFondo = contraste(acento, fondo);
    r["contraste"] = {{"texto", std::round(cTexto * 100) / 100},
                      {"acento", std::round(cAcento * 100) / 100},
                      {"acentoSobreFondo", std::round(cAcentoFondo * 100) / 100}};
    json avisos = json::array();
    if (cTexto < 4.5) avisos.push_back({{"codigo", "texto"}, {"contraste", conDosDecimales(cTexto)}, {"minimo", "4.5"}});
    if (cAcento < 4.5) avisos.push_back({{"codigo", "acentoTexto"}, {"contraste", conDosDecimales(cAcento)}, {"minimo", "4.5"}});
    if (cAcentoFondo < 3) avisos.push_back({{"codigo", "acentoFondo"}, {"contraste", conDosDecimales(cAcentoFondo)}, {"minimo", "3"}});

    // Tipografía: nombres saneados; los archivos, solo de fuentes y con nombre de Tema/.
    const json tip = c.value("tipografia", json::object());
    json archivos = json::array();
    if (tip.is_object() && tip.contains("archivos") && tip["archivos"].is_array())
        for (const json& a : tip["archivos"]) {
            const std::string fam = familia(texto(a, "familia")), arch = texto(a, "archivo");
            if (fam.empty() || !esFuente(arch)) continue;
            std::string peso;
            for (const char ch : texto(a, "peso"))
                if ((ch >= '0' && ch <= '9') || (ch == ' ' && !peso.empty() && peso.back() != ' ')) peso.push_back(ch);
            while (!peso.empty() && peso.back() == ' ') peso.pop_back();
            if (peso.empty() || peso.size() > 9) peso = "400";
            archivos.push_back({{"familia", fam}, {"archivo", arch}, {"peso", peso},
                                {"estilo", texto(a, "estilo") == "italic" ? "italic" : "normal"}});
        }
    const std::string famTexto = familia(texto(tip, "texto"));
    const std::string famTitulos = familia(texto(tip, "titulos"));
    r["tipografia"] = {{"texto", famTexto}, {"titulos", famTitulos.empty() ? famTexto : famTitulos}, {"archivos", archivos}};

    // La forma: el radio de las esquinas (vacío = el de cada página).
    const json forma = c.value("forma", json::object());
    if (forma.is_object() && forma.contains("radio") && forma["radio"].is_number())
        r["forma"] = {{"radio", std::clamp(static_cast<int>(std::lround(forma["radio"].get<double>())), 0, 32)}};
    else r["forma"] = json::object();

    // La marca: el nombre y los archivos de Tema/ (solo imágenes, con nombre válido).
    const json marca = c.value("marca", json::object());
    std::string nombre = texto(marca, "nombre");
    if (nombre.size() > 120) nombre.resize(120);
    json m{{"nombre", nombre}};
    for (const char* k : {"logo", "logoClaro", "favicon"}) {
        const std::string a = texto(marca, k);
        m[k] = esImagenDeMarca(a) ? a : std::string();
    }
    r["marca"] = m;

    // El alcance: el público ve el tema completo; las consolas, por omisión, el acento (el
    // neumorfismo depende de su fondo).
    const json al = c.value("alcance", json::object());
    r["alcance"] = {{"publico", nombreDe(nivelDe(texto(al, "publico"), Nivel::Todo))},
                    {"control", nombreDe(nivelDe(texto(al, "control"), Nivel::Acento))}};
    r["avisos"] = avisos;
    return r;
}

// --- La hoja ---------------------------------------------------------------------------

namespace {

std::string formatoDe(const std::string& archivo) {
    if (terminaEn(archivo, ".woff2")) return "woff2";
    if (terminaEn(archivo, ".woff")) return "woff";
    if (terminaEn(archivo, ".otf")) return "opentype";
    return "truetype";
}

/// Los puentes del acento, a cada dialecto: neuro y sala (--acento…), invitados y sitio
/// (--accent…), el marco del evento (--color).
std::string puentesAcento() {
    return "  --acento: var(--ev-acento); --acento-texto: var(--ev-acento-texto); --foco: var(--ev-foco);\n"
           "  --accent: var(--ev-acento); --accent-2: var(--ev-acento-2); --accent-ink: var(--ev-acento-texto);\n"
           "  --color: var(--ev-acento);\n";
}

/// Los puentes de todo lo demás. Ojo con los nombres que significan otra cosa en otro
/// dialecto: --hundido es una sombra en neuro y un color en la sala, así que no se toca.
std::string puentesTodo(const json& r) {
    std::string s;
    s += std::string("  color-scheme: ") + (r.value("modo", std::string()) == "claro" ? "light" : "dark") + ";\n";
    s += "  --fondo: var(--ev-fondo); --bg: var(--ev-fondo);\n";
    s += "  --superficie: var(--ev-superficie); --superficie-plana: var(--ev-superficie);"
         " --superficie-hundida: var(--ev-hundido);\n";
    s += "  --panel: var(--ev-superficie); --card: var(--ev-superficie); --surface: var(--ev-superficie);"
         " --surface-2: var(--ev-superficie-2);\n";
    s += "  --texto: var(--ev-texto); --fg: var(--ev-texto); --text: var(--ev-texto);\n";
    s += "  --texto-suave: var(--ev-texto-suave); --tenue: var(--ev-texto-suave); --muted: var(--ev-texto-suave);"
         " --text-muted: var(--ev-texto-suave); --texto-tenue: var(--ev-texto-tenue);\n";
    s += "  --borde: var(--ev-borde); --line: var(--ev-borde); --border: var(--ev-borde);"
         " --linea: var(--ev-borde); --border-hover: var(--ev-borde-fuerte); --borde-fuerte: var(--ev-borde-fuerte);\n";
    // Los resaltes, los campos hundidos, los velos y las sombras (con un tema claro, lo
    // que estaba fijo para fondo oscuro no se veía o quedaba sucio).
    s += "  --tinte: var(--ev-tinte); --tinte-fuerte: var(--ev-tinte-fuerte); --campo: var(--ev-campo);"
         " --velo: var(--ev-velo); --sombra: var(--ev-sombra);\n";
    if (r.value("forma", json::object()).contains("radio")) s += "  --radius: var(--ev-radio); --radio: var(--ev-radio);\n";
    if (!r.value("tipografia", json::object()).value("texto", std::string()).empty())
        s += "  --sans: var(--ev-fuente); --fuente: var(--ev-fuente);\n";
    return s;
}

} // namespace

std::string hojaCss(const json& r, const std::string& recursos, const std::string& version) {
    std::string s = "/* El tema del evento abierto. Lo genera el gateway (src/core/tema/TemaWeb.cpp) con lo que\n"
                    "   manda la app: no se edita. Cada página dice quién es en <html data-ev=\"…\">:\n"
                    "   publico | control | acento (y nada: no se tematiza). */\n";
    if (!r.is_object() || !r.contains("colores")) return s;
    const json& col = r["colores"];
    const auto c = [&](const char* k) { return col.value(k, std::string()); };
    const std::string v = version.empty() ? std::string() : "?v=" + version;

    const json forma = r.value("forma", json::object());
    const json tip = r.value("tipografia", json::object());
    for (const json& a : tip.value("archivos", json::array()))
        s += "@font-face { font-family: \"" + a.value("familia", std::string()) + "\"; src: url(\"" + recursos +
             a.value("archivo", std::string()) + v + "\") format(\"" + formatoDe(a.value("archivo", std::string())) +
             "\"); font-weight: " + a.value("peso", std::string("400")) + "; font-style: " +
             a.value("estilo", std::string("normal")) + "; font-display: swap; }\n";

    // Cada familia de páginas y lo que le toca según el alcance.
    const json al = r.value("alcance", json::object());
    struct Grupo {
        const char* nombre;
        Nivel nivel;
    };
    const Grupo grupos[] = {{"publico", nivelDe(al.value("publico", std::string()), Nivel::Todo)},
                            {"control", nivelDe(al.value("control", std::string()), Nivel::Acento)},
                            {"acento", Nivel::Acento}};
    // Los tokens propios, solo en las familias que toman algo (con «nada», una página que
    // los usa con respaldo —var(--ev-acento-suave, …)— se queda con el suyo). El selector es
    // más específico que :root y que [data-tema=…] de las consolas: gana sin depender del
    // orden de las hojas.
    std::string activos;
    for (const Grupo& g : grupos)
        if (g.nivel != Nivel::Nada) activos += std::string(activos.empty() ? "" : ", ") + "html[data-ev~=\"" + g.nombre + "\"]";
    if (activos.empty()) return s;
    s += activos + " {\n";
    s += "  --ev-acento: " + c("acento") + "; --ev-acento-texto: " + c("acentoTexto") + "; --ev-acento-2: " +
         c("acento2") + ";\n";
    // El acento con transparencia, ya calculado (sin color-mix): para brillos, pulsos y fondos.
    s += "  --ev-acento-suave: " + c("acentoSuave") + "; --ev-acento-medio: " + c("acentoMedio") +
         "; --ev-acento-fuerte: " + c("acentoFuerte") + "; --ev-acento-nada: " + c("acentoNada") + ";\n";
    s += "  --ev-foco: " + c("foco") + ";\n";
    s += "  --ev-fondo: " + c("fondo") + "; --ev-superficie: " + c("superficie") + "; --ev-superficie-2: " +
         c("superficie2") + "; --ev-hundido: " + c("hundido") + "; --ev-fondo-velo: " + c("fondoVelo") + ";\n";
    s += "  --ev-texto: " + c("texto") + "; --ev-texto-suave: " + c("textoSuave") + "; --ev-texto-tenue: " +
         c("textoTenue") + ";\n";
    s += "  --ev-borde: " + c("borde") + "; --ev-borde-fuerte: " + c("bordeFuerte") + ";\n";
    s += "  --ev-tinte: " + c("tinte") + "; --ev-tinte-fuerte: " + c("tinteFuerte") + "; --ev-campo: " + c("campo") +
         "; --ev-velo: " + c("velo") + "; --ev-sombra: " + c("sombra") + ";\n";
    if (forma.contains("radio")) s += "  --ev-radio: " + std::to_string(forma.value("radio", 0)) + "px;\n";
    const std::string famT = tip.value("texto", std::string());
    const std::string famH = tip.value("titulos", std::string());
    if (!famT.empty()) s += "  --ev-fuente: " + pila(famT) + ";\n";
    if (!famH.empty()) s += "  --ev-fuente-titulos: " + pila(famH) + ";\n";
    s += "}\n";

    // Los puentes a los dialectos de cada familia.
    for (const Grupo& g : grupos) {
        if (g.nivel == Nivel::Nada) continue;
        const std::string sel = std::string("html[data-ev~=\"") + g.nombre + "\"]";
        s += sel + " {\n" + puentesAcento() + (g.nivel == Nivel::Todo ? puentesTodo(r) : std::string()) + "}\n";
        if (g.nivel != Nivel::Todo) continue;
        // La tipografía pisa la de la página directamente (las páginas la escriben fija en body).
        if (!famT.empty()) s += sel + " body { font-family: var(--ev-fuente); }\n";
        if (!famH.empty())
            s += sel + " h1, " + sel + " h2, " + sel + " h3 { font-family: var(--ev-fuente-titulos); }\n";
    }
    return s;
}

json coloresQr(const json& r) {
    const json col = r.is_object() ? r.value("colores", json::object()) : json::object();
    const auto color = [&col](const char* k) { return leerColor(col.value(k, std::string())); };
    const Rgb blanco{1, 1, 1}, negro{0, 0, 0};
    // El fondo: el del evento si es claro (un QR claro sobre oscuro no lo lee cualquier teléfono).
    Rgb fondo = blanco;
    if (const auto f = color("fondo"); f && luminancia(*f) >= 0.6) fondo = *f;
    // Los módulos: oscuros y con contraste de texto contra ese fondo.
    const auto sirve = [&fondo](const std::optional<Rgb>& c) { return c && luminancia(*c) < luminancia(fondo) && contraste(*c, fondo) >= 4.5; };
    Rgb primario = negro;
    if (const auto a = color("acento"); sirve(a)) primario = *a;
    else if (const auto t = color("texto"); sirve(t)) primario = *t;
    return {{"primario", hex(primario)}, {"fondo", hex(fondo)}};
}

std::string imagenComoDataUrl(const std::filesystem::path& archivo) {
    const std::u8string u = archivo.filename().u8string();
    const std::string nombre(u.begin(), u.end());
    if (!esImagenDeMarca(nombre)) return {};
    std::error_code ec;
    const auto peso = std::filesystem::file_size(archivo, ec);
    if (ec || peso == 0 || peso > 2u * 1024 * 1024) return {};
    std::ifstream f(archivo, std::ios::binary);
    const std::string datos((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (datos.size() != peso) return {};
    // Base64 a mano (lo usan también pruebas que no enlazan mbedTLS).
    static const char* abc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string b64;
    b64.reserve((datos.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < datos.size(); i += 3) {
        const std::uint32_t a = static_cast<unsigned char>(datos[i]);
        const std::uint32_t b = i + 1 < datos.size() ? static_cast<unsigned char>(datos[i + 1]) : 0;
        const std::uint32_t c = i + 2 < datos.size() ? static_cast<unsigned char>(datos[i + 2]) : 0;
        const std::uint32_t t = (a << 16) | (b << 8) | c;
        b64 += abc[(t >> 18) & 63];
        b64 += abc[(t >> 12) & 63];
        b64 += i + 1 < datos.size() ? abc[(t >> 6) & 63] : '=';
        b64 += i + 2 < datos.size() ? abc[t & 63] : '=';
    }
    const char* tipo = nombre.ends_with(".svg") ? "image/svg+xml" : nombre.ends_with(".png") ? "image/png" : "image/webp";
    return std::string("data:") + tipo + ";base64," + b64;
}

std::string huella(const std::string& t) {
    std::uint64_t h = 1469598103934665603ULL;
    for (const char ch : t) {
        h ^= static_cast<unsigned char>(ch);
        h *= 1099511628211ULL;
    }
    char b[24];
    std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(h));
    return std::string(b).substr(0, 12);
}

} // namespace ba::tema
