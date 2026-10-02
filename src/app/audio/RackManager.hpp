// ============================================================================
// src/app/audio/RackManager.hpp — racks de audio de todas las fuentes
//
// Cada fuente con audio puede tener su rack (AudioRack) montado como un
// filtro de audio de OBS ("ba_audio_rack"): OBS le pasa el audio de la
// fuente en su hilo de audio y el rack lo devuelve procesado al mezclador.
//
// El manager:
//   - abre los plugins LSP del bundle (plugins/lsp/lsp-plugins.clap);
//   - da los servicios del hilo principal que piden los plugins (timers, fds,
//     "correr en el principal") con Qt;
//   - guarda el grafo y el estado de cada rack en Config/audio-racks.json
//     (por nombre de fuente) y los restaura al arrancar;
//   - lista las fuentes con audio (tambien videos con sonido, invitados...);
//   - la ESCUCHA: el nodo "monitor" de un rack se manda a una salida de audio
//     del sistema con una fuente privada de OBS en modo MONITOR_ONLY (nunca a
//     la mezcla). El dispositivo es uno solo para toda la app (asi lo maneja
//     libobs) y se guarda en Config/audio-escucha.json.
// Todo en el hilo principal salvo el procesamiento (hilo de audio de OBS).
// ============================================================================
#pragma once

#include <QObject>
#include <QTimer>

#include <set>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "audio/AudioRack.hpp"

namespace ba::app::audio {

class QtHostServices;

class RackManager : public QObject {
    Q_OBJECT
   public:
    using json = nlohmann::json;

    RackManager(std::filesystem::path dataRoot, QObject* parent = nullptr);
    ~RackManager() override;

    /// Los plugins LSP (se abren la primera vez). nullptr + error si no estan.
    std::shared_ptr<ClapLibrary> library(std::string* error = nullptr);
    /// Fuentes con audio: [{nombre, tipo, rack: bool, modulos: n}].
    json audioSources() const;
    /// El rack de la fuente; con `create` lo monta si no tiene.
    AudioRack* rack(const std::string& source, bool create, std::string* error = nullptr);
    /// Quita el rack de la fuente (el audio vuelve a pasar directo).
    bool removeRack(const std::string& source);

    /// Salidas de audio del equipo agrupadas por dispositivo:
    /// [{dispositivo, salidas: [{id, nombre}]}].
    json outputDevices() const;
    /// Manda el audio del rack de `source` a la salida `device`. Devuelve el
    /// error o "". OJO: el motor reproduce por UNA salida a la vez.
    std::string setOutput(const std::string& source, const std::string& device);
    /// Los auriculares del operador: la salida de escucha de toda la app (la de
    /// los racks con nodo de salida y la de la voz del asistente). "" = la del
    /// sistema. Devuelve el error o "".
    std::string setAuriculares(const std::string& device);
    /// {id, nombre} de la salida elegida ("" si es la del sistema).
    json auriculares() const { return json{{"id", monitorId_}, {"nombre", monitorName_}}; }

    // --- Voz a texto ----------------------------------------------------------
    /// Pone (o cambia de modo) el nodo de voz a texto en el rack de `source`,
    /// montando el rack si no tiene. `modo`: "dictado" o "continuo".
    std::string setTexto(const std::string& source, const std::string& modo);
    /// Las fuentes con nodo de voz a texto: [{fuente, modo}].
    json fuentesConTexto() const;
    /// A dónde va el audio de esos nodos: (fuente, modo 1=dictado 2=continuo,
    /// mono, frames, frecuencia). Se llama EN EL HILO DE AUDIO de OBS: tiene
    /// que copiar y volver. Una sola para toda la app (la Voz).
    using TextoSink = std::function<void(const std::string& fuente, int modo, const float* mono, uint32_t n,
                                         uint32_t frecuencia)>;
    static void setTextoSink(TextoSink sink);
    // --- Plantillas -----------------------------------------------------------
    // Un rack armado a mano (compuerta, compresor, de-esser, sus parametros y
    // sus cables) es media hora de trabajo. Guardarlo con nombre permite
    // aplicarselo a otra fuente en un clic, y marcarlo como PREDETERMINADO de
    // un tipo de fuente hace que los invitados lo tomen solos al conectarse,
    // que es donde mas se nota: nadie va a armarle el rack a mano a cada uno
    // mientras el evento empieza.
    //
    // Viven en Config/audio-plantillas.json, o sea que viajan con la carpeta
    // portable.

    /// [{nombre, modulos, paraTipo}] (paraTipo vacio = no es predeterminada).
    json plantillas() const;
    /// Guarda el rack de `source` con ese nombre. Si ya existe, lo reemplaza.
    /// Devuelve el error o "".
    std::string guardarPlantilla(const std::string& source, const std::string& nombre);
    /// Le pone a `source` el rack de la plantilla, REEMPLAZANDO el que tenga.
    std::string aplicarPlantilla(const std::string& nombre, const std::string& source);
    std::string borrarPlantilla(const std::string& nombre);
    /// Marca (o desmarca, con `tipo` vacio) la plantilla como la que toman
    /// solas las fuentes nuevas de ese tipo de libobs.
    std::string plantillaPorDefecto(const std::string& nombre, const std::string& tipo);

    /// Tarjeta ENTRADA en el rack de `source`: trae el audio de `fuente`
    /// (después de su propio rack). Devuelve el id del nodo o "" con el error.
    std::string addEntrada(const std::string& source, const std::string& fuente, std::string* error);
    /// Cambia de qué fuente trae el audio una tarjeta de entrada.
    std::string setEntradaFuente(const std::string& source, const std::string& nodo, const std::string& fuente);
    /// "" si el rack de `source` puede traer el audio de `fuente`; si no, por
    /// qué (no existe, es ella misma, o haría un lazo entre racks).
    std::string validarEntrada(const std::string& source, const std::string& fuente) const;
    /// Las fuentes de las que algún rack trae audio (la Matriz abre las
    /// entradas de placa que se usen así).
    std::set<std::string> fuentesDeEntradas() const;

    /// Algo cambio en un rack: se guarda en un momento.
    void markDirty();
    /// Guarda ya (tambien al salir).
    void saveNow();
    /// Restaura los racks guardados de las fuentes que existan.
    void restore();
    /// «Restablecer interfaz LSP»: cierra las ventanas LSP abiertas y vuelve la
    /// configuración global de LSP (la de su menú: zoom, escala del host,
    /// letra, zoom por plugin) a lo de fábrica, con una copia del archivo.
    /// Arregla las ventanas que no aparecen (más grandes que la pantalla).
    /// Devuelve qué hizo, o el error.
    std::string restablecerInterfazLsp(bool* ok = nullptr);
    double sampleRate() const;

    /// Servicios del hilo principal para los plugins.
    HostServices* services() const;
    /// Uso interno del filtro de OBS.
    struct Filter;
    void attach(Filter* f);
    void detach(Filter* f);

   signals:
    void changed();

   private:
    std::filesystem::path root_;
    QtHostServices* services_ = nullptr; // del proceso (ver el constructor)
    std::shared_ptr<ClapLibrary> lib_;
    std::string libError_;
    bool libTried_ = false;
    std::map<std::string, Filter*> filters_; // por fuente
    /// Prende o apaga la escucha de cada rack segun el dispositivo elegido.
    void syncMonitors();
    void saveMonitor() const;
    void restoreMonitor();

    QTimer saveTimer_;
    json plantillas_ = json::object(); ///< nombre -> {rack, paraTipo}
    void cargarPlantillas();
    void guardarPlantillas() const;
    /// La plantilla marcada para ese tipo de fuente, o vacio.
    std::string plantillaDeTipo(const std::string& tipo) const;
    std::string monitorId_;   // "" = sin escucha aparte
    std::string monitorName_; // el nombre que espera libobs junto al id
    json pending_;            // racks guardados de fuentes que todavia no existen

    /// La toma de audio de una tarjeta de entrada: el callback de captura de
    /// la fuente de origen empuja a su anillo; el rack lo saca.
    struct Toma;
    std::map<std::string, std::unique_ptr<Toma>> tomas_; ///< "<rack>\n<nodo>"
    /// Pone y saca las tomas según las tarjetas de entrada de todos los racks.
    void sincronizarEntradas();
    /// Monta los racks guardados cuya fuente apareció después (invitados que
    /// llegan, entradas virtuales y de placa que se crean tarde).
    void montarPendientes();
    QTimer revision_;
};

} // namespace ba::app::audio
