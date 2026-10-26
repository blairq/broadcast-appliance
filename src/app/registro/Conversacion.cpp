// ============================================================================
// src/app/registro/Conversacion.cpp — ver Conversacion.hpp
// ============================================================================
#include "registro/Conversacion.hpp"

#include <algorithm>
#include <cctype>
#include <map>

#include "documentos/Documento.hpp"

namespace ba::registro {
namespace {

using nlohmann::json;

// --- Texto -------------------------------------------------------------------

/// Parte en palabras por espacios.
std::vector<std::string> palabras(const std::string& s) {
    std::vector<std::string> out;
    std::string w;
    for (char c : s) {
        if (c == ' ') {
            if (!w.empty()) out.push_back(w);
            w.clear();
        } else {
            w.push_back(c);
        }
    }
    if (!w.empty()) out.push_back(w);
    return out;
}

std::string unir(const std::vector<std::string>& v, std::size_t desde = 0, std::size_t hasta = std::string::npos,
                 const char* sep = " ") {
    std::string out;
    for (std::size_t i = desde; i < v.size() && i < hasta; ++i) {
        if (!out.empty()) out += sep;
        out += v[i];
    }
    return out;
}

/// Aplanado y con los signos de puntuación como espacios (salvo los que se
/// pidan conservar: el correo necesita el punto, la arroba y los guiones).
std::string sinSignos(const std::string& s, const std::string& conservar = {}) {
    std::string out;
    for (char c : aplanar(s)) {
        const bool signo = std::ispunct(static_cast<unsigned char>(c)) != 0;
        out.push_back(signo && conservar.find(c) == std::string::npos ? ' ' : c);
    }
    return aplanar(out);
}

/// Si la frase (palabras separadas por un espacio) contiene `f` como palabras enteras.
bool tiene(const std::string& frase, const std::string& f) {
    return (" " + frase + " ").find(" " + f + " ") != std::string::npos;
}

bool todoDigitos(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// --- Números dichos ----------------------------------------------------------

const std::map<std::string, int>& kUnidades() {
    static const std::map<std::string, int> m{
        {"cero", 0},        {"uno", 1},         {"un", 1},           {"una", 1},          {"dos", 2},
        {"tres", 3},        {"cuatro", 4},      {"cinco", 5},        {"seis", 6},         {"siete", 7},
        {"ocho", 8},        {"nueve", 9},       {"diez", 10},        {"once", 11},        {"doce", 12},
        {"trece", 13},      {"catorce", 14},    {"quince", 15},      {"dieciseis", 16},   {"diecisiete", 17},
        {"dieciocho", 18},  {"diecinueve", 19}, {"veinte", 20},      {"veintiuno", 21},   {"veintiun", 21},
        {"veintiuna", 21},  {"veintidos", 22},  {"veintitres", 23},  {"veinticuatro", 24}, {"veinticinco", 25},
        {"veintiseis", 26}, {"veintisiete", 27}, {"veintiocho", 28}, {"veintinueve", 29}};
    return m;
}
const std::map<std::string, int>& kDecenas() {
    static const std::map<std::string, int> m{{"treinta", 30}, {"cuarenta", 40}, {"cincuenta", 50},
                                              {"sesenta", 60}, {"setenta", 70},  {"ochenta", 80},
                                              {"noventa", 90}};
    return m;
}
const std::map<std::string, int>& kCentenas() {
    static const std::map<std::string, int> m{
        {"cien", 100},          {"ciento", 100},       {"doscientos", 200},  {"doscientas", 200},
        {"trescientos", 300},   {"trescientas", 300},  {"cuatrocientos", 400}, {"cuatrocientas", 400},
        {"quinientos", 500},    {"quinientas", 500},   {"seiscientos", 600}, {"seiscientas", 600},
        {"setecientos", 700},   {"setecientas", 700},  {"ochocientos", 800}, {"ochocientas", 800},
        {"novecientos", 900},   {"novecientas", 900}};
    return m;
}

bool esPalabraNumero(const std::string& w) {
    return kUnidades().count(w) != 0 || kDecenas().count(w) != 0 || kCentenas().count(w) != 0;
}

/// Un grupo de 0 a 999 dicho en palabras a partir de `i` («cuatrocientos
/// cuarenta y cinco», «once», «treinta»). Devuelve -1 si en `i` no empieza uno.
int grupo(const std::vector<std::string>& w, std::size_t& i) {
    if (i >= w.size()) return -1;
    int v = -1;
    if (auto c = kCentenas().find(w[i]); c != kCentenas().end()) {
        v = c->second;
        ++i;
        // «cien» va solo; «ciento» y las demás pueden seguir («ciento veinte»).
        if (w[i - 1] == "cien") return v;
        std::size_t j = i;
        const int resto = grupo(w, j);
        if (resto > 0 && resto < 100) {
            v += resto;
            i = j;
        }
        return v;
    }
    if (auto d = kDecenas().find(w[i]); d != kDecenas().end()) {
        v = d->second;
        ++i;
        if (i + 1 < w.size() && w[i] == "y") {
            if (auto u = kUnidades().find(w[i + 1]); u != kUnidades().end() && u->second >= 1 && u->second <= 9) {
                v += u->second;
                i += 2;
            }
        }
        return v;
    }
    if (auto u = kUnidades().find(w[i]); u != kUnidades().end()) {
        ++i;
        return u->second;
    }
    return -1;
}

std::string digitoEnPalabra(char d) {
    static const char* k[] = {"cero", "uno", "dos", "tres", "cuatro", "cinco", "seis", "siete", "ocho", "nueve"};
    return d >= '0' && d <= '9' ? k[d - '0'] : std::string(1, d);
}

// --- Para no confundir respuestas con datos ----------------------------------

const std::vector<std::string>& kSi() {
    static const std::vector<std::string> v{
        "si", "sip", "sisi", "si si", "dale", "claro", "obvio", "bueno", "correcto", "exacto", "afirmativo",
        "por supuesto", "ok", "okey", "oka", "esta bien", "de una", "joya", "perfecto", "eso", "eso es",
        "asi es", "seguro", "re si", "barbaro", "genial", "va", "listo", "tal cual", "ajam", "aja", "ya"};
    return v;
}
const std::vector<std::string>& kNo() {
    static const std::vector<std::string> v{"no", "nop", "nones", "negativo", "para nada", "nada que ver",
                                            "ni ahi", "esta mal", "incorrecto", "equivocado", "tampoco",
                                            "nunca", "jamas", "mal", "no es", "no era"};
    return v;
}

bool esCancelar(const std::string& f) {
    return f == "cancelar" || f == "cancela" || f == "cancelalo" || tiene(f, "cancelar el registro") ||
           tiene(f, "cancela el registro") || f == "me voy" || f == "chau";
}

bool esRepetir(const std::string& f) {
    static const char* k[] = {"como",          "que",     "que dijiste", "repeti",        "repetime", "repetir",
                              "otra vez",      "de nuevo", "no entendi", "no te entendi", "perdon",   "como dijiste",
                              "no escuche",    "no te escuche", "eh"};
    for (const char* x : k)
        if (f == x) return true;
    return false;
}

bool esSaltear(const std::string& f) {
    static const char* k[] = {"paso",       "saltear",    "saltealo",  "salteala",      "siguiente",
                              "prefiero no", "no quiero", "no tengo",  "no lo tengo",   "no se",
                              "no me acuerdo", "despues",  "otro dia", "prefiero no decirlo"};
    for (const char* x : k)
        if (f == x || (std::string(x).size() > 6 && tiene(f, x))) return true;
    return false;
}

/// Pasa a minúscula o mayúscula la primera letra de una palabra en UTF-8 (las
/// acentuadas del castellano son dos bytes, 0xC3 + algo).
std::string capital(const std::string& w) {
    std::string s;
    // Todo a minúscula primero.
    for (std::size_t i = 0; i < w.size(); ++i) {
        const auto c = static_cast<unsigned char>(w[i]);
        if (c == 0xC3 && i + 1 < w.size()) {
            auto d = static_cast<unsigned char>(w[i + 1]);
            if (d >= 0x80 && d <= 0x9E && d != 0x97) d = static_cast<unsigned char>(d + 0x20);
            s.push_back(static_cast<char>(c));
            s.push_back(static_cast<char>(d));
            ++i;
        } else {
            s.push_back(static_cast<char>(std::tolower(c)));
        }
    }
    if (s.empty()) return s;
    const auto c0 = static_cast<unsigned char>(s[0]);
    if (c0 == 0xC3 && s.size() > 1) {
        auto d = static_cast<unsigned char>(s[1]);
        if (d >= 0xA0 && d <= 0xBE && d != 0xB7) s[1] = static_cast<char>(d - 0x20);
    } else {
        s[0] = static_cast<char>(std::toupper(c0));
    }
    return s;
}

/// Solo la primera letra en mayúscula; el resto queda como vino (una sigla
/// sigue siendo sigla).
std::string mayusculaInicial(std::string s) {
    if (s.empty()) return s;
    const auto c0 = static_cast<unsigned char>(s[0]);
    if (c0 == 0xC3 && s.size() > 1) {
        const auto d = static_cast<unsigned char>(s[1]);
        if (d >= 0xA0 && d <= 0xBE && d != 0xB7) s[1] = static_cast<char>(d - 0x20);
    } else {
        s[0] = static_cast<char>(std::toupper(c0));
    }
    return s;
}

/// Si lo dicho es, entero, una de las respuestas de sí o de no.
bool esRespuestaSuelta(const std::string& f) {
    for (const std::string& x : kSi())
        if (f == x) return true;
    for (const std::string& x : kNo())
        if (f == x) return true;
    return false;
}

std::string listar(const std::vector<std::string>& v) {
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i > 0) out += i + 1 == v.size() ? " o " : ", ";
        out += v[i];
    }
    return out;
}

std::string sinPuntoFinal(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '.' || s.back() == ',' || s.back() == ';')) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && s[i] == ' ') ++i;
    return s.substr(i);
}

} // namespace

// --- Normalizadores ------------------------------------------------------------

std::string aplanar(const std::string& s) {
    static const struct {
        const char* de;
        char a;
    } kAcentos[] = {{"á", 'a'}, {"é", 'e'}, {"í", 'i'}, {"ó", 'o'}, {"ú", 'u'}, {"ü", 'u'}, {"ñ", 'n'},
                    {"Á", 'a'}, {"É", 'e'}, {"Í", 'i'}, {"Ó", 'o'}, {"Ú", 'u'}, {"Ü", 'u'}, {"Ñ", 'n'},
                    {"¿", ' '}, {"¡", ' '}};
    std::string out;
    bool espacio = false;
    for (std::size_t i = 0; i < s.size();) {
        char c = 0;
        if (static_cast<unsigned char>(s[i]) >= 0x80 && i + 1 < s.size()) {
            for (const auto& a : kAcentos)
                if (s.compare(i, 2, a.de) == 0) c = a.a;
            i += 2;
            if (c == 0) continue; // otro carácter de dos bytes: no aporta
        } else {
            c = s[i++];
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            espacio = !out.empty();
            continue;
        }
        if (espacio) out.push_back(' ');
        espacio = false;
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

bool entenderCorreo(const std::string& dicho, std::string& out) {
    // Se conservan los signos que pueden venir escritos: la voz a texto a
    // veces ya pone «jorge.ortega@gmail.com», y lo tipeado siempre.
    std::string s = sinSignos(sinPuntoFinal(dicho), ".@_-+");
    // Las palabras que se dicen en vez del signo. Primero las de dos palabras.
    for (const auto& [de, a] : std::vector<std::pair<std::string, std::string>>{
             {"guion bajo", "_"}, {"guion medio", "-"}, {"piso bajo", "_"}, {"punto com", ".com"}}) {
        std::string f = " " + s + " ";
        for (std::size_t p = f.find(" " + de + " "); p != std::string::npos; p = f.find(" " + de + " ", p + 1))
            f.replace(p + 1, de.size(), a);
        s = aplanar(f);
    }
    std::vector<std::string> w = palabras(s);
    for (std::string& x : w) {
        if (x == "arroba" || x == "aroba") x = "@";
        else if (x == "punto") x = ".";
        else if (x == "guion" || x == "menos") x = "-";
        else if (auto u = kUnidades().find(x); u != kUnidades().end() && u->second <= 9 && x != "un" && x != "una")
            x = std::to_string(u->second);
    }
    // Lo que se dice antes del correo («mi correo es…») y después («gracias»).
    static const std::vector<std::string> kAntes{"mi",   "correo", "mail", "email", "imeil", "e-mail", "electronico",
                                                 "es",   "el",     "la",   "direccion", "de", "seria", "este",
                                                 "eh",   "em",     "bueno", "dale", "si",  "ok",   "tengo"};
    static const std::vector<std::string> kDespues{"gracias", "nada", "mas", "eso", "es", "todo", "listo"};
    while (!w.empty() && std::find(kAntes.begin(), kAntes.end(), w.front()) != kAntes.end()) w.erase(w.begin());
    while (!w.empty() && std::find(kDespues.begin(), kDespues.end(), w.back()) != kDespues.end()) w.pop_back();
    std::string c = unir(w, 0, std::string::npos, "");
    const auto arroba = c.find('@');
    if (arroba == std::string::npos || arroba == 0 || c.find('@', arroba + 1) != std::string::npos) return false;
    std::string local = c.substr(0, arroba), dominio = c.substr(arroba + 1);
    // Un gmail sin «punto com» es un gmail.com: nadie tiene otro.
    static const char* kConocidos[] = {"gmail", "hotmail", "yahoo", "outlook", "live", "icloud", "fibertel", "speedy"};
    if (dominio.find('.') == std::string::npos)
        for (const char* k : kConocidos)
            if (dominio == k) dominio += ".com";
    // Puntos de más que deja «punto punto» o un punto al final.
    while (!dominio.empty() && dominio.back() == '.') dominio.pop_back();
    while (!local.empty() && local.front() == '.') local.erase(local.begin());
    const auto valido = [](const std::string& x, const char* extra) {
        return !x.empty() && std::all_of(x.begin(), x.end(), [extra](char ch) {
            return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || std::string(extra).find(ch) != std::string::npos;
        });
    };
    if (!valido(local, "._%+-") || !valido(dominio, ".-")) return false;
    const auto punto = dominio.rfind('.');
    if (punto == std::string::npos || punto == 0 || dominio.size() - punto - 1 < 2 || dominio.find("..") != std::string::npos ||
        local.find("..") != std::string::npos)
        return false;
    const std::string tld = dominio.substr(punto + 1);
    if (!std::all_of(tld.begin(), tld.end(), [](char ch) { return ch >= 'a' && ch <= 'z'; })) return false;
    out = local + "@" + dominio;
    return true;
}

bool entenderTelefono(const std::string& dicho, std::string& out) {
    const std::vector<std::string> w = palabras(sinSignos(dicho, "+"));
    std::string r;
    for (std::size_t i = 0; i < w.size();) {
        const std::string& x = w[i];
        if ((x == "mas" || x == "+") && r.empty()) {
            r = "+";
            ++i;
        } else if (x.size() > 1 && x[0] == '+' && todoDigitos(x.substr(1)) && r.empty()) {
            r = x;
            ++i;
        } else if (todoDigitos(x)) {
            r += x;
            ++i;
        } else if ((x == "doble" || x == "triple") && i + 1 < w.size()) {
            const auto u = kUnidades().find(w[i + 1]);
            const bool digito = u != kUnidades().end() && u->second <= 9;
            const bool escrito = w[i + 1].size() == 1 && todoDigitos(w[i + 1]);
            if (digito || escrito) {
                const char d = digito ? static_cast<char>('0' + u->second) : w[i + 1][0];
                r.append(x == "doble" ? 2 : 3, d);
                i += 2;
            } else {
                ++i;
            }
        } else if (esPalabraNumero(x)) {
            std::size_t j = i;
            const int v = grupo(w, j);
            r += std::to_string(v);
            i = j;
        } else {
            ++i; // «mi número es», «el», «y» suelta: relleno
        }
    }
    const auto digitos = static_cast<std::size_t>(std::count_if(r.begin(), r.end(), [](char c) { return c >= '0' && c <= '9'; }));
    if (digitos < 6 || digitos > 15) return false;
    out = r;
    return true;
}

bool entenderNumero(const std::string& dicho, long long& out) {
    const std::vector<std::string> w = palabras(sinSignos(sinPuntoFinal(dicho), "."));
    // Escrito con cifras: «35», «2.500». El primero que aparezca.
    for (const std::string& x0 : w) {
        std::string x;
        for (char c : x0)
            if (c != '.') x.push_back(c);
        if (todoDigitos(x) && x.size() <= 15) {
            out = std::stoll(x);
            return true;
        }
    }
    long long total = 0, actual = 0;
    bool hay = false;
    for (std::size_t i = 0; i < w.size();) {
        const std::string& x = w[i];
        if (x == "ninguno" || x == "ninguna" || x == "ningun" || x == "nadie" || x == "nada") {
            hay = true;
            ++i;
        } else if (x == "mil") {
            actual = (actual == 0 ? 1 : actual) * 1000;
            total += actual;
            actual = 0;
            hay = true;
            ++i;
        } else if (x == "millon" || x == "millones") {
            total = (total + (actual == 0 ? 1 : actual)) * 1000000;
            actual = 0;
            hay = true;
            ++i;
        } else if (esPalabraNumero(x)) {
            std::size_t j = i;
            actual += grupo(w, j);
            i = j;
            hay = true;
        } else {
            ++i;
        }
    }
    if (!hay) return false;
    out = total + actual;
    return true;
}

int entenderSiNo(const std::string& dicho) {
    std::string f = sinSignos(dicho);
    // «no sé» no es un no: es que no sabe. Se saca antes de buscar.
    for (const char* x : {"no se", "no lo se", "no sabria"})
        if (tiene(f, x)) return -1;
    // Los «no» que llevan un «bien» o un «es» adentro, antes de mirar palabra por palabra.
    for (const char* x : {"no esta bien", "no es asi", "no no", "esta mal", "para nada", "nada que ver", "ni ahi", "no es", "no era"})
        if (tiene(f, x)) return 0;
    bool si = false, no = false;
    for (const std::string& x : kSi())
        if (tiene(f, x)) si = true;
    for (const std::string& x : kNo())
        if (tiene(f, x)) no = true;
    if (si == no) return -1; // ni una ni otra, o las dos: se vuelve a preguntar
    return si ? 1 : 0;
}

bool entenderNombre(const std::string& dicho, std::string& out) {
    // Las palabras ORIGINALES (con sus acentos) y su versión aplanada al lado,
    // para reconocer «me llamo» sin perder la tilde de «Martínez».
    std::string limpio;
    for (char c : dicho) {
        const bool signo = std::string(",.;:?!\"()[]{}").find(c) != std::string::npos;
        limpio.push_back(signo ? ' ' : c);
    }
    // ¿ y ¡ son de dos bytes.
    for (const char* x : {"¿", "¡"})
        for (std::size_t p = limpio.find(x); p != std::string::npos; p = limpio.find(x)) limpio.replace(p, 2, " ");
    std::vector<std::string> orig = palabras(limpio);
    std::vector<std::string> plano;
    for (const std::string& o : orig) plano.push_back(aplanar(o));
    static const std::vector<std::vector<std::string>> kAntes{
        {"hola"},  {"buenas"}, {"buen", "dia"}, {"buenos", "dias"}, {"buenas", "tardes"}, {"buenas", "noches"},
        {"bueno"}, {"eh"},     {"este"},        {"si"},             {"dale"},
        {"yo", "me", "llamo"}, {"me", "llamo"}, {"mi", "nombre", "es"}, {"mi", "nombre", "completo", "es"},
        {"el", "nombre", "es"}, {"mi", "nombre"}, {"yo", "soy"}, {"soy"}, {"me", "dicen"}, {"es"}, {"seria"}};
    bool saco = true;
    while (saco && !plano.empty()) {
        saco = false;
        for (const auto& p : kAntes) {
            if (p.size() > plano.size() || !std::equal(p.begin(), p.end(), plano.begin())) continue;
            plano.erase(plano.begin(), plano.begin() + static_cast<std::ptrdiff_t>(p.size()));
            orig.erase(orig.begin(), orig.begin() + static_cast<std::ptrdiff_t>(p.size()));
            saco = true;
            break;
        }
    }
    while (!plano.empty() && (plano.back() == "gracias" || plano.back() == "nomas")) {
        plano.pop_back();
        orig.pop_back();
    }
    if (orig.empty() || orig.size() > 7) return false;
    // Una respuesta suelta («sí», «no», «dale»), una negativa («no quiero»)
    // o un número no son un nombre.
    if (esRespuestaSuelta(unir(plano)) || plano.front() == "no") return false;
    for (const std::string& p : plano)
        if (std::any_of(p.begin(), p.end(), [](char c) { return c >= '0' && c <= '9'; })) return false;
    if (std::all_of(plano.begin(), plano.end(), [](const std::string& q) { return esPalabraNumero(q); })) return false;
    static const std::vector<std::string> kParticulas{"de", "del", "la", "las", "los", "y", "e", "da", "di", "van", "von", "el"};
    std::string r;
    for (std::size_t i = 0; i < orig.size(); ++i) {
        const bool part = i > 0 && std::find(kParticulas.begin(), kParticulas.end(), plano[i]) != kParticulas.end();
        if (!r.empty()) r += " ";
        r += part ? plano[i] : capital(orig[i]);
    }
    if (aplanar(r).size() < 2) return false;
    out = r;
    return true;
}

bool entenderOpcion(const std::string& dicho, const std::vector<std::string>& opciones, std::string& out) {
    const std::string f = sinSignos(dicho);
    if (f.empty() || opciones.empty()) return false;
    // Por su texto: la opción más larga que aparezca entera en lo dicho («sala
    // grande» gana sobre «sala» si dijo «la sala grande»).
    int mejor = -1;
    std::size_t largo = 0;
    for (std::size_t i = 0; i < opciones.size(); ++i) {
        const std::string o = sinSignos(opciones[i]);
        if (!o.empty() && tiene(f, o) && o.size() > largo) {
            mejor = static_cast<int>(i);
            largo = o.size();
        }
    }
    // O dijo un pedazo que solo está en una («la grande»).
    if (mejor < 0) {
        int cuantas = 0;
        for (std::size_t i = 0; i < opciones.size(); ++i) {
            const std::string o = sinSignos(opciones[i]);
            for (const std::string& p : palabras(f))
                if (p.size() >= 4 && tiene(o, p)) {
                    if (mejor != static_cast<int>(i)) ++cuantas;
                    mejor = static_cast<int>(i);
                    break;
                }
        }
        if (cuantas != 1) mejor = -1;
    }
    // O por el orden: «la primera», «la segunda», «la última».
    if (mejor < 0) {
        static const std::vector<std::vector<std::string>> kOrden{
            {"primera", "primero", "uno", "1"}, {"segunda", "segundo", "dos", "2"}, {"tercera", "tercero", "tres", "3"},
            {"cuarta", "cuarto", "cuatro", "4"}, {"quinta", "quinto", "cinco", "5"}};
        for (std::size_t i = 0; i < kOrden.size() && i < opciones.size() && mejor < 0; ++i)
            for (const std::string& p : kOrden[i])
                if (tiene(f, p)) mejor = static_cast<int>(i);
        if (mejor < 0 && (tiene(f, "ultima") || tiene(f, "ultimo"))) mejor = static_cast<int>(opciones.size()) - 1;
    }
    if (mejor < 0) return false;
    out = opciones[static_cast<std::size_t>(mejor)];
    return true;
}

bool entenderTexto(const std::string& dicho, std::string& out) {
    const std::string s = sinPuntoFinal(dicho);
    if (aplanar(s).empty()) return false;
    out = mayusculaInicial(s);
    return true;
}

bool entender(const Campo& c, const std::string& dicho, json& out) {
    switch (c.tipo) {
        case TipoCampo::Correo: {
            std::string v;
            if (!entenderCorreo(dicho, v)) return false;
            out = v;
            return true;
        }
        case TipoCampo::Telefono: {
            std::string v;
            if (!entenderTelefono(dicho, v)) return false;
            out = v;
            return true;
        }
        case TipoCampo::Numero: {
            long long v = 0;
            if (!entenderNumero(dicho, v)) return false;
            out = v;
            return true;
        }
        case TipoCampo::SiNo: {
            const int v = entenderSiNo(dicho);
            if (v < 0) return false;
            out = v == 1;
            return true;
        }
        case TipoCampo::Nombre: {
            std::string v;
            if (!entenderNombre(dicho, v)) return false;
            out = v;
            return true;
        }
        case TipoCampo::Opcion: {
            std::string v;
            if (!entenderOpcion(dicho, c.opciones, v)) return false;
            out = v;
            return true;
        }
        case TipoCampo::Texto: {
            std::string v;
            if (!entenderTexto(dicho, v)) return false;
            out = v;
            return true;
        }
        case TipoCampo::Documento: {
            // Lo que se dice (o tipea) es el número: el respaldo de la cámara.
            std::string v;
            if (!entenderNumeroDocumento(dicho, v)) return false;
            out = json{{"numero", v}, {"origen", "dicho"}};
            return true;
        }
    }
    return false;
}

bool entenderNumeroDocumento(const std::string& dicho, std::string& out) {
    // «11.222.333» tipeado o «once dos dos dos…» dicho: lo mismo que un teléfono
    // (dígitos o palabras, con lo que se dice antes), con el largo de un documento.
    std::string t;
    if (!entenderTelefono(dicho, t) || t.empty() || t[0] == '+') {
        long long n = 0;
        if (!entenderNumero(dicho, n) || n <= 0) return false;
        t = std::to_string(n);
    }
    t.erase(0, std::min(t.find_first_not_of('0'), t.size()));
    if (t.size() < 6 || t.size() > 9 || !todoDigitos(t)) return false;
    out = t;
    return true;
}

namespace {
/// «Juana Andrea Prueba, DNI 11.222.333» (o lo que haya) de un documento ya en `datos`.
std::string documentoParaDecir(const json& v, const char* separador) {
    const ba::documentos::Datos d = ba::documentos::Datos::deJson(v);
    const bool dni = d.tipo.empty() || d.tipo == "dni-ar";
    const std::string n = d.numero.empty() ? std::string()
                                           : std::string(dni ? "DNI " : "documento ") +
                                                 ba::documentos::numeroConPuntos(d.numero);
    const std::string quien = d.apellido.empty() ? std::string() : d.nombreCompleto();
    if (quien.empty()) return n;
    return n.empty() ? quien : quien + separador + n;
}
} // namespace

namespace {
/// Los dígitos de un teléfono en grupos («11 4567 8901»): el primero con lo
/// que sobra, los demás de a cuatro. Así se dicen y se leen en Argentina.
std::vector<std::string> gruposDeTelefono(const std::string& t) {
    std::string d;
    for (char c : t)
        if (c >= '0' && c <= '9') d.push_back(c);
    std::vector<std::string> g;
    std::size_t primero = d.size() % 4;
    if (primero == 0) primero = std::min<std::size_t>(4, d.size());
    if (!d.empty()) g.push_back(d.substr(0, primero));
    for (std::size_t i = primero; i < d.size(); i += 4) g.push_back(d.substr(i, 4));
    return g;
}
} // namespace

std::string paraDecir(const Campo& c, const json& v) {
    if (c.tipo == TipoCampo::Documento && v.is_object()) return documentoParaDecir(v, ", ");
    if (v.is_boolean()) return v.get<bool>() ? "que sí" : "que no";
    if (v.is_number_integer()) return std::to_string(v.get<long long>());
    if (!v.is_string()) return {};
    const std::string s = v.get<std::string>();
    if (c.tipo == TipoCampo::Correo) {
        std::string out;
        for (char ch : s) {
            std::string pal;
            if (ch == '.') pal = "punto";
            else if (ch == '@') pal = "arroba";
            else if (ch == '_') pal = "guion bajo";
            else if (ch == '-') pal = "guion";
            else if (ch >= '0' && ch <= '9') pal = digitoEnPalabra(ch);
            if (pal.empty()) {
                out.push_back(ch);
            } else {
                if (!out.empty() && out.back() != ' ') out += " ";
                out += pal + " ";
            }
        }
        return sinPuntoFinal(out);
    }
    if (c.tipo == TipoCampo::Telefono) {
        // De a un dígito y por grupos, separados por comas: la voz hace una
        // pausa en cada coma, que es como se dicta un número.
        std::string out = s.rfind('+', 0) == 0 ? "más " : "";
        bool primero = true;
        for (const std::string& g : gruposDeTelefono(s)) {
            if (!primero) out += ", ";
            primero = false;
            for (std::size_t i = 0; i < g.size(); ++i) out += (i ? " " : "") + digitoEnPalabra(g[i]);
        }
        return out;
    }
    return s;
}

std::string paraMostrar(const Campo& c, const json& v) {
    if (v.is_null()) return {};
    if (c.tipo == TipoCampo::Documento && v.is_object()) return documentoParaDecir(v, " · ");
    if (v.is_boolean()) return v.get<bool>() ? "Sí" : "No";
    if (v.is_number_integer()) return std::to_string(v.get<long long>());
    if (!v.is_string()) return v.dump();
    if (c.tipo == TipoCampo::Telefono) {
        const std::string s = v.get<std::string>();
        std::string out = s.rfind('+', 0) == 0 ? "+" : "";
        for (const std::string& g : gruposDeTelefono(s)) out += (out.empty() || out == "+" ? "" : " ") + g;
        return out;
    }
    return v.get<std::string>();
}

// --- La conversación ------------------------------------------------------------

Conversacion::Conversacion(Totem totem, std::string evento) : t_(std::move(totem)), evento_(std::move(evento)) {
    valores_.assign(t_.campos.size(), json());
    salteado_.assign(t_.campos.size(), false);
}

std::string Conversacion::titulo() const { return conDatos(t_.titulo, evento_, {}); }
std::string Conversacion::subtitulo() const { return conDatos(t_.subtitulo, evento_, {}); }

std::string Conversacion::pregunta(std::size_t i) const {
    const Campo& c = t_.campos[i];
    std::string p = c.pregunta.empty() ? preguntaDeFabrica(c.tipo) : c.pregunta;
    // Las opciones se dicen siempre: por voz no hay una lista que mirar.
    if (c.tipo == TipoCampo::Opcion && !c.opciones.empty()) p += " Puede ser: " + listar(c.opciones) + ".";
    // Si no se acepta el DNI, no se pide «tu DNI».
    if (c.tipo == TipoCampo::Documento && c.pregunta.empty() && !c.documentos.empty() &&
        std::find(c.documentos.begin(), c.documentos.end(), "dni-ar") == c.documentos.end())
        p = "Mostrame tu documento a la cámara, del lado del código de barras.";
    return p;
}

std::string Conversacion::reintento(const Campo& c, int intento) const {
    std::string r;
    switch (c.tipo) {
        case TipoCampo::Correo:
            r = "No te entendí el correo. Decímelo despacio, por ejemplo: " +
                (c.ejemplo.empty() ? std::string("juan punto perez arroba gmail punto com") : c.ejemplo) + ".";
            break;
        case TipoCampo::Telefono:
            r = "No me quedó claro el número. Decímelo de a un número, por ejemplo: " +
                (c.ejemplo.empty() ? std::string("once, cuatro cinco seis siete, ocho nueve cero uno") : c.ejemplo) + ".";
            break;
        case TipoCampo::Numero: r = "No entendí el número. ¿Me lo repetís?"; break;
        case TipoCampo::SiNo: r = "Perdón, no te entendí. ¿Sí o no?"; break;
        case TipoCampo::Opcion: r = "No te entendí. Puede ser: " + listar(c.opciones) + "."; break;
        case TipoCampo::Nombre: r = "No te escuché bien. ¿Me repetís tu nombre?"; break;
        case TipoCampo::Texto: r = "No te escuché bien. ¿Me lo repetís?"; break;
        case TipoCampo::Documento: r = "No te entendí el número. Decímelo de a un número."; break;
    }
    if (!c.ejemplo.empty() && (c.tipo == TipoCampo::Numero || c.tipo == TipoCampo::Texto || c.tipo == TipoCampo::Nombre))
        r += " Por ejemplo: " + c.ejemplo + ".";
    if (intento >= 2) r += " Si preferís, escribilo en la pantalla.";
    return r;
}

Conversacion::Respuesta Conversacion::empezar(const Cara& cara) {
    Respuesta r;
    if (estado_ != Estado::Esperando) return r;
    cara_ = cara;
    const std::string nombreSaludo = t_.reconocer ? cara.nombre : std::string();
    if (cara.yaRegistrado && !t_.yaRegistrado.empty()) {
        estado_ = Estado::Terminada;
        r.decir = conDatos(t_.yaRegistrado, evento_, nombreSaludo);
        r.terminar = true;
        return r;
    }
    // Lo que ya se sabe por la cara no se pregunta: a quien reconoce, no le
    // pide el nombre que tiene en su ficha.
    if (t_.reconocer && !cara.nombre.empty())
        for (std::size_t i = 0; i < t_.campos.size(); ++i)
            if (t_.campos[i].tipo == TipoCampo::Nombre) {
                valores_[i] = cara.nombre;
                break;
            }
    avanzar(r, conDatos(t_.saludo, evento_, nombreSaludo));
    r.campos = true;
    return r;
}

void Conversacion::conocerCara(const Cara& cara) {
    if (cara_.personaId.empty()) cara_.personaId = cara.personaId;
    if (cara_.nombre.empty()) cara_.nombre = cara.nombre;
}

void Conversacion::avanzar(Respuesta& r, const std::string& antes) {
    for (std::size_t i = 0; i < t_.campos.size(); ++i) {
        if (!valores_[i].is_null() || salteado_[i]) continue;
        estado_ = Estado::Preguntando;
        actual_ = i;
        intentos_ = 0;
        dorsoPedido_ = false;
        r.decir = antes.empty() ? pregunta(i) : antes + " " + pregunta(i);
        r.pedir = t_.campos[i].clave;
        // Un documento no se pregunta: se busca en la cámara (lo hace quien llama).
        if (t_.campos[i].tipo == TipoCampo::Documento) {
            estado_ = Estado::Documento;
            r.documento = true;
        }
        return;
    }
    terminar(r, antes);
}

void Conversacion::terminar(Respuesta& r, const std::string& antes) {
    // Antes de despedirse, la foto de frente: la que tiene que quedar en la ficha es una
    // mirando a la cámara, no la de alguien que se estaba acomodando.
    if (t_.foto && !fotoHecha_) {
        estado_ = Estado::Foto;
        fotoIntentos_ = 0;
        const std::string pedido = "Por último, mirá derecho a la cámara un momento, así te saco una foto de frente.";
        r.decir = antes.empty() ? pedido : antes + " " + pedido;
        r.foto = true;
        r.campos = true;
        r.pedir.clear();
        return;
    }
    estado_ = Estado::Terminada;
    // El nombre que dijo, o el de su ficha si se lo puede llamar por la cara.
    std::string quien = nombre();
    if (quien == cara_.nombre && !t_.reconocer) quien.clear();
    const std::string chau = conDatos(t_.despedida, evento_, quien);
    r.decir = antes.empty() ? chau : antes + " " + chau;
    r.guardar = true;
    r.terminar = true;
    r.campos = true;
    r.pedir.clear();
}

void Conversacion::aceptar(const json& v) {
    valores_[actual_] = v;
    salteado_[actual_] = false;
    if (t_.campos[actual_].tipo == TipoCampo::Documento) completarDesdeDocumento(v);
}

void Conversacion::fallo(Respuesta& r) {
    const Campo& c = t_.campos[actual_];
    ++intentos_;
    r.pedir = c.clave;
    // Dos reintentos y, si no es obligatorio, se sigue: trabar la fila de un
    // evento por un teléfono que la voz no entiende es peor que no tenerlo.
    if (intentos_ <= 2) {
        r.decir = reintento(c, intentos_);
    } else if (!c.obligatorio) {
        salteado_[actual_] = true;
        avanzar(r, "No pasa nada, lo dejamos en blanco.");
    } else {
        r.decir = "Escribilo en la pantalla, así no hay error.";
    }
}

Conversacion::Respuesta Conversacion::oido(const std::string& texto) {
    Respuesta r;
    if (!enCurso() || atendida_) return r; // atendida: le contesta una persona, no el guion
    const std::string f = sinSignos(texto);
    if (f.empty()) return r;
    silencios_ = 0;
    if (esCancelar(f)) return cancelar();
    if (estado_ == Estado::Foto) return r; // mirando a la cámara: lo que diga no cambia nada
    if (estado_ == Estado::Documento) return oidoDocumento(texto, f);
    const Campo& c = t_.campos[actual_];
    r.pedir = c.clave;
    if (estado_ == Estado::Confirmando) {
        const int sn = entenderSiNo(texto);
        if (sn == 1) {
            aceptar(propuesto_);
            r.campos = true;
            avanzar(r, "Perfecto.");
            return r;
        }
        if (sn == 0 && c.tipo == TipoCampo::Documento) {
            // Leyó mal (u otro documento): a mostrarlo de nuevo.
            estado_ = Estado::Documento;
            ++intentos_;
            r.decir = dorsoPedido_ ? "Perdón. Mostrame de nuevo la parte de atrás, más cerca de la cámara."
                                   : "Perdón. Mostrámelo de nuevo, más cerca de la cámara.";
            if (intentos_ >= 2) r.decir += " Si preferís, escribí el número en la pantalla.";
            r.documento = true;
            return r;
        }
        if (sn == 0) {
            estado_ = Estado::Preguntando;
            ++intentos_;
            r.decir = "Perdón. Decímelo de nuevo, despacio.";
            if (intentos_ >= 2) r.decir += " Si preferís, escribilo en la pantalla.";
            return r;
        }
        // Ni sí ni no: puede que haya dicho el dato corregido directamente
        // («no, ortega con hache…» suele venir como el correo entero de nuevo).
        json v;
        if (entender(c, texto, v) && v != propuesto_) {
            propuesto_ = v;
            r.decir = "Anoté " + paraDecir(c, v) + ". ¿Está bien?";
            return r;
        }
        r.decir = "¿Está bien " + paraDecir(c, propuesto_) + "? Decime sí o no.";
        return r;
    }
    if (esRepetir(f)) {
        r.decir = pregunta(actual_);
        return r;
    }
    // «Paso», «no tengo»… salvo que se le esté preguntando sí o no, donde «no
    // quiero» ES la respuesta. En un texto o un nombre se mira ANTES de
    // entender (si no, «paso» quedaría anotado como texto); en los demás,
    // después (un «no tengo» no es un correo, pero «ninguno» sí es un número).
    const auto saltear = [&]() {
        if (!c.obligatorio) {
            salteado_[actual_] = true;
            avanzar(r, "Dale, lo salteamos.");
        } else {
            r.decir = "Este dato lo necesitamos. " + pregunta(actual_);
        }
    };
    const bool libre = c.tipo == TipoCampo::Texto || c.tipo == TipoCampo::Nombre;
    if (libre && esSaltear(f)) {
        saltear();
        return r;
    }
    json v;
    if (!entender(c, texto, v)) {
        if (c.tipo != TipoCampo::SiNo && esSaltear(f)) saltear();
        else fallo(r);
        return r;
    }
    // Un sí o un no no se confirma con otro sí o no: sería preguntar dos veces lo mismo.
    if (t_.confirmar && c.tipo != TipoCampo::SiNo) {
        estado_ = Estado::Confirmando;
        propuesto_ = v;
        r.decir = "Anoté " + paraDecir(c, v) + ". ¿Está bien?";
        return r;
    }
    aceptar(v);
    r.campos = true;
    avanzar(r, "Perfecto.");
    return r;
}

Conversacion::Respuesta Conversacion::escrito(const std::string& clave, const std::string& valor) {
    if (atendida_) { // lo anota sin que el asistente diga nada: le está hablando una persona
        std::string error;
        return poner(clave, valor, error);
    }
    Respuesta r;
    if (!enCurso() || estado_ == Estado::Foto) return r;
    std::size_t i = 0;
    while (i < t_.campos.size() && t_.campos[i].clave != clave) ++i;
    if (i == t_.campos.size()) return r;
    const Campo& c = t_.campos[i];
    json v;
    r.pedir = t_.campos[actual_].clave;
    if (c.tipo == TipoCampo::Documento) {
        if (!entender(c, valor, v)) {
            r.decir = "Eso no parece un número de documento. Fijate si está bien escrito.";
            return r;
        }
        v["origen"] = "tipeado";
    } else if (!entender(c, valor, v)) {
        r.decir = c.tipo == TipoCampo::Correo     ? "Eso no parece un correo. Fijate si está bien escrito."
                  : c.tipo == TipoCampo::Telefono ? "Eso no parece un teléfono. Fijate si está bien escrito."
                                                  : "No entendí lo que escribiste. Fijate si está bien.";
        return r;
    }
    // Lo tipeado no se confirma: lo está viendo escrito.
    valores_[i] = v;
    salteado_[i] = false;
    if (c.tipo == TipoCampo::Documento) completarDesdeDocumento(v);
    r.campos = true;
    if (i == actual_) avanzar(r, "Gracias, ya lo anoté.");
    return r;
}

Conversacion::Respuesta Conversacion::silencio() {
    Respuesta r;
    // En el paso de la foto el que espera es quien llama (fotoFallo): callado es lo esperable.
    // Lo mismo mostrando el documento (documentoFallo). Atendida: los silencios son de la
    // charla con la persona, no del guion.
    if (!enCurso() || estado_ == Estado::Foto || estado_ == Estado::Documento || atendida_) return r;
    r.pedir = t_.campos[actual_].clave;
    ++silencios_;
    if (silencios_ == 1) {
        r.decir = estado_ == Estado::Confirmando
                      ? "¿Seguís ahí? ¿Está bien " + paraDecir(t_.campos[actual_], propuesto_) + "?"
                      : "¿Seguís ahí? " + pregunta(actual_);
        return r;
    }
    // Se fue (o se distrajo con otra cosa): se corta sin guardar, para que el
    // siguiente no herede lo que contestó este a medias.
    estado_ = Estado::Terminada;
    r.decir = "Bueno, lo dejamos acá. Cuando quieras, empezamos de nuevo.";
    r.pedir.clear();
    r.terminar = true;
    return r;
}

Conversacion::Respuesta Conversacion::cancelar() {
    Respuesta r;
    if (!enCurso()) return r;
    estado_ = Estado::Terminada;
    r.decir = "Listo, cancelé el registro. ¡Hasta luego!";
    r.terminar = true;
    return r;
}

Conversacion::Respuesta Conversacion::fotoLista() {
    Respuesta r;
    if (estado_ != Estado::Foto || atendida_) return r;
    fotoHecha_ = true;
    terminar(r, "¡Salió muy bien!");
    return r;
}

Conversacion::Respuesta Conversacion::fotoFallo(const std::string& sugerencia) {
    Respuesta r;
    if (estado_ != Estado::Foto || atendida_) return r;
    if (++fotoIntentos_ == 1) {
        r.decir = (sugerencia.empty() ? std::string("No te veo bien.") : sugerencia) + " Mirá derecho a la cámara.";
        r.foto = true;
        return r;
    }
    // Una segunda vez no: la fila de la puerta importa más que la foto (la cara que
    // se vio durante la charla queda igual, si sirvió).
    fotoHecha_ = true;
    terminar(r, "No pasa nada, seguimos sin la foto.");
    return r;
}

json Conversacion::datos() const {
    json d = json::object();
    for (std::size_t i = 0; i < t_.campos.size(); ++i)
        if (!valores_[i].is_null()) d[t_.campos[i].clave] = valores_[i];
    return d;
}

json Conversacion::camposParaMostrar() const {
    json out = json::array();
    for (std::size_t i = 0; i < t_.campos.size(); ++i) {
        const Campo& c = t_.campos[i];
        out.push_back({{"clave", c.clave},
                       {"etiqueta", c.etiqueta.empty() ? c.clave : c.etiqueta},
                       {"valor", paraMostrar(c, valores_[i])},
                       {"listo", !valores_[i].is_null()}});
    }
    return out;
}

json Conversacion::pedido() const {
    if (!enCurso() || estado_ == Estado::Foto) return nullptr;
    const Campo& c = t_.campos[actual_];
    return {{"clave", c.clave},
            {"etiqueta", c.etiqueta.empty() ? c.clave : c.etiqueta},
            {"tipo", nombreDeTipo(c.tipo)},
            {"ejemplo", c.ejemplo}};
}

std::string Conversacion::nombre() const {
    for (std::size_t i = 0; i < t_.campos.size(); ++i)
        if (t_.campos[i].tipo == TipoCampo::Nombre && valores_[i].is_string()) return valores_[i].get<std::string>();
    return cara_.nombre;
}

std::string Conversacion::paso() const {
    if (atendida_ && enCurso()) return "la atiende " + (atendidaPor_.empty() ? std::string("alguien del equipo") : atendidaPor_);
    switch (estado_) {
        case Estado::Esperando: return "esperando";
        case Estado::Terminada: return "terminada";
        case Estado::Foto: return "mirando a la cámara";
        case Estado::Documento: return dorsoPedido_ ? "leyendo el dorso del documento" : "leyendo el documento";
        case Estado::Preguntando:
        case Estado::Confirmando: {
            const Campo& c = t_.campos[actual_];
            return std::string(estado_ == Estado::Preguntando ? "preguntando " : "confirmando ") +
                   (c.etiqueta.empty() ? c.clave : c.etiqueta);
        }
    }
    return {};
}

// --- El documento ---------------------------------------------------------------

namespace {
/// Cómo se pide la parte de atrás: corto, hablado, y diciendo QUÉ mostrar (los
/// renglones de abajo), que es lo que tiene que quedar frente a la cámara.
constexpr const char* kPedirDorso =
    "Dalo vuelta y mostrame la parte de atrás, la de los renglones de letras y números de abajo.";
} // namespace

Conversacion::Respuesta Conversacion::oidoDocumento(const std::string& texto, const std::string& f) {
    Respuesta r;
    const Campo& c = t_.campos[actual_];
    r.pedir = c.clave;
    if (esSaltear(f)) {
        if (!c.obligatorio) {
            salteado_[actual_] = true;
            avanzar(r, "Dale, lo salteamos.");
        } else {
            r.decir = "Este dato lo necesitamos. Si la cámara no lo lee, escribí el número en la pantalla.";
            r.documento = true;
        }
        return r;
    }
    if (esRepetir(f)) {
        r.decir = dorsoPedido_ ? std::string(kPedirDorso) : pregunta(actual_);
        r.documento = true;
        return r;
    }
    // Dictó el número: se toma como si lo hubiera tipeado, pero dicho se confirma.
    json v;
    if (entender(c, texto, v)) {
        if (t_.confirmar) {
            estado_ = Estado::Confirmando;
            propuesto_ = v;
            r.decir = "Anoté " + paraDecir(c, v) + ". ¿Está bien?";
            return r;
        }
        aceptar(v);
        r.campos = true;
        avanzar(r, "Perfecto.");
    }
    // Lo demás («ahí va», «a ver») es charla mientras acomoda el documento: nada.
    return r;
}

Conversacion::Respuesta Conversacion::documentoLeido(const json& datos) {
    Respuesta r;
    if (estado_ != Estado::Documento || !datos.is_object()) return r;
    const Campo& c = t_.campos[actual_];
    json v = datos;
    if (!v.contains("origen")) v["origen"] = "codigo";
    r.pedir = c.clave;
    const ba::documentos::Datos d = ba::documentos::Datos::deJson(v);
    // Atendida por una persona: lo leído se anota callado (le está hablando ella) y el guion
    // sigue desde ahí si se lo devuelven.
    if (atendida_) {
        aceptar(v);
        r.campos = true;
        return r;
    }
    if (t_.confirmar) {
        estado_ = Estado::Confirmando;
        propuesto_ = v;
        r.decir = (d.apellido.empty() ? "Leí " : "Sos ") + paraDecir(c, v) + ". ¿Está bien?";
        return r;
    }
    aceptar(v);
    r.campos = true;
    avanzar(r, d.nombres.empty() ? std::string("Listo, ya lo leí.")
                                 : "Gracias, " + d.nombres.substr(0, d.nombres.find(' ')) + ". Ya lo leí.");
    return r;
}

Conversacion::Respuesta Conversacion::documentoFallo(const std::string& sugerencia) {
    Respuesta r;
    if (estado_ != Estado::Documento || atendida_) return r;
    const Campo& c = t_.campos[actual_];
    r.pedir = c.clave;
    if (++intentos_ == 1 && dorsoDisponible_ && !dorsoPedido_) {
        // El código del frente no sale (gastado, con reflejo, una cámara que no
        // enfoca tan cerca): la parte de atrás tiene los mismos datos en renglones de
        // letras grandes, que se leen con OCR. Se lo pide una vez, en vez de insistir
        // con el frente; si lo vuelve a mostrar, también se lee. La sugerencia es del
        // frente («mostrame el lado del código»): acá no va.
        dorsoPedido_ = true;
        r.decir = std::string("No pude leer el código. ") + kPedirDorso;
        r.documento = true;
        r.dorso = true;
        return r;
    }
    if (intentos_ == 1) {
        r.decir = (sugerencia.empty() ? std::string("No lo pude leer.") : sugerencia) +
                  " Tenelo quieto frente a la cámara, del lado del código de barras, sin que le dé el reflejo."
                  " Si preferís, escribí el número en la pantalla.";
        r.documento = true;
        return r;
    }
    // La segunda vez no se insiste: la fila de la puerta importa más que el documento.
    if (!c.obligatorio) {
        salteado_[actual_] = true;
        avanzar(r, "No pasa nada, seguimos sin el documento.");
        return r;
    }
    r.decir = "No lo puedo leer. Escribí el número en la pantalla, así seguimos.";
    r.documento = true;
    return r;
}

std::vector<std::string> Conversacion::documentosAceptados() const {
    if (!enCurso() || actual_ >= t_.campos.size() || t_.campos[actual_].tipo != TipoCampo::Documento) return {};
    return t_.campos[actual_].documentos;
}

void Conversacion::completarDesdeDocumento(const json& doc) {
    const ba::documentos::Datos d = ba::documentos::Datos::deJson(doc);
    for (std::size_t i = 0; i < t_.campos.size(); ++i) {
        if (i == actual_ || !valores_[i].is_null()) continue; // lo que ya contestó no se pisa
        const Campo& c = t_.campos[i];
        json x;
        if (c.tipo == TipoCampo::Nombre) {
            if (!d.apellido.empty() && !d.nombres.empty()) x = d.nombreCompleto();
        } else if (c.tipo == TipoCampo::Texto || c.tipo == TipoCampo::Numero) {
            // Por la clave: es lo único que dice qué dato es un campo de texto libre.
            std::string v;
            if (c.clave == "apellido" || c.clave == "apellidos") v = d.apellido;
            else if (c.clave == "nombres" || c.clave == "nombre_pila") v = d.nombres;
            else if (c.clave == "dni" || c.clave == "documento" || c.clave == "numero_documento") v = d.numero;
            else if (c.clave == "nacimiento" || c.clave == "fecha_nacimiento") v = d.nacimiento;
            else if (c.clave == "cuil") v = d.cuil;
            else if (c.clave == "sexo") v = d.sexo;
            if (!v.empty()) {
                if (c.tipo != TipoCampo::Numero) x = v;
                else if (todoDigitos(v)) x = std::stoll(v);
            }
        }
        if (x.is_null()) continue;
        valores_[i] = x;
        salteado_[i] = false;
    }
}

// --- Atendida por una persona ------------------------------------------------------------

bool Conversacion::atender(const std::string& quien) {
    if (estado_ == Estado::Terminada) return false;
    if (estado_ == Estado::Esperando) {
        // Empieza callada: el saludo y las preguntas los hace la persona que atiende.
        actual_ = 0;
        for (std::size_t i = 0; i < t_.campos.size(); ++i)
            if (valores_[i].is_null() && !salteado_[i]) {
                actual_ = i;
                break;
            }
        estado_ = Estado::Preguntando;
    }
    // Lo que se estaba confirmando queda sin confirmar: se lo dice a la persona, que lo anota.
    if (estado_ == Estado::Confirmando) estado_ = Estado::Preguntando;
    atendida_ = true;
    atendidaPor_ = quien;
    silencios_ = 0;
    intentos_ = 0;
    return true;
}

Conversacion::Respuesta Conversacion::devolver() {
    Respuesta r;
    if (!atendida_) return r;
    atendida_ = false;
    atendidaPor_.clear();
    if (!enCurso()) return r;
    silencios_ = 0;
    if (estado_ == Estado::Confirmando) estado_ = Estado::Preguntando;
    avanzar(r, "Sigo yo.");
    r.campos = true;
    return r;
}

Conversacion::Respuesta Conversacion::poner(const std::string& clave, const std::string& valor, std::string& error) {
    Respuesta r;
    error.clear();
    if (!enCurso()) {
        error = "Esa conversación no está en curso.";
        return r;
    }
    std::size_t i = 0;
    while (i < t_.campos.size() && t_.campos[i].clave != clave) ++i;
    if (i == t_.campos.size()) {
        error = "Ese tótem no pide «" + clave + "».";
        return r;
    }
    if (estado_ != Estado::Foto) r.pedir = t_.campos[actual_].clave;
    std::string limpio = valor;
    while (!limpio.empty() && std::isspace(static_cast<unsigned char>(limpio.back()))) limpio.pop_back();
    while (!limpio.empty() && std::isspace(static_cast<unsigned char>(limpio.front()))) limpio.erase(limpio.begin());
    if (limpio.empty()) {
        // Borrarlo: corregir es también sacar lo que quedó mal. Sin atender, si era el que
        // se pedía, se lo vuelve a preguntar más adelante (avanzar lo encuentra sin dato).
        valores_[i] = json();
        salteado_[i] = false;
        r.campos = true;
        return r;
    }
    const Campo& c = t_.campos[i];
    json v;
    if (!entender(c, limpio, v)) {
        error = c.tipo == TipoCampo::Correo     ? "Eso no parece un correo."
                : c.tipo == TipoCampo::Telefono ? "Eso no parece un teléfono."
                : c.tipo == TipoCampo::Numero   ? "Eso no es un número."
                : c.tipo == TipoCampo::SiNo     ? "Tiene que ser sí o no."
                : c.tipo == TipoCampo::Opcion   ? "Tiene que ser una de las opciones: " + listar(c.opciones) + "."
                                                : "No se entiende ese valor.";
        return r;
    }
    // Sin atender y en medio de las preguntas: es lo mismo que tipearlo en la pantalla (si era el
    // que se pedía, el asistente lo agradece y sigue).
    if (!atendida_ && estado_ != Estado::Foto) return escrito(clave, limpio);
    valores_[i] = v;
    salteado_[i] = false;
    if (estado_ == Estado::Confirmando && i == actual_) estado_ = Estado::Preguntando;
    r.campos = true;
    return r;
}

std::vector<std::string> Conversacion::faltan() const {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < t_.campos.size(); ++i)
        if (t_.campos[i].obligatorio && valores_[i].is_null())
            out.push_back(t_.campos[i].etiqueta.empty() ? t_.campos[i].clave : t_.campos[i].etiqueta);
    return out;
}

Conversacion::Respuesta Conversacion::guardarYa(std::string& error) {
    Respuesta r;
    error.clear();
    if (!enCurso()) {
        error = "Esa conversación no está en curso.";
        return r;
    }
    const auto f = faltan();
    if (!f.empty()) {
        error = "Falta " + listar(f) + " (es obligatorio).";
        return r;
    }
    // Lo decidió una persona: sin el paso de la foto (queda la mejor que se vio en la charla).
    atendida_ = false;
    atendidaPor_.clear();
    fotoHecha_ = true;
    terminar(r, "");
    return r;
}

} // namespace ba::registro
