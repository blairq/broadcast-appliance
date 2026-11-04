// ============================================================================
// src/app/arranque/AireDurable.hpp — lo que está al aire sobrevive a un corte
//
// Pieza de AppRuntime (ver RuntimeConfig.hpp). EL DOCUMENTO DE AIRE (F3-04,
// emision/DocumentoAire.hpp): la sección de las salidas (qué sale de cada una, su escena, el
// negro y la placa, y si el cierre fue ordenado), lo que dejó la corrida anterior y VOLVER
// COMO ESTABA en un solo lugar (restaurarAire); el corte de luz con poca batería (F1-11), el
// rearme por la GPU perdida (F3-09) y los avisos de persistencia que levanta la alarma.
//
// Hilos:
//   - EL DUEÑO (con el turno): poner la sección de las salidas (writeOnAirState), volver como
//     estaba, la energía y el rearme. El documento lo VUELCA su propio dueño (el plano de
//     control, ticControl; ver setDuenoDelAire): el documento tiene su candado.
//   - DESDE CUALQUIER HILO: avisosPersistencia y avisarPersistencia (avisosMu_).
//   - Lo que se lee después del arranque (aireAnterior, corteAnterior, lineaDesconocida,
//     placaRestaurada) no cambia más salvo en el rearme por la GPU (en el dueño).
// ============================================================================
#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "emision/DocumentoAire.hpp"

namespace ba::app {

class AppRuntime;

class AireDurable {
   public:
    explicit AireDurable(AppRuntime& rt) : rt_(rt) {}

    /// Lee el documento de aire (dos generaciones; la versión 1 con placa.json y
    /// linea-aire.json aparte, también) al arrancar: deja aireAnterior_ y corteAnterior_, y
    /// si está dañado lo avisa (F1-03).
    void leerAireAnterior();
    /// VOLVER COMO ESTABA, en un solo lugar (F3-04): la escena de cada salida, el negro y
    /// la placa, y lo que salía (restoreOutputs).
    void restaurarAire();
    /// Ya volvió como estaba: desde acá el documento se escribe.
    void listo();
    /// La sección de las salidas del documento de aire. No escribe nada hasta listo().
    void writeOnAirState();
    /// Algo de lo que sale cambió por fuera del runtime (el negro, la placa).
    void aireCambio();
    /// Retoma las grabaciones pendientes de un corte (ver AppRuntime::retomarGrabaciones).
    void retomarGrabaciones();
    /// El cierre ordenado (stop): nada que retomar, salvo en un corte de luz con poca batería.
    void cerrar();
    /// El documento, a disco (en el hilo de su dueño).
    void volcar() { documento_.volcar(); }
    void setDuenoDelAire(std::function<void()> despertar) { documento_.setDueno(std::move(despertar)); }
    /// F3-09: la GPU del video se perdió (motor/GpuPerdida.hpp; lo mira la vuelta del motor).
    void rearmarPorGpu();

    // --- El corte de luz con poca batería (F1-11) ----------------------------------------
    std::vector<std::string> cerrarGrabacionesPorEnergia();
    void retomarGrabacionesPorEnergia();
    bool emergenciaEnergia() const { return emergenciaEnergia_; }

    // --- Lo de la corrida anterior (ver AppRuntime) --------------------------------------
    bool corteAnterior() const { return corteAnterior_; }
    const salidas::AlAire& aireAnterior() const { return aireAnterior_; }
    bool lineaDesconocida() const { return lineaDesconocida_; }
    bool placaRestaurada() const { return placaRestaurada_; }
    salidas::DocumentoAire& documento() { return documento_; }

    std::vector<std::string> avisosPersistencia() const;
    void avisarPersistencia(const std::string& texto);

   private:
    /// Lo que sale ahora de cada salida (escena, transmisión, destinos, grabación, negro y
    /// placa): lo que escribe writeOnAirState y lo que retoma el rearme por la GPU.
    salidas::AlAire aireActual() const;
    /// El negro y la placa de cada salida como dice `aire`. true si había alguno.
    bool restaurarPlacas(const salidas::AlAire& aire);
    /// Retoma lo que transmitían y grababan TODAS las salidas (F1-04). `deEstaCorrida`: lo
    /// que retoma es de esta misma corrida (el rearme por la GPU), también con --test.
    void restoreOutputs(bool deEstaCorrida = false);

    AppRuntime& rt_;
    salidas::AlAire aireAnterior_;
    salidas::DocumentoAire documento_;
    bool aireListo_ = false;        ///< ya volvió como estaba: el documento se puede escribir
    bool lineaDesconocida_ = false; ///< ver AppRuntime::lineaDesconocida
    bool placaRestaurada_ = false;
    mutable std::mutex avisosMu_;
    std::vector<std::string> avisosPersistencia_;
    bool corteAnterior_ = false;
    bool cerrando_ = false;              ///< stop(): al-aire.json dice «cerrado»
    bool emergenciaEnergia_ = false;     ///< cerrarGrabacionesPorEnergia: un cierre es un corte
    bool grabacionesPorRetomar_ = false; ///< restoreOutputs: esperan la carpeta del evento
};

} // namespace ba::app
