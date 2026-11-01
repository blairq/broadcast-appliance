// ============================================================================
// src/app/emision/MezclaApp.cpp — ver MezclaApp.hpp
// ============================================================================

#include "emision/MezclaApp.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include <nlohmann/json.hpp>

#include "Motor.hpp"
#include "emision/SalidaMotor.hpp"
#include "i18n/I18n.hpp"

namespace ba::app {

using json = nlohmann::json;

namespace {
/// La sección `k` de un objeto, sin copiarla (un objeto vacío si no está).
const json& seccion(const json& j, const char* k) {
    static const json vacio = json::object();
    if (!j.is_object()) return vacio;
    const auto it = j.find(k);
    return it != j.end() && it->is_object() ? *it : vacio;
}
} // namespace

std::map<std::string, EscenasYFuentes::Fuente>& MezclaApp::modelo() const { return m_.escenas_->todasLasFuentes(); }

void MezclaApp::alAac(SalidaAire& s, int kbps) {
    const int antes = s.kbpsAac;
    if (kbps <= 0 || (antes > 0 && antes <= kbps)) return;
    s.kbpsAac = kbps;
    if (kbps < 128 && (antes == 0 || antes >= 128)) aplicarLoudness(); // cambia el techo
}

void MezclaApp::reponerEscucha() {
    if (!pfl_.empty() && modelo().count(pfl_) == 0) pfl_.clear();
    aplicarEscucha();
}

void MezclaApp::olvidarFuente(const std::string& nombre) {
    if (pfl_ == nombre) pfl_.clear();
}

void MezclaApp::renombrarFuente(const std::string& de, const std::string& a) {
    if (pfl_ == de) pfl_ = a;
}

void MezclaApp::alBorrarSalida(const std::string& id) {
    // La escucha que seguía su fader (el motor la apagó) vuelve con la por omisión.
    const bool escuchaSuya = salidaEscucha_ == id || (!pfl_.empty() && pflModo_ == "afl" && pflSalida_ == id);
    if (salidaEscucha_ == id) salidaEscucha_ = BroadcastEngine::kSalidaPorOmision;
    if (pflSalida_ == id) pflSalida_ = BroadcastEngine::kSalidaPorOmision;
    if (escuchaSuya && m_.motor_) aplicarEscucha();
}

std::vector<std::string> MezclaApp::audioSources() const {
    std::vector<std::string> out;
    const json& e = seccion(m_.estadoMotor(), "entradas");
    for (const auto& n : m_.escenas_->sourceNames())
        if (e.contains(n) && e[n].value("audio", false)) out.push_back(n);
    return out;
}

bool MezclaApp::setSourceVolume(const std::string& name, float mul, const std::string& salida) {
    auto it = modelo().find(name);
    if (it == modelo().end()) return false;
    const float v = std::max(0.0f, mul);
    SalidaAire* s = m_.laSalida(salida);
    if (s == nullptr) return false;
    if (!s->porOmision()) {
        // La mezcla de otra salida: solo su fader en su bus (la fuente y su matriz son de la por omisión).
        // Avisa igual: el Mezclador puede estar mirando esta salida (la salida activa de la interfaz).
        const bool cambia = sourceVolume(name, s->id) != v;
        s->volumen[name] = v;
        const bool ok = m_.motor_->ponerNivel(name, nivelAplicado(name, v, *s), s->id).empty();
        if (cambia && m_.cambioAudio_) m_.cambioAudio_(name, "volumen");
        return ok;
    }
    const bool cambia = it->second.spec.volume != v;
    it->second.spec.volume = v;
    const bool ok = m_.motor_->ponerNivel(name, nivelAplicado(name, v, *s)).empty();
    if (cambia && m_.cambioAudio_) m_.cambioAudio_(name, "volumen");
    return ok;
}

std::string MezclaApp::grupoDeAudio(const std::string& nombre) {
    std::string g;
    for (char c : nombre) {
        if (c >= '0' && c <= '9') continue;
        if (c == ' ' && (g.empty() || g.back() == ' ')) continue;
        g.push_back(c);
    }
    while (!g.empty() && g.back() == ' ') g.pop_back();
    // «Invitado  (audio)» queda con un espacio solo antes del paréntesis.
    for (std::size_t p = g.find(" )"); p != std::string::npos; p = g.find(" )")) g.erase(p, 1);
    return g;
}

float MezclaApp::nivelAplicado(const std::string& name, float volumen, const SalidaAire& s) const {
    if (s.gananciaGrupo.empty()) return volumen;
    const auto it = s.gananciaGrupo.find(grupoDeAudio(name));
    return it != s.gananciaGrupo.end() ? volumen * it->second : volumen;
}

void MezclaApp::setGananciaGrupo(const std::string& grupo, float mul, const std::string& salida) {
    SalidaAire* s = m_.laSalida(salida);
    if (s == nullptr) return;
    const float g = std::max(0.0f, mul);
    if (std::abs(g - gananciaGrupo(grupo, s->id)) < 1e-6f) return;
    if (std::abs(g - 1.0f) < 1e-6f) s->gananciaGrupo.erase(grupo);
    else s->gananciaGrupo[grupo] = g;
    // El VCA es del grupo en la mezcla de ESA salida (cada fuente con su fader ahí).
    for (const auto& [nombre, f] : modelo()) {
        if (grupoDeAudio(nombre) != grupo) continue;
        if (m_.motor_) // sin audio no tiene nivel: no importa
            m_.motor_->ponerNivel(nombre, nivelAplicado(nombre, sourceVolume(nombre, s->id), *s), s->porOmision() ? std::string() : s->id);
        if (m_.cambioAudio_) m_.cambioAudio_(nombre, "volumen");
    }
}

float MezclaApp::gananciaGrupo(const std::string& grupo, const std::string& salida) const {
    const SalidaAire* s = m_.laSalida(salida);
    if (s == nullptr) return 1.0f;
    const auto it = s->gananciaGrupo.find(grupo);
    return it != s->gananciaGrupo.end() ? it->second : 1.0f;
}

float MezclaApp::sourceVolume(const std::string& name, const std::string& salida) const {
    auto it = modelo().find(name);
    if (it == modelo().end()) return 0.0f;
    const SalidaAire* s = m_.laSalida(salida);
    if (s == nullptr || s->porOmision()) return it->second.spec.volume;
    const auto v = s->volumen.find(name);
    return v != s->volumen.end() ? v->second : it->second.spec.volume;
}

bool MezclaApp::setSourceMuted(const std::string& name, bool muted, const std::string& salida) {
    auto it = modelo().find(name);
    if (it == modelo().end()) return false;
    SalidaAire* s = m_.laSalida(salida);
    if (s == nullptr) return false;
    if (!s->porOmision()) {
        const bool cambia = sourceMuted(name, s->id) != muted;
        s->mute[name] = muted;
        const bool ok = m_.motor_->ponerMute(name, muted, s->id).empty();
        if (cambia && m_.cambioAudio_) m_.cambioAudio_(name, "mute");
        return ok;
    }
    const bool cambia = it->second.spec.muted != muted;
    it->second.spec.muted = muted;
    const bool ok = m_.motor_->ponerMute(name, muted).empty();
    if (cambia && m_.cambioAudio_) m_.cambioAudio_(name, "mute");
    return ok;
}

bool MezclaApp::sourceMuted(const std::string& name, const std::string& salida) const {
    auto it = modelo().find(name);
    if (it == modelo().end()) return false;
    const SalidaAire* s = m_.laSalida(salida);
    if (s == nullptr || s->porOmision()) return it->second.spec.muted;
    const auto m = s->mute.find(name);
    return m != s->mute.end() ? m->second : it->second.spec.muted;
}

float MezclaApp::sourcePeakDb(const std::string& name) const {
    // Lo que entrega la entrada (después de su rack, vaya a donde vaya), como los
    // vúmetros del Mezclador. El del bus Programa marcaba -100 con audio si la
    // fuente no iba al Programa (un micrófono que sale solo por un parlante).
    if (m_.motor_)
        if (const auto niveles = m_.motor_->niveles())
            for (const auto& n : *niveles)
                if (!n.bus && n.id == name) return std::max(-100.0f, std::max(n.pico[0], n.pico[1]));
    const json& e = seccion(seccion(m_.estadoMotor(), "programa"), "entradas");
    if (!e.contains(name)) return -100.0f;
    return static_cast<float>(std::max(-100.0, e[name].value("pico_db", -100.0)));
}

bool MezclaApp::setSourceMonitoring(const std::string& name, bool on) {
    auto it = modelo().find(name);
    if (it == modelo().end()) return false;
    // La escucha fija: las marcadas suenan juntas (con un solo prendido, el motor solo la
    // marca: suena al soltarlo).
    const bool cambia = it->second.monitor != on;
    it->second.monitor = on;
    const std::string e = m_.motor_->ponerEscucha(name, on);
    if (!e.empty()) m_.setError(e);
    if (cambia && m_.cambioAudio_) m_.cambioAudio_(name, "escucha");
    return e.empty();
}

bool MezclaApp::sourceMonitoring(const std::string& name) const {
    auto it = modelo().find(name);
    return it != modelo().end() && (it->second.monitor || pfl_ == name);
}

bool MezclaApp::sourceEscuchaFija(const std::string& name) const {
    auto it = modelo().find(name);
    return it != modelo().end() && it->second.monitor;
}

std::string MezclaApp::aplicarEscucha() {
    const auto idMotor = [this](const std::string& salida) {
        const SalidaAire* s = m_.laSalida(salida);
        return s == nullptr || s->porOmision() ? std::string() : s->id;
    };
    // Sin solo, la selección (las marcadas) con el fader de su salida; sin ninguna marcada
    // el motor la apaga.
    if (pfl_.empty()) return m_.motor_->escuchar("seleccion", "", idMotor(salidaEscucha_));
    return m_.motor_->escuchar(pflModo_, pfl_, pflModo_ == "afl" ? idMotor(pflSalida_) : std::string());
}

bool MezclaApp::setPfl(const std::string& name, const std::string& modo, const std::string& salida) {
    if (modo != "pfl" && modo != "afl") {
        m_.setError(ba::i18n::traducir("emision", "el solo de la escucha es «pfl» o «afl»"));
        return false;
    }
    if (!name.empty() && modelo().count(name) == 0) return false;
    const SalidaAire* s = m_.laSalida(salida);
    if (s == nullptr) return false;
    if (name == pfl_ && (name.empty() || (modo == pflModo_ && s->id == pflSalida_))) return true;
    const std::string antes = pfl_;
    pfl_ = name;
    pflModo_ = modo;
    pflSalida_ = s->id;
    // Uno solo a la vez: el nuevo reemplaza al anterior; soltarlo devuelve la escucha fija.
    const std::string e = aplicarEscucha();
    if (!e.empty()) {
        m_.setError(e);
        pfl_.clear();
        aplicarEscucha();
    }
    if (m_.cambioAudio_) {
        if (!antes.empty()) m_.cambioAudio_(antes, "escucha");
        if (!name.empty() && name != antes) m_.cambioAudio_(name, "escucha");
    }
    return e.empty();
}

bool MezclaApp::setSalidaEscucha(const std::string& salida) {
    const SalidaAire* s = m_.laSalida(salida);
    if (s == nullptr) return false;
    if (s->id == salidaEscucha_) return true;
    salidaEscucha_ = s->id;
    // Con un solo prendido queda para cuando se suelte.
    if (pfl_.empty()) {
        const std::string e = aplicarEscucha();
        if (!e.empty()) {
            m_.setError(e);
            return false;
        }
    }
    return true;
}

bool MezclaApp::setSourceSyncOffset(const std::string& name, int ms) {
    auto it = modelo().find(name);
    if (it == modelo().end()) {
        m_.setError(ba::i18n::llenar(ba::i18n::traducir("emision", "no existe la fuente '%1'"), {name}));
        return false;
    }
    const int v = std::clamp(ms, -5000, 5000);
    const bool cambia = it->second.syncMs != v;
    it->second.syncMs = v;
    json ajustes;
    if (!m_.escenas_->ajustesMotor(it->second.spec, ajustes)) return false;
    ajustes["desfase_audio"] = it->second.syncMs; // solo el audio; cambia andando (el offset del pad)
    const bool ok = m_.motor_->actualizarEntrada(name, ajustes).empty();
    if (cambia && m_.cambioAudio_) m_.cambioAudio_(name, "retardo");
    return ok;
}

int MezclaApp::sourceSyncOffset(const std::string& name) const {
    auto it = modelo().find(name);
    return it != modelo().end() ? it->second.syncMs : 0;
}

bool MezclaApp::setSourcePistas(const std::string& name, const std::vector<int>& pistas) {
    auto it = modelo().find(name);
    if (it == modelo().end()) return false;
    const std::string e = m_.motor_->ponerPistas(name, pistas);
    if (!e.empty()) {
        m_.setError(e);
        return false;
    }
    const bool cambia = it->second.pistas != pistas;
    it->second.pistas = pistas;
    if (cambia && m_.cambioAudio_) m_.cambioAudio_(name, "pistas");
    return true;
}

std::vector<int> MezclaApp::sourcePistas(const std::string& name) const {
    auto it = modelo().find(name);
    return it != modelo().end() ? it->second.pistas : std::vector<int>{};
}

void MezclaApp::setRetornoInvitado(int slot, const std::string& modo) {
    retornos_[slot] = modo;
    const std::string name = "Invitado " + std::to_string(slot) + " (audio)";
    if (m_.motor_ && modelo().count(name) > 0) m_.motor_->retorno(slot, modo, name);
}

std::string MezclaApp::retornoInvitado(int slot) const {
    const auto it = retornos_.find(slot);
    return it != retornos_.end() ? it->second : "mix_minus";
}

audio::LoudnessPrograma& MezclaApp::loudness(const std::string& salida) {
    const SalidaAire* s = m_.laSalida(salida);
    return (s != nullptr ? *s : m_.porOmision()).medidas->loudness;
}

audio::LoudnessPrograma* MezclaApp::loudnessDe(const std::string& salida) {
    const SalidaAire* s = std::as_const(m_).laSalida(salida); // sin anotar un error: preguntar no falla
    return s != nullptr ? &s->medidas->loudness : nullptr;
}

void MezclaApp::aplicarLoudness() {
    if (!m_.motor_) return;
    for (const auto& s : m_.lasSalidas()) {
        const audio::LoudnessPrograma::Objetivo ob = s->medidas->loudness.objetivo();
        double techo = ob.tp;
        // AES TD1004: un códec con pérdida a poca tasa reconstruye picos por encima de
        // lo que entró; con AAC por debajo de 128 kbps, 1 dB más de margen.
        const int kbps = s->kbpsAac;
        if (kbps > 0 && kbps < 128) techo = std::min(techo, -2.0);
        // En la cola del motor: un error vuelve por alFallar.
        m_.motor_->ponerLoudness(ob.agc, ob.lufs, techo, s->porOmision() ? std::string() : s->id);
    }
}

int MezclaApp::pistasGrabacionMask() const {
    return m_.porOmision().pistasGrabacionMask;
}

} // namespace ba::app
