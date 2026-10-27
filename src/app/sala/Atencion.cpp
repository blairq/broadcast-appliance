// ============================================================================
// src/app/sala/Atencion.cpp — ver Atencion.hpp
// ============================================================================
#include "sala/Atencion.hpp"

#include "i18n/I18n.hpp"

#include <algorithm>
#include <cctype>

namespace ba::app::sala {

Atendedor quienAtiende(bool recepcionActiva, bool automatizacionCorriendo) {
    if (recepcionActiva) return Atendedor::Recepcion;
    if (automatizacionCorriendo) return Atendedor::Automatizacion;
    return Atendedor::Nadie;
}

const char* claveDe(Atendedor a) {
    switch (a) {
        case Atendedor::Recepcion: return "recepcion";
        case Atendedor::Automatizacion: return "automatizacion";
        case Atendedor::Nadie: break;
    }
    return "";
}

std::string nombreDe(Atendedor a) {
    switch (a) {
        case Atendedor::Recepcion: return ba::i18n::traducir("sala", "el rol de recepción");
        case Atendedor::Automatizacion: return ba::i18n::traducir("sala", "el asistente de automatización");
        case Atendedor::Nadie: break;
    }
    return ba::i18n::traducir("sala", "nadie");
}

std::string porQueNadie(Atendedor a) {
    if (a != Atendedor::Nadie) return {};
    return ba::i18n::traducir("sala", "No hay quién atienda: prendé el rol «recepción» (Agentes) o el asistente de "
                                      "automatización (pestaña ✦). El del evento no atiende el lobby: es la charla con vos.");
}

const char* claveDe(Ambito a) { return a == Ambito::Asamblea ? "asamblea" : "sala"; }
const char* claveDe(Modo m) { return m == Modo::Voz ? "voz" : "chat"; }
const char* claveDe(Fin f) { return f == Fin::Admitido ? "admitido" : "se_fue"; }

nlohmann::json AjustesAtencion::aJson() const {
    return {{"salaHablando", salaHablando},
            {"asambleaAtiende", asambleaAtiende},
            {"asambleaHablando", asambleaHablando},
            {"voz", {{"repo", vozRepo}, {"hablante", vozHablante}, {"velocidad", vozVelocidad}}}};
}

AjustesAtencion AjustesAtencion::deJson(const nlohmann::json& j) {
    AjustesAtencion a;
    if (!j.is_object()) return a;
    const auto booleano = [&j](const char* k, bool porDefecto) {
        const auto it = j.find(k);
        return it != j.end() && it->is_boolean() ? it->get<bool>() : porDefecto;
    };
    a.salaHablando = booleano("salaHablando", false);
    a.asambleaAtiende = booleano("asambleaAtiende", false);
    a.asambleaHablando = booleano("asambleaHablando", false);
    if (const auto v = j.find("voz"); v != j.end() && v->is_object()) {
        if (const auto r = v->find("repo"); r != v->end() && r->is_string()) a.vozRepo = r->get<std::string>();
        if (const auto h = v->find("hablante"); h != v->end() && h->is_number_integer())
            a.vozHablante = std::max(0, h->get<int>());
        if (const auto s = v->find("velocidad"); s != v->end() && s->is_number())
            a.vozVelocidad = std::clamp(s->get<double>(), 0.7, 1.5);
    }
    return a;
}

// --- Los turnos -------------------------------------------------------------

Turnos::Turno Turnos::empezar(const std::string& id, const std::string& nombre, Ambito ambito, Atendedor quien,
                              bool hablando) {
    if (const Turno* t = turno(id)) return *t;
    Turno t;
    t.id = id;
    t.nombre = nombre;
    t.ambito = ambito;
    t.quien = quien;
    // Una sola voz del que atiende, sea para el lobby o para la sesión remota.
    t.modo = hablando && conLaVoz().empty() ? Modo::Voz : Modo::Chat;
    turnos_.push_back(t);
    return t;
}

std::vector<Turnos::Terminado> Turnos::actualizar(Ambito ambito, const std::set<std::string>& esperando,
                                                  const std::set<std::string>& adentro) {
    std::vector<Terminado> out;
    for (auto it = turnos_.begin(); it != turnos_.end();) {
        if (it->ambito != ambito || esperando.count(it->id) != 0) {
            ++it;
            continue;
        }
        out.push_back({it->id, adentro.count(it->id) != 0 ? Fin::Admitido : Fin::SeFue, it->modo == Modo::Voz});
        it = turnos_.erase(it);
    }
    return out;
}

std::string Turnos::pasarLaVoz(const std::function<bool(const std::string&)>& puede) {
    if (!conLaVoz().empty()) return {};
    for (Turno& t : turnos_) {
        if (t.modo == Modo::Chat && !t.sinVoz && (!puede || puede(t.id))) {
            t.modo = Modo::Voz;
            return t.id;
        }
    }
    return {};
}

void Turnos::sinVoz(const std::string& id) {
    for (Turno& t : turnos_) {
        if (t.id != id) continue;
        t.modo = Modo::Chat;
        t.sinVoz = true;
    }
}

std::string Turnos::soltarLaVoz(Ambito ambito) {
    std::string tenia;
    for (Turno& t : turnos_) {
        if (t.ambito != ambito) continue;
        if (t.modo == Modo::Voz) tenia = t.id;
        t.modo = Modo::Chat;
    }
    return tenia;
}

std::vector<std::string> Turnos::terminarTodos(Ambito ambito) {
    std::vector<std::string> out;
    for (auto it = turnos_.begin(); it != turnos_.end();) {
        if (it->ambito == ambito) {
            out.push_back(it->id);
            it = turnos_.erase(it);
        } else {
            ++it;
        }
    }
    return out;
}

std::string Turnos::conLaVoz() const {
    for (const Turno& t : turnos_)
        if (t.modo == Modo::Voz) return t.id;
    return {};
}

const Turnos::Turno* Turnos::turno(const std::string& id) const {
    for (const Turno& t : turnos_)
        if (t.id == id) return &t;
    return nullptr;
}

// --- El nombre que dio ------------------------------------------------------

std::string nombreDado(const std::string& nombre) {
    // Espacios de más afuera (y adentro, de a uno): es como se tipea en un celular.
    std::string limpio;
    bool espacio = false;
    for (const char c : nombre) {
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            espacio = !limpio.empty();
            continue;
        }
        if (espacio) limpio += ' ';
        espacio = false;
        limpio += c;
    }
    // Letras: las ASCII y cualquier byte de un carácter UTF-8 (á, ñ, ü…), que
    // en un nombre son letras. Con una sola no es un nombre: es un apuro.
    int letras = 0;
    for (const char c : limpio) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isdigit(u) != 0) return {}; // un nombre no lleva números: «Invitado 3», «Juan123»
        if (std::isalpha(u) != 0 || (u >= 0xC0)) ++letras; // 0xC0+: el primer byte de un carácter de 2+ bytes
    }
    if (letras < 2) return {};
    // Lo que ponen los navegadores, los celulares o el apuro: no es el nombre de nadie.
    std::string primera = limpio.substr(0, limpio.find(' '));
    std::transform(primera.begin(), primera.end(), primera.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    static const std::set<std::string> kNoSonNombres{
        "invitado", "invitada", "guest",   "anonimo",  "anónimo",  "anonymous", "usuario", "user",
        "participante", "test", "prueba",  "iphone",   "ipad",     "android",   "celular", "telefono",
        "teléfono", "samsung",  "xiaomi",  "motorola", "pc",       "notebook",  "laptop",  "operador",
        "nombre",   "yo",       "hola",    "asdf",     "qwerty",   "x",         "xx",      "xxx"};
    if (kNoSonNombres.count(primera) != 0) return {};
    return limpio;
}

} // namespace ba::app::sala
