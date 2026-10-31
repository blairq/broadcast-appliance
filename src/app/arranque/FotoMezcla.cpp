// src/app/arranque/FotoMezcla.cpp — la parte Mezcla de la foto (AppRuntime, F3-03)
//
// Ver emision/FotoMotor.hpp («Mezcla») y FotoDelRuntime.cpp. Corre en el dueño con el turno:
// lee el audio de cada fuente del motor y lo deja como valor para la interfaz (el MixerModel,
// el Mezclador, la matriz, los racks). Los niveles van aparte (emision/NivelesMotor).

#include "arranque/AppRuntime.hpp"

#include <set>

namespace ba::app {

void AppRuntime::anotarCambioAudio(const std::string& fuente, const char* que) {
    // Lo llama el motor en el hilo que hizo el cambio (el dueño, casi siempre). Una fuente
    // que nace o se rehace con el mismo nombre cuenta un alta: la matriz le pone lo guardado.
    if (que != nullptr && std::string(que) == "alta") {
        const std::lock_guard<std::mutex> l(altasMu_);
        ++altasAudio_[fuente];
    }
    fotoSucia(FotoMotor::Mezcla);
}

void AppRuntime::armarMezcla(FotoMotor::MezclaAudio& m) {
    m = FotoMotor::MezclaAudio{};
    const std::vector<std::string> salidas = engine_.salidaIds();
    m.porOmision = engine_.idDeSalida("");
    std::map<std::string, std::uint64_t> altas;
    {
        const std::lock_guard<std::mutex> l(altasMu_);
        altas = altasAudio_;
    }
    std::map<std::string, int> porGrupo; // cuántas fuentes tiene cada grupo
    for (const std::string& n : engine_.audioSources()) {
        FotoMotor::CanalAudio c;
        c.nombre = n;
        c.tipo = engine_.sourceTypeId(n);
        c.ajustes = nlohmann::json::parse(engine_.sourceSettingsJson(n), nullptr, false);
        if (!c.ajustes.is_object()) c.ajustes = nlohmann::json::object();
        c.grupo = BroadcastEngine::grupoDeAudio(n);
        for (const std::string& s : salidas) {
            c.volumen[s] = engine_.sourceVolume(n, s);
            c.mudo[s] = engine_.sourceMuted(n, s);
        }
        c.retardoMs = engine_.sourceSyncOffset(n);
        for (int p : engine_.sourcePistas(n))
            if (p >= 1 && p <= 6) c.pistas |= 1u << (p - 1);
        c.escuchaFija = engine_.sourceEscuchaFija(n);
        c.escucha = engine_.sourceMonitoring(n);
        if (const auto it = altas.find(n); it != altas.end()) c.altas = it->second;
        ++porGrupo[c.grupo];
        m.canales.push_back(std::move(c));
    }
    // El VCA de los grupos que hay (dos o más fuentes con la misma clave), en cada salida.
    for (const std::string& s : salidas)
        for (const auto& [g, n] : porGrupo)
            if (n >= 2) m.grupos[s][g] = engine_.gananciaGrupo(g, s);
    m.pfl = engine_.pfl();
    m.pflModo = engine_.pflModo();
    m.salidaEscucha = engine_.salidaEscucha();
    m.balistica = cfg_.balistica;
    m.pistasGrabacion = engine_.pistasGrabacionMask();
    m.midiendoSincro = engine_.midiendoSincro();
}

} // namespace ba::app
