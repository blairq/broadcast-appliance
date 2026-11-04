// ============================================================================
// src/app/arranque/ServicioSalidas.cpp — ver ServicioSalidas.hpp
// ============================================================================

#include "arranque/ServicioSalidas.hpp"

#include "arranque/AireDurable.hpp"
#include "arranque/RuntimeAire.hpp"
#include "arranque/ArchivoConfig.hpp"
#include "arranque/ServicioEmision.hpp"
#include "diag/Registro.hpp"
#include "emision/SalidasConfig.hpp"
#include "i18n/I18nTexto.hpp"
#include "persist/ArchivoConfig.hpp"
#include "security/CredentialStore.hpp"

namespace ba::app {

using Cambio = AppRuntime::Cambio;

std::filesystem::path ServicioSalidas::archivoSalidas() { return ba::persist::archivoConfig("salidas.json"); }

void ServicioSalidas::guardarGrupo(const std::string& salida, const std::string& grupo, float ganancia) {
    salidas::Lista l = salidas::leer(config::leerObjeto(archivoSalidas()));
    if (salidas::ponerGrupo(l, salida, grupo, ganancia)) config::guardar(archivoSalidas(), salidas::aJson(l));
}

audio::ObjetivoLoudness ServicioSalidas::objetivoLoudness() {
    return audio::ObjetivoLoudness(
        rt_.root_, [this](const std::string& salida) { return rt_.engine_->loudnessDe(salida); }, [this]() { rt_.engine_->aplicarLoudness(); });
}

void ServicioSalidas::crearSalidasGuardadas() {
    bool alguna = false;
    for (const auto& s : salidas::leer(config::leerObjeto(archivoSalidas())).salidas) {
        if (rt_.engine_->existeSalida(s.id)) continue;
        if (!rt_.engine_->crearSalida(s.id, s.nombre)) {
            ba::diag::registro().linea(ba::diag::Nivel::Error, "salidas", 0,
                                       ba::i18n::llenar(ba::i18n::traducir("salidas", "la salida «%1» no arrancó: %2"),
                                                        {s.id, rt_.engine_->lastError()}));
            continue;
        }
        // Su mezcla como quedó (arrancó con la de la por omisión). Una fuente que ya no
        // está no tiene fader: se saltea (queda anotada por si vuelve).
        for (const auto& [fuente, m] : s.mezcla) {
            rt_.engine_->setSourceVolume(fuente, static_cast<float>(m.volumen), s.id);
            rt_.engine_->setSourceMuted(fuente, m.mute, s.id);
        }
        for (const auto& [grupo, g] : s.grupos) rt_.engine_->setGananciaGrupo(grupo, static_cast<float>(g), s.id);
        alguna = true;
    }
    if (alguna) objetivoLoudness().cargar(); // el objetivo de loudness que guardó cada una
}

EncoderConfig ServicioSalidas::codificadorDe(const std::string& salida) const {
    EncoderConfig e = rt_.ajustesAire().encoder;
    const std::string id = rt_.engine_->idDeSalida(salida);
    if (id.empty() || id == salidas::kPorOmision) return e;
    const salidas::Lista l = salidas::leer(config::leerObjeto(archivoSalidas()));
    if (const salidas::Salida* s = l.buscar(id); s != nullptr && !s->codificador.empty()) e.videoEncoder = s->codificador;
    return e;
}

void ServicioSalidas::guardarMando(const std::string& salida, const std::string& fuente) {
    // El fader y el mute de otra salida: en Config/salidas.json (encolado: un arrastre
    // de fader escribe lo último, no cada paso).
    salidas::Lista l = salidas::leer(config::leerObjeto(archivoSalidas()));
    if (salidas::ponerMando(l, salida, fuente, {rt_.engine_->sourceVolume(fuente, salida), rt_.engine_->sourceMuted(fuente, salida)}))
        config::guardar(archivoSalidas(), salidas::aJson(l));
}

nlohmann::json ServicioSalidas::salidas() const {
    const salidas::Lista l = salidas::leer(config::leerObjeto(archivoSalidas()));
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& s : l.salidas)
        arr.push_back({{"id", s.id},
                       {"nombre", s.nombre},
                       {"color", s.color},
                       {"orden", s.orden},
                       {"por_omision", s.id == salidas::kPorOmision},
                       {"andando", rt_.engine_->existeSalida(s.id)},
                       {"escena", rt_.engine_->currentScene(s.id)},
                       {"codificador", s.codificador}});
    return {{"tope", l.tope}, {"salidas", arr}};
}

std::string ServicioSalidas::crearSalida(const std::string& id, const std::string& nombre, const std::string& color) {
    salidas::Lista l = salidas::leer(config::leerObjeto(archivoSalidas()));
    if (std::string e = salidas::agregar(l, {id, nombre, color, 0, {}, {}}); !e.empty()) return e;
    // Que la máquina dé abasto lo dice sistema_recursos: el tope es el límite que puso el operador.
    if (!rt_.engine_->crearSalida(id, l.buscar(id)->nombre)) return rt_.engine_->lastError();
    if (!config::guardar(archivoSalidas(), salidas::aJson(l)))
        ba::diag::registro().linea(ba::diag::Nivel::Aviso, "salidas", 0,
                                   ba::i18n::traducir("salidas", "no se pudo guardar Config/salidas.json"));
    ba::diag::registro().linea(ba::diag::Nivel::Info, "salidas", 0,
                               ba::i18n::llenar(ba::i18n::traducir("salidas", "salida nueva: «%1»"), {id}));
    rt_.aire_->writeOnAirState();
    publicarSalidas(rt_.engine_->status());
    rt_.avisar(Cambio::Salidas);
    return {};
}

std::string ServicioSalidas::editarSalida(const std::string& id, const nlohmann::json& cambios) {
    salidas::Lista l = salidas::leer(config::leerObjeto(archivoSalidas()));
    if (std::string e = salidas::editar(l, id, cambios); !e.empty()) return e;
    config::guardar(archivoSalidas(), salidas::aJson(l));
    publicarSalidas(rt_.engine_->status());
    rt_.avisar(Cambio::Salidas);
    return {};
}

std::string ServicioSalidas::borrarSalida(const std::string& id, const std::string& susDestinos) {
    salidas::Lista l = salidas::leer(config::leerObjeto(archivoSalidas()));
    // Sus destinos: a otra salida o borrados, lo que se decidió (antes de tocar nada).
    nlohmann::json destinos = ServicioEmision::leerDestinos();
    std::vector<std::string> borrados;
    std::string aDonde = susDestinos; // «borrar», o la otra salida (si existe, por su id)
    if (!aDonde.empty() && aDonde != "borrar")
        if (const std::string r = rt_.engine_->idDeSalida(aDonde); !r.empty()) aDonde = r;
    if (std::string e = salidas::destinosDeBorrada(destinos, l, id, aDonde, &borrados); !e.empty()) return e;
    if (std::string e = salidas::quitar(l, id); !e.empty()) return e;
    // Lo que sale de ella (transmisiones, grabación, repetición, limpia) se para con ella.
    if (rt_.engine_->existeSalida(id) && !rt_.engine_->borrarSalida(id)) return rt_.engine_->lastError();
    objetivoLoudness().olvidar(id);
    config::guardar(archivoSalidas(), salidas::aJson(l));
    // Lo suyo (y los destinos que salían con ella, ya cortados) no vuelve tras un corte.
    rt_.emision_->pedidos().olvidar(id);
    for (const auto& b : borrados) ba::security::CredentialStore::bundle().remove("stream-" + b);
    ServicioEmision::escribirDestinos(destinos);
    ba::diag::registro().linea(ba::diag::Nivel::Info, "salidas", 0,
                               ba::i18n::llenar(ba::i18n::traducir("salidas", "salida borrada: «%1»"), {id}));
    rt_.aire_->writeOnAirState();
    publicarSalidas(rt_.engine_->status());
    rt_.avisar(Cambio::Salidas);
    return {};
}

std::string ServicioSalidas::setTopeSalidas(int tope) {
    if (tope < 1 || tope > salidas::kTopeMaximo)
        return ba::i18n::llenar(ba::i18n::traducir("salidas", "el tope va de 1 a %1"), {std::to_string(salidas::kTopeMaximo)});
    salidas::Lista l = salidas::leer(config::leerObjeto(archivoSalidas()));
    if (tope < static_cast<int>(l.salidas.size()))
        return ba::i18n::llenar(ba::i18n::traducir("salidas", "ya hay %1 salidas: borrá alguna antes de bajar el tope"),
                                {std::to_string(l.salidas.size())});
    l.tope = tope;
    config::guardar(archivoSalidas(), salidas::aJson(l));
    rt_.avisar(Cambio::Salidas);
    return {};
}

void ServicioSalidas::publicarSalidas(const EngineStatus& st) {
    // Qué salidas hay, para quien las pida desde otro hilo (el MCP valida `salida` con
    // esto en el hilo de la herramienta, sin tocar el motor).
    nlohmann::json l = nlohmann::json::array();
    for (const auto& s : st.salidas) l.push_back({{"id", s.id}, {"nombre", s.nombre}, {"por_omision", s.porOmision}});
    std::lock_guard<std::mutex> g(salidasMu_);
    salidasPublicadas_ = std::move(l);
}

nlohmann::json ServicioSalidas::salidasPublicadas() const {
    std::lock_guard<std::mutex> g(salidasMu_);
    return salidasPublicadas_;
}

} // namespace ba::app
