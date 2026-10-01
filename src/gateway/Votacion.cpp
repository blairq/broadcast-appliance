// ============================================================================
// src/gateway/Votacion.cpp — ver Votacion.hpp
// ============================================================================
#include "Votacion.hpp"

#include <algorithm>

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

const char* Votacion::nombreDeModo(Modo m) { return m == Modo::Formal ? "formal" : "encuesta"; }

bool Votacion::modoDeNombre(const std::string& s, Modo& out) {
    if (s == "formal") {
        out = Modo::Formal;
        return true;
    }
    if (s == "encuesta") {
        out = Modo::Encuesta;
        return true;
    }
    return false;
}

bool Votacion::abrir(const std::string& pregunta, const std::vector<std::string>& opciones, Modo modo,
                     bool secreta, const std::map<std::string, std::string>& habilitados, int cuantosPodian,
                     std::int64_t ahoraMs) {
    const std::string p = recortar(pregunta, kMaxTexto);
    if (p.empty()) return false;
    std::vector<std::string> ops;
    for (const std::string& o : opciones) {
        if (ops.size() >= kMaxOpciones) break;
        const std::string t = recortar(o, kMaxTexto);
        if (!t.empty()) ops.push_back(t);
    }
    // Con una sola opcion no hay nada que decidir, y con ninguna tampoco.
    if (ops.size() < 2) return false;

    std::lock_guard<std::mutex> g(m_);
    // Una que se reemplaza sin cerrar IGUAL PASO: para el acta importa que se
    // voto, no que alguien se haya acordado de apretar cerrar.
    if (abierta_) archivarSinLock();
    abierta_ = true;
    hubo_ = true;
    modo_ = modo;
    secreta_ = secreta;
    pregunta_ = p;
    opciones_ = std::move(ops);
    desdeMs_ = ahoraMs;
    hastaMs_ = 0;
    cuantosPodian_ = std::max(0, cuantosPodian);
    habilitados_ = habilitados;
    // Los votos de la anterior NO se arrastran: son de otra pregunta.
    votos_.clear();
    return true;
}

void Votacion::archivarSinLock() {
    if (!hubo_ || pregunta_.empty()) return;
    json e = estadoSinLock();
    e["hastaMs"] = hastaMs_;
    historial_.push_back(std::move(e));
}

bool Votacion::cerrar() {
    std::lock_guard<std::mutex> g(m_);
    if (!abierta_) return false;
    abierta_ = false;
    hastaMs_ = 0;
    archivarSinLock();
    return true;
}

json Votacion::historial() const {
    std::lock_guard<std::mutex> g(m_);
    json out = json::array();
    for (const json& v : historial_)
        out.push_back(v);
    return out;
}

void Votacion::olvidarHistorial() {
    std::lock_guard<std::mutex> g(m_);
    historial_.clear();
}

bool Votacion::hayAbierta() const {
    std::lock_guard<std::mutex> g(m_);
    return abierta_;
}

void Votacion::setMostrarEnVivo(bool si) {
    std::lock_guard<std::mutex> g(m_);
    enVivo_ = si;
}

std::string Votacion::votar(const std::string& quien, const std::string& codigo, int opcion) {
    std::lock_guard<std::mutex> g(m_);
    if (!abierta_) return "la votacion no esta abierta";
    if (opcion < 0 || static_cast<std::size_t>(opcion) >= opciones_.size()) return "esa opcion no existe";

    std::string clave;
    if (modo_ == Modo::Formal) {
        // El navegador NO alcanza: se borra el sitio y se vota de nuevo. Lo que
        // identifica es el codigo, y solo valen los que mando A desde el
        // padron.
        if (codigo.empty()) return "falta el codigo: en una votacion formal hay que identificarse";
        if (habilitados_.find(codigo) == habilitados_.end())
            return "ese codigo no figura entre los que pueden votar";
        clave = codigo;
    } else {
        if (quien.empty()) return "no se de que telefono viene";
        clave = quien;
    }
    // Volver a votar CAMBIA el voto: alguien que se equivoco de boton tiene que
    // poder arreglarlo, y cerrar la urna es lo que lo vuelve definitivo.
    votos_[clave] = opcion;
    return {};
}

json Votacion::recuentoSinLock() const {
    json conteo = json::array();
    for (std::size_t i = 0; i < opciones_.size(); ++i) {
        int n = 0;
        for (const auto& [k, v] : votos_)
            if (v == static_cast<int>(i)) ++n;
        conteo.push_back(json{{"opcion", opciones_[i]}, {"votos", n}});
    }
    return conteo;
}

json Votacion::estado() const {
    std::lock_guard<std::mutex> g(m_);
    return estadoSinLock();
}

json Votacion::estadoSinLock() const {
    if (!hubo_) return json{{"hubo", false}};
    json e{{"hubo", true},
           {"abierta", abierta_},
           {"modo", nombreDeModo(modo_)},
           {"secreta", secreta_},
           {"enVivo", enVivo_},
           {"pregunta", pregunta_},
           {"desdeMs", desdeMs_},
           {"votaron", static_cast<int>(votos_.size())},
           {"podian", cuantosPodian_},
           {"resultado", recuentoSinLock()}};
    // El quorum, dicho: «votaron 12 de 40» y no solo «12». Sin el
    // denominador, dos a favor y cero en contra parece unanime cuando en
    // realidad no vino nadie.
    if (modo_ == Modo::Formal) {
        json quienes = json::array();
        for (const auto& [codigo, opcion] : votos_) {
            const auto it = habilitados_.find(codigo);
            json q{{"nombre", it == habilitados_.end() ? std::string() : it->second}};
            // SECRETA no es anonima: para el acta hace falta saber QUE FULANO
            // VOTO —si no, no hay como probar el quorum— y puede no
            // corresponder saber QUE voto.
            if (!secreta_) q["opcion"] = opciones_[static_cast<std::size_t>(opcion)];
            quienes.push_back(q);
        }
        e["quienes"] = quienes;
        json faltan = json::array();
        for (const auto& [codigo, nombre] : habilitados_)
            if (votos_.find(codigo) == votos_.end()) faltan.push_back(nombre);
        e["faltanVotar"] = faltan;
    }
    return e;
}

json Votacion::estadoPublico(const std::string& quien, const std::string& codigo) const {
    std::lock_guard<std::mutex> g(m_);
    if (!hubo_) return json{{"hay", false}};
    const std::string clave = modo_ == Modo::Formal ? codigo : quien;
    const auto mio = votos_.find(clave);
    json e{{"hay", true},
           {"abierta", abierta_},
           {"modo", nombreDeModo(modo_)},
           {"pregunta", pregunta_},
           {"opciones", opciones_},
           {"miVoto", mio == votos_.end() ? -1 : mio->second},
           {"votaron", static_cast<int>(votos_.size())}};
    if (modo_ == Modo::Formal)
        e["puedeVotar"] = !codigo.empty() && habilitados_.find(codigo) != habilitados_.end();
    // El resultado mientras esta abierta solo si se dejo. En una votacion
    // formal suele NO quererse: ver que va perdiendo cambia lo que vota el que
    // todavia no voto.
    if (!abierta_ || enVivo_) e["resultado"] = recuentoSinLock();
    return e;
}

} // namespace ba::gateway
