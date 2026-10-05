// ============================================================================
// src/app/plugins/Plugins.hpp — los plugins de Python (docs/PLUGINS.md)
//
// Un plugin es una carpeta en `Datos/Plugins/<id>/` (plugin.json + plugin.py +
// sus bibliotecas en _libs/): vive en los DATOS, no en el programa, así que
// sobrevive a las actualizaciones. Corre en su propio proceso con el Python
// que viene en el programa (Resources/python) y el SDK (plugins-python/sdk).
//
// Esta clase:
//   - los arranca, los vigila (si se cae, lo vuelve a arrancar con espera
//     creciente; si se cae 5 veces en un minuto, lo apaga y avisa) y los cierra;
//   - baja sus dependencias (pip, a la carpeta del plugin);
//   - habla el protocolo de control (JSON por línea, stdin/stdout del plugin):
//       entradas  -> fuentes ba_gst_fuente (bapluginsrc o el pipeline que pida)
//       tarjetas  -> se pueden poner en el grafo (gst `baplugin`) o como filtro
//                    de audio (`ba_plugin_audio`)
//       ptz       -> el backend «plugin» de esa entrada (ptz/PtzPlugin)
//       controles -> se piden con plugin_control
//       eventos   -> el bus: los escuchan otros plugins y quedan los últimos
//       llamar    -> cualquier herramienta del servidor MCP (lo que puede el
//                    asistente, lo puede un plugin)
//   - y le anota al módulo de video el puerto de medios de cada uno.
//
// UN PLUGIN NO PUEDE TRABAR LA APP: todo lo que viene de él llega por
// señales de QProcess (sin esperas), y todo pedido a él tiene timeout.
// ============================================================================
#pragma once

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace ba::app {
class AppRuntime;
class OperatorMessages;
namespace video {
class VideoGrafos;
}
namespace ptz {
class ControlCamaras;
}
} // namespace ba::app

namespace ba::app::plugins {

class Plugins : public QObject {
    Q_OBJECT
    // --- Para la interfaz (pages/PluginsPage.qml) ---------------------------------
    Q_PROPERTY(QVariantList lista READ listaQml NOTIFY cambio)
    Q_PROPERTY(QVariantList ejemplos READ ejemplosQml NOTIFY cambio)
    Q_PROPERTY(QVariantMap pruebas READ pruebasQml NOTIFY cambio)
    Q_PROPERTY(QVariantList eventosRecientes READ eventosQml NOTIFY eventosCambio)
    Q_PROPERTY(QVariantMap equipos READ equiposQml NOTIFY equiposCambio)
    Q_PROPERTY(bool buscandoEquipos READ buscandoEquipos NOTIFY equiposCambio)
    Q_PROPERTY(QString carpetaTexto READ carpetaTexto CONSTANT)
    Q_PROPERTY(bool hayPython READ hayPython CONSTANT)
   public:
    using json = nlohmann::json;
    /// Llamar una herramienta del servidor MCP (lo arma GuiMode, que tiene el servicio).
    using Llamador = std::function<void(const std::string& herramienta, const json& args,
                                        std::function<void(json resultado)> listo)>;

    Plugins(AppRuntime& rt, video::VideoGrafos* video, ptz::ControlCamaras* camaras,
            OperatorMessages* mensajes, QObject* parent = nullptr);
    ~Plugins() override;

    void setLlamador(Llamador l) { llamador_ = std::move(l); }
    /// Lo que publican los plugins va también al bus de la app (plugin/<id>/<canal>).
    void setAlPublicar(std::function<void(const std::string& tema, const json& datos)> fn) { alPublicar_ = std::move(fn); }
    /// Un evento de la app (bus): a los plugins que escuchan ese tema
    /// (@plugin.escuchar("aire/tally"), "audio/nivel/+", "alarma/#").
    void eventoApp(const std::string& tema, const json& datos);
    // --- Superficies de hardware (Stream Deck, MIDI, X-keys…) ---------------------
    // Un plugin registra un aparato como una botonera más (control/Botonera):
    // el operador le asigna perfil y página, el plugin recibe el estado de cada
    // celda para dibujarlo y avisa qué tecla apretaron.
    using AltaSuperficie = std::function<void(const std::string& id, const std::string& nombre, const json& capacidades)>;
    using BajaSuperficie = std::function<void(const std::string& id)>;
    using EntradaSuperficie = std::function<void(const std::string& id, const json& msg)>;
    void setSuperficies(AltaSuperficie alta, BajaSuperficie baja, EntradaSuperficie entrada) {
        altaSup_ = std::move(alta);
        bajaSup_ = std::move(baja);
        entradaSup_ = std::move(entrada);
    }
    /// El estado renderizado para el aparato `id` ("plugin:<plugin>/<aparato>").
    void superficieEstado(const std::string& id, const json& msg);
    /// Arranca los que estaban prendidos (al iniciar la app, cuando ya hay video).
    void arrancarGuardados();

    std::filesystem::path carpeta() const { return carpeta_; }
    std::filesystem::path carpetaDe(const std::string& id) const { return carpeta_ / id; }
    std::filesystem::path python() const;
    std::filesystem::path sdk() const;
    std::filesystem::path ejemplos() const;

    // --- Lo que ve el asistente -------------------------------------------------
    json listar() const;
    json ver(const std::string& id, const std::string& archivo) const;
    json ejemplosDisponibles() const;
    /// Escribe (crea o pisa) archivos del plugin `id`; `manifiesto` se mezcla con plugin.json.
    std::string escribir(const std::string& id, const json& archivos, const json& manifiesto, json* hecho);
    std::string desdeEjemplo(const std::string& ejemplo, const std::string& id);
    std::string borrar(const std::string& id);
    /// Un .zip con el plugin (sin _libs ni datos) en Datos/Exportado. Async: {archivo} o {error}.
    void exportar(const std::string& id, std::function<void(json)> listo);
    /// Instala un plugin desde un .zip exportado (o una carpeta). Async: {id} o {error}.
    void importar(const std::string& archivo, bool reemplazar, std::function<void(json)> listo);
    /// Baja las dependencias de plugin.json a _libs (si cambiaron). Async.
    void dependencias(const std::string& id,
                      std::function<void(std::string error, std::string salida)> listo);
    /// Corre el probador (sin la app) y devuelve su informe. Async.
    void probar(const std::string& id, double segundos, const json& controles, bool ptz,
                std::function<void(json)> listo);
    /// Prende o apaga (y lo deja así para la próxima vez). `listo` cuando terminó de declararse o falló.
    void activar(const std::string& id, bool activo, std::function<void(json estado)> listo);
    void reiniciar(const std::string& id, std::function<void(json estado)> listo);
    json estado(const std::string& id) const;
    std::string registro(const std::string& id, int lineas) const;
    /// Un pedido al plugin (control, ptz.*, info). `listo` con {ok, resultado|error}.
    void pedir(const std::string& id, const std::string& accion, const json& args,
               std::function<void(json)> listo, int timeoutMs = 10000);
    json eventos(const std::string& id, const std::string& canal, int cuantos) const;
    json secretosDe(const std::string& id) const;
    /// Lo que hay conectado: cámaras y micrófonos (GStreamer) + USB, HID y serie (Python).
    void dispositivos(std::function<void(json)> listo);
    void dispositivosPython(json gst, std::function<void(json)> listo);
    /// Cámaras y micrófonos que ve GStreamer, con el `gst` para un pipeline. Bloquea (~1 s): en otro hilo.
    static json camarasYMicrofonos();
    /// Corre `codigo` con el Python del programa, el SDK y las bibliotecas de herramientas (pyusb,
    /// hidapi, pyserial); `listo` con lo que imprima como JSON (o {error}).
    void correrPython(const QString& codigo, const QStringList& args, std::function<void(json)> listo);
    /// El panel del plugin (lo que declaró con plugin.panel): {titulo, controles} o null si no tiene.
    json panel(const std::string& id) const;
    /// Los valores del panel (lo del operador, guardado, y lo que mostró el plugin).
    json valores(const std::string& id) const;
    /// Mueve un control del panel (o toca un botón) como si fuera el operador. "" o el error.
    std::string ponerValor(const std::string& id, const std::string& clave, const json& valor);

    /// Pone una tarjeta de video del plugin en una entrada (grafo).
    std::string ponerTarjeta(const std::string& id, const std::string& tarjeta, const std::string& entrada,
                             const json& parametros, json* hecho);
    /// Pone una tarjeta de audio del plugin como filtro de una fuente.
    std::string ponerFiltroAudio(const std::string& id, const std::string& tarjeta, const std::string& fuente,
                                 const json& parametros, json* hecho);

    // --- Para la interfaz ----------------------------------------------------------
    QVariantList listaQml() const;
    QVariantList ejemplosQml() const;
    QVariantMap pruebasQml() const;
    QVariantList eventosQml() const;
    QVariantMap equiposQml() const;
    bool buscandoEquipos() const { return buscandoEquipos_; }
    QString carpetaTexto() const;
    bool hayPython() const;
    Q_INVOKABLE void prender(const QString& id, bool activo);
    Q_INVOKABLE void reiniciarUi(const QString& id);
    Q_INVOKABLE void probarUi(const QString& id);
    Q_INVOKABLE QString registroUi(const QString& id) const;
    Q_INVOKABLE QString copiarEjemplo(const QString& ejemplo, const QString& id);
    Q_INVOKABLE QString borrarUi(const QString& id);
    Q_INVOKABLE QString configTexto(const QString& id) const;
    /// Guarda `config` (JSON) en plugin.json y se la pasa al plugin si corre.
    Q_INVOKABLE QString guardarConfig(const QString& id, const QString& texto);
    /// Llama un control; la respuesta llega por `respuesta(id, control, texto, ok)`.
    Q_INVOKABLE void controlUi(const QString& id, const QString& control, const QString& argsJson);
    Q_INVOKABLE QString tarjetaUi(const QString& id, const QString& tarjeta, const QString& destino,
                                  bool audio);
    Q_INVOKABLE QStringList entradasVideo() const;
    Q_INVOKABLE QStringList fuentesConAudio() const;
    Q_INVOKABLE void buscarEquipos();
    Q_INVOKABLE void abrirCarpeta(const QString& id) const;
    Q_INVOKABLE QString codigo(const QString& id) const;
    /// Exportar / importar desde la interfaz: el resultado llega por `aviso(texto, ok)`.
    Q_INVOKABLE void exportarUi(const QString& id);
    Q_INVOKABLE void importarUi(const QUrl& archivo);
    /// Las claves que declara el plugin ("secretos" en plugin.json): [{clave, etiqueta, cargada}].
    Q_INVOKABLE QVariantList secretosUi(const QString& id) const;
    /// Guarda una clave del plugin, cifrada (Credenciales/plugin-<id>-<clave>). "" o el error.
    Q_INVOKABLE QString guardarSecreto(const QString& id, const QString& nombre, const QString& valor);
    /// El panel: los valores de ahora (cambian por `valoresCambio(id)`) y mover un control.
    Q_INVOKABLE QVariantMap valoresUi(const QString& id) const;
    Q_INVOKABLE QString panelUi(const QString& id, const QString& clave, const QVariant& valor);

   signals:
    void cambio();
    void eventosCambio();
    void equiposCambio();
    void respuesta(const QString& id, const QString& control, const QString& texto, bool ok);
    void aviso(const QString& texto, bool ok);
    /// Cambió algún valor del panel de un plugin (va aparte de `cambio`: puede ser 10 veces por segundo).
    void valoresCambio(const QString& id);
    /// Un evento de un plugin (para los disparadores).
    void evento(const QString& plugin, const QString& canal, const QString& datosJson);

   private:
    struct Proceso;
    struct Pendiente {
        std::function<void(json)> listo;
        QTimer* timer;
    };

    void arrancar(const std::string& id);
    void detener(const std::string& id, bool porPedido);
    void alSalir(const std::string& id, int codigo, bool caido);
    void alLeer(const std::string& id);
    void alError(const std::string& id);
    void recibir(const std::string& id, const json& m);
    void enviar(const std::string& id, const json& m);
    void declararEntrada(const std::string& id, const json& d);
    void declararPtz(const std::string& id, const json& d);
    void publicarEvento(const std::string& id, const json& m);
    void anotarPuerto(const std::string& id, int puerto);
    void guardarActivos() const;
    json leerActivos() const;
    void avisar(const std::string& texto, const std::string& prioridad) const;
    void escribirRegistro(const std::string& id, const QByteArray& datos);
    json& valoresDe(const std::string& id) const;
    void guardarValores();

    AppRuntime& rt_;
    QPointer<video::VideoGrafos> video_;
    QPointer<ptz::ControlCamaras> camaras_;
    QPointer<OperatorMessages> mensajes_;
    Llamador llamador_;
    std::function<void(const std::string&, const json&)> alPublicar_;
    AltaSuperficie altaSup_;
    BajaSuperficie bajaSup_;
    EntradaSuperficie entradaSup_;
    std::map<std::string, std::set<std::string>> superficiesDe_; ///< plugin -> ids de aparatos
    std::filesystem::path carpeta_, logs_, config_;
    QString credenciales_;
    std::map<std::string, std::unique_ptr<Proceso>> procesos_;
    std::map<int, Pendiente> pendientes_;
    int n_ = 0;
    std::deque<json> eventos_; ///< los últimos (de todos)
    std::map<std::string, json> ultimoEvento_;
    std::map<std::string, json> pruebas_;
    json equipos_;
    bool buscandoEquipos_ = false;
    /// id -> valores del panel (Plugins/<id>/.panel.json; se cargan al primer uso).
    mutable std::map<std::string, json> valores_;
    std::set<std::string> valoresSucios_;
    QTimer* guardarValoresT_ = nullptr;
};

} // namespace ba::app::plugins
