// ============================================================================
// src/app/emision/MezclaApp.hpp — el audio de cada fuente en la mezcla de cada salida
//
// Pieza de BroadcastEngine (ver BroadcastEngine.hpp). El fader y el mute de cada fuente en
// cada salida, los VCA de los grupos, la escucha del operador (la fija y el solo PFL/AFL),
// el retardo y las pistas de cada fuente, lo que oye cada invitado (su retorno) y el
// loudness de cada salida (el objetivo y el techo, que baja con AAC por debajo de 128 kbps).
//
// Hilos: EL DUEÑO DEL MOTOR (con el turno, que toma la fachada). El AAC con que transmite
// cada salida lo cuenta el hilo de salidas por el buzón (alAac, ya en el dueño).
// ============================================================================
#pragma once

#include <map>
#include <string>
#include <vector>

#include "emision/EscenasYFuentes.hpp"

namespace ba::app {

class MezclaApp {
   public:
    explicit MezclaApp(BroadcastEngine& m) : m_(m) {}

    std::vector<std::string> audioSources() const;
    bool setSourceVolume(const std::string& name, float mul, const std::string& salida);
    float sourceVolume(const std::string& name, const std::string& salida) const;
    static std::string grupoDeAudio(const std::string& nombre);
    /// Lo que va al control de nivel del motor en la salida `s`: el fader de la fuente por la
    /// ganancia de su grupo en esa salida.
    float nivelAplicado(const std::string& name, float volumen, const SalidaAire& s) const;
    void setGananciaGrupo(const std::string& grupo, float mul, const std::string& salida);
    float gananciaGrupo(const std::string& grupo, const std::string& salida) const;
    bool setSourceMuted(const std::string& name, bool muted, const std::string& salida);
    bool sourceMuted(const std::string& name, const std::string& salida) const;
    float sourcePeakDb(const std::string& name) const;
    bool setSourceMonitoring(const std::string& name, bool on);
    bool sourceMonitoring(const std::string& name) const;
    bool sourceEscuchaFija(const std::string& name) const;
    bool setPfl(const std::string& name, const std::string& modo, const std::string& salida);
    std::string pfl() const { return pfl_; }
    std::string pflModo() const { return pfl_.empty() ? std::string() : pflModo_; }
    bool setSalidaEscucha(const std::string& salida);
    std::string salidaEscucha() const { return salidaEscucha_; }
    bool setSourceSyncOffset(const std::string& name, int ms);
    int sourceSyncOffset(const std::string& name) const;
    bool setSourcePistas(const std::string& name, const std::vector<int>& pistas);
    std::vector<int> sourcePistas(const std::string& name) const;
    void setRetornoInvitado(int slot, const std::string& modo);
    std::string retornoInvitado(int slot) const;
    const std::map<int, std::string>& retornos() const { return retornos_; }

    // --- El loudness de cada salida -------------------------------------------------------
    audio::LoudnessPrograma& loudness(const std::string& salida);
    audio::LoudnessPrograma* loudnessDe(const std::string& salida);
    void aplicarLoudness();
    int pistasGrabacionMask() const;
    /// El hilo de salidas transmite con AAC a `kbps` por `s` (el más chico hasta ahora).
    void alAac(SalidaAire& s, int kbps);

    // --- Para las otras piezas ------------------------------------------------------------
    /// Lo que pide la app a la escucha del motor: el solo, o la escucha fija con su salida.
    std::string aplicarEscucha();
    /// La escucha como estaba en un motor nuevo (un solo de una fuente que ya no está, afuera).
    void reponerEscucha();
    /// Se fue una fuente / cambió de nombre: el solo la sigue.
    void olvidarFuente(const std::string& nombre);
    void renombrarFuente(const std::string& de, const std::string& a);
    /// Se borró una salida: la escucha que seguía su fader vuelve a la por omisión.
    void alBorrarSalida(const std::string& id);

   private:
    std::map<std::string, EscenasYFuentes::Fuente>& modelo() const;

    BroadcastEngine& m_;
    std::string pfl_;              ///< el canal en solo ("" = ninguno); no se guarda
    std::string pflModo_ = "pfl";  ///< el solo: "pfl" o "afl"
    std::string pflSalida_ = BroadcastEngine::kSalidaPorOmision;     ///< la salida del fader del AFL
    std::string salidaEscucha_ = BroadcastEngine::kSalidaPorOmision; ///< la salida del fader de la escucha fija
    std::map<int, std::string> retornos_; ///< slot → modo
};

} // namespace ba::app
