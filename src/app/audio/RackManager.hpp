// ============================================================================
// src/app/audio/RackManager.hpp — racks de audio de todas las fuentes
//
// Cada fuente con audio puede tener su rack (AudioRack). Corre en la cadena de
// audio de su entrada del motor, como el elemento `barack` (audio/GstRack.hpp):
// el ajuste `rack` de la entrada lo pone al frente del audio.
//
// El manager:
//   - abre los plugins LSP del bundle (plugins/lsp/lsp-plugins.clap);
//   - da los servicios del hilo principal que piden los plugins (timers, fds,
//     "correr en el principal") con Qt;
//   - guarda el grafo y el estado de cada rack en Config/audio-racks.json
//     (por nombre de fuente) y los restaura al arrancar;
//   - lista las fuentes con audio (tambien videos con sonido, invitados...);
//   - la ESCUCHA: el nodo "monitor" de un rack sale por la salida de audio de
//     los auriculares del operador (un QAudioSink por rack; nunca a la mezcla).
//     El dispositivo es uno solo para toda la app (también la escucha del
//     motor) y se guarda en Config/audio-escucha.json.
// Todo en el hilo principal salvo el procesamiento (hilo de GStreamer).
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
    /// La vista general del audio (página Audio → Estudio; MCP audio_estudio):
    /// todas las fuentes, cada una en su franja con su cadena entera de izquierda
    /// a derecha —origen → módulos de su rack
    /// → filtros de su cadena de GStreamer (automixer…) → la mezcla (volumen,
    /// silencio)—, los cables entre franjas (las tarjetas «Entrada» que traen el
    /// audio de otra fuente) y una tarjeta por mesa del automixer con sus
    /// micrófonos. Con el formato del Patchbay: {nodos, cables, franjas}.
    json estudio() const;
    /// El rack de la fuente; con `create` lo monta si no tiene.
    AudioRack* rack(const std::string& source, bool create, std::string* error = nullptr);
    /// Los módulos que el vigía saltó en todos los racks ("<fuente>: <módulo>").
    std::vector<std::string> tomarSaltados();
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
    /// Pone (o cambia de modo) la voz a texto en el rack de `source`, montando
    /// el rack si no tiene: si ya tiene una, le cambia el modo a la primera.
    /// `modo`: "dictado" o "continuo".
    std::string setTexto(const std::string& source, const std::string& modo);
    /// Agrega OTRA voz a texto al rack de `source` con la salida de texto
    /// `flujo` ("" = el nombre de la fuente, o «<fuente> 2»...). Devuelve el
    /// id del nodo o "" con el error.
    std::string addTexto(const std::string& source, const std::string& modo, const std::string& flujo,
                         std::string* error);
    /// Cambia una voz a texto: {modo?, flujo?, salidas?: {archivo, rotulo}}.
    std::string setTextoOpciones(const std::string& source, const std::string& nodo, const json& cambios);
    /// Quita TODAS las voces a texto del rack de `source`.
    void quitarTextos(const std::string& source);
    /// Las salidas de texto (las voces a texto de todos los racks):
    /// [{flujo, fuente, nodo, modo, salidas}].
    json textos() const;
    /// La de ese nombre (null si no hay).
    json textoDe(const std::string& flujo) const;
    /// Las fuentes con voz a texto: [{fuente, modo, flujo}] (compatibilidad).
    json fuentesConTexto() const;
    /// A dónde va el audio de esos nodos: (flujo, modo 1=dictado 2=continuo,
    /// mono, frames, frecuencia). Se llama EN EL HILO DE AUDIO (GStreamer): tiene
    /// que copiar y volver. Una sola para toda la app (la Voz).
    using TextoSink = std::function<void(const std::string& flujo, int modo, const float* mono, uint32_t n,
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
    /// solas las fuentes nuevas de ese tipo.
    std::string plantillaPorDefecto(const std::string& nombre, const std::string& tipo);

    /// Tarjeta ENTRADA en el rack de `source`: trae el audio de `fuente`
    /// (después de su propio rack). Devuelve el id del nodo o "" con el error.
    std::string addEntrada(const std::string& source, const std::string& fuente, std::string* error);
    /// Los analizadores de LSP para mirar una fuente con su interfaz (el Mezclador):
    /// `cual` = espectro (Spectrum Analyzer x2), fase (Phase Detector: L contra R),
    /// osciloscopio (Oscilloscope x2), latencia (Latency Meter) o perfil (Profiler).
    /// Si la fuente ya lo tiene en su rack lo reusa; si no lo agrega al final (los
    /// analizadores dejan pasar el audio sin tocarlo). Abre su interfaz. "" si salió.
    std::string abrirAnalisisLsp(const std::string& source, const std::string& cual);
    /// Los que hay: [{id, nombre, para}] (para el menú del Mezclador y el MCP).
    static json analisisLsp();
    /// Cambia de qué fuente trae el audio una tarjeta de entrada.
    std::string setEntradaFuente(const std::string& source, const std::string& nodo,
                                 const std::string& fuente);
    /// "" si el rack de `source` puede traer el audio de `fuente`; si no, por
    /// qué (no existe, es ella misma, o haría un lazo entre racks).
    std::string validarEntrada(const std::string& source, const std::string& fuente) const;
    /// Las fuentes de las que algún rack trae audio (la Matriz abre las
    /// entradas de placa que se usen así).
    std::set<std::string> fuentesDeEntradas() const;
    /// Las fuentes cuyo rack tiene algo (un filtro, voz a texto, una voz…): la
    /// Matriz abre su entrada de placa aunque no vaya a ninguna salida.
    std::set<std::string> fuentesConRack() const;

    // --- Texto a voz (tarjeta «habla») ------------------------------------------
    /// Agrega una tarjeta TEXTO A VOZ al rack de `source`. Opciones: {flujo,
    /// voz, hablante, velocidad}. Devuelve el id o "" con el error.
    std::string addHabla(const std::string& source, const json& opciones, std::string* error);
    std::string setHablaOpciones(const std::string& source, const std::string& nodo, const json& cambios);
    /// Todas: [{fuente, nodo, flujo, voz, hablante, velocidad}].
    json hablas() const;
    /// El anillo por el que suena esa tarjeta (nulo si no existe).
    std::shared_ptr<anillo::Ring> anilloHabla(const std::string& source, const std::string& nodo) const;

    // --- Traducir (tarjeta «traducir», la usa agent/Traductor) -------------------
    /// Agrega una tarjeta TRADUCIR. `t`: {fuente?, desde, idioma_desde, a, nombre,
    /// rotulo, archivo, activa, espera_ms}. Sin `fuente` va al rack de la voz a
    /// texto que publica `desde` (o de la traducción que lo publica). Devuelve el
    /// id «<fuente>#<nodo>» o "" con el error.
    std::string addTraduccion(const json& t, std::string* error);
    /// `id`: «<fuente>#<nodo>».
    std::string setTraduccionOpciones(const std::string& id, const json& cambios);
    std::string quitarTraduccion(const std::string& id);
    /// Todas: [{id, fuente, nodo, desde, idioma_desde, a, nombre, rotulo, archivo, activa, espera_ms}].
    json traducciones() const;

    // --- Grabar lo que le llega a la tarjeta de salida -------------------------
    /// Prende o apaga la grabación de la tarjeta «Salida de audio» de `source`
    /// (Recordings/Audio/<fuente>-<fecha>.wav). "" o el error.
    std::string setGrabar(const std::string& source, bool grabar);
    /// {grabando, archivo, segundos, bytes, conCable} de la tarjeta de salida.
    json grabacion(const std::string& source) const;
    /// Carpeta de las grabaciones de audio.
    std::filesystem::path carpetaGrabaciones() const { return root_ / "Recordings" / "Audio"; }

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
    /// Uso interno (el rack de cada fuente).
    struct Filter;
    void attach(Filter* f);
    void detach(Filter* f);

   signals:
    void changed();
    /// Cambió algo de algún rack (cada markDirty: también al quitar una tarjeta a mano).
    void editado();

   private:
    std::filesystem::path root_;
    QtHostServices* services_ = nullptr; // del proceso (ver el constructor)
    std::shared_ptr<ClapLibrary> lib_;
    std::string libError_;
    bool libTried_ = false;
    std::map<std::string, Filter*> filters_; // por fuente
    /// Los racks de las entradas de GStreamer (los procesa `barack`): son de acá.
    std::map<std::string, std::shared_ptr<Filter>> gstFilters_;
    void ponerRackEnFuente(const std::string& fuente, const std::string& rack);
    void revisarRacksGst();
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
    std::string monitorName_; // el nombre del dispositivo (para mostrar)
    std::string dispositivoEscucha_; // con el que están abiertas las escuchas de los racks
    json pending_;            // racks guardados de fuentes que todavia no existen

    /// La toma de audio de una tarjeta de entrada: la toma del motor de la
    /// fuente de origen empuja a su anillo; el rack lo saca.
    struct Toma;
    std::map<std::string, std::unique_ptr<Toma>> tomas_;                ///< "<rack>\n<nodo>"
    std::map<std::string, std::shared_ptr<anillo::Ring>> anillosHabla_; ///< "<rack>\n<nodo>"
    /// Pone y saca las tomas según las tarjetas de entrada de todos los racks.
    void sincronizarEntradas();
    /// Monta los racks guardados cuya fuente apareció después (invitados que
    /// llegan, entradas virtuales y de placa que se crean tarde).
    void montarPendientes();
    /// Cada voz a texto con un nombre de salida, único en todo el sistema
    /// (los racks viejos no tenían: toman el de su fuente).
    void normalizarFlujos();
    /// Arranca o para los grabadores según la marca de cada tarjeta de salida.
    void sincronizarGrabaciones();
    bool flujoLibre(const std::string& flujo, const std::string& salvoRack,
                    const std::string& salvoNodo) const;
    QTimer revision_;
};

} // namespace ba::app::audio
