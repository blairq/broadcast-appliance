// src/app/video/GrafoVideo.cpp — ver GrafoVideo.hpp

#include "video/GrafoVideo.hpp"

#include <algorithm>
#include <set>

namespace ba::app::video {

using json = nlohmann::json;

namespace {

/// Grilla del editor: columnas de 220 (tarjetas de 180 y lugar para el cable), paso 20.
constexpr int kColumna = 220;

json fabrica(const std::string& tipo) {
    if (tipo == "color") return {{"brillo", 0.0}, {"contraste", 1.0}, {"saturacion", 1.0}, {"gamma", 1.0}};
    if (tipo == "croma")
        return {{"clave", 0x00B140}, {"similitud", 0.4}, {"suavidad", 0.08}, {"derrame", 0.1}};
    if (tipo == "recorte")
        return {{"izquierda", 0.0}, {"arriba", 0.0}, {"derecha", 0.0}, {"abajo", 0.0}, {"espejoH", false},
                {"espejoV", false}};
    if (tipo == "salida") return {{"nombre", ""}};
    return json::object();
}

std::string titulo(const std::string& tipo) {
    if (tipo == "in") return "Entrada";
    if (tipo == "out") return "A la escena";
    if (tipo == "color") return "Color";
    if (tipo == "croma") return "Croma";
    if (tipo == "recorte") return "Recorte y espejo";
    if (tipo == "salida") return "Salida";
    return tipo;
}

} // namespace

GrafoVideo::GrafoVideo() {
    nodos_.push_back({"in", "in", "Entrada", 0, 0, json::object()});
    nodos_.push_back({"out", "out", "A la escena", kColumna * 2, 0, json::object()});
    cables_.push_back({"in", "out"});
}

json GrafoVideo::catalogo() {
    json out = json::array();
    const std::pair<const char*, const char*> tipos[] = {
        {"color", "Brillo, contraste, saturación y gamma."},
        {"croma", "Saca un fondo de color (verde de fábrica): queda transparente."},
        {"recorte", "Recorta los bordes (quedan transparentes) y da vuelta la imagen."},
        {"salida", "Publica la imagen de ese punto como una fuente con nombre, para cualquier escena."},
    };
    for (const auto& [t, d] : tipos) out.push_back({{"tipo", t}, {"titulo", titulo(t)}, {"descripcion", d}, {"params", fabrica(t)}});
    return out;
}

bool GrafoVideo::tipoValido(const std::string& t) { return t == "color" || t == "croma" || t == "recorte" || t == "salida"; }

const GrafoVideo::Nodo* GrafoVideo::nodo(const std::string& id) const {
    for (const auto& n : nodos_)
        if (n.id == id) return &n;
    return nullptr;
}

std::string GrafoVideo::idNuevo(const std::string& tipo) const {
    if (nodo(tipo) == nullptr) return tipo;
    for (int i = 2;; ++i) {
        const std::string id = tipo + "-" + std::to_string(i);
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

std::string GrafoVideo::agregar(const std::string& tipo, const std::string& despues, const json& params,
                                std::string* error) {
    if (!tipoValido(tipo)) {
        if (error) *error = "no existe la tarjeta «" + tipo + "»";
        return {};
    }
    std::string desde = despues;
    if (desde.empty()) {
        desde = "in";
        for (const auto& c : cables_)
            if (c.hacia == "out") desde = c.desde;
    }
    const Nodo* d = nodo(desde);
    if (d == nullptr || d->tipo == "out") {
        if (error) *error = "no hay una tarjeta «" + desde + "» de la que colgarla";
        return {};
    }
    Nodo n;
    n.id = idNuevo(tipo);
    n.tipo = tipo;
    n.etiqueta = titulo(tipo);
    n.params = fabrica(tipo);
    if (params.is_object())
        for (auto it = params.begin(); it != params.end(); ++it) n.params[it.key()] = it.value();
    n.x = d->x + kColumna;
    n.y = d->y + (tipo == "salida" ? 160 : 0);
    if (tipo != "salida") {
        // Intercalada: lo que salía de `desde` sale ahora de la nueva, y lo
        // que venía después se corre una columna.
        for (auto& c : cables_)
            if (c.desde == desde) c.desde = n.id;
        for (auto& m : nodos_)
            if (m.x >= n.x && m.y == n.y && m.id != desde) m.x += kColumna;
    }
    cables_.push_back({desde, n.id});
    nodos_.push_back(std::move(n));
    return nodos_.back().id;
}

bool GrafoVideo::quitar(const std::string& id, std::string* error) {
    const Nodo* n = nodo(id);
    if (n == nullptr) {
        if (error) *error = "no existe la tarjeta «" + id + "»";
        return false;
    }
    if (n->tipo == "in" || n->tipo == "out") {
        if (error) *error = "la entrada y la escena no se quitan";
        return false;
    }
    std::string antes;
    std::vector<std::string> despues;
    for (const auto& c : cables_) {
        if (c.hacia == id) antes = c.desde;
        if (c.desde == id) despues.push_back(c.hacia);
    }
    cables_.erase(std::remove_if(cables_.begin(), cables_.end(),
                                 [&](const Cable& c) { return c.desde == id || c.hacia == id; }),
                  cables_.end());
    if (!antes.empty())
        for (const auto& h : despues) cables_.push_back({antes, h});
    nodos_.erase(std::remove_if(nodos_.begin(), nodos_.end(), [&](const Nodo& x) { return x.id == id; }), nodos_.end());
    return true;
}

bool GrafoVideo::conectar(const std::string& desde, const std::string& hacia, std::string* error) {
    const Nodo* d = nodo(desde);
    const Nodo* h = nodo(hacia);
    if (d == nullptr || h == nullptr) {
        if (error) *error = "no existe «" + (d == nullptr ? desde : hacia) + "»";
        return false;
    }
    if (d->tipo == "out") {
        if (error) *error = "«A la escena» no tiene salida";
        return false;
    }
    if (h->tipo == "in") {
        if (error) *error = "a la entrada no le llega nada";
        return false;
    }
    if (desde == hacia || llega(hacia, desde)) {
        if (error) *error = "ese cable cerraría un círculo";
        return false;
    }
    // Una sola cosa por entrada: el cable nuevo reemplaza al que llegaba.
    cables_.erase(std::remove_if(cables_.begin(), cables_.end(), [&](const Cable& c) { return c.hacia == hacia; }),
                  cables_.end());
    cables_.push_back({desde, hacia});
    return true;
}

bool GrafoVideo::desconectar(const std::string& desde, const std::string& hacia) {
    const auto antes = cables_.size();
    cables_.erase(std::remove(cables_.begin(), cables_.end(), Cable{desde, hacia}), cables_.end());
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
            // El tipo de fábrica manda (un número sigue siendo número).
            const json& viejo = n.params[it.key()];
            if ((viejo.is_number() && !it.value().is_number()) || (viejo.is_boolean() && !it.value().is_boolean()) ||
                (viejo.is_string() && !it.value().is_string())) {
                if (error) *error = "«" + it.key() + "» no es de ese tipo";
                return false;
            }
            n.params[it.key()] = it.value();
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

bool GrafoVideo::trivial() const { return nodos_.size() == 2 && cables_.size() == 1 && cables_[0] == Cable{"in", "out"}; }

std::vector<std::string> GrafoVideo::salidas() const {
    std::vector<std::string> out;
    for (const auto& n : nodos_)
        if (n.tipo == "salida" && !n.params.value("nombre", std::string()).empty()) out.push_back(n.params.value("nombre", ""));
    return out;
}

nlohmann::json GrafoVideo::aJson() const {
    nlohmann::json ns = nlohmann::json::array(), cs = nlohmann::json::array();
    for (const auto& n : nodos_)
        ns.push_back({{"id", n.id}, {"tipo", n.tipo}, {"etiqueta", n.etiqueta}, {"x", n.x}, {"y", n.y}, {"params", n.params}});
    for (const auto& c : cables_) cs.push_back({{"desde", c.desde}, {"hacia", c.hacia}});
    return {{"version", 1}, {"nodos", ns}, {"cables", cs}};
}

GrafoVideo GrafoVideo::desde(const nlohmann::json& j) {
    GrafoVideo g;
    if (!j.is_object()) return g;
    g.nodos_.clear();
    g.cables_.clear();
    std::set<std::string> ids;
    for (const auto& n : j.value("nodos", nlohmann::json::array())) {
        Nodo x;
        x.id = n.value("id", std::string());
        x.tipo = n.value("tipo", std::string());
        if (x.id.empty() || ids.count(x.id) || !(x.tipo == "in" || x.tipo == "out" || tipoValido(x.tipo))) continue;
        x.etiqueta = n.value("etiqueta", titulo(x.tipo));
        x.x = n.value("x", 0);
        x.y = n.value("y", 0);
        x.params = fabrica(x.tipo);
        const nlohmann::json p = n.value("params", nlohmann::json::object());
        if (p.is_object())
            for (auto it = p.begin(); it != p.end(); ++it) x.params[it.key()] = it.value();
        ids.insert(x.id);
        g.nodos_.push_back(std::move(x));
    }
    if (g.nodo("in") == nullptr || g.nodo("in")->tipo != "in") g.nodos_.insert(g.nodos_.begin(), {"in", "in", "Entrada", 0, 0, nlohmann::json::object()});
    if (g.nodo("out") == nullptr || g.nodo("out")->tipo != "out")
        g.nodos_.push_back({"out", "out", "A la escena", kColumna * 2, 0, nlohmann::json::object()});
    for (const auto& c : j.value("cables", nlohmann::json::array())) {
        std::string error;
        g.conectar(c.value("desde", std::string()), c.value("hacia", std::string()), &error); // valida todo
    }
    return g;
}

nlohmann::json GrafoVideo::paraFiltro() const {
    nlohmann::json ns = nlohmann::json::array(), cs = nlohmann::json::array();
    for (const auto& n : nodos_) ns.push_back({{"id", n.id}, {"tipo", n.tipo}, {"params", n.params}});
    for (const auto& c : cables_) cs.push_back({{"desde", c.desde}, {"hacia", c.hacia}});
    return {{"nodos", ns}, {"cables", cs}};
}

nlohmann::json GrafoVideo::describir() const {
    nlohmann::json ns = nlohmann::json::array(), cs = nlohmann::json::array();
    for (const auto& n : nodos_)
        ns.push_back({{"id", n.id},
                      {"tipo", n.tipo},
                      {"titulo", titulo(n.tipo)},
                      {"etiqueta", n.etiqueta},
                      {"x", n.x},
                      {"y", n.y},
                      {"params", n.params},
                      {"entradas", n.tipo == "in" ? 0 : 1},
                      {"salidas", n.tipo == "out" ? 0 : 1}});
    for (const auto& c : cables_) cs.push_back({{"desde", c.desde}, {"hacia", c.hacia}});
    return {{"nodos", ns}, {"cables", cs}, {"trivial", trivial()}};
}

} // namespace ba::app::video
