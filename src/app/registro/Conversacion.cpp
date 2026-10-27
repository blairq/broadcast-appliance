// ============================================================================
// src/app/registro/Conversacion.cpp — ver Conversacion.hpp
//
// Lo propio de cada idioma está en DATOS: cómo se entiende lo dicho (los números en
// palabras, el sí y el no, el correo dictado, los nombres…) en
// `assets/idiomas/<código>/totem.json` (ver `Lengua` abajo y docs/desarrollo/I18N.md,
// «Tótem: datos del idioma»), y lo que dice el tótem en el catálogo, contexto «totem», en
// el idioma del evento. Acá no hay ninguna palabra de ningún idioma.
// ============================================================================
#include "registro/Conversacion.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include <mutex>

#include "diag/Registro.hpp"
#include "documentos/Documento.hpp"
#include "i18n/I18n.hpp"

namespace ba::registro {
namespace {

using nlohmann::json;

// --- El idioma: lo que se entiende (totem.json) ------------------------------------

/// Una lista de frases: lo dicho entero es una de `exactas`, o aparece una de `contiene`.
struct Frases {
    std::vector<std::string> exactas, contiene;
};

/// Los datos del idioma para entender lo que se dice (assets/idiomas/<código>/totem.json).
/// Las palabras ya aplanadas (como llega lo dicho); lo que se DICE (digitos, correoDecir,
/// decirMas), tal cual.
struct Lengua {
    std::string codigo;
    std::map<std::string, std::string> aplanar; ///< letra en UTF-8 -> cómo queda
    // Números.
    std::string reglaNumeros = "es";
    std::map<std::string, int> unidades, decenas, centenas;
    std::vector<std::string> centenasSolas, conectores, conectoresCentena, mil, millon, ninguno;
    std::vector<std::string> digitos; ///< del 0 al 9, para decir
    // Sí, no y lo demás.
    std::vector<std::string> si, no, noSabe, noFrases;
    Frases cancelar, repetir, saltear;
    // El correo.
    std::vector<std::pair<std::string, std::string>> correoFrases, correoPalabras; ///< dicho -> signo
    std::vector<std::string> correoNoDigitos, correoAntes, correoDespues, terminaciones;
    std::map<std::string, std::string> dominios;
    std::map<char, std::string> correoDecir;
    // El teléfono.
    std::vector<std::string> mas;
    std::string decirMas;
    std::map<std::string, int> repetidores;
    std::size_t grupoTelefono = 4;
    // El nombre.
    std::vector<std::vector<std::string>> nombreAntes;
    std::vector<std::string> nombreDespues, nombreNoEmpieza, particulas;
    // Las opciones.
    std::vector<std::vector<std::string>> orden;
    std::vector<std::string> ultima;
    bool etiquetaEnMinuscula = true;
};

/// Cuántos bytes ocupa la letra UTF-8 que empieza con `b` (1 si es ASCII o un byte suelto).
std::size_t largoUtf8(unsigned char b) {
    if (b >= 0xF0) return 4;
    if (b >= 0xE0) return 3;
    if (b >= 0xC0) return 2;
    return 1;
}

std::string aplanarCon(const Lengua& L, const std::string& s) {
    std::string out;
    bool espacio = false;
    const auto poner = [&](char c) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            espacio = !out.empty();
            return;
        }
        if (espacio) out.push_back(' ');
        espacio = false;
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    };
    for (std::size_t i = 0; i < s.size();) {
        const auto b = static_cast<unsigned char>(s[i]);
        if (b < 0x80) {
            poner(s[i++]);
            continue;
        }
        const std::size_t n = std::min(largoUtf8(b), s.size() - i);
        // Una letra que el idioma no aplana (otra escritura, un emoji): no aporta.
        if (const auto it = L.aplanar.find(s.substr(i, n)); it != L.aplanar.end())
            for (char c : it->second) poner(c);
        i += n;
    }
    return out;
}

std::vector<std::string> textosDe(const json& j, const char* clave) {
    std::vector<std::string> out;
    if (!j.is_object() || !j.contains(clave) || !j[clave].is_array()) return out;
    for (const json& x : j[clave])
        if (x.is_string()) out.push_back(x.get<std::string>());
    return out;
}

std::vector<std::string> aplanadas(const Lengua& L, const json& j, const char* clave) {
    std::vector<std::string> out = textosDe(j, clave);
    for (std::string& x : out) x = aplanarCon(L, x);
    return out;
}

std::map<std::string, int> numerosDe(const Lengua& L, const json& j, const char* clave) {
    std::map<std::string, int> out;
    if (!j.is_object() || !j.contains(clave) || !j[clave].is_object()) return out;
    for (auto it = j[clave].begin(); it != j[clave].end(); ++it)
        if (it.value().is_number_integer() && it.key().rfind('_', 0) != 0) out[aplanarCon(L, it.key())] = it.value().get<int>();
    return out;
}

Frases frasesDe(const Lengua& L, const json& j, const char* clave) {
    const json x = j.is_object() ? j.value(clave, json::object()) : json::object();
    return Frases{aplanadas(L, x, "exactas"), aplanadas(L, x, "contiene")};
}

Lengua cargarLengua(const std::string& codigo) {
    Lengua L;
    L.codigo = codigo;
    const std::string texto = ba::i18n::En(codigo).leer("totem.json");
    const json j = json::parse(texto, nullptr, false, true);
    if (!j.is_object()) {
        // Sin los datos del idioma el tótem no entiende nada (vuelve a preguntar, y se puede
        // tipear): es un bundle roto, no un idioma sin tótem (ese usa el de su respaldo).
        ba::diag::registro().linea(ba::diag::Nivel::Aviso, "registro", 0,
                                   ba::i18n::llenar(ba::i18n::traducir("registro", "el tótem no tiene los datos del idioma "
                                                                                   "«%1» (totem.json): no va a entender lo "
                                                                                   "que se le diga"),
                                                    {codigo}));
        return L;
    }
    // Primero cómo se aplana: con eso se aplanan las listas.
    if (j.contains("aplanar") && j["aplanar"].is_object())
        for (auto it = j["aplanar"].begin(); it != j["aplanar"].end(); ++it)
            if (it.value().is_string() && it.key().rfind('_', 0) != 0) L.aplanar[it.key()] = it.value().get<std::string>();

    const json n = j.value("numeros", json::object());
    L.reglaNumeros = n.value("regla", std::string("es"));
    L.unidades = numerosDe(L, n, "unidades");
    L.decenas = numerosDe(L, n, "decenas");
    L.centenas = numerosDe(L, n, "centenas");
    L.centenasSolas = aplanadas(L, n, "centenasSolas");
    L.conectores = aplanadas(L, n, "conectores");
    L.conectoresCentena = aplanadas(L, n, "conectoresCentena");
    L.mil = aplanadas(L, n, "mil");
    L.millon = aplanadas(L, n, "millon");
    L.ninguno = aplanadas(L, n, "ninguno");
    L.digitos = textosDe(n, "digitos");
    if (L.digitos.size() != 10) L.digitos.clear();

    const json r = j.value("respuestas", json::object());
    L.si = aplanadas(L, r, "si");
    L.no = aplanadas(L, r, "no");
    L.noSabe = aplanadas(L, r, "noSabe");
    L.noFrases = aplanadas(L, r, "noFrases");
    L.cancelar = frasesDe(L, j, "cancelar");
    L.repetir = frasesDe(L, j, "repetir");
    L.saltear = frasesDe(L, j, "saltear");

    const json c = j.value("correo", json::object());
    if (c.contains("signos") && c["signos"].is_array())
        for (const json& p : c["signos"]) {
            if (!p.is_array() || p.size() != 2 || !p[0].is_string() || !p[1].is_string()) continue;
            const std::string dicho = aplanarCon(L, p[0].get<std::string>());
            if (dicho.empty()) continue;
            (dicho.find(' ') != std::string::npos ? L.correoFrases : L.correoPalabras).emplace_back(dicho, p[1].get<std::string>());
        }
    L.correoNoDigitos = aplanadas(L, c, "noSonDigitos");
    L.correoAntes = aplanadas(L, c, "antes");
    L.correoDespues = aplanadas(L, c, "despues");
    L.terminaciones = aplanadas(L, c, "terminaciones");
    if (c.contains("dominios") && c["dominios"].is_object())
        for (auto it = c["dominios"].begin(); it != c["dominios"].end(); ++it)
            if (it.value().is_string()) L.dominios[aplanarCon(L, it.key())] = it.value().get<std::string>();
    if (c.contains("decir") && c["decir"].is_object())
        for (auto it = c["decir"].begin(); it != c["decir"].end(); ++it)
            if (it.key().size() == 1 && it.value().is_string()) L.correoDecir[it.key()[0]] = it.value().get<std::string>();

    const json t = j.value("telefono", json::object());
    L.mas = aplanadas(L, t, "mas");
    L.decirMas = t.value("decirMas", std::string());
    L.repetidores = numerosDe(L, t, "repetir");
    if (t.contains("grupo") && t["grupo"].is_number_integer() && t["grupo"].get<int>() > 0)
        L.grupoTelefono = static_cast<std::size_t>(t["grupo"].get<int>());

    const json nm = j.value("nombre", json::object());
    for (const std::string& f : aplanadas(L, nm, "antes")) {
        std::vector<std::string> w;
        std::string x;
        for (char ch : f + " ") {
            if (ch != ' ') {
                x.push_back(ch);
            } else if (!x.empty()) {
                w.push_back(x);
                x.clear();
            }
        }
        if (!w.empty()) L.nombreAntes.push_back(std::move(w));
    }
    L.nombreDespues = aplanadas(L, nm, "despues");
    L.nombreNoEmpieza = aplanadas(L, nm, "noEmpiezaCon");
    L.particulas = aplanadas(L, nm, "particulas");

    const json o = j.value("opciones", json::object());
    if (o.contains("orden") && o["orden"].is_array())
        for (const json& x : o["orden"]) {
            std::vector<std::string> v;
            if (x.is_array())
                for (const json& y : x)
                    if (y.is_string()) v.push_back(aplanarCon(L, y.get<std::string>()));
            L.orden.push_back(std::move(v));
        }
    L.ultima = aplanadas(L, o, "ultima");
    L.etiquetaEnMinuscula = j.value("etiquetaEnMinuscula", true);
    return L;
}

/// "" = el idioma del operador.
std::string codigoDe(const std::string& idioma) { return idioma.empty() ? ba::i18n::actual().codigo : idioma; }

/// Los datos de ese idioma (o de su respaldo), leídos una vez.
const Lengua& lengua(const std::string& idioma) {
    static std::mutex mu;
    static std::map<std::string, std::unique_ptr<const Lengua>> cache;
    const std::string codigo = codigoDe(idioma);
    std::lock_guard<std::mutex> l(mu);
    auto& p = cache[codigo];
    if (!p) p = std::make_unique<const Lengua>(cargarLengua(codigo));
    return *p;
}

/// Lo que dice el tótem, en ese idioma.
ba::i18n::En enIdioma(const std::string& idioma) { return ba::i18n::En(codigoDe(idioma)); }

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
std::string sinSignos(const Lengua& L, const std::string& s, const std::string& conservar = {}) {
    std::string out;
    for (char c : aplanarCon(L, s)) {
        const bool signo = std::ispunct(static_cast<unsigned char>(c)) != 0;
        out.push_back(signo && conservar.find(c) == std::string::npos ? ' ' : c);
    }
    return aplanarCon(L, out);
}

/// Si la frase (palabras separadas por un espacio) contiene `f` como palabras enteras.
bool tiene(const std::string& frase, const std::string& f) {
    return (" " + frase + " ").find(" " + f + " ") != std::string::npos;
}

bool esUna(const std::vector<std::string>& v, const std::string& x) { return std::find(v.begin(), v.end(), x) != v.end(); }

bool coincide(const Frases& fs, const std::string& f) {
    if (esUna(fs.exactas, f)) return true;
    for (const std::string& x : fs.contiene)
        if (tiene(f, x)) return true;
    return false;
}

bool todoDigitos(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// --- Números dichos ----------------------------------------------------------

bool esPalabraNumero(const Lengua& L, const std::string& w) {
    return L.unidades.count(w) != 0 || L.decenas.count(w) != 0 || L.centenas.count(w) != 0;
}

/// Un grupo de 0 a 999 dicho en palabras a partir de `i` («cuatrocientos
/// cuarenta y cinco», «once», «treinta»). Devuelve -1 si en `i` no empieza uno.
/// La regla «es» del perfil (la única por ahora; ver totem.json, «numeros»): otra forma de
/// armar los números va acá, elegida por `L.reglaNumeros`.
int grupo(const Lengua& L, const std::vector<std::string>& w, std::size_t& i) {
    if (i >= w.size()) return -1;
    int v = -1;
    if (auto c = L.centenas.find(w[i]); c != L.centenas.end()) {
        v = c->second;
        ++i;
        // Una centena sola («cien») no lleva resto; las demás pueden seguir («ciento veinte»).
        if (esUna(L.centenasSolas, w[i - 1])) return v;
        std::size_t j = i;
        if (j < w.size() && esUna(L.conectoresCentena, w[j])) ++j;
        const int resto = grupo(L, w, j);
        if (resto > 0 && resto < 100) {
            v += resto;
            i = j;
        }
        return v;
    }
    if (auto d = L.decenas.find(w[i]); d != L.decenas.end()) {
        v = d->second;
        ++i;
        // Sin conectores, la unidad va pegada («thirty five»).
        if (L.conectores.empty()) {
            if (auto u = i < w.size() ? L.unidades.find(w[i]) : L.unidades.end();
                u != L.unidades.end() && u->second >= 1 && u->second <= 9) {
                v += u->second;
                ++i;
            }
        } else if (i + 1 < w.size() && esUna(L.conectores, w[i])) {
            if (auto u = L.unidades.find(w[i + 1]); u != L.unidades.end() && u->second >= 1 && u->second <= 9) {
                v += u->second;
                i += 2;
            }
        }
        return v;
    }
    if (auto u = L.unidades.find(w[i]); u != L.unidades.end()) {
        ++i;
        return u->second;
    }
    return -1;
}

std::string digitoEnPalabra(const Lengua& L, char d) {
    return d >= '0' && d <= '9' && !L.digitos.empty() ? L.digitos[static_cast<std::size_t>(d - '0')] : std::string(1, d);
}

/// Una de las unidades del 0 al 9 (para el correo y el teléfono), o -1.
int digitoDicho(const Lengua& L, const std::string& w) {
    const auto u = L.unidades.find(w);
    return u != L.unidades.end() && u->second >= 0 && u->second <= 9 ? u->second : -1;
}

// --- Para no confundir respuestas con datos ----------------------------------

bool esCancelar(const Lengua& L, const std::string& f) { return coincide(L.cancelar, f); }
bool esRepetir(const Lengua& L, const std::string& f) { return coincide(L.repetir, f); }
bool esSaltear(const Lengua& L, const std::string& f) { return coincide(L.saltear, f); }

/// Pasa a minúscula o mayúscula la primera letra de una palabra en UTF-8 (las
/// acentuadas del latín-1 son dos bytes, 0xC3 + algo).
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
bool esRespuestaSuelta(const Lengua& L, const std::string& f) { return esUna(L.si, f) || esUna(L.no, f); }

/// «a, b o c»: las opciones, como se dicen.
std::string listar(const ba::i18n::En& en, const std::vector<std::string>& v) {
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i == 0) out = v[i];
        else if (i + 1 == v.size()) out = ba::i18n::llenar(en.traducir("totem", "%1 o %2"), {out, v[i]});
        else out = ba::i18n::llenar(en.traducir("totem", "%1, %2"), {out, v[i]});
    }
    return out;
}

std::string sinPuntoFinal(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '.' || s.back() == ',' || s.back() == ';')) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && s[i] == ' ') ++i;
    return s.substr(i);
}

// --- Los normalizadores, con los datos del idioma -----------------------------------

bool correoCon(const Lengua& L, const std::string& dicho, std::string& out) {
    // Se conservan los signos que pueden venir escritos: la voz a texto a
    // veces ya pone «jorge.ortega@gmail.com», y lo tipeado siempre.
    std::string s = sinSignos(L, sinPuntoFinal(dicho), ".@_-+");
    // Las palabras que se dicen en vez del signo. Primero las de varias palabras.
    for (const auto& [de, a] : L.correoFrases) {
        std::string f = " " + s + " ";
        for (std::size_t p = f.find(" " + de + " "); p != std::string::npos; p = f.find(" " + de + " ", p + 1))
            f.replace(p + 1, de.size(), a);
        s = aplanarCon(L, f);
    }
    std::vector<std::string> w = palabras(s);
    for (std::string& x : w) {
        bool signo = false;
        for (const auto& [de, a] : L.correoPalabras)
            if (x == de) {
                x = a;
                signo = true;
                break;
            }
        if (signo) continue;
        if (const int d = digitoDicho(L, x); d >= 0 && !esUna(L.correoNoDigitos, x)) x = std::to_string(d);
    }
    // Lo que se dice antes del correo («mi correo es…») y después («gracias»).
    while (!w.empty() && esUna(L.correoAntes, w.front())) w.erase(w.begin());
    while (!w.empty() && esUna(L.correoDespues, w.back())) w.pop_back();
    // «gmail com», «yahoo com ar»: el punto no se dijo (o la voz a texto lo perdió). Un
    // dominio no tiene espacios, así que las terminaciones conocidas sueltas van con punto.
    const auto conPunto = [](const std::string& x) { return x.find('.') != std::string::npos; };
    if (const auto at = std::find(w.begin(), w.end(), "@"); at != w.end() && std::none_of(at, w.end(), conPunto)) {
        for (auto it = w.end() - 1; it > at + 1 && esUna(L.terminaciones, *it); --it) *it = "." + *it;
    }
    std::string c = unir(w, 0, std::string::npos, "");
    const auto arroba = c.find('@');
    if (arroba == std::string::npos || arroba == 0 || c.find('@', arroba + 1) != std::string::npos) return false;
    std::string local = c.substr(0, arroba), dominio = c.substr(arroba + 1);
    // Un gmail sin «punto com» es un gmail.com: nadie tiene otro.
    if (dominio.find('.') == std::string::npos)
        if (const auto it = L.dominios.find(dominio); it != L.dominios.end()) dominio = it->second;
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

bool telefonoCon(const Lengua& L, const std::string& dicho, std::string& out) {
    const std::vector<std::string> w = palabras(sinSignos(L, dicho, "+"));
    std::string r;
    for (std::size_t i = 0; i < w.size();) {
        const std::string& x = w[i];
        const auto rep = L.repetidores.find(x);
        if ((esUna(L.mas, x) || x == "+") && r.empty()) {
            r = "+";
            ++i;
        } else if (x.size() > 1 && x[0] == '+' && todoDigitos(x.substr(1)) && r.empty()) {
            r = x;
            ++i;
        } else if (todoDigitos(x)) {
            r += x;
            ++i;
        } else if (rep != L.repetidores.end() && rep->second > 0 && i + 1 < w.size()) {
            const int u = digitoDicho(L, w[i + 1]);
            const bool escrito = w[i + 1].size() == 1 && todoDigitos(w[i + 1]);
            if (u >= 0 || escrito) {
                const char d = u >= 0 ? static_cast<char>('0' + u) : w[i + 1][0];
                r.append(static_cast<std::size_t>(rep->second), d);
                i += 2;
            } else {
                ++i;
            }
        } else if (esPalabraNumero(L, x)) {
            std::size_t j = i;
            const int v = grupo(L, w, j);
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

bool numeroCon(const Lengua& L, const std::string& dicho, long long& out) {
    const std::vector<std::string> w = palabras(sinSignos(L, sinPuntoFinal(dicho), "."));
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
        if (esUna(L.ninguno, x)) {
            hay = true;
            ++i;
        } else if (esUna(L.mil, x)) {
            actual = (actual == 0 ? 1 : actual) * 1000;
            total += actual;
            actual = 0;
            hay = true;
            ++i;
        } else if (esUna(L.millon, x)) {
            total = (total + (actual == 0 ? 1 : actual)) * 1000000;
            actual = 0;
            hay = true;
            ++i;
        } else if (esPalabraNumero(L, x)) {
            std::size_t j = i;
            actual += grupo(L, w, j);
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

int siNoCon(const Lengua& L, const std::string& dicho) {
    std::string f = sinSignos(L, dicho);
    // «no sé» no es un no: es que no sabe. Se saca antes de buscar.
    for (const std::string& x : L.noSabe)
        if (tiene(f, x)) return -1;
    // Los «no» que llevan un «bien» o un «es» adentro, antes de mirar palabra por palabra.
    for (const std::string& x : L.noFrases)
        if (tiene(f, x)) return 0;
    bool si = false, no = false;
    for (const std::string& x : L.si)
        if (tiene(f, x)) si = true;
    for (const std::string& x : L.no)
        if (tiene(f, x)) no = true;
    if (si == no) return -1; // ni una ni otra, o las dos: se vuelve a preguntar
    return si ? 1 : 0;
}

bool nombreCon(const Lengua& L, const std::string& dicho, std::string& out) {
    // Las palabras ORIGINALES (con sus acentos) y su versión aplanada al lado,
    // para reconocer «me llamo» sin perder la tilde de «Martínez».
    std::string limpio;
    for (char c : dicho) {
        const bool signo = std::string(",.;:?!\"()[]{}").find(c) != std::string::npos;
        limpio.push_back(signo ? ' ' : c);
    }
    // Los signos de más de un byte que el idioma aplana a un espacio («¿», «¡»).
    for (const auto& [letra, queda] : L.aplanar) {
        if (queda != " ") continue;
        for (std::size_t p = limpio.find(letra); p != std::string::npos; p = limpio.find(letra))
            limpio.replace(p, letra.size(), " ");
    }
    std::vector<std::string> orig = palabras(limpio);
    std::vector<std::string> plano;
    for (const std::string& o : orig) plano.push_back(aplanarCon(L, o));
    bool saco = true;
    while (saco && !plano.empty()) {
        saco = false;
        for (const auto& p : L.nombreAntes) {
            if (p.size() > plano.size() || !std::equal(p.begin(), p.end(), plano.begin())) continue;
            plano.erase(plano.begin(), plano.begin() + static_cast<std::ptrdiff_t>(p.size()));
            orig.erase(orig.begin(), orig.begin() + static_cast<std::ptrdiff_t>(p.size()));
            saco = true;
            break;
        }
    }
    while (!plano.empty() && esUna(L.nombreDespues, plano.back())) {
        plano.pop_back();
        orig.pop_back();
    }
    if (orig.empty() || orig.size() > 7) return false;
    // Una respuesta suelta («sí», «no», «dale»), una negativa («no quiero»)
    // o un número no son un nombre.
    if (esRespuestaSuelta(L, unir(plano)) || esUna(L.nombreNoEmpieza, plano.front())) return false;
    for (const std::string& p : plano)
        if (std::any_of(p.begin(), p.end(), [](char c) { return c >= '0' && c <= '9'; })) return false;
    if (std::all_of(plano.begin(), plano.end(), [&L](const std::string& q) { return esPalabraNumero(L, q); })) return false;
    std::string r;
    for (std::size_t i = 0; i < orig.size(); ++i) {
        const bool part = i > 0 && esUna(L.particulas, plano[i]);
        if (!r.empty()) r += " ";
        r += part ? plano[i] : capital(orig[i]);
    }
    if (aplanarCon(L, r).size() < 2) return false;
    out = r;
    return true;
}

bool opcionCon(const Lengua& L, const std::string& dicho, const std::vector<std::string>& opciones, std::string& out) {
    const std::string f = sinSignos(L, dicho);
    if (f.empty() || opciones.empty()) return false;
    // Por su texto: la opción más larga que aparezca entera en lo dicho («sala
    // grande» gana sobre «sala» si dijo «la sala grande»).
    int mejor = -1;
    std::size_t largo = 0;
    for (std::size_t i = 0; i < opciones.size(); ++i) {
        const std::string o = sinSignos(L, opciones[i]);
        if (!o.empty() && tiene(f, o) && o.size() > largo) {
            mejor = static_cast<int>(i);
            largo = o.size();
        }
    }
    // O dijo un pedazo que solo está en una («la grande»).
    if (mejor < 0) {
        int cuantas = 0;
        for (std::size_t i = 0; i < opciones.size(); ++i) {
            const std::string o = sinSignos(L, opciones[i]);
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
        for (std::size_t i = 0; i < L.orden.size() && i < opciones.size() && mejor < 0; ++i)
            for (const std::string& p : L.orden[i])
                if (tiene(f, p)) mejor = static_cast<int>(i);
        if (mejor < 0)
            for (const std::string& p : L.ultima)
                if (tiene(f, p)) mejor = static_cast<int>(opciones.size()) - 1;
    }
    if (mejor < 0) return false;
    out = opciones[static_cast<std::size_t>(mejor)];
    return true;
}

} // namespace

// --- Normalizadores ------------------------------------------------------------

std::string aplanar(const std::string& s, const std::string& idioma) { return aplanarCon(lengua(idioma), s); }

bool entenderCorreo(const std::string& dicho, std::string& out, const std::string& idioma) {
    return correoCon(lengua(idioma), dicho, out);
}

bool entenderTelefono(const std::string& dicho, std::string& out, const std::string& idioma) {
    return telefonoCon(lengua(idioma), dicho, out);
}

bool entenderNumero(const std::string& dicho, long long& out, const std::string& idioma) {
    return numeroCon(lengua(idioma), dicho, out);
}

int entenderSiNo(const std::string& dicho, const std::string& idioma) { return siNoCon(lengua(idioma), dicho); }

std::string palabraSiNo(bool si, const std::string& idioma) {
    const Lengua& L = lengua(idioma);
    const std::vector<std::string>& v = si ? L.si : L.no;
    return v.empty() ? std::string(si ? "1" : "0") : v.front();
}

bool entenderNombre(const std::string& dicho, std::string& out, const std::string& idioma) {
    return nombreCon(lengua(idioma), dicho, out);
}

bool entenderOpcion(const std::string& dicho, const std::vector<std::string>& opciones, std::string& out,
                    const std::string& idioma) {
    return opcionCon(lengua(idioma), dicho, opciones, out);
}

bool entenderTexto(const std::string& dicho, std::string& out, const std::string& idioma) {
    const std::string s = sinPuntoFinal(dicho);
    if (aplanar(s, idioma).empty()) return false;
    out = mayusculaInicial(s);
    return true;
}

bool entender(const Campo& c, const std::string& dicho, json& out, const std::string& idioma) {
    switch (c.tipo) {
        case TipoCampo::Correo: {
            std::string v;
            if (!entenderCorreo(dicho, v, idioma)) return false;
            out = v;
            return true;
        }
        case TipoCampo::Telefono: {
            std::string v;
            if (!entenderTelefono(dicho, v, idioma)) return false;
            out = v;
            return true;
        }
        case TipoCampo::Numero: {
            long long v = 0;
            if (!entenderNumero(dicho, v, idioma)) return false;
            out = v;
            return true;
        }
        case TipoCampo::SiNo: {
            const int v = entenderSiNo(dicho, idioma);
            if (v < 0) return false;
            out = v == 1;
            return true;
        }
        case TipoCampo::Nombre: {
            std::string v;
            if (!entenderNombre(dicho, v, idioma)) return false;
            out = v;
            return true;
        }
        case TipoCampo::Opcion: {
            std::string v;
            if (!entenderOpcion(dicho, c.opciones, v, idioma)) return false;
            out = v;
            return true;
        }
        case TipoCampo::Texto: {
            std::string v;
            if (!entenderTexto(dicho, v, idioma)) return false;
            out = v;
            return true;
        }
        case TipoCampo::Documento: {
            // Lo que se dice (o tipea) es el número: el respaldo de la cámara.
            std::string v;
            if (!entenderNumeroDocumento(dicho, v, idioma)) return false;
            out = json{{"numero", v}, {"origen", "dicho"}};
            return true;
        }
    }
    return false;
}

bool entenderNumeroDocumento(const std::string& dicho, std::string& out, const std::string& idioma) {
    // «11.222.333» tipeado o «once dos dos dos…» dicho: lo mismo que un teléfono
    // (dígitos o palabras, con lo que se dice antes), con el largo de un documento.
    std::string t;
    if (!entenderTelefono(dicho, t, idioma) || t.empty() || t[0] == '+') {
        long long n = 0;
        if (!entenderNumero(dicho, n, idioma) || n <= 0) return false;
        t = std::to_string(n);
    }
    t.erase(0, std::min(t.find_first_not_of('0'), t.size()));
    if (t.size() < 6 || t.size() > 9 || !todoDigitos(t)) return false;
    out = t;
    return true;
}

namespace {
/// «Juana Andrea Prueba, DNI 11.222.333» (o lo que haya) de un documento ya en `datos`.
std::string documentoParaDecir(const json& v, const char* separador, const std::string& idioma) {
    const ba::documentos::Datos d = ba::documentos::Datos::deJson(v);
    const bool dni = d.tipo.empty() || d.tipo == "dni-ar";
    const ba::i18n::En e = enIdioma(idioma);
    const std::string n = d.numero.empty() ? std::string()
                                           : ba::i18n::llenar(dni ? e.traducir("totem", "DNI %1") : e.traducir("totem", "documento %1"),
                                                              {ba::documentos::numeroConPuntos(d.numero)});
    const std::string quien = d.apellido.empty() ? std::string() : d.nombreCompleto();
    if (quien.empty()) return n;
    return n.empty() ? quien : quien + separador + n;
}

/// Los dígitos de un teléfono en grupos («11 4567 8901»): el primero con lo
/// que sobra, los demás de a `grupo` (4: así se dicen y se leen en Argentina; totem.json).
std::vector<std::string> gruposDeTelefono(const std::string& t, std::size_t grupo) {
    std::string d;
    for (char c : t)
        if (c >= '0' && c <= '9') d.push_back(c);
    std::vector<std::string> g;
    std::size_t primero = d.size() % grupo;
    if (primero == 0) primero = std::min<std::size_t>(grupo, d.size());
    if (!d.empty()) g.push_back(d.substr(0, primero));
    for (std::size_t i = primero; i < d.size(); i += grupo) g.push_back(d.substr(i, grupo));
    return g;
}
} // namespace

std::string paraDecir(const Campo& c, const json& v, const std::string& idioma) {
    if (c.tipo == TipoCampo::Documento && v.is_object()) return documentoParaDecir(v, ", ", idioma);
    if (v.is_boolean()) return v.get<bool>() ? enIdioma(idioma).traducir("totem", "que sí") : enIdioma(idioma).traducir("totem", "que no");
    if (v.is_number_integer()) return std::to_string(v.get<long long>());
    if (!v.is_string()) return {};
    const Lengua& L = lengua(idioma);
    const std::string s = v.get<std::string>();
    if (c.tipo == TipoCampo::Correo) {
        std::string out;
        for (char ch : s) {
            std::string pal;
            if (const auto it = L.correoDecir.find(ch); it != L.correoDecir.end()) pal = it->second;
            else if (ch >= '0' && ch <= '9') pal = digitoEnPalabra(L, ch);
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
        std::string out = s.rfind('+', 0) == 0 ? (L.decirMas.empty() ? std::string("+") : L.decirMas) + " " : "";
        bool primero = true;
        for (const std::string& g : gruposDeTelefono(s, L.grupoTelefono)) {
            if (!primero) out += ", ";
            primero = false;
            for (std::size_t i = 0; i < g.size(); ++i) out += (i ? " " : "") + digitoEnPalabra(L, g[i]);
        }
        return out;
    }
    return s;
}

std::string paraMostrar(const Campo& c, const json& v, const std::string& idioma) {
    if (v.is_null()) return {};
    if (c.tipo == TipoCampo::Documento && v.is_object()) return documentoParaDecir(v, " · ", idioma);
    if (v.is_boolean()) return v.get<bool>() ? enIdioma(idioma).traducir("totem", "Sí") : enIdioma(idioma).traducir("totem", "No");
    if (v.is_number_integer()) return std::to_string(v.get<long long>());
    if (!v.is_string()) return v.dump();
    if (c.tipo == TipoCampo::Telefono) {
        const std::string s = v.get<std::string>();
        std::string out = s.rfind('+', 0) == 0 ? "+" : "";
        for (const std::string& g : gruposDeTelefono(s, lengua(idioma).grupoTelefono))
            out += (out.empty() || out == "+" ? "" : " ") + g;
        return out;
    }
    return v.get<std::string>();
}

// --- La conversación ------------------------------------------------------------

Conversacion::Conversacion(Totem totem, std::string evento, std::string idioma)
    : t_(std::move(totem)), evento_(std::move(evento)), idioma_(codigoDe(idioma)) {
    valores_.assign(t_.campos.size(), json());
    salteado_.assign(t_.campos.size(), false);
    pendiente_.assign(t_.campos.size(), false);
    pendienteAntes_.assign(t_.campos.size(), false);
}

ba::i18n::En Conversacion::en() const { return ba::i18n::En(idioma_); }

std::string Conversacion::titulo() const { return conDatos(t_.titulo, evento_, {}, idioma_); }
std::string Conversacion::subtitulo() const { return conDatos(t_.subtitulo, evento_, {}, idioma_); }

std::string Conversacion::confirmar() const {
    // Cómo se pide la confirmación. Una frase larga y no un «sí» suelto: medio segundo de voz se
    // pierde entre el detector de voz y el reconocedor (probado en un tótem: el «sí» no entraba).
    //: Lo que se le pide que diga tiene que entenderse con las respuestas de assets/idiomas/<código>/totem.json.
    return en().traducir("totem", "¿Es correcto? Decime «sí, es correcto» o «no, es incorrecto».");
}

std::string Conversacion::anoteYConfirmar(const std::string& dicho) const {
    return ba::i18n::llenar(en().traducir("totem", "Anoté %1."), {dicho}) + " " + confirmar();
}

std::string Conversacion::pedirDorso() const {
    // Cómo se pide la parte de atrás: corto, hablado, y diciendo QUÉ mostrar (los
    // renglones de abajo), que es lo que tiene que quedar frente a la cámara.
    return en().traducir("totem", "Dalo vuelta y mostrame la parte de atrás, la de los renglones de letras y números de abajo.");
}

std::string Conversacion::pregunta(std::size_t i) const {
    const Campo& c = t_.campos[i];
    std::string p = c.pregunta.empty() ? preguntaDeFabrica(c.tipo, idioma_) : c.pregunta;
    // Las opciones se dicen siempre: por voz no hay una lista que mirar.
    if (c.tipo == TipoCampo::Opcion && !c.opciones.empty())
        p += " " + ba::i18n::llenar(en().traducir("totem", "Puede ser: %1."), {listar(en(), c.opciones)});
    // Si no se acepta el DNI, no se pide «tu DNI».
    if (c.tipo == TipoCampo::Documento && c.pregunta.empty() && !c.documentos.empty() &&
        std::find(c.documentos.begin(), c.documentos.end(), "dni-ar") == c.documentos.end())
        p = en().traducir("totem", "Mostrame tu documento a la cámara, del lado del código de barras.");
    return p;
}

std::string Conversacion::reintento(const Campo& c, int intento) const {
    const ba::i18n::En e = en();
    std::string r;
    switch (c.tipo) {
        case TipoCampo::Correo:
            r = ba::i18n::llenar(e.traducir("totem", "No te entendí el correo. Decímelo despacio, por ejemplo: %1."),
                                 {c.ejemplo.empty() ? e.traducir("totem", "juan punto perez arroba gmail punto com") : c.ejemplo});
            break;
        case TipoCampo::Telefono:
            r = ba::i18n::llenar(e.traducir("totem", "No me quedó claro el número. Decímelo de a un número, por ejemplo: %1."),
                                 {c.ejemplo.empty() ? e.traducir("totem", "once, cuatro cinco seis siete, ocho nueve cero uno")
                                                    : c.ejemplo});
            break;
        case TipoCampo::Numero: r = e.traducir("totem", "No entendí el número. ¿Me lo repetís?"); break;
        case TipoCampo::SiNo: r = e.traducir("totem", "Perdón, no te entendí. ¿Sí o no?"); break;
        case TipoCampo::Opcion:
            r = ba::i18n::llenar(e.traducir("totem", "No te entendí. Puede ser: %1."), {listar(e, c.opciones)});
            break;
        case TipoCampo::Nombre: r = e.traducir("totem", "No te escuché bien. ¿Me repetís tu nombre?"); break;
        case TipoCampo::Texto: r = e.traducir("totem", "No te escuché bien. ¿Me lo repetís?"); break;
        case TipoCampo::Documento: r = e.traducir("totem", "No te entendí el número. Decímelo de a un número."); break;
    }
    if (!c.ejemplo.empty() && (c.tipo == TipoCampo::Numero || c.tipo == TipoCampo::Texto || c.tipo == TipoCampo::Nombre))
        r += " " + ba::i18n::llenar(e.traducir("totem", "Por ejemplo: %1."), {c.ejemplo});
    if (intento >= 2) r += " " + e.traducir("totem", "Si preferís, escribilo en la pantalla.");
    return r;
}

Conversacion::Respuesta Conversacion::empezar(const Cara& cara) {
    Respuesta r;
    if (estado_ != Estado::Esperando) return r;
    cara_ = cara;
    const std::string nombreSaludo = t_.reconocer ? cara.nombre : std::string();
    if (cara.yaRegistrado && !t_.yaRegistrado.empty()) {
        estado_ = Estado::Terminada;
        r.decir = conDatos(t_.yaRegistrado, evento_, nombreSaludo, idioma_);
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
    avanzar(r, conDatos(t_.saludo, evento_, nombreSaludo, idioma_));
    r.campos = true;
    return r;
}

void Conversacion::conocerCara(const Cara& cara) {
    if (cara_.personaId.empty()) cara_.personaId = cara.personaId;
    if (cara_.nombre.empty()) cara_.nombre = cara.nombre;
}

void Conversacion::avanzar(Respuesta& r, const std::string& antes) {
    // Lo que anotó el modelo no pasa sin el «sí»: antes de seguir, se lo confirma todo junto.
    if (hayPendientes()) {
        estado_ = Estado::Confirmando;
        confirmandoVarios_ = true;
        for (std::size_t i = 0; i < pendiente_.size(); ++i)
            if (pendiente_[i]) {
                actual_ = i;
                break;
            }
        r.decir = antes.empty() ? fraseConfirmarVarios() : antes + " " + fraseConfirmarVarios();
        r.pedir = t_.campos[actual_].clave;
        r.campos = true;
        return;
    }
    confirmandoVarios_ = false;
    corrigiendo_ = false;
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
        const std::string pedido =
            en().traducir("totem", "Por último, mirá derecho a la cámara un momento, así te saco una foto de frente.");
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
    const std::string chau = conDatos(t_.despedida, evento_, quien, idioma_);
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
        avanzar(r, en().traducir("totem", "No pasa nada, lo dejamos en blanco."));
    } else {
        r.decir = en().traducir("totem", "Escribilo en la pantalla, así no hay error.");
    }
}

Conversacion::Respuesta Conversacion::oido(const std::string& texto) {
    Respuesta r;
    if (!enCurso() || atendida_) return r; // atendida: le contesta una persona, no el guion
    const Lengua& L = lengua(idioma_);
    const std::string f = sinSignos(L, texto);
    if (f.empty()) return r;
    silencios_ = 0;
    if (esCancelar(L, f)) return cancelar();
    if (estado_ == Estado::Foto) return r; // mirando a la cámara: lo que diga no cambia nada
    if (estado_ == Estado::Documento) return oidoDocumento(texto, f);
    const Campo& c = t_.campos[actual_];
    r.pedir = c.clave;
    if (estado_ == Estado::Confirmando && confirmandoVarios_) return oidoConfirmandoVarios(texto);
    if (estado_ == Estado::Confirmando) {
        const int sn = entenderSiNo(texto, idioma_);
        if (sn == 1) {
            aceptar(propuesto_);
            r.campos = true;
            avanzar(r, en().traducir("totem", "Perfecto."));
            return r;
        }
        if (sn == 0 && c.tipo == TipoCampo::Documento) {
            // Leyó mal (u otro documento): a mostrarlo de nuevo.
            estado_ = Estado::Documento;
            ++intentos_;
            r.decir = dorsoPedido_ ? en().traducir("totem", "Perdón. Mostrame de nuevo la parte de atrás, más cerca de la cámara.")
                                   : en().traducir("totem", "Perdón. Mostrámelo de nuevo, más cerca de la cámara.");
            if (intentos_ >= 2) r.decir += " " + en().traducir("totem", "Si preferís, escribí el número en la pantalla.");
            r.documento = true;
            return r;
        }
        if (sn == 0) {
            estado_ = Estado::Preguntando;
            ++intentos_;
            r.decir = en().traducir("totem", "Perdón. Decímelo de nuevo, despacio.");
            if (intentos_ >= 2) r.decir += " " + en().traducir("totem", "Si preferís, escribilo en la pantalla.");
            return r;
        }
        // Ni sí ni no: puede que haya dicho el dato corregido directamente
        // («no, ortega con hache…» suele venir como el correo entero de nuevo).
        json v;
        if (entender(c, texto, v, idioma_) && v != propuesto_) {
            propuesto_ = v;
            r.decir = anoteYConfirmar(paraDecir(c, v, idioma_));
            return r;
        }
        r.decir = en().traducir("totem", "No te entendí.") + " " + anoteYConfirmar(paraDecir(c, propuesto_, idioma_));
        return r;
    }
    if (esRepetir(L, f)) {
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
            avanzar(r, en().traducir("totem", "Dale, lo salteamos."));
        } else {
            r.decir = en().traducir("totem", "Este dato lo necesitamos.") + " " + pregunta(actual_);
        }
    };
    const bool libre = c.tipo == TipoCampo::Texto || c.tipo == TipoCampo::Nombre;
    if (libre && esSaltear(L, f)) {
        saltear();
        return r;
    }
    json v;
    if (!entender(c, texto, v, idioma_)) {
        if (c.tipo != TipoCampo::SiNo && esSaltear(L, f)) saltear();
        else fallo(r);
        return r;
    }
    // Un sí o un no no se confirma con otro sí o no: sería preguntar dos veces lo mismo.
    if (t_.confirmar && c.tipo != TipoCampo::SiNo) {
        estado_ = Estado::Confirmando;
        propuesto_ = v;
        r.decir = anoteYConfirmar(paraDecir(c, v, idioma_));
        return r;
    }
    aceptar(v);
    r.campos = true;
    avanzar(r, en().traducir("totem", "Perfecto."));
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
    // Confirmando, los botones «Sí, es correcto» / «No, corregir» mandan «sí» o «no» para el
    // campo que se confirma: es la misma respuesta que la hablada.
    if (estado_ == Estado::Confirmando && i == actual_ && entenderSiNo(valor, idioma_) != -1) return oido(valor);
    const Campo& c = t_.campos[i];
    json v;
    r.pedir = t_.campos[actual_].clave;
    if (c.tipo == TipoCampo::Documento) {
        if (!entender(c, valor, v, idioma_)) {
            r.decir = en().traducir("totem", "Eso no parece un número de documento. Fijate si está bien escrito.");
            return r;
        }
        v["origen"] = "tipeado";
    } else if (!entender(c, valor, v, idioma_)) {
        r.decir = c.tipo == TipoCampo::Correo     ? en().traducir("totem", "Eso no parece un correo. Fijate si está bien escrito.")
                  : c.tipo == TipoCampo::Telefono ? en().traducir("totem", "Eso no parece un teléfono. Fijate si está bien escrito.")
                                                  : en().traducir("totem", "No entendí lo que escribiste. Fijate si está bien.");
        return r;
    }
    // Lo tipeado no se confirma: lo está viendo escrito.
    valores_[i] = v;
    salteado_[i] = false;
    pendiente_[i] = false;
    if (c.tipo == TipoCampo::Documento) completarDesdeDocumento(v);
    r.campos = true;
    // Confirmando lo que anotó el modelo, lo tipeado es una corrección: se vuelve a confirmar
    // lo que quede pendiente (avanzar), o se sigue.
    if (i == actual_ || confirmandoVarios_) avanzar(r, en().traducir("totem", "Gracias, ya lo anoté."));
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
        r.decir = en().traducir("totem", "¿Seguís ahí?") + " " + retomar();
        return r;
    }
    // Se fue (o se distrajo con otra cosa): se corta sin guardar, para que el
    // siguiente no herede lo que contestó este a medias.
    estado_ = Estado::Terminada;
    r.decir = en().traducir("totem", "Bueno, lo dejamos acá. Cuando quieras, empezamos de nuevo.");
    r.pedir.clear();
    r.terminar = true;
    return r;
}

Conversacion::Respuesta Conversacion::cancelar() {
    Respuesta r;
    if (!enCurso()) return r;
    estado_ = Estado::Terminada;
    r.decir = en().traducir("totem", "Listo, cancelé el registro. ¡Hasta luego!");
    r.terminar = true;
    return r;
}

Conversacion::Respuesta Conversacion::fotoLista() {
    Respuesta r;
    if (estado_ != Estado::Foto || atendida_) return r;
    fotoHecha_ = true;
    terminar(r, en().traducir("totem", "¡Salió muy bien!"));
    return r;
}

Conversacion::Respuesta Conversacion::fotoFallo(const std::string& sugerencia) {
    Respuesta r;
    if (estado_ != Estado::Foto || atendida_) return r;
    if (++fotoIntentos_ == 1) {
        r.decir = (sugerencia.empty() ? en().traducir("totem", "No te veo bien.") : sugerencia) + " " +
                  en().traducir("totem", "Mirá derecho a la cámara.");
        r.foto = true;
        return r;
    }
    // Una segunda vez no: la fila de la puerta importa más que la foto (la cara que
    // se vio durante la charla queda igual, si sirvió).
    fotoHecha_ = true;
    terminar(r, en().traducir("totem", "No pasa nada, seguimos sin la foto."));
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
                       {"valor", paraMostrar(c, valores_[i], idioma_)},
                       {"listo", !valores_[i].is_null() && !pendiente_[i]},
                       // anotado por el modelo, esperando el «sí» (la pantalla lo muestra a confirmar)
                       {"pendiente", pendiente_[i]}});
    }
    return out;
}

json Conversacion::pedido() const {
    if (!enCurso() || estado_ == Estado::Foto) return nullptr;
    const Campo& c = t_.campos[actual_];
    return {{"clave", c.clave},
            {"etiqueta", c.etiqueta.empty() ? c.clave : c.etiqueta},
            {"tipo", nombreDeTipo(c.tipo)},
            {"ejemplo", c.ejemplo},
            // Confirmando: la pantalla muestra «Sí, es correcto» / «No, corregir» (el respaldo
            // táctil del sí hablado).
            {"confirmando", estado_ == Estado::Confirmando}};
}

std::string Conversacion::nombre() const {
    for (std::size_t i = 0; i < t_.campos.size(); ++i)
        if (t_.campos[i].tipo == TipoCampo::Nombre && valores_[i].is_string()) return valores_[i].get<std::string>();
    return cara_.nombre;
}

std::string Conversacion::paso() const { return pasoEn(en()); }
std::string Conversacion::pasoParaOperador() const { return pasoEn(enIdioma(std::string())); }

std::string Conversacion::pasoEn(const ba::i18n::En& e) const {
    if (atendida_ && enCurso())
        return ba::i18n::llenar(e.traducir("totem", "la atiende %1"),
                                {atendidaPor_.empty() ? e.traducir("totem", "alguien del equipo") : atendidaPor_});
    switch (estado_) {
        case Estado::Esperando: return e.traducir("totem", "esperando");
        case Estado::Terminada: return e.traducir("totem", "terminada");
        case Estado::Foto: return e.traducir("totem", "mirando a la cámara");
        case Estado::Documento:
            return dorsoPedido_ ? e.traducir("totem", "leyendo el dorso del documento") : e.traducir("totem", "leyendo el documento");
        case Estado::Preguntando:
        case Estado::Confirmando: {
            const Campo& c = t_.campos[actual_];
            const std::string etiqueta = c.etiqueta.empty() ? c.clave : c.etiqueta;
            return ba::i18n::llenar(estado_ == Estado::Preguntando ? e.traducir("totem", "preguntando %1")
                                                                   : e.traducir("totem", "confirmando %1"),
                                    {etiqueta});
        }
    }
    return {};
}

// --- El documento ---------------------------------------------------------------

Conversacion::Respuesta Conversacion::oidoDocumento(const std::string& texto, const std::string& f) {
    Respuesta r;
    const Lengua& L = lengua(idioma_);
    const Campo& c = t_.campos[actual_];
    r.pedir = c.clave;
    if (esSaltear(L, f)) {
        if (!c.obligatorio) {
            salteado_[actual_] = true;
            avanzar(r, en().traducir("totem", "Dale, lo salteamos."));
        } else {
            r.decir = en().traducir("totem", "Este dato lo necesitamos. Si la cámara no lo lee, escribí el número en la pantalla.");
            r.documento = true;
        }
        return r;
    }
    if (esRepetir(L, f)) {
        r.decir = dorsoPedido_ ? pedirDorso() : pregunta(actual_);
        r.documento = true;
        return r;
    }
    // Dictó el número: se toma como si lo hubiera tipeado, pero dicho se confirma.
    json v;
    if (entender(c, texto, v, idioma_)) {
        if (t_.confirmar) {
            estado_ = Estado::Confirmando;
            propuesto_ = v;
            r.decir = anoteYConfirmar(paraDecir(c, v, idioma_));
            return r;
        }
        aceptar(v);
        r.campos = true;
        avanzar(r, en().traducir("totem", "Perfecto."));
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
        r.decir = ba::i18n::llenar(d.apellido.empty() ? en().traducir("totem", "Leí %1.") : en().traducir("totem", "Sos %1."),
                                   {paraDecir(c, v, idioma_)}) +
                  " " + confirmar();
        return r;
    }
    aceptar(v);
    r.campos = true;
    avanzar(r, d.nombres.empty() ? en().traducir("totem", "Listo, ya lo leí.")
                                 : ba::i18n::llenar(en().traducir("totem", "Gracias, %1. Ya lo leí."),
                                                    {d.nombres.substr(0, d.nombres.find(' '))}));
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
        r.decir = en().traducir("totem", "No pude leer el código.") + " " + pedirDorso();
        r.documento = true;
        r.dorso = true;
        return r;
    }
    if (intentos_ == 1) {
        r.decir = (sugerencia.empty() ? en().traducir("totem", "No lo pude leer.") : sugerencia) + " " +
                  en().traducir("totem", "Tenelo quieto frente a la cámara, del lado del código de barras, sin que le dé el "
                                         "reflejo. Si preferís, escribí el número en la pantalla.");
        r.documento = true;
        return r;
    }
    // La segunda vez no se insiste: la fila de la puerta importa más que el documento.
    if (!c.obligatorio) {
        salteado_[actual_] = true;
        avanzar(r, en().traducir("totem", "No pasa nada, seguimos sin el documento."));
        return r;
    }
    r.decir = en().traducir("totem", "No lo puedo leer. Escribí el número en la pantalla, así seguimos.");
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
            // Por la clave: es lo único que dice qué dato es un campo de texto libre. Las claves
            // son API (registro.json), no palabras del idioma del evento.
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
    // Lo que anotó el modelo sí queda: la persona que atiende lo ve en la lista y lo corrige.
    aceptarPendientes();
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
    avanzar(r, en().traducir("totem", "Sigo yo."));
    r.campos = true;
    return r;
}

Conversacion::Respuesta Conversacion::poner(const std::string& clave, const std::string& valor, std::string& error) {
    // Los errores son para quien atiende (la web, la app, el MCP): en el idioma del operador.
    Respuesta r;
    error.clear();
    if (!enCurso()) {
        error = ba::i18n::traducir("registro", "Esa conversación no está en curso.");
        return r;
    }
    std::size_t i = 0;
    while (i < t_.campos.size() && t_.campos[i].clave != clave) ++i;
    if (i == t_.campos.size()) {
        error = ba::i18n::llenar(ba::i18n::traducir("registro", "Ese tótem no pide «%1»."), {clave});
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
        pendiente_[i] = false;
        r.campos = true;
        return r;
    }
    const Campo& c = t_.campos[i];
    json v;
    if (!entender(c, limpio, v, idioma_)) {
        error = c.tipo == TipoCampo::Correo     ? ba::i18n::traducir("registro", "Eso no parece un correo.")
                : c.tipo == TipoCampo::Telefono ? ba::i18n::traducir("registro", "Eso no parece un teléfono.")
                : c.tipo == TipoCampo::Numero   ? ba::i18n::traducir("registro", "Eso no es un número.")
                : c.tipo == TipoCampo::SiNo     ? ba::i18n::traducir("registro", "Tiene que ser sí o no.")
                : c.tipo == TipoCampo::Opcion
                    ? ba::i18n::llenar(ba::i18n::traducir("registro", "Tiene que ser una de las opciones: %1."),
                                       {listar(enIdioma(std::string()), c.opciones)})
                    : ba::i18n::traducir("registro", "No se entiende ese valor.");
        return r;
    }
    // Sin atender y en medio de las preguntas: es lo mismo que tipearlo en la pantalla (si era el
    // que se pedía, el asistente lo agradece y sigue).
    if (!atendida_ && estado_ != Estado::Foto) return escrito(clave, limpio);
    valores_[i] = v;
    salteado_[i] = false;
    pendiente_[i] = false;
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
        error = ba::i18n::traducir("registro", "Esa conversación no está en curso.");
        return r;
    }
    const auto f = faltan();
    if (!f.empty()) {
        error = ba::i18n::llenar(ba::i18n::traducir("registro", "Falta %1 (es obligatorio)."), {listar(enIdioma(std::string()), f)});
        return r;
    }
    // Lo decidió una persona: sin el paso de la foto (queda la mejor que se vio en la charla).
    aceptarPendientes();
    atendida_ = false;
    atendidaPor_.clear();
    fotoHecha_ = true;
    terminar(r, "");
    return r;
}

// --- Con un modelo que entiende (iniciativa mixta) ------------------------------------------

namespace {
/// «a, b y c»: lo anotado se dice como se enumera hablando (listar() es para opciones: «o»).
std::string enumerar(const ba::i18n::En& en, const std::vector<std::string>& v) {
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i == 0) out = v[i];
        else if (i + 1 == v.size()) out = ba::i18n::llenar(en.traducir("totem", "%1 y %2"), {out, v[i]});
        else out = ba::i18n::llenar(en.traducir("totem", "%1, %2"), {out, v[i]});
    }
    return out;
}

/// La etiqueta para decirla en medio de una frase: en minúscula si el idioma lo hace
/// («correo», «teléfono»; totem.json, etiquetaEnMinuscula). Solo la primera letra: una sigla
/// («DNI») queda como está.
std::string etiquetaHablada(const Campo& c, bool minuscula) {
    std::string e = c.etiqueta.empty() ? c.clave : c.etiqueta;
    if (minuscula && e.size() > 1 && std::isupper(static_cast<unsigned char>(e[0])) &&
        !std::isupper(static_cast<unsigned char>(e[1])))
        e[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(e[0])));
    return e;
}
} // namespace

bool Conversacion::hayPendientes() const {
    return std::find(pendiente_.begin(), pendiente_.end(), true) != pendiente_.end();
}

bool Conversacion::paraElGuion(const std::string& dicho) const {
    const Lengua& L = lengua(idioma_);
    const std::string f = sinSignos(L, dicho);
    if (f.empty() || !enCurso() || atendida_) return true;
    if (esCancelar(L, f) || esRepetir(L, f)) return true;
    // Mirando a la cámara o mostrando el documento, lo que diga lo maneja el guion (dictar el
    // número, «saltear»): el modelo no ve la cámara.
    if (estado_ == Estado::Foto || estado_ == Estado::Documento) return true;
    // Un «sí, es correcto» o un «no» suelto a una confirmación: al instante. Con más palabras
    // («no, el correo es con jota») puede traer la corrección: eso lo entiende el modelo.
    if (estado_ == Estado::Confirmando && entenderSiNo(dicho, idioma_) != -1 && palabras(f).size() <= 3) return true;
    return false;
}

void Conversacion::empezarTurnoModelo() {
    pendienteAntes_ = pendiente_;
    anotadosTurno_ = 0;
    propuestosTurno_ = 0;
    confirmoTurno_ = false;
}

bool Conversacion::proponer(const std::string& clave, const std::string& valor, std::string& error,
                            std::string* comoQuedo) {
    // Los errores son para el modelo, que habla con la persona: en el idioma del evento.
    const ba::i18n::En e = en();
    error.clear();
    if (!enCurso() || atendida_ || estado_ == Estado::Foto) {
        error = e.traducir("asistente", "Ahora no se anotan datos (la conversación no está en un paso de preguntas).");
        return false;
    }
    std::size_t i = 0;
    while (i < t_.campos.size() && t_.campos[i].clave != clave) ++i;
    if (i == t_.campos.size()) {
        error = ba::i18n::llenar(e.traducir("asistente", "No hay un campo «%1» en este registro."), {clave});
        return false;
    }
    const Campo& c = t_.campos[i];
    json v;
    if (!entender(c, valor, v, idioma_)) {
        error = c.tipo == TipoCampo::Correo      ? e.traducir("asistente", "No es un correo válido. Pedile que lo repita despacio.")
                : c.tipo == TipoCampo::Telefono  ? e.traducir("asistente", "No es un teléfono válido (de 6 a 15 dígitos). Pedile que lo repita.")
                : c.tipo == TipoCampo::Numero    ? e.traducir("asistente", "No es un número.")
                : c.tipo == TipoCampo::SiNo      ? e.traducir("asistente", "Tiene que ser sí o no.")
                : c.tipo == TipoCampo::Opcion
                    ? ba::i18n::llenar(e.traducir("asistente", "Tiene que ser una de estas opciones: %1."), {listar(e, c.opciones)})
                : c.tipo == TipoCampo::Documento ? e.traducir("asistente", "No es un número de documento (de 6 a 9 dígitos).")
                : c.tipo == TipoCampo::Nombre    ? e.traducir("asistente", "No es un nombre.")
                                                 : e.traducir("asistente", "No se entiende ese valor.");
        if (!c.ejemplo.empty()) error += " " + ba::i18n::llenar(e.traducir("asistente", "Por ejemplo: %1."), {c.ejemplo});
        return false;
    }
    // Lo que se estaba confirmando por el guion (un solo dato) pasa a ser un pendiente más:
    // así lo nuevo y lo de antes se confirman juntos, sin perder ninguno.
    if (estado_ == Estado::Confirmando && !confirmandoVarios_ && !propuesto_.is_null() && actual_ != i &&
        valores_[actual_].is_null()) {
        valores_[actual_] = propuesto_;
        pendiente_[actual_] = true;
        pendienteAntes_[actual_] = true;
    }
    valores_[i] = v;
    salteado_[i] = false;
    // Un sí o un no no se confirma con otro sí o no (como en el guion), ni nada si el tótem no
    // confirma.
    pendiente_[i] = t_.confirmar && c.tipo != TipoCampo::SiNo;
    if (!pendiente_[i]) ++anotadosTurno_;
    ++propuestosTurno_;
    if (comoQuedo != nullptr) *comoQuedo = paraDecir(c, v, idioma_);
    return true;
}

bool Conversacion::confirmarPendientes() {
    bool alguno = false;
    for (std::size_t i = 0; i < pendiente_.size(); ++i)
        if (pendiente_[i] && pendienteAntes_[i]) {
            pendiente_[i] = false;
            alguno = true;
        }
    // El dato que confirmaba el guion (uno solo, sin pasar por el modelo).
    if (!alguno && estado_ == Estado::Confirmando && !confirmandoVarios_ && !propuesto_.is_null()) {
        aceptar(propuesto_);
        alguno = true;
    }
    if (alguno) confirmoTurno_ = true;
    return alguno;
}

void Conversacion::aceptarPendientes() {
    pendiente_.assign(pendiente_.size(), false);
    confirmandoVarios_ = false;
    corrigiendo_ = false;
}

std::string Conversacion::fraseConfirmarVarios() const {
    const ba::i18n::En e = en();
    const bool minuscula = lengua(idioma_).etiquetaEnMinuscula;
    std::vector<std::string> v;
    std::size_t solo = 0;
    for (std::size_t i = 0; i < pendiente_.size(); ++i)
        if (pendiente_[i]) {
            solo = i;
            //: la etiqueta de un campo y el dato, al repasar lo anotado («correo jorge punto ortega…»)
            v.push_back(ba::i18n::llenar(e.traducir("totem", "%1 %2"),
                                         {etiquetaHablada(t_.campos[i], minuscula), paraDecir(t_.campos[i], valores_[i], idioma_)}));
        }
    if (v.empty()) return {};
    // Uno solo, con la misma frase del guion («Anoté jorge punto…»).
    if (v.size() == 1) return anoteYConfirmar(paraDecir(t_.campos[solo], valores_[solo], idioma_));
    // Varios de una: se repasan todos y se pregunta por el conjunto. Con el modelo, la
    // persona puede decir directamente cuál está mal («el correo es con jota»).
    return ba::i18n::llenar(
        modelo_ ? e.traducir("totem", "Anoté %1. ¿Está todo correcto? Decime «sí, está todo correcto» o cuál dato está mal.")
                : e.traducir("totem", "Anoté %1. ¿Está todo correcto? Decime «sí, está todo correcto» o «no, hay algo mal»."),
        {enumerar(e, v)});
}

std::string Conversacion::retomar() const {
    switch (estado_) {
        case Estado::Preguntando: return pregunta(actual_);
        case Estado::Confirmando:
            if (confirmandoVarios_) return corrigiendo_ ? en().traducir("totem", "¿Qué dato está mal?") : fraseConfirmarVarios();
            return anoteYConfirmar(paraDecir(t_.campos[actual_], propuesto_, idioma_));
        case Estado::Documento: return dorsoPedido_ ? pedirDorso() : pregunta(actual_);
        case Estado::Foto: return en().traducir("totem", "Mirá derecho a la cámara un momento.");
        case Estado::Esperando:
        case Estado::Terminada: break;
    }
    return {};
}

Conversacion::Respuesta Conversacion::oidoConfirmandoVarios(const std::string& texto) {
    Respuesta r;
    r.pedir = t_.campos[actual_].clave;
    const int sn = entenderSiNo(texto, idioma_);
    if (sn == 1) {
        aceptarPendientes();
        estado_ = Estado::Preguntando;
        r.campos = true;
        avanzar(r, en().traducir("totem", "Perfecto."));
        return r;
    }
    if (sn == 0) {
        std::vector<std::size_t> pend;
        for (std::size_t i = 0; i < pendiente_.size(); ++i)
            if (pendiente_[i]) pend.push_back(i);
        if (pend.size() == 1) {
            // Uno solo: como en el guion, se lo pide de nuevo.
            valores_[pend[0]] = json();
            pendiente_[pend[0]] = false;
            confirmandoVarios_ = false;
            corrigiendo_ = false;
            estado_ = Estado::Preguntando;
            actual_ = pend[0];
            ++intentos_;
            r.pedir = t_.campos[actual_].clave;
            r.decir = en().traducir("totem", "Perdón. Decímelo de nuevo, despacio.");
            if (intentos_ >= 2) r.decir += " " + en().traducir("totem", "Si preferís, escribilo en la pantalla.");
            r.campos = true;
            return r;
        }
        if (modelo_ && !corrigiendo_) {
            // Con el modelo, lo que diga ahora («el correo está mal, es con jota») lo corrige.
            corrigiendo_ = true;
            r.decir = en().traducir("totem", "Perdón. ¿Qué dato está mal?");
            return r;
        }
        // Sin modelo no hay cómo saber cuál: se pregunta de nuevo todo lo pendiente.
        for (std::size_t i : pend) {
            valores_[i] = json();
            pendiente_[i] = false;
        }
        confirmandoVarios_ = false;
        corrigiendo_ = false;
        estado_ = Estado::Preguntando;
        r.campos = true;
        avanzar(r, en().traducir("totem", "Perdón, vamos de nuevo."));
        return r;
    }
    r.decir = en().traducir("totem", "No te entendí.") + " " + retomar();
    return r;
}

Conversacion::Respuesta Conversacion::despuesDelModelo(bool dijoAlgo, bool rechazos, bool& usarGuion) {
    Respuesta r;
    usarGuion = false;
    if (!enCurso() || atendida_) return r;
    // Anotó algo a confirmar (nuevo, o una corrección de algo que ya esperaba el «sí»).
    const bool anoto = hayPendientes() && propuestosTurno_ > anotadosTurno_;
    if (anoto || anotadosTurno_ > 0 || confirmoTurno_) {
        // Lo anotado sin «sí» lo confirma avanzar() todo junto; lo ya confirmado, sigue.
        if (estado_ == Estado::Confirmando && !confirmandoVarios_) estado_ = Estado::Preguntando;
        corrigiendo_ = false;
        intentos_ = 0;
        r.campos = true;
        avanzar(r, anoto ? std::string() : en().traducir("totem", "Perfecto."));
        return r;
    }
    r.pedir = t_.campos[actual_].clave;
    if (rechazos) {
        // Algo no validó: si el modelo ya lo repreguntó, nada más; si no, el reintento del guion.
        if (!dijoAlgo && estado_ == Estado::Preguntando) fallo(r);
        else if (!dijoAlgo) r.decir = retomar();
        return r;
    }
    if (dijoAlgo) {
        // Contestó una pregunta (o dijo que con eso no puede ayudar): se retoma lo pendiente.
        r.decir = retomar();
        return r;
    }
    r.pedir.clear();
    usarGuion = true;
    return r;
}

json Conversacion::estadoParaModelo() const {
    json anotados = json::object(), pend = json::array(), faltanAun = json::array();
    for (std::size_t i = 0; i < t_.campos.size(); ++i) {
        const Campo& c = t_.campos[i];
        if (!valores_[i].is_null()) anotados[c.clave] = paraMostrar(c, valores_[i], idioma_);
        if (pendiente_[i]) pend.push_back(c.clave);
        if (valores_[i].is_null() && !salteado_[i]) faltanAun.push_back(c.clave);
    }
    json j{{"anotados", anotados}, {"pendientes", pend}, {"faltan", faltanAun}, {"paso", paso()}};
    if (estado_ == Estado::Preguntando || estado_ == Estado::Confirmando) {
        j["campo"] = t_.campos[actual_].clave;
        j["pregunta"] = retomar();
    }
    j["confirmando"] = estado_ == Estado::Confirmando;
    if (estado_ == Estado::Confirmando && !confirmandoVarios_ && !propuesto_.is_null())
        j["seConfirma"] = {{t_.campos[actual_].clave, paraMostrar(t_.campos[actual_], propuesto_, idioma_)}};
    j["corrigiendo"] = corrigiendo_;
    return j;
}

} // namespace ba::registro
