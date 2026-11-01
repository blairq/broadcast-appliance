// ============================================================================
// src/gateway/Preguntas.cpp — ver Preguntas.hpp
// ============================================================================
#include "Preguntas.hpp"

#include <algorithm>
#include <cstdio>

namespace ba::gateway {
namespace {

using nlohmann::json;

/// Recorta y saca los espacios de los bordes y los saltos de linea: una
/// pregunta es un renglon, no un parrafo con formato.
std::string recortar(const std::string& s, std::size_t tope) {
    std::string out;
    out.reserve(std::min(s.size(), tope));
    bool espacio = false;
    bool corto = false; // true si se llego al tope y quedo texto afuera
    for (char c : s) {
        if (out.size() >= tope) {
            corto = true;
            break;
        }
        const unsigned char u = static_cast<unsigned char>(c);
        if (u == '\n' || u == '\r' || u == '\t' || c == ' ') {
            if (!out.empty()) espacio = true;
            continue;
        }
        if (u < 0x20) continue; // otros caracteres de control, fuera
        if (espacio) {
            out.push_back(' ');
            espacio = false;
        }
        out.push_back(c);
    }
    // SOLO SI SE CORTO. Un corte a la mitad de un caracter multibyte deja
    // basura en pantalla, pero hacer esto siempre se come la ultima letra
    // acentuada de todo texto que termine en una: «Si» quedaba en «S» y «Jose»
    // en «Jos». Se vio proyectando una votacion.
    if (corto) {
        while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80)
            out.pop_back();
        if (!out.empty() && (static_cast<unsigned char>(out.back()) & 0x80) != 0) out.pop_back();
    }
    return out;
}

} // namespace

const char* Preguntas::nombreDeEstado(Estado e) {
    switch (e) {
        case Estado::Nueva: return "nueva";
        case Estado::Elegida: return "elegida";
        case Estado::AlAire: return "al_aire";
        case Estado::Respondida: return "respondida";
        case Estado::Descartada: return "descartada";
    }
    return "nueva";
}

bool Preguntas::estadoDeNombre(const std::string& s, Estado& out) {
    for (Estado e : {Estado::Nueva, Estado::Elegida, Estado::AlAire, Estado::Respondida, Estado::Descartada})
        if (s == nombreDeEstado(e)) {
            out = e;
            return true;
        }
    return false;
}

Preguntas::Pregunta* Preguntas::buscar(const std::string& id) {
    for (Pregunta& p : ps_)
        if (p.id == id) return &p;
    return nullptr;
}

void Preguntas::hacerLugar() {
    if (ps_.size() < kMax) return;
    // Primero las que ya no importan. Perder una pregunta SIN CONTESTAR porque
    // alguien lleno la lista seria el peor de los dos males.
    const auto vieja = std::find_if(ps_.begin(), ps_.end(), [](const Pregunta& p) {
        return p.estado == Estado::Respondida || p.estado == Estado::Descartada;
    });
    if (vieja != ps_.end()) {
        ps_.erase(vieja);
        return;
    }
    // Si TODAS estan esperando, se va la mas vieja sin votos; y si todas tienen
    // votos, la mas vieja a secas.
    const auto sinVotos = std::find_if(
        ps_.begin(), ps_.end(), [](const Pregunta& p) { return p.estado == Estado::Nueva && p.votos == 0; });
    ps_.erase(sinVotos != ps_.end() ? sinVotos : ps_.begin());
}

std::string Preguntas::preguntar(const std::string& nombre, const std::string& texto,
                                 const std::string& deDonde, const std::string& quien, std::int64_t ahoraMs) {
    const std::string t = recortar(texto, kMaxTexto);
    if (t.empty()) return {};
    std::lock_guard<std::mutex> g(m_);
    hacerLugar();
    Pregunta p;
    p.id = "q" + std::to_string(++n_) + "-" + std::to_string(ahoraMs % 100000);
    p.nombre = recortar(nombre, kMaxNombre);
    p.texto = t;
    p.deDonde = deDonde.empty() ? "publico" : deDonde;
    p.cuandoMs = ahoraMs;
    // Quien la manda la vota: si no, la propia pregunta arranca por debajo de
    // cualquier otra que ya tenga un voto, y el orden por votos no diria nada
    // el primer minuto.
    p.votos = 1;
    const std::string id = p.id;
    ps_.push_back(std::move(p));
    if (!quien.empty()) votos_.emplace_back(quien, id);
    return id;
}

bool Preguntas::votar(const std::string& id, const std::string& quien) {
    std::lock_guard<std::mutex> g(m_);
    Pregunta* p = buscar(id);
    if (p == nullptr) return false;
    if (!quien.empty()) {
        const auto ya = std::find(votos_.begin(), votos_.end(), std::make_pair(quien, id));
        if (ya != votos_.end()) return false;
        votos_.emplace_back(quien, id);
    }
    ++p->votos;
    return true;
}

bool Preguntas::elegir(const std::string& id) {
    std::lock_guard<std::mutex> g(m_);
    Pregunta* p = buscar(id);
    if (p == nullptr || p->estado == Estado::Elegida) return p != nullptr;
    p->estado = Estado::Elegida;
    p->turno = ++siguienteTurno_;
    return true;
}

bool Preguntas::desElegir(const std::string& id) {
    std::lock_guard<std::mutex> g(m_);
    Pregunta* p = buscar(id);
    if (p == nullptr) return false;
    p->estado = Estado::Nueva;
    p->turno = 0;
    return true;
}

bool Preguntas::alAire(const std::string& id) {
    std::lock_guard<std::mutex> g(m_);
    Pregunta* p = buscar(id);
    if (p == nullptr) return false;
    // Una sola al aire: la que estaba pasa a respondida, porque si se saco otra
    // es que con aquella ya se termino.
    for (Pregunta& o : ps_)
        if (o.estado == Estado::AlAire && o.id != id) o.estado = Estado::Respondida;
    if (p->turno == 0) p->turno = ++siguienteTurno_;
    p->estado = Estado::AlAire;
    return true;
}

bool Preguntas::responder(const std::string& id) {
    std::lock_guard<std::mutex> g(m_);
    Pregunta* p = buscar(id);
    if (p == nullptr) return false;
    p->estado = Estado::Respondida;
    return true;
}

bool Preguntas::descartar(const std::string& id) {
    std::lock_guard<std::mutex> g(m_);
    Pregunta* p = buscar(id);
    if (p == nullptr) return false;
    p->estado = Estado::Descartada;
    p->turno = 0;
    return true;
}

std::string Preguntas::proxima() const {
    std::lock_guard<std::mutex> g(m_);
    const Pregunta* mejor = nullptr;
    for (const Pregunta& p : ps_) {
        if (p.estado != Estado::Elegida) continue;
        if (mejor == nullptr || p.turno < mejor->turno) mejor = &p;
    }
    return mejor == nullptr ? std::string() : mejor->id;
}

namespace {

json aJson(const Preguntas::Pregunta& p) {
    return json{{"id", p.id},
                {"nombre", p.nombre},
                {"texto", p.texto},
                {"deDonde", p.deDonde},
                {"estado", Preguntas::nombreDeEstado(p.estado)},
                {"cuandoMs", p.cuandoMs},
                {"votos", p.votos},
                {"turno", p.turno}};
}

/// El orden en que las quiere ver el que modera: la que esta al aire arriba,
/// despues la cola en el orden en que se va a preguntar, y al final las que
/// todavia nadie miro, las mas votadas primero.
int rango(Preguntas::Estado e) {
    switch (e) {
        case Preguntas::Estado::AlAire: return 0;
        case Preguntas::Estado::Elegida: return 1;
        case Preguntas::Estado::Nueva: return 2;
        case Preguntas::Estado::Respondida: return 3;
        case Preguntas::Estado::Descartada: return 4;
    }
    return 5;
}

} // namespace

json Preguntas::lista() const {
    std::lock_guard<std::mutex> g(m_);
    std::vector<const Pregunta*> orden;
    orden.reserve(ps_.size());
    for (const Pregunta& p : ps_)
        orden.push_back(&p);
    std::sort(orden.begin(), orden.end(), [](const Pregunta* a, const Pregunta* b) {
        const int ra = rango(a->estado), rb = rango(b->estado);
        if (ra != rb) return ra < rb;
        if (a->estado == Estado::Elegida) return a->turno < b->turno;
        if (a->votos != b->votos) return a->votos > b->votos;
        return a->cuandoMs < b->cuandoMs;
    });
    json out = json::array();
    for (const Pregunta* p : orden)
        out.push_back(aJson(*p));
    return out;
}

json Preguntas::listaPublica(const std::string& quien) const {
    std::lock_guard<std::mutex> g(m_);
    json mias = json::array();
    json otras = json::array();
    for (const Pregunta& p : ps_) {
        const bool esMia = !quien.empty() && std::find(votos_.begin(), votos_.end(),
                                                       std::make_pair(quien, p.id)) != votos_.end();
        if (esMia) {
            // La suya SI con su estado: saber que la eligieron es la mitad de
            // la gracia de haber preguntado.
            mias.push_back(aJson(p));
            continue;
        }
        // De las ajenas, solo las que siguen en juego y sin el estado de
        // moderacion: enterarse de que a alguien se la bocharon no le sirve a
        // nadie, y ver la propia descartada invita a mandarla de nuevo.
        if (p.estado == Estado::Descartada) continue;
        otras.push_back(json{{"id", p.id},
                             {"nombre", p.nombre},
                             {"texto", p.texto},
                             {"votos", p.votos},
                             {"alAire", p.estado == Estado::AlAire}});
    }
    std::sort(otras.begin(), otras.end(), [](const json& a, const json& b) {
        if (a.value("alAire", false) != b.value("alAire", false)) return a.value("alAire", false);
        return a.value("votos", 0) > b.value("votos", 0);
    });
    return json{{"mias", mias}, {"otras", otras}};
}

std::string Preguntas::marcaDe(const std::string& id, const std::string& quien) {
    std::uint32_t h = 2166136261u;
    for (const unsigned char c : id + ":" + quien) {
        h ^= c;
        h *= 16777619u;
    }
    char buf[9];
    std::snprintf(buf, sizeof buf, "%08x", h);
    return buf;
}

json Preguntas::listaComun() const {
    std::lock_guard<std::mutex> g(m_);
    json todas = json::array();
    json enCola = json::array();
    for (const Pregunta& p : ps_) {
        if (p.estado == Estado::Descartada) continue;
        todas.push_back(json{{"id", p.id},
                             {"nombre", p.nombre},
                             {"texto", p.texto},
                             {"votos", p.votos},
                             {"alAire", p.estado == Estado::AlAire},
                             {"respondida", p.estado == Estado::Respondida}});
    }
    for (const auto& [quien, id] : votos_) {
        if (enCola.size() >= 4096) break; // tope: la respuesta va a todos los teléfonos
        const auto it = std::find_if(ps_.begin(), ps_.end(), [&](const Pregunta& p) { return p.id == id; });
        if (it != ps_.end() && it->estado == Estado::Elegida) enCola.push_back(marcaDe(id, quien));
    }
    std::sort(todas.begin(), todas.end(), [](const json& a, const json& b) {
        if (a.value("alAire", false) != b.value("alAire", false)) return a.value("alAire", false);
        return a.value("votos", 0) > b.value("votos", 0);
    });
    return json{{"todas", todas}, {"enCola", enCola}};
}

json Preguntas::recuento() const {
    std::lock_guard<std::mutex> g(m_);
    json r = json{{"nuevas", 0},      {"elegidas", 0},    {"al_aire", 0},
                  {"respondidas", 0}, {"descartadas", 0}, {"total", ps_.size()}};
    for (const Pregunta& p : ps_) {
        switch (p.estado) {
            case Estado::Nueva: r["nuevas"] = r["nuevas"].get<int>() + 1; break;
            case Estado::Elegida: r["elegidas"] = r["elegidas"].get<int>() + 1; break;
            case Estado::AlAire: r["al_aire"] = r["al_aire"].get<int>() + 1; break;
            case Estado::Respondida: r["respondidas"] = r["respondidas"].get<int>() + 1; break;
            case Estado::Descartada: r["descartadas"] = r["descartadas"].get<int>() + 1; break;
        }
    }
    return r;
}

std::size_t Preguntas::limpiar() {
    std::lock_guard<std::mutex> g(m_);
    const std::size_t antes = ps_.size();
    ps_.erase(std::remove_if(ps_.begin(), ps_.end(),
                             [](const Pregunta& p) {
                                 return p.estado == Estado::Respondida || p.estado == Estado::Descartada;
                             }),
              ps_.end());
    return antes - ps_.size();
}

json Preguntas::foto() const {
    std::lock_guard<std::mutex> g(m_);
    json ps = json::array();
    for (const Pregunta& p : ps_) ps.push_back(aJson(p));
    json vs = json::array();
    for (const auto& [quien, id] : votos_) vs.push_back(json::array({quien, id}));
    return json{{"preguntas", ps}, {"votos", vs}, {"siguienteTurno", siguienteTurno_}, {"n", n_}};
}

void Preguntas::restaurar(const json& f) {
    std::lock_guard<std::mutex> g(m_);
    ps_.clear();
    votos_.clear();
    siguienteTurno_ = 0;
    n_ = 0;
    if (!f.is_object()) return;
    for (const json& j : f.value("preguntas", json::array())) {
        if (!j.is_object() || ps_.size() >= kMax) continue;
        Pregunta p;
        p.id = j.value("id", std::string());
        p.texto = recortar(j.value("texto", std::string()), kMaxTexto);
        if (p.id.empty() || p.texto.empty()) continue;
        p.nombre = recortar(j.value("nombre", std::string()), kMaxNombre);
        p.deDonde = j.value("deDonde", std::string("publico"));
        estadoDeNombre(j.value("estado", std::string("nueva")), p.estado);
        p.cuandoMs = j.value("cuandoMs", std::int64_t{0});
        p.votos = std::max(0, j.value("votos", 0));
        p.turno = std::max(0, j.value("turno", 0));
        ps_.push_back(std::move(p));
    }
    for (const json& v : f.value("votos", json::array()))
        if (v.is_array() && v.size() == 2 && v[0].is_string() && v[1].is_string())
            votos_.emplace_back(v[0].get<std::string>(), v[1].get<std::string>());
    siguienteTurno_ = std::max(0, f.value("siguienteTurno", 0));
    n_ = std::max<std::int64_t>(0, f.value("n", std::int64_t{0}));
}

void Preguntas::vaciar() {
    std::lock_guard<std::mutex> g(m_);
    ps_.clear();
    votos_.clear();
    siguienteTurno_ = 0;
}

} // namespace ba::gateway
