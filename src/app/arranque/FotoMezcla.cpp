// src/app/arranque/FotoMezcla.cpp — la parte Mezcla de la foto (AppRuntime, F3-03)
//
// Ver emision/FotoMotor.hpp («Mezcla») y PublicadorFoto.hpp. Corre en el dueño con el turno:
// lee el audio de cada fuente del motor y lo deja como valor para la interfaz (el MixerModel,
// el Mezclador, la matriz, los racks). Los niveles van aparte (emision/NivelesMotor).

#include "arranque/PublicadorFoto.hpp"
#include "arranque/RuntimeMotor.hpp"
#include <set>

namespace ba::app {

void PublicadorFoto::anotarCambioAudio(const std::string& fuente, const char* que) {
    // Lo llama el motor en el hilo que hizo el cambio (el dueño, casi siempre). Una fuente
    // que nace o se rehace con el mismo nombre cuenta un alta: la matriz le pone lo guardado.
    // Un alta o una baja («baja»: EscenasYFuentes::soltarSiHuerfana) cambia además la lista
    // de fuentes, que se rearma con Mezcla (FotoMotor::kFuentes).
    if (que != nullptr && std::string(que) == "alta") {
        const std::lock_guard<std::mutex> l(altasMu_);
        ++altasAudio_[fuente];
    }
    fotoSucia(FotoMotor::Mezcla);
}

void PublicadorFoto::seguirCanalesDelMotor() {
    // Qué fuentes tienen audio lo dice el motor en su foto (BroadcastEngine::audioSources),
    // que sale una vuelta después de armar una entrada: el aviso de alta llega antes y la
    // Mezcla se rearmaba sin ella. Sin esto, un micrófono recién agregado no tenía canal (ni
    // rack: «no tiene audio») hasta que otra cosa tocara la mezcla.
    if (rt_.engine_->audioSources() != trabajo_.mezcla.nombres()) marcar(FotoMotor::Mezcla);
}

void PublicadorFoto::armarMezcla(FotoMotor::MezclaAudio& m) {
    const BroadcastEngine& eng = *rt_.engine_;
    // Los canales de antes se guardan: si los nuevos son iguales, la foto sigue con esos (el
    // mismo valor compartido: ni se copian ni se recorren al compararlos).
    const Compartido<std::vector<FotoMotor::CanalAudio>> antes = m.canales;
    m = FotoMotor::MezclaAudio{};
    m.canales = antes;
    std::vector<FotoMotor::CanalAudio> canales;
    const std::vector<std::string> salidas = eng.salidaIds();
    m.porOmision = eng.idDeSalida("");
    std::map<std::string, std::uint64_t> altas;
    {
        const std::lock_guard<std::mutex> l(altasMu_);
        altas = altasAudio_;
    }
    std::map<std::string, int> porGrupo; // cuántas fuentes tiene cada grupo
    for (const std::string& n : eng.audioSources()) {
        FotoMotor::CanalAudio c;
        c.nombre = n;
        c.tipo = eng.sourceTypeId(n);
        c.ajustes = nlohmann::json::parse(eng.sourceSettingsJson(n), nullptr, false);
        if (!c.ajustes.is_object()) c.ajustes = nlohmann::json::object();
        c.grupo = BroadcastEngine::grupoDeAudio(n);
        for (const std::string& s : salidas) {
            c.volumen[s] = eng.sourceVolume(n, s);
            c.mudo[s] = eng.sourceMuted(n, s);
        }
        c.retardoMs = eng.sourceSyncOffset(n);
        for (int p : eng.sourcePistas(n))
            if (p >= 1 && p <= 6) c.pistas |= 1u << (p - 1);
        c.escuchaFija = eng.sourceEscuchaFija(n);
        c.escucha = eng.sourceMonitoring(n);
        if (const auto it = altas.find(n); it != altas.end()) c.altas = it->second;
        ++porGrupo[c.grupo];
        canales.push_back(std::move(c));
    }
    m.canales = std::move(canales);
    // El VCA de los grupos que hay (dos o más fuentes con la misma clave), en cada salida.
    for (const std::string& s : salidas)
        for (const auto& [g, n] : porGrupo)
            if (n >= 2) m.grupos[s][g] = eng.gananciaGrupo(g, s);
    m.pfl = eng.pfl();
    m.pflModo = eng.pflModo();
    m.salidaEscucha = eng.salidaEscucha();
    m.balistica = rt_.cfg_.balistica;
    m.pistasGrabacion = eng.pistasGrabacionMask();
    m.midiendoSincro = eng.midiendoSincro();
}

} // namespace ba::app
