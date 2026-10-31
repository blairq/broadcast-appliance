// ============================================================================
// src/app/mcp/McpIndice.cpp — ver McpIndice.hpp
// ============================================================================
#include "mcp/McpIndice.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "i18n/I18n.hpp"

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

bool termina(const std::string& x, const std::string& fin) {
    return x.size() >= fin.size() && x.compare(x.size() - fin.size(), fin.size(), fin) == 0;
}

/// Cuántas letras tiene (una letra fuera de ASCII son varios bytes en UTF-8).
std::size_t letras(const std::string& w) {
    std::size_t n = 0;
    for (const char c : w)
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
    return n;
}

/// Cuántos bytes ocupa la letra UTF-8 que empieza en `i` (0 si no es una secuencia
/// válida: ese byte solo separa).
std::size_t largoUtf8(const std::string& s, std::size_t i) {
    const auto b = static_cast<unsigned char>(s[i]);
    const std::size_t n = (b >= 0xF0 && b <= 0xF4) ? 4 : (b >= 0xE0 && b <= 0xEF) ? 3 : (b >= 0xC2 && b <= 0xDF) ? 2 : 0;
    if (n == 0 || i + n > s.size()) return 0;
    for (std::size_t k = 1; k < n; ++k)
        if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return 0;
    return n;
}

/// Sin perfil del idioma: una letra fuera de ASCII es parte de la palabra salvo que
/// sea puntuación (la de Latin-1, ×, ÷, la general de Unicode, la del CJK y las
/// formas anchas). Saber eso es de Unicode, no de un idioma.
bool letraUnicode(const std::string& seq) {
    const auto b0 = static_cast<unsigned char>(seq[0]);
    std::uint32_t cp = seq.size() == 2 ? (b0 & 0x1Fu) : seq.size() == 3 ? (b0 & 0x0Fu) : (b0 & 0x07u);
    for (std::size_t k = 1; k < seq.size(); ++k)
        cp = (cp << 6) | (static_cast<unsigned char>(seq[k]) & 0x3Fu);
    if (cp <= 0xBF || cp == 0xD7 || cp == 0xF7) return false;
    if (cp >= 0x2000 && cp <= 0x2BFF) return false;
    if (cp >= 0x3000 && cp <= 0x303F) return false;
    if (cp >= 0xFE30 && cp <= 0xFE4F) return false;
    if (cp >= 0xFF00 && cp <= 0xFF0F) return false;
    return true;
}

/// LO QUE CAMBIA CON EL IDIOMA, en datos: el campo «buscador» del perfil
/// (assets/idiomas/<código>.json; ver docs/desarrollo/I18N.md, «Perfil del idioma:
/// buscador»). En español: las tildes y la eñe, las palabras vacías, el imperativo
/// del voseo y los pronombres pegados. Un idioma sin ese campo (ni en sus
/// respaldos) busca sin nada de eso: minúsculas y palabras enteras o por prefijo.
struct Reglas {
    /// Las letras fuera de ASCII que son parte de una palabra, en minúscula. Vacío:
    /// todas las de Unicode (letraUnicode).
    std::map<std::string, std::string> letras;
    /// Lo que se le saca a cada letra para comparar («á» -> «a»).
    std::vector<std::pair<std::string, std::string>> sinTildes;
    std::set<std::string> vacias;
    // El imperativo, a su infinitivo (ver imperativo()).
    std::set<std::string> excepciones;
    std::vector<std::pair<std::string, std::string>> agudas;
    std::string sufijo;
    std::size_t letrasMinimas = 3;
    std::vector<std::string> pronombres;
    std::set<std::string> pronombresSinTilde;
    std::set<char> vocalesSinTilde;
    std::size_t letrasDelVerbo = 4;
    /// Las terminaciones del infinitivo: su raíz es la palabra sin la última letra.
    std::vector<std::string> infinitivo;
    /// Los pronombres que se pegan al verbo en una consulta («exportame»).
    std::vector<std::string> pegados;

    bool esInfinitivo(const std::string& t) const {
        for (const auto& f : infinitivo)
            if (termina(t, f)) return true;
        return false;
    }
};

std::vector<std::string> lista(const nlohmann::json& j, const char* clave) {
    std::vector<std::string> out;
    if (j.contains(clave) && j[clave].is_array())
        for (const auto& v : j[clave])
            if (v.is_string()) out.push_back(v.get<std::string>());
    return out;
}

std::vector<std::pair<std::string, std::string>> pares(const nlohmann::json& j, const char* clave) {
    std::vector<std::pair<std::string, std::string>> out;
    if (j.contains(clave) && j[clave].is_object())
        for (const auto& [k, v] : j[clave].items())
            if (v.is_string()) out.emplace_back(k, v.get<std::string>());
    return out;
}

Reglas reglasDe(const nlohmann::json& b) {
    Reglas r;
    if (!b.is_object()) return r;
    for (auto& [k, v] : pares(b, "letras")) r.letras[k] = v;
    r.sinTildes = pares(b, "sinTildes");
    for (auto& v : lista(b, "vacias")) r.vacias.insert(v);
    if (b.contains("imperativo") && b["imperativo"].is_object()) {
        const nlohmann::json& im = b["imperativo"];
        for (auto& v : lista(im, "excepciones")) r.excepciones.insert(v);
        r.agudas = pares(im, "agudas");
        r.sufijo = im.value("sufijo", std::string());
        r.letrasMinimas = im.value("letrasMinimas", std::size_t{3});
        r.pronombres = lista(im, "pronombres");
        for (auto& v : lista(im, "pronombresSinTilde")) r.pronombresSinTilde.insert(v);
        for (auto& v : lista(im, "vocalesSinTilde"))
            if (v.size() == 1) r.vocalesSinTilde.insert(v[0]);
        r.letrasDelVerbo = im.value("letrasDelVerbo", std::size_t{4});
    }
    r.infinitivo = lista(b, "infinitivo");
    r.pegados = lista(b, "pegados");
    return r;
}

/// El campo «buscador» de un perfil o, si no lo tiene, el del primero de sus respaldos.
nlohmann::json buscadorDe(const ba::i18n::Perfil& p) {
    nlohmann::json b = p.datos.is_object() ? p.datos.value("buscador", nlohmann::json()) : nlohmann::json();
    for (std::size_t i = 0; !b.is_object() && i < p.respaldo.size(); ++i) {
        const ba::i18n::Perfil r = ba::i18n::perfil(p.respaldo[i]);
        if (r.datos.is_object()) b = r.datos.value("buscador", nlohmann::json());
    }
    return b;
}

/// DOS LENGUAS EN EL MISMO INDICE. Con un operador en otro idioma, el índice tiene
/// las dos: la descripción y unas etiquetas en el original (castellano: es lo que lee
/// el modelo, ver traducirParaVer) y el título y las otras etiquetas traducidos. Con
/// las reglas de un solo idioma, el ruso separaba «cámara» en «c» y «mara» (la «á» no
/// es una letra suya) y el inglés indexaba «de» y «la». Por eso se le SUMAN las letras,
/// las tildes y las palabras vacías del original. Lo que cambia la palabra (el
/// imperativo, el infinitivo, los pronombres pegados) queda solo del idioma del
/// operador: el voseo aplicado al inglés volvía «mobile» un «mobir».
void sumarDelOriginal(Reglas& r, const Reglas& o) {
    if (!r.letras.empty())
        for (const auto& [k, v] : o.letras) r.letras.emplace(k, v);
    for (const auto& par : o.sinTildes)
        if (std::none_of(r.sinTildes.begin(), r.sinTildes.end(), [&](const auto& x) { return x.first == par.first; }))
            r.sinTildes.push_back(par);
    r.vacias.insert(o.vacias.begin(), o.vacias.end());
}

/// Las del idioma del operador (o las de sus respaldos), leídas una vez por idioma.
std::shared_ptr<const Reglas> reglas() {
    static std::mutex mu;
    static std::string de;
    static std::shared_ptr<const Reglas> hay;
    const std::string codigo = ba::i18n::actual().codigo;
    std::lock_guard<std::mutex> l(mu);
    if (hay && de == codigo) return hay;
    const ba::i18n::Perfil p = ba::i18n::perfil(codigo);
    Reglas r = reglasDe(buscadorDe(p));
    if (!p.fuente && !p.pseudo)
        for (const ba::i18n::Perfil& o : ba::i18n::idiomas())
            if (o.fuente) sumarDelOriginal(r, reglasDe(buscadorDe(o)));
    hay = std::make_shared<const Reglas>(std::move(r));
    de = p.codigo;
    return hay;
}

/// Quita tildes y la eñe: quien busca desde un teclado apurado no las pone.
std::string sinTildes(const Reglas& r, const std::string& s) {
    std::string out = minuscula(s);
    for (const auto& [de, a] : r.sinTildes) {
        std::size_t p = 0;
        while ((p = out.find(de, p)) != std::string::npos) {
            out.replace(p, de.size(), a);
            p += a.size();
        }
    }
    return out;
}

/// EL IMPERATIVO DEL VOSEO, a su infinitivo: «pará» -> parar, «poné» -> poner,
/// «subí» -> subir; con los pronombres pegados, «bajale» -> bajar, «pasame» ->
/// pasar, «mostrámela» -> mostrar. Así «pará» deja de ser «para» (que está en
/// cientos de descripciones), «bajale» deja de ser «baja» (bajar de internet,
/// bajar la transmisión) y las etiquetas pueden decir el verbo una sola vez.
/// Con lo/la/los/las el verbo lleva la tilde justo antes del pronombre
/// («mostrá|mela»): si no, «modelo» sería «moder», «escala» «escar» y «zócalo»
/// «zocar». Con le/les/me/nos alcanza la vocal («baja|le»). «se» y «te» no
/// van: frase, siete. «está» no es un imperativo (es estar): «¿cómo está?» pide
/// el estado. Las tildes, los pronombres y las excepciones salen del perfil
/// (Reglas): acá está solo el mecanismo.
std::string imperativo(const Reglas& r, const std::string& w) {
    if (r.agudas.empty() || r.excepciones.count(w) != 0) return w;
    for (const auto& [t, v] : r.agudas)
        if (letras(w) >= r.letrasMinimas && termina(w, t)) return w.substr(0, w.size() - t.size()) + v + r.sufijo;
    for (const std::string& p : r.pronombres) {
        if (!termina(w, p) || letras(w) < letras(p) + r.letrasDelVerbo) continue;
        const std::string base = w.substr(0, w.size() - p.size());
        for (const auto& [t, v] : r.agudas)
            if (termina(base, t)) return base.substr(0, base.size() - t.size()) + v + r.sufijo;
        if (r.pronombresSinTilde.count(p) != 0 && r.vocalesSinTilde.count(base.back()) != 0) return base + r.sufijo;
        return w;
    }
    return w;
}

} // namespace

namespace {

/// Los términos, y si cada uno salió de un imperativo («pará» -> parar).
std::vector<std::pair<std::string, bool>> terminosMarcados(const Reglas& r, const std::string& s) {
    std::vector<std::pair<std::string, bool>> out;
    std::string cur;
    // PALABRAS VACIAS (las del perfil), y no un minimo de tres letras. Las
    // etiquetas se escriben con frases de como habla una persona («que pantalla
    // ES cual»), y ahi entran palabras que no distinguen nada; indexadas, buscar
    // «es» traia media aplicacion, porque entre las etiquetas son raras y el IDF
    // las premia justamente por eso.
    //
    // Subir el minimo a tres letras parecia la solucion y es peor: se lleva
    // puestos «no» y «se», y en castellano «no se escucha» y «se escucha» son
    // cosas distintas —justo las que hay que distinguir cuando alguien se
    // queja—. Por eso van por lista, y «no» y «se» NO estan en ella. Tampoco
    // «para»: en rioplatense es tambien el imperativo de parar —«para la
    // grabacion»— y sacarlo dejaba a `record_stop` sin esa frase.
    //
    // Cada palabra CON sus tildes primero (el imperativo las necesita: «pará» no
    // es «para»), después sin tildes. Una vacía lo es antes y después: «está»
    // no pasa a «estar», queda afuera como «esta».
    const auto cerrar = [&] {
        if (!cur.empty()) {
            const std::string antes = sinTildes(r, cur);
            const std::string t = sinTildes(r, imperativo(r, cur));
            if (r.vacias.count(antes) == 0 && t.size() >= 2 && r.vacias.count(t) == 0)
                out.emplace_back(t, t != antes);
        }
        cur.clear();
    };
    const std::string m = minuscula(s);
    for (std::size_t i = 0; i < m.size(); ++i) {
        const auto c = static_cast<unsigned char>(m[i]);
        // El guion bajo SEPARA: "scene_switch" tiene que encontrarse buscando
        // "escena" o "switch" sueltos, que es como la nombra una persona.
        // Fuera de ASCII, una letra del idioma (las del perfil, que dicen su
        // minúscula) es parte de la palabra y lo demás («¿», «»», «—») separa:
        // `isalnum` depende del locale y pegaba esos bytes a las palabras.
        if (letraODigito(static_cast<char>(c))) {
            cur.push_back(static_cast<char>(c));
            continue;
        }
        const std::size_t n = c >= 0x80 ? largoUtf8(m, i) : 0;
        if (n == 0) {
            cerrar();
            continue;
        }
        const std::string seq = m.substr(i, n);
        i += n - 1;
        if (r.letras.empty()) {
            if (letraUnicode(seq)) cur += seq;
            else cerrar();
        } else if (const auto it = r.letras.find(seq); it != r.letras.end()) {
            cur += it->second;
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
    for (auto& par : terminosMarcados(*reglas(), s)) out.push_back(std::move(par.first));
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
    const std::shared_ptr<const Reglas> rp = reglas();
    const Reglas& r = *rp;
    const std::vector<std::pair<std::string, bool>> marcados = terminosMarcados(r, consulta);
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
        if (derivados.count(t) != 0 && t.size() >= 5 && r.esInfinitivo(t)) {
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
        // como estan escritas muchas etiquetas («arma el vinculo», «salta a»). Las
        // terminaciones son del perfil del idioma.
        if (t.size() >= 5 && r.esInfinitivo(t) && porPrefijo(t.substr(0, t.size() - 1))) continue;
        // 3) El verbo con el pronombre pegado, como se pide en cabina: «exportame»,
        //    «poneme», «sacale», «mostrame». Sin el pronombre es el verbo
        //    («exporta» -> «exportar»). Los pronombres son del perfil del idioma.
        for (const std::string& p : r.pegados) {
            const std::size_t n = p.size();
            if (t.size() >= n + kPrefijoMinimo && termina(t, p) &&
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
