// ============================================================================
// src/app/mcp/McpIndice.cpp — ver McpIndice.hpp
// ============================================================================
#include "mcp/McpIndice.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <unordered_map>

namespace ba::app::mcp {
namespace {

/// Cuánto pesa un término según dónde apareció.
///
/// El NOMBRE manda: quien busca "scene_switch" quiere esa. Las ETIQUETAS van
/// casi igual porque son lo único que se escribió PARA que se encuentre la
/// herramienta; el título y el cuerpo se escribieron para explicarla, que no
/// es lo mismo. El cuerpo pesa poco a propósito: es largo, y sin eso una
/// herramienta muy documentada le gana a la que realmente hace lo que se pide.
constexpr double kPesoNombre = 6.0;
constexpr double kPesoEtiqueta = 5.0;
constexpr double kPesoTitulo = 3.0;
constexpr double kPesoCuerpo = 0.6;

/// Un prefijo cuenta menos que la palabra entera: "silenci" encuentra
/// "silenciar", pero si algo dice "silenciar" exacto, ese va primero.
constexpr double kDescuentoPrefijo = 0.45;
/// Un prefijo más corto que esto engancha con cualquier cosa.
constexpr std::size_t kPrefijoMinimo = 4;

/// Un término que aparece en más de esta fracción de las herramientas no
/// distingue nada: "de", "el", "una", "que". Se descartan enteros en vez de
/// dejarlos valer poco, porque igual contaminaban dos cosas: el bono por
/// cubrir varias palabras de la consulta (cubrir "de" no es cubrir nada) y la
/// explicación de por qué salió cada resultado, que es lo que deja corregir
/// la búsqueda en vez de volver a probar a ciegas.
///
/// El umbral sale de los datos y no de una lista escrita a mano: una palabra
/// que hoy distingue y mañana está en todos lados deja de contar sola.
constexpr double kFraccionInutil = 0.4;

/// Solo ASCII, a mano: `std::tolower` depende del locale, y dentro de la app (Qt
/// pone el del sistema, UTF-8) en macOS convertía el primer byte de «ú» (0xC3,
/// la «Ã» de Latin-1) en 0xE3: «música» dejaba de ser «musica» y no encontraba
/// nada. En las pruebas el locale era «C» y no se veía.
std::string minuscula(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

bool letraODigito(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }

/// Quita tildes y la eñe: quien busca desde un teclado apurado no las pone.
std::string sinTildes(const std::string& s) {
    static const std::pair<const char*, char> kMapa[] = {
        {"á", 'a'}, {"é", 'e'}, {"í", 'i'}, {"ó", 'o'}, {"ú", 'u'}, {"ü", 'u'}, {"ñ", 'n'},
        {"Á", 'a'}, {"É", 'e'}, {"Í", 'i'}, {"Ó", 'o'}, {"Ú", 'u'}, {"Ü", 'u'}, {"Ñ", 'n'}};
    std::string out = minuscula(s);
    for (const auto& [de, a] : kMapa) {
        const std::string f = de;
        std::size_t p = 0;
        while ((p = out.find(f, p)) != std::string::npos) {
            out.replace(p, f.size(), 1, a);
            ++p;
        }
    }
    return out;
}

/// Las vocales con tilde y la eñe en minúscula (UTF-8: 0xC3 + segundo byte).
/// Las mayúsculas acentuadas (0xC3 0x81...) pasan a minúsculas sumando 0x20.
bool tildeMinuscula(unsigned char b) {
    return b == 0xA1 || b == 0xA9 || b == 0xAD || b == 0xB3 || b == 0xBA || b == 0xBC || b == 0xB1;
}

/// Cuántas letras tiene (cada tilde o eñe son dos bytes y una letra).
std::size_t letras(const std::string& w) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < w.size(); ++i, ++n)
        if (static_cast<unsigned char>(w[i]) == 0xC3) ++i;
    return n;
}

/// EL IMPERATIVO DEL VOSEO, a su infinitivo: «pará» -> parar, «poné» -> poner,
/// «subí» -> subir; con los pronombres pegados, «bajale» -> bajar, «pasame» ->
/// pasar, «mostrámela» -> mostrar. Así «pará» deja de ser «para» (que está en
/// cientos de descripciones), «bajale» deja de ser «baja» (bajar de internet,
/// bajar la transmisión) y las etiquetas pueden decir el verbo una sola vez.
/// Con lo/la/los/las el verbo lleva la tilde justo antes del pronombre
/// («mostrá|mela»): si no, «modelo» sería «moder», «escala» «escar» y «zócalo»
/// «zocar». Con le/les/me/nos alcanza la vocal («baja|le»). «se» y «te» no
/// van: frase, siete.
std::string imperativo(const std::string& w) {
    // «está» no es un imperativo (es estar): «¿cómo está?» pide el estado
    if (w == "est\xC3\xA1" || w == "est\xC3\xA1s") return w;
    static const std::pair<const char*, char> kAguda[] = {{"\xC3\xA1", 'a'}, {"\xC3\xA9", 'e'}, {"\xC3\xAD", 'i'}};
    const auto termina = [](const std::string& x, const std::string& fin) {
        return x.size() >= fin.size() && x.compare(x.size() - fin.size(), fin.size(), fin) == 0;
    };
    for (const auto& [t, v] : kAguda)
        if (letras(w) >= 3 && termina(w, t)) return w.substr(0, w.size() - 2) + v + "r";
    static const char* kPegados[] = {"selos", "selas", "selo", "sela", "melos", "melas", "melo", "mela",
                                     "les",   "nos",   "los",  "las",  "le",    "me",    "lo",   "la"};
    for (const char* c : kPegados) {
        const std::string p = c;
        if (!termina(w, p) || letras(w) < p.size() + 4) continue;
        const std::string base = w.substr(0, w.size() - p.size());
        for (const auto& [t, v] : kAguda)
            if (termina(base, t)) return base.substr(0, base.size() - 2) + v + "r";
        const bool sinTildeVale = p == "le" || p == "les" || p == "me" || p == "nos";
        if (sinTildeVale && (base.back() == 'a' || base.back() == 'e' || base.back() == 'i')) return base + "r";
        return w;
    }
    return w;
}

} // namespace

namespace {

/// Los términos, y si cada uno salió de un imperativo («pará» -> parar).
std::vector<std::pair<std::string, bool>> terminosMarcados(const std::string& s) {
    std::vector<std::pair<std::string, bool>> out;
    std::string cur;
    // PALABRAS VACIAS, y no un minimo de tres letras. Las etiquetas se escriben
    // con frases de como habla una persona («que pantalla ES cual»), y ahi
    // entran palabras que no distinguen nada; indexadas, buscar «es» traia
    // media aplicacion, porque entre las etiquetas son raras y el IDF las
    // premia justamente por eso.
    //
    // Subir el minimo a tres letras parecia la solucion y es peor: se lleva
    // puestos «no» y «se», y en castellano «no se escucha» y «se escucha» son
    // cosas distintas —justo las que hay que distinguir cuando alguien se
    // queja—. Por eso van por lista, y «no» y «se» NO estan en ella.
    static const std::set<std::string> vacias{
        "el",  "la",  "lo",  "los", "las",   "un",     "una",   "unos", "unas", "de",  "del", "al", "es",
        "son", "en",  "y",   "o",   "que",   "con",    "por",
        // las que en la cabina no dicen QUE se pide (revision de la busqueda, 17/10/2026)
        "su",  "sus", "mi",  "mis", "tu",    "tus",    "ya",    "muy",  "pero", "porque", "porfa", "dale",
        "che", "eh",  "ahi", "aca", "aqui",  "alla",   "asi",   "mas"};
    // «para» NO esta: en rioplatense es tambien el imperativo de parar —«para
    // la grabacion»— y sacarlo dejaba a `record_stop` sin esa frase.
    // Cada palabra CON sus tildes primero (el imperativo las necesita: «pará» no
    // es «para»), después sin tildes. Una vacía lo es antes y después: «está»
    // no pasa a «estar», queda afuera como «esta».
    const auto cerrar = [&] {
        if (!cur.empty()) {
            const std::string antes = sinTildes(cur);
            const std::string t = sinTildes(imperativo(cur));
            if (vacias.count(antes) == 0 && t.size() >= 2 && vacias.count(t) == 0) out.emplace_back(t, t != antes);
        }
        cur.clear();
    };
    const std::string m = minuscula(s);
    for (std::size_t i = 0; i < m.size(); ++i) {
        const auto c = static_cast<unsigned char>(m[i]);
        // El guion bajo SEPARA: "scene_switch" tiene que encontrarse buscando
        // "escena" o "switch" sueltos, que es como la nombra una persona.
        // Lo que queda fuera de ASCII («¿», «»», «—») separa: `isalnum` también
        // depende del locale y pegaba esos bytes a las palabras. Las tildes y la
        // eñe (0xC3 + un byte) son parte de la palabra; las mayúsculas, a minúscula.
        if (letraODigito(static_cast<char>(c))) {
            cur.push_back(static_cast<char>(c));
        } else if (c == 0xC3 && i + 1 < m.size()) {
            const auto b = static_cast<unsigned char>(m[i + 1]);
            const unsigned char bm = (b >= 0x80 && b <= 0x9F) ? static_cast<unsigned char>(b + 0x20) : b;
            if (tildeMinuscula(bm)) {
                cur.push_back(static_cast<char>(0xC3));
                cur.push_back(static_cast<char>(bm));
                ++i;
            } else {
                cerrar();
            }
        } else {
            cerrar();
        }
    }
    cerrar();
    return out;
}

} // namespace

std::vector<std::string> Indice::terminos(const std::string& s) {
    std::vector<std::string> out;
    for (auto& par : terminosMarcados(s)) out.push_back(std::move(par.first));
    return out;
}

std::size_t Indice::agregar(Documento d) {
    armado_ = false;
    docs_.push_back(std::move(d));
    return docs_.size() - 1;
}

void Indice::armar() {
    invertido_.clear();
    for (std::size_t i = 0; i < docs_.size(); ++i) {
        const Documento& d = docs_[i];
        // Por documento se acumula el peso de cada término y recién después se
        // vuelca: si no, una palabra repetida en el cuerpo entra varias veces y
        // un texto largo gana por repetir, no por venir al caso.
        std::unordered_map<std::string, double> peso;
        const auto sumar = [&peso](const std::vector<std::string>& ts, double p) {
            for (const std::string& t : ts)
                peso[t] = std::max(peso[t], p);
        };
        sumar(terminos(d.nombre), kPesoNombre);
        for (const std::string& e : d.etiquetas)
            sumar(terminos(e), kPesoEtiqueta);
        sumar(terminos(d.titulo), kPesoTitulo);
        sumar(terminos(d.cuerpo), kPesoCuerpo);
        for (const auto& [t, p] : peso)
            invertido_[t].push_back(Aparicion{i, p});
    }
    armado_ = true;
}

std::vector<Resultado> Indice::buscar(const std::string& consulta, std::size_t tope) const {
    std::vector<Resultado> vacio;
    if (!armado_ || docs_.empty()) return vacio;
    // Un término que salió de un imperativo («quitá» -> quitar) pesa como una
    // coincidencia por prefijo: los nombres de las herramientas dicen el verbo en
    // infinitivo (audio_bus_quitar, atajo_quitar...) y, exacto, el verbo le ganaba
    // al objeto («quitá la placa» traía todas las *_quitar antes que aire_placa).
    const std::vector<std::pair<std::string, bool>> marcados = terminosMarcados(consulta);
    std::vector<std::string> q;
    std::set<std::string> derivados;
    for (const auto& [t, d] : marcados) {
        q.push_back(t);
        if (d) derivados.insert(t);
    }
    if (q.empty()) return vacio;

    const double n = static_cast<double>(docs_.size());
    // Un término que está en casi todo no distingue nada. `log(n/df)` lo deja
    // en ~0 solo, sin lista de palabras vacías escrita a mano: cuando mañana
    // haya cincuenta herramientas mas que digan "pantalla", esa palabra pierde
    // peso sola y lo que la acompaña pasa a mandar.
    const auto idf = [n](std::size_t df) {
        return df == 0 ? 0.0 : std::log(1.0 + n / static_cast<double>(df));
    };

    std::unordered_map<std::size_t, double> puntaje;
    std::unordered_map<std::size_t, std::set<std::string>> porque;
    std::set<std::string> yaVistos;

    const std::size_t dfInutil = static_cast<std::size_t>(n * kFraccionInutil);
    for (const std::string& t : q) {
        if (!yaVistos.insert(t).second) continue; // la misma palabra dos veces no vale doble
        const auto porPrefijo = [&](const std::string& raiz) {
            bool alguno = false;
            for (auto it = invertido_.lower_bound(raiz);
                 it != invertido_.end() && it->first.compare(0, raiz.size(), raiz) == 0; ++it) {
                if (it->second.size() > dfInutil) continue;
                alguno = true;
                const double w = idf(it->second.size()) * kDescuentoPrefijo;
                for (const Aparicion& a : it->second) {
                    puntaje[a.doc] += a.peso * w;
                    porque[a.doc].insert(it->first);
                }
            }
            return alguno;
        };
        // 0) Un imperativo («grabá» -> grabar): por prefijo de su raíz («graba…»
        //    trae grabar y grabacion), con el descuento del prefijo.
        if (derivados.count(t) != 0 && t.size() >= 5 && t.back() == 'r') {
            porPrefijo(t.substr(0, t.size() - 1));
            continue;
        }
        // 1) Coincidencia exacta.
        if (const auto it = invertido_.find(t); it != invertido_.end()) {
            if (it->second.size() > dfInutil) continue; // está en todos lados: no dice nada
            const double w = idf(it->second.size()) * (derivados.count(t) != 0 ? kDescuentoPrefijo : 1.0);
            for (const Aparicion& a : it->second) {
                puntaje[a.doc] += a.peso * w;
                porque[a.doc].insert(t);
            }
            continue;
        }
        // 2) Por prefijo, solo si es lo bastante largo. El índice está
        //    ordenado, así que alcanza con caminar desde donde empieza.
        if (t.size() < kPrefijoMinimo) continue;
        if (porPrefijo(t)) continue;
        // un infinitivo que no esta tal cual: su raiz («armar» -> «arma…»), que es
        // como estan escritas muchas etiquetas («arma el vinculo», «salta a»)
        if (t.size() >= 5 && t.back() == 'r' &&
            (t[t.size() - 2] == 'a' || t[t.size() - 2] == 'e' || t[t.size() - 2] == 'i') &&
            porPrefijo(t.substr(0, t.size() - 1)))
            continue;
        // 3) El verbo con el pronombre pegado, como se pide en cabina: «exportame»,
        //    «poneme», «sacale», «mostrame». Sin el pronombre es el verbo
        //    («exporta» -> «exportar»).
        static const char* kPegados[] = {"selos", "selas", "selo", "sela", "nos", "les", "los",
                                         "las",   "me",    "te",   "le",   "lo",  "la",  "se"};
        for (const char* p : kPegados) {
            const std::size_t n = std::char_traits<char>::length(p);
            if (t.size() >= n + kPrefijoMinimo && t.compare(t.size() - n, n, p) == 0 &&
                porPrefijo(t.substr(0, t.size() - n)))
                break;
        }
    }

    // Si la consulta ES el nombre de una herramienta, esa va primera y punto.
    // Sin esto `crear_fuente` y `guest_source_add` empatan —las dos tienen los
    // dos terminos— y desempata el orden alfabetico, que no significa nada.
    // Quien escribe un nombre exacto no esta explorando: ya sabe cual quiere.
    std::size_t exacta = docs_.size();
    for (std::size_t i = 0; i < docs_.size(); ++i) {
        if (terminos(docs_[i].nombre) == q) {
            exacta = i;
            break;
        }
    }
    // Lo mismo con el TITULO, un poco menos: «grabar» es el título de
    // `record_start`, y empataba con todas las que dicen «grabar» en algún lado
    // (desempataba el orden alfabético y quedaba quinta).
    std::set<std::size_t> tituloExacto;
    for (std::size_t i = 0; i < docs_.size(); ++i)
        if (!docs_[i].titulo.empty() && terminos(docs_[i].titulo) == q) tituloExacto.insert(i);

    std::vector<Resultado> out;
    out.reserve(puntaje.size());
    for (const auto& [doc, p] : puntaje) {
        Resultado r;
        r.doc = doc;
        r.puntaje = p;
        // Cubrir MAS palabras de la consulta vale mas que enganchar una sola
        // muy fuerte: quien escribe "silenciar a un invitado" quiere las dos
        // cosas, no la que mas grita "invitado".
        const auto it = porque.find(doc);
        if (it != porque.end()) {
            r.porque.assign(it->second.begin(), it->second.end());
            r.puntaje *= 1.0 + 0.35 * static_cast<double>(it->second.size() - 1);
        }
        if (doc == exacta) r.puntaje *= 100.0;
        else if (tituloExacto.count(doc) != 0) r.puntaje *= 3.0;
        out.push_back(std::move(r));
    }
    std::sort(out.begin(), out.end(), [this](const Resultado& a, const Resultado& b) {
        if (a.puntaje != b.puntaje) return a.puntaje > b.puntaje;
        return docs_[a.doc].id < docs_[b.doc].id; // desempate estable
    });
    if (out.size() > tope) out.resize(tope);
    return out;
}

} // namespace ba::app::mcp
