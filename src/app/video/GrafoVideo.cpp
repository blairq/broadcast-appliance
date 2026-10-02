// src/app/video/GrafoVideo.cpp — ver GrafoVideo.hpp

#include "video/GrafoVideo.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <set>
#include <tuple>

namespace ba::app::video {

using json = nlohmann::json;

namespace {

/// Grilla del editor: columnas de 220 (tarjetas de 180 y lugar para el cable), paso 20.
constexpr int kColumna = 220;

bool esFiltro(const std::string& t) { return t == "color" || t == "croma" || t == "recorte" || t == "obs"; }

json fabrica(const std::string& tipo) {
    if (tipo == "color") return {{"brillo", 0.0}, {"contraste", 1.0}, {"saturacion", 1.0}, {"gamma", 1.0}};
    if (tipo == "croma") return {{"clave", 0x00B140}, {"similitud", 0.4}, {"suavidad", 0.08}, {"derrame", 0.1}};
    if (tipo == "recorte")
        return {{"izquierda", 0.0}, {"arriba", 0.0}, {"derecha", 0.0}, {"abajo", 0.0}, {"espejoH", false}, {"espejoV", false}};
    if (tipo == "salida") return {{"nombre", ""}};
    if (tipo == "obs") return {{"filtro", ""}, {"ajustes", json::object()}};
    // Por puerto: {"in_0": {x, y, ancho, alto, opacidad, orden}} (lo ejecuta el estudio).
    if (tipo == "compositor") return {{"ancho", 1920}, {"alto", 1080}, {"capas", json::object()}};
    if (tipo == "entrada" || tipo == "escena") return {{"fuente", ""}};
    return json::object();
}

std::string titulo(const std::string& tipo) {
    if (tipo == "entrada") return "Entrada";
    if (tipo == "escena") return "A la escena";
    if (tipo == "color") return "Color";
    if (tipo == "croma") return "Croma";
    if (tipo == "recorte") return "Recorte y espejo";
    if (tipo == "salida") return "Salida";
    if (tipo == "obs") return "Filtro";
    if (tipo == "compositor") return "Compositor";
    return tipo;
}

/// Un id legible y estable a partir del nombre de la entrada (sin acentos ni espacios).
std::string slug(const std::string& original) {
    // Las letras con tilde, a su letra (UTF-8: «Cámara» -> «camara»).
    static const std::pair<const char*, char> kTildes[] = {
        {"á", 'a'}, {"é", 'e'}, {"í", 'i'}, {"ó", 'o'}, {"ú", 'u'}, {"ü", 'u'}, {"ñ", 'n'},
        {"Á", 'a'}, {"É", 'e'}, {"Í", 'i'}, {"Ó", 'o'}, {"Ú", 'u'}, {"Ü", 'u'}, {"Ñ", 'n'}};
    std::string s = original;
    for (const auto& [con, sin] : kTildes)
        for (size_t p = s.find(con); p != std::string::npos; p = s.find(con, p)) s.replace(p, std::strlen(con), 1, sin);
    std::string out;
    bool guion = false;
    for (unsigned char c : s) {
        // Los bytes de un acento (UTF-8) se saltean ANTES de preguntar nada:
        // con el locale de la app, isalnum/tolower los toman por Latin-1 y
        // los cambian, y el id queda con UTF-8 roto (el json se cae al guardar).
        if (c >= 0x80) continue;
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z')) {
            out += static_cast<char>(c);
            guion = false;
        } else if (c >= 'A' && c <= 'Z') {
            out += static_cast<char>(c - 'A' + 'a');
            guion = false;
        } else if (!guion && !out.empty()) {
            out += '-';
            guion = true;
        }
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out.empty() ? "entrada" : out;
}

int indicePedido(const std::string& puerto) {
    if (puerto.rfind("in_", 0) != 0 || puerto.size() < 4) return -1;
    for (size_t i = 3; i < puerto.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(puerto[i]))) return -1;
    return std::stoi(puerto.substr(3));
}

} // namespace

json GrafoVideo::catalogo() {
    json out = json::array();
    const std::tuple<const char*, const char*, const char*> tipos[] = {
        {"color", "Brillo, contraste, saturación y gamma.", "entrada"},
        {"croma", "Saca un fondo de color (verde de fábrica): queda transparente.", "entrada"},
        {"recorte", "Recorta los bordes (quedan transparentes) y da vuelta la imagen.", "entrada"},
        {"salida", "Publica la imagen de ese punto como una fuente con nombre, para cualquier escena.", "entrada"},
        {"compositor", "Junta varias entradas en una imagen (mosaico, imagen en imagen). Es del estudio.", "estudio"},
    };
    for (const auto& [t, d, a] : tipos)
        out.push_back({{"tipo", t}, {"titulo", titulo(t)}, {"descripcion", d}, {"params", fabrica(t)}, {"ambito", a}});
    return out;
}

bool GrafoVideo::tipoValido(const std::string& t) { return esFiltro(t) || t == "salida" || t == "compositor"; }

std::string GrafoVideo::idEntrada(const std::string& fuente) { return slug(fuente) + ".entrada"; }
std::string GrafoVideo::idEscena(const std::string& fuente) { return slug(fuente) + ".escena"; }

const GrafoVideo::Nodo* GrafoVideo::nodo(const std::string& id) const {
    for (const auto& n : nodos_)
        if (n.id == id) return &n;
    return nullptr;
}

std::vector<GrafoVideo::Cable> GrafoVideo::entrantes(const std::string& id) const {
    std::vector<Cable> out;
    for (const auto& c : cables_)
        if (c.hacia == id) out.push_back(c);
    return out;
}

std::vector<GrafoVideo::Cable> GrafoVideo::salientes(const std::string& id) const {
    std::vector<Cable> out;
    for (const auto& c : cables_)
        if (c.desde == id) out.push_back(c);
    return out;
}

std::vector<GrafoVideo::Puerto> GrafoVideo::puertosEntrada(const Nodo& n) const {
    if (n.tipo == "entrada") return {};
    if (n.tipo != "compositor") return {{"in", "video", false}};
    // A pedido: los conectados, en orden, y uno libre al final.
    std::vector<int> usados;
    for (const auto& c : entrantes(n.id))
        if (indicePedido(c.entrada) >= 0) usados.push_back(indicePedido(c.entrada));
    std::sort(usados.begin(), usados.end());
    std::vector<Puerto> out;
    for (int i : usados) out.push_back({"in_" + std::to_string(i), "video", false});
    out.push_back({"in_" + std::to_string(usados.empty() ? 0 : usados.back() + 1), "video", true});
    return out;
}

std::vector<GrafoVideo::Puerto> GrafoVideo::puertosSalida(const Nodo& n) const {
    if (n.tipo == "escena" || n.tipo == "salida") return {};
    return {{"out", "video", false}};
}

std::string GrafoVideo::idNuevo(const std::string& grupo, const std::string& tipo) const {
    const std::string base = (grupo.empty() ? std::string("estudio") : slug(grupo)) + "." + tipo;
    if (nodo(base) == nullptr) return base;
    for (int i = 2;; ++i) {
        const std::string id = base + "-" + std::to_string(i);
        if (nodo(id) == nullptr) return id;
    }
}

bool GrafoVideo::llega(const std::string& desde, const std::string& hasta) const {
    std::vector<std::string> pila{desde};
    std::set<std::string> visto;
    while (!pila.empty()) {
        const std::string n = pila.back();
        pila.pop_back();
        if (n == hasta) return true;
        if (!visto.insert(n).second) continue;
        for (const auto& c : cables_)
            if (c.desde == n) pila.push_back(c.hacia);
    }
    return false;
}

bool GrafoVideo::asegurarEntrada(const std::string& fuente) {
    const std::string ie = idEntrada(fuente), is = idEscena(fuente);
    bool creo = false;
    if (nodo(ie) == nullptr) {
        // Debajo de las que ya hay: cada entrada en su franja.
        int y = 0;
        for (const auto& n : nodos_)
            if (n.tipo == "entrada") y = std::max(y, n.y + 200);
        nodos_.push_back({ie, "entrada", fuente, fuente, 0, y, json{{"fuente", fuente}}});
        creo = true;
    }
    if (nodo(is) == nullptr) {
        const int y = nodo(ie)->y;
        nodos_.push_back({is, "escena", "A la escena", fuente, kColumna * 2, y, json{{"fuente", fuente}}});
        cables_.push_back({ie, "out", is, "in"});
        creo = true;
    }
    return creo;
}

std::vector<std::string> GrafoVideo::entradas() const {
    std::vector<std::string> out;
    for (const auto& n : nodos_)
        if (n.tipo == "entrada") out.push_back(n.grupo);
    return out;
}

std::string GrafoVideo::agregar(const std::string& tipo, const std::string& grupo, const std::string& despues,
                                const json& params, std::string* error) {
    if (!tipoValido(tipo)) {
        if (error) *error = "no existe la tarjeta «" + tipo + "»";
        return {};
    }
    if (tipo == "obs" && (!params.is_object() || params.value("filtro", std::string()).empty())) {
        if (error) *error = "falta qué filtro de OBS (`filtro`)";
        return {};
    }
    Nodo n;
    n.tipo = tipo;
    n.etiqueta = titulo(tipo);
    n.params = fabrica(tipo);
    if (params.is_object())
        for (auto it = params.begin(); it != params.end(); ++it) n.params[it.key()] = it.value();
    // Un compositor es del estudio: suelto, a la derecha de todo.
    if (tipo == "compositor") {
        n.grupo = "";
        n.id = idNuevo("", tipo);
        int x = 0;
        for (const auto& m : nodos_) x = std::max(x, m.x + kColumna);
        n.x = x;
        n.y = 0;
        nodos_.push_back(std::move(n));
        return nodos_.back().id;
    }
    if (grupo.empty() || nodo(idEntrada(grupo)) == nullptr) {
        if (error) *error = "«" + grupo + "» no es una entrada del grafo";
        return {};
    }
    std::string desde = despues;
    if (desde.empty()) {
        desde = idEntrada(grupo);
        for (const auto& c : entrantes(idEscena(grupo))) desde = c.desde;
    }
    const Nodo* d = nodo(desde);
    if (d == nullptr || puertosSalida(*d).empty()) {
        if (error) *error = "no hay una tarjeta «" + desde + "» de la que colgarla";
        return {};
    }
    n.grupo = grupo;
    n.id = idNuevo(grupo, tipo);
    n.x = d->x + kColumna;
    n.y = d->y + (tipo == "salida" ? 140 : 0);
    if (tipo != "salida") {
        // Intercalado: lo que salía de `desde` sale ahora de la nueva, y lo
        // que venía después en la misma fila se corre una columna.
        for (auto& c : cables_)
            if (c.desde == desde) c.desde = n.id;
        for (auto& m : nodos_)
            if (m.grupo == grupo && m.x >= n.x && m.y == n.y && m.id != desde) m.x += kColumna;
    }
    cables_.push_back({desde, "out", n.id, "in"});
    nodos_.push_back(std::move(n));
    return nodos_.back().id;
}

bool GrafoVideo::quitar(const std::string& id, std::string* error) {
    const Nodo* n = nodo(id);
    if (n == nullptr) {
        if (error) *error = "no existe la tarjeta «" + id + "»";
        return false;
    }
    if (n->tipo == "entrada" || n->tipo == "escena") {
        if (error) *error = "la entrada y la escena no se quitan";
        return false;
    }
    // Un filtro (una entrada, una salida) se puentea: lo de antes sigue a lo de después.
    const std::vector<Cable> antes = entrantes(id), despues = salientes(id);
    const bool puentear = esFiltro(n->tipo) && antes.size() == 1;
    cables_.erase(std::remove_if(cables_.begin(), cables_.end(),
                                 [&](const Cable& c) { return c.desde == id || c.hacia == id; }),
                  cables_.end());
    if (puentear)
        for (const auto& c : despues) cables_.push_back({antes[0].desde, antes[0].salida, c.hacia, c.entrada});
    nodos_.erase(std::remove_if(nodos_.begin(), nodos_.end(), [&](const Nodo& x) { return x.id == id; }), nodos_.end());
    return true;
}

std::string GrafoVideo::conectar(const std::string& desde, const std::string& salida, const std::string& hacia,
                                 const std::string& entrada, std::string* error) {
    const Nodo* d = nodo(desde);
    const Nodo* h = nodo(hacia);
    if (d == nullptr || h == nullptr) {
        if (error) *error = "no existe «" + (d == nullptr ? desde : hacia) + "»";
        return {};
    }
    const auto ps = puertosSalida(*d);
    const std::string sal = salida.empty() ? (ps.empty() ? std::string() : ps[0].id) : salida;
    if (std::none_of(ps.begin(), ps.end(), [&](const Puerto& p) { return p.id == sal; })) {
        if (error) *error = "«" + d->etiqueta + "» no tiene salida" + (salida.empty() ? "" : " «" + salida + "»");
        return {};
    }
    const auto pe = puertosEntrada(*h);
    std::string ent = entrada;
    if (ent.empty()) ent = pe.empty() ? std::string() : (h->tipo == "compositor" ? pe.back().id : pe[0].id);
    const bool valida = std::any_of(pe.begin(), pe.end(), [&](const Puerto& p) { return p.id == ent; }) ||
                        (h->tipo == "compositor" && indicePedido(ent) >= 0);
    if (!valida) {
        if (error) *error = "«" + h->etiqueta + "» no tiene entrada" + (entrada.empty() ? "" : " «" + entrada + "»");
        return {};
    }
    if (desde == hacia || llega(hacia, desde)) {
        if (error) *error = "ese cable cerraría un círculo";
        return {};
    }
    // Una sola cosa por puerto de entrada: el cable nuevo reemplaza al que llegaba.
    cables_.erase(std::remove_if(cables_.begin(), cables_.end(),
                                 [&](const Cable& c) { return c.hacia == hacia && c.entrada == ent; }),
                  cables_.end());
    cables_.push_back({desde, sal, hacia, ent});
    return ent;
}

bool GrafoVideo::desconectar(const std::string& desde, const std::string& salida, const std::string& hacia,
                             const std::string& entrada) {
    const auto antes = cables_.size();
    cables_.erase(std::remove_if(cables_.begin(), cables_.end(),
                                 [&](const Cable& c) {
                                     return c.desde == desde && c.hacia == hacia &&
                                            (salida.empty() || c.salida == salida) &&
                                            (entrada.empty() || c.entrada == entrada);
                                 }),
                  cables_.end());
    return cables_.size() != antes;
}

bool GrafoVideo::ajustar(const std::string& id, const json& params, std::string* error) {
    for (auto& n : nodos_) {
        if (n.id != id) continue;
        if (!params.is_object()) return true;
        for (auto it = params.begin(); it != params.end(); ++it) {
            if (it.key() == "etiqueta" && it.value().is_string()) {
                n.etiqueta = it.value().get<std::string>();
                continue;
            }
            if (!n.params.contains(it.key())) {
                if (error) *error = "«" + n.etiqueta + "» no tiene «" + it.key() + "»";
                return false;
            }
            json& viejo = n.params[it.key()];
            // Los ajustes de un filtro de OBS y las capas de un compositor se
            // mezclan clave por clave (no se pisan enteros).
            if (viejo.is_object() && it.value().is_object()) {
                for (auto k = it.value().begin(); k != it.value().end(); ++k) {
                    if (viejo[k.key()].is_object() && k.value().is_object())
                        for (auto q = k.value().begin(); q != k.value().end(); ++q) viejo[k.key()][q.key()] = q.value();
                    else
                        viejo[k.key()] = k.value();
                }
                continue;
            }
            if ((viejo.is_number() && !it.value().is_number()) || (viejo.is_boolean() && !it.value().is_boolean()) ||
                (viejo.is_string() && !it.value().is_string())) {
                if (error) *error = "«" + it.key() + "» no es de ese tipo";
                return false;
            }
            viejo = it.value();
        }
        return true;
    }
    if (error) *error = "no existe la tarjeta «" + id + "»";
    return false;
}

bool GrafoVideo::mover(const std::string& id, int x, int y) {
    for (auto& n : nodos_)
        if (n.id == id) {
            n.x = x;
            n.y = y;
            return true;
        }
    return false;
}

void GrafoVideo::limpiar(const std::string& grupo) {
    std::vector<std::string> sacar;
    for (const auto& n : nodos_)
        if (n.grupo == grupo && n.tipo != "entrada" && n.tipo != "escena") sacar.push_back(n.id);
    const std::string ie = idEntrada(grupo), is = idEscena(grupo);
    // También lo que entraba a su escena desde afuera.
    cables_.erase(std::remove_if(cables_.begin(), cables_.end(),
                                 [&](const Cable& c) {
                                     return c.hacia == is ||
                                            std::find(sacar.begin(), sacar.end(), c.desde) != sacar.end() ||
                                            std::find(sacar.begin(), sacar.end(), c.hacia) != sacar.end();
                                 }),
                  cables_.end());
    nodos_.erase(std::remove_if(nodos_.begin(), nodos_.end(),
                                [&](const Nodo& x) { return std::find(sacar.begin(), sacar.end(), x.id) != sacar.end(); }),
                 nodos_.end());
    if (nodo(ie) != nullptr && nodo(is) != nullptr) cables_.push_back({ie, "out", is, "in"});
}

bool GrafoVideo::trivial(const std::string& grupo) const {
    for (const auto& n : nodos_)
        if (n.grupo == grupo && n.tipo != "entrada" && n.tipo != "escena") return false;
    const std::string ie = idEntrada(grupo), is = idEscena(grupo);
    // Ni cables de su entrada hacia afuera (un compositor) ni de afuera a su escena.
    for (const auto& c : cables_)
        if ((c.desde == ie || c.hacia == is) && !(c.desde == ie && c.hacia == is)) return false;
    const auto e = entrantes(is);
    return e.size() == 1 && e[0].desde == ie;
}

std::vector<std::string> GrafoVideo::salidas() const {
    std::vector<std::string> out;
    for (const auto& n : nodos_)
        if (n.tipo == "salida" && !n.params.value("nombre", std::string()).empty()) out.push_back(n.params.value("nombre", ""));
    return out;
}

json GrafoVideo::aJson() const {
    json ns = json::array(), cs = json::array();
    for (const auto& n : nodos_)
        ns.push_back({{"id", n.id}, {"tipo", n.tipo}, {"etiqueta", n.etiqueta}, {"grupo", n.grupo}, {"x", n.x}, {"y", n.y},
                      {"params", n.params}});
    for (const auto& c : cables_)
        cs.push_back({{"desde", c.desde}, {"salida", c.salida}, {"hacia", c.hacia}, {"entrada", c.entrada}});
    return {{"version", 2}, {"nodos", ns}, {"cables", cs}};
}

GrafoVideo GrafoVideo::desde(const json& j) {
    GrafoVideo g;
    if (!j.is_object()) return g;
    if (j.value("version", 0) == 2) {
        std::set<std::string> ids;
        for (const auto& n : j.value("nodos", json::array())) {
            Nodo x;
            x.id = n.value("id", std::string());
            x.tipo = n.value("tipo", std::string());
            if (x.id.empty() || ids.count(x.id) || !(x.tipo == "entrada" || x.tipo == "escena" || tipoValido(x.tipo))) continue;
            x.etiqueta = n.value("etiqueta", titulo(x.tipo));
            x.grupo = n.value("grupo", std::string());
            x.x = n.value("x", 0);
            x.y = n.value("y", 0);
            x.params = fabrica(x.tipo);
            const json p = n.value("params", json::object());
            if (p.is_object())
                for (auto it = p.begin(); it != p.end(); ++it) x.params[it.key()] = it.value();
            ids.insert(x.id);
            g.nodos_.push_back(std::move(x));
        }
        for (const auto& c : j.value("cables", json::array())) {
            std::string e;
            g.conectar(c.value("desde", std::string()), c.value("salida", std::string("out")),
                       c.value("hacia", std::string()), c.value("entrada", std::string("in")), &e); // valida todo
        }
        return g;
    }
    // Formato de antes: un grafo por entrada, con `in`/`out` y la salida como
    // tarjeta de paso.
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (!it.value().is_object()) continue;
        const std::string fuente = it.key();
        g.asegurarEntrada(fuente);
        const int y0 = g.nodo(idEntrada(fuente))->y;
        const std::string pre = slug(fuente) + ".";
        const auto mapear = [&](const std::string& id) {
            return id == "in" ? idEntrada(fuente) : id == "out" ? idEscena(fuente) : pre + id;
        };
        for (const auto& n : it.value().value("nodos", json::array())) {
            const std::string tipo = n.value("tipo", std::string());
            if (tipo == "in" || tipo == "out") {
                g.mover(mapear(tipo), n.value("x", 0), y0 + n.value("y", 0));
                continue;
            }
            if (!tipoValido(tipo)) continue;
            Nodo x;
            x.id = mapear(n.value("id", std::string()));
            x.tipo = tipo;
            x.etiqueta = n.value("etiqueta", titulo(tipo));
            x.grupo = fuente;
            x.x = n.value("x", 0);
            x.y = y0 + n.value("y", 0);
            x.params = fabrica(tipo);
            const json p = n.value("params", json::object());
            if (p.is_object())
                for (auto q = p.begin(); q != p.end(); ++q) x.params[q.key()] = q.value();
            g.nodos_.push_back(std::move(x));
        }
        g.cables_.erase(std::remove_if(g.cables_.begin(), g.cables_.end(),
                                       [&](const Cable& c) { return c.hacia == idEscena(fuente); }),
                        g.cables_.end());
        const json cs = it.value().value("cables", json::array());
        std::map<std::string, std::string> alimenta; // quién alimentaba cada nodo
        for (const auto& c : cs) alimenta[mapear(c.value("hacia", std::string()))] = mapear(c.value("desde", std::string()));
        for (const auto& c : cs) {
            std::string d = mapear(c.value("desde", std::string()));
            // Una salida ya no deja pasar: lo que salía de ella sale de lo que la alimentaba.
            const Nodo* nd = g.nodo(d);
            if (nd != nullptr && nd->tipo == "salida" && alimenta.count(d)) d = alimenta[d];
            std::string e;
            g.conectar(d, "out", mapear(c.value("hacia", std::string())), "in", &e);
        }
    }
    return g;
}

json GrafoVideo::paraFiltro(const std::string& fuente) const {
    const std::string ie = idEntrada(fuente), is = idEscena(fuente);
    // Lo que se alcanza desde la entrada por nodos de UNA entrada (los de
    // varias, y las escenas de otras entradas, los ejecuta el estudio).
    std::set<std::string> dentro{ie};
    std::vector<std::string> pila{ie};
    while (!pila.empty()) {
        const std::string n = pila.back();
        pila.pop_back();
        for (const auto& c : salientes(n)) {
            const Nodo* h = nodo(c.hacia);
            if (h == nullptr || dentro.count(h->id)) continue;
            if (!(esFiltro(h->tipo) || h->tipo == "salida")) continue;
            dentro.insert(h->id);
            pila.push_back(h->id);
        }
    }
    const auto mapear = [&](const std::string& id) {
        return id == ie ? std::string("in") : id == is ? std::string("out") : id;
    };
    json ns = json::array(), cs = json::array();
    ns.push_back({{"id", "in"}, {"tipo", "in"}});
    ns.push_back({{"id", "out"}, {"tipo", "out"}});
    for (const auto& n : nodos_)
        if (dentro.count(n.id) && n.id != ie) ns.push_back({{"id", n.id}, {"tipo", n.tipo}, {"params", n.params}});
    for (const auto& c : cables_)
        if (dentro.count(c.desde) && (dentro.count(c.hacia) || c.hacia == is))
            cs.push_back({{"desde", mapear(c.desde)}, {"hacia", mapear(c.hacia)}});
    return {{"nodos", ns}, {"cables", cs}};
}

json GrafoVideo::describir(const std::string& grupo, bool soloGrupo) const {
    std::set<std::string> visibles;
    for (const auto& n : nodos_)
        if (!soloGrupo || n.grupo == grupo) visibles.insert(n.id);
    std::set<std::string> externos;
    // Los nodos del estudio (un compositor) se ven siempre, como de afuera:
    // así se los puede cablear desde cualquier entrada.
    if (soloGrupo)
        for (const auto& n : nodos_)
            if (n.grupo.empty()) externos.insert(n.id);
    if (soloGrupo)
        for (const auto& c : cables_) {
            if (visibles.count(c.desde) && !visibles.count(c.hacia)) externos.insert(c.hacia);
            if (visibles.count(c.hacia) && !visibles.count(c.desde)) externos.insert(c.desde);
        }
    const auto puertos = [](const std::vector<Puerto>& ps) {
        json a = json::array();
        for (const auto& p : ps) a.push_back({{"id", p.id}, {"tipo", p.tipo}, {"pedido", p.pedido}});
        return a;
    };
    json ns = json::array(), cs = json::array();
    for (const auto& n : nodos_) {
        if (!visibles.count(n.id) && !externos.count(n.id)) continue;
        ns.push_back({{"id", n.id},
                      {"tipo", n.tipo},
                      {"titulo", titulo(n.tipo)},
                      {"etiqueta", n.etiqueta},
                      {"grupo", n.grupo},
                      {"x", n.x},
                      {"y", n.y},
                      {"params", n.params},
                      {"externo", externos.count(n.id) > 0},
                      {"entradas", puertos(puertosEntrada(n))},
                      {"salidas", puertos(puertosSalida(n))}});
    }
    for (const auto& c : cables_)
        if ((visibles.count(c.desde) || externos.count(c.desde)) && (visibles.count(c.hacia) || externos.count(c.hacia)))
            cs.push_back({{"desde", c.desde}, {"salida", c.salida}, {"hacia", c.hacia}, {"entrada", c.entrada}});
    return {{"nodos", ns}, {"cables", cs}, {"trivial", soloGrupo ? trivial(grupo) : false}};
}

} // namespace ba::app::video
