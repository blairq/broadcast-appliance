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

std::string minuscula(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

/// Quita tildes y la eñe: quien busca desde un teclado apurado no las pone.
std::string sinTildes(const std::string& s) {
    static const std::pair<const char*, char> kMapa[] = {
        {"á", 'a'}, {"é", 'e'}, {"í", 'i'}, {"ó", 'o'}, {"ú", 'u'}, {"ü", 'u'}, {"ñ", 'n'}};
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

} // namespace

std::vector<std::string> Indice::terminos(const std::string& s) {
    std::vector<std::string> out;
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
    static const std::set<std::string> vacias{"el", "la", "lo", "los", "las", "un", "una",
                                              "unos", "unas", "de", "del", "al", "es", "son",
                                              "en", "y", "o", "que", "con", "por"};
    // «para» NO esta: en rioplatense es tambien el imperativo de parar —«para
    // la grabacion»— y sacarlo dejaba a `record_stop` sin esa frase.
    const auto cerrar = [&] {
        if (cur.size() >= 2 && vacias.count(cur) == 0) out.push_back(cur);
        cur.clear();
    };
    for (const char c : sinTildes(s)) {
        // El guion bajo SEPARA: "scene_switch" tiene que encontrarse buscando
        // "escena" o "switch" sueltos, que es como la nombra una persona.
        if (std::isalnum(static_cast<unsigned char>(c)) != 0) cur.push_back(c);
        else cerrar();
    }
    cerrar();
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
            for (const std::string& t : ts) peso[t] = std::max(peso[t], p);
        };
        sumar(terminos(d.nombre), kPesoNombre);
        for (const std::string& e : d.etiquetas) sumar(terminos(e), kPesoEtiqueta);
        sumar(terminos(d.titulo), kPesoTitulo);
        sumar(terminos(d.cuerpo), kPesoCuerpo);
        for (const auto& [t, p] : peso) invertido_[t].push_back(Aparicion{i, p});
    }
    armado_ = true;
}

std::vector<Resultado> Indice::buscar(const std::string& consulta, std::size_t tope) const {
    std::vector<Resultado> vacio;
    if (!armado_ || docs_.empty()) return vacio;
    const std::vector<std::string> q = terminos(consulta);
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
        // 1) Coincidencia exacta.
        if (const auto it = invertido_.find(t); it != invertido_.end()) {
            if (it->second.size() > dfInutil) continue; // está en todos lados: no dice nada
            const double w = idf(it->second.size());
            for (const Aparicion& a : it->second) {
                puntaje[a.doc] += a.peso * w;
                porque[a.doc].insert(t);
            }
            continue;
        }
        // 2) Por prefijo, solo si es lo bastante largo. El índice está
        //    ordenado, así que alcanza con caminar desde donde empieza.
        if (t.size() < kPrefijoMinimo) continue;
        for (auto it = invertido_.lower_bound(t);
             it != invertido_.end() && it->first.compare(0, t.size(), t) == 0; ++it) {
            if (it->second.size() > dfInutil) continue;
            const double w = idf(it->second.size()) * kDescuentoPrefijo;
            for (const Aparicion& a : it->second) {
                puntaje[a.doc] += a.peso * w;
                porque[a.doc].insert(it->first);
            }
        }
    }

    // Si la consulta ES el nombre de una herramienta, esa va primera y punto.
    // Sin esto `source_add` y `guest_source_add` empatan —las dos tienen los
    // dos terminos— y desempata el orden alfabetico, que no significa nada.
    // Quien escribe un nombre exacto no esta explorando: ya sabe cual quiere.
    std::size_t exacta = docs_.size();
    for (std::size_t i = 0; i < docs_.size(); ++i) {
        if (terminos(docs_[i].nombre) == q) {
            exacta = i;
            break;
        }
    }

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
