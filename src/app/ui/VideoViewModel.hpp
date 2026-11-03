// ============================================================================
// src/app/ui/VideoViewModel.hpp — el grafo de video, visto y pedido desde la interfaz (F3-03)
//
// video::VideoGrafos es del dueño del motor (vive en el hilo del plano de control, ver
// su .hpp y docs/arquitectura/05-motor-del-plano.md). Esto es lo que la interfaz ve de él
// (Studio.video) y cómo le pide, sin tocarlo:
//
//   LEER: su vista viaja en la foto del motor (FotoMotor::video, parte Video). Al llegar,
//         se avisa: `cambio` si cambiaron las entradas o los grafos (lo que redibuja el
//         patchbay) y, de lo que se mueve (la red, el programador, la latencia…), el
//         aviso de cada propiedad, solo si cambió lo suyo. Lo que antes se sondeaba
//         (estadoRed(), programadorUi()…) son propiedades.
//   PEDIR: cada edición va por la ventanilla a VideoGrafos (en el dueño, con el turno) y
//         vuelve la vista al día; los Q_INVOKABLE que devolvían el error devuelven "" y el
//         error llega por `notify`.
//
// La entrada elegida y qué muestra la página son de la interfaz: se guardan acá (y la
// elegida se le dice también a VideoGrafos, que la usa para las tarjetas sin entrada).
//
// Para los demás modelos de la interfaz (la superresolución, la red, el seguidor, los
// plugins, Personas, el escritorio): lo mismo en C++ (entradas, estadoEntrada, uriDe…,
// leídos de la foto) y `pedir`, que corre una orden sobre VideoGrafos en el dueño.
// ============================================================================
#pragma once

#include "comun/JsonSignals.hpp"
#include "emision/Compartido.hpp"
#include "video/TamanosOrigen.hpp"

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include "ui/CapturaError.hpp"

namespace ba::app {
class VentanillaMotor;
}
namespace ba::app::video {
class CatalogoGst;
class VideoGrafos;
} // namespace ba::app::video

namespace ba::app {

class VideoViewModel : public QObject {
    Q_OBJECT
    /// El error de una acción de la interfaz (las Q_INVOKABLE void): a quien mira.
    void avisarError(const QString& e) {
        if (!e.isEmpty() && !::ba::app::CapturaError::tomar(e)) emit notify(e, "error");
    }
    void avisarError(const std::string& e) { avisarError(QString::fromStdString(e)); }
    /// Para las acciones que dicen si se hicieron: el error a quien mira y true si no hubo.
    bool sinError(const QString& e) {
        avisarError(e);
        return e.isEmpty();
    }
    bool sinError(const std::string& e) { return sinError(QString::fromStdString(e)); }
    /// Las entradas de video que hay.
    Q_PROPERTY(QStringList entradas READ entradas NOTIFY cambio)
    /// Las entradas virtuales que se pueden crear: [{id, nombre, descripcion, pipeline}]
    /// (un catálogo fijo).
    Q_PROPERTY(QVariantList virtuales READ virtuales CONSTANT)
    /// Las mismas, agrupadas como en el audio: [{nombre, grupo («Cámaras», «Red», «NDI»),
    /// tarjetas (cuántas tiene)}].
    Q_PROPERTY(QVariantList entradasInfo READ entradasInfo NOTIFY cambio)
    /// La que se está editando.
    Q_PROPERTY(QString entrada READ entrada WRITE seleccionar NOTIFY cambio)
    /// Su sub-grafo: {nodos:[{id,tipo,titulo,etiqueta,grupo,x,y,params,externo,resumen,
    /// entradas:[{id,tipo,pedido}],salidas:[…]}], cables:[{desde,salida,hacia,entrada}], trivial}.
    /// `resumen`: lo que se lee en la tarjeta del nodo (qué hace, con sus ajustes principales).
    Q_PROPERTY(QVariantMap grafo READ grafo NOTIFY cambio)
    /// Todo el estudio (mismo formato).
    Q_PROPERTY(QVariantMap estudio READ estudio NOTIFY cambio)
    /// Qué muestra la página: "racks" (una entrada) o "estudio" (todo).
    Q_PROPERTY(QString vista READ vista WRITE setVista NOTIFY vistaCambio)
    /// Las tarjetas propias: [{tipo,titulo,descripcion,params,ambito}].
    Q_PROPERTY(QVariantList catalogo READ catalogo CONSTANT)
    /// Las entradas de GStreamer (red y plugins) con su estado y diagnóstico.
    Q_PROPERTY(QVariantList estadoRed READ estadoRed NOTIFY redCambio)
    /// Las entradas con algo que decir: [{entrada, diagnostico}].
    Q_PROPERTY(QVariantList diagnostico READ diagnostico NOTIFY diagnosticoCambio)
    /// Los eventos «ba/*» de las tarjetas de la entrada elegida: {tarjeta: [{evento,
    /// direccion, n, hace_ms}]}.
    Q_PROPERTY(QVariantMap eventos READ eventos NOTIFY eventosCambio)
    /// El programador de superresolución (su configuración y el estado por entrada).
    Q_PROPERTY(QVariantMap programador READ programador NOTIFY programadorCambio)
    /// El reloj del estudio.
    Q_PROPERTY(QVariantMap reloj READ reloj NOTIFY relojCambio)
    /// La latencia del estudio: {latencia_ms, tope_ms, automatico, tarde}.
    Q_PROPERTY(QVariantMap latencia READ latencia NOTIFY latenciaCambio)
    /// La mesa del automixer: {mesas, microfonos}.
    Q_PROPERTY(QVariantMap automixer READ automixer NOTIFY automixerCambio)
    /// Los filtros de audio de cada entrada de GStreamer: {entrada: [{id, elemento, propiedades}]}.
    Q_PROPERTY(QVariantMap filtrosAudio READ filtrosAudio NOTIFY filtrosAudioCambio)
    /// Las escenas (para elegir dónde poner algo).
    Q_PROPERTY(QStringList escenas READ escenas NOTIFY escenasCambio)
    /// Si este sistema tiene superresolución de Apple.
    Q_PROPERTY(bool haySuperres READ haySuperres NOTIFY haySuperresCambio)
    /// Los módulos de video de GStreamer (CatalogoGst::modulos), los de audio y la versión.
    Q_PROPERTY(QVariantList modulosGst READ modulosGst NOTIFY gstListo)
    Q_PROPERTY(QVariantList modulosAudio READ modulosAudio NOTIFY gstListo)
    Q_PROPERTY(QString versionGst READ versionGst NOTIFY gstListo)
    /// Los controles de cada tarjeta de GStreamer y de cada pad de una Mezcla (como
    /// PropertiesModel: [{name, label, kind, value, min, max, step, options, suffix, help}]):
    /// {id: {props: [...], pads: {puerto: [...]}}}. Cambia con lo que dice la vista (lo
    /// cambie la página o el MCP).
    Q_PROPERTY(QVariantMap formularios READ formularios NOTIFY formulariosCambio)

   public:
    /// `grafos` vive en el dueño: solo se lo nombra dentro de las órdenes (corren allá).
    VideoViewModel(VentanillaMotor& ventanilla, video::VideoGrafos& grafos, QObject* parent = nullptr);
    ~VideoViewModel() override;

    // --- Lo que lee QML ----------------------------------------------------------------
    QStringList entradas() const { return entradas_; }
    QVariantList entradasInfo() const { return entradasInfo_; }
    QString entrada() const { return entrada_; }
    void seleccionar(const QString& entrada);
    QVariantMap grafo() const { return grafo_; }
    // (Las que crecen con las entradas se convierten al leerlas: ver `releer`.)
    QVariantMap estudio() const;
    QString vista() const { return vista_; }
    void setVista(const QString& v);
    static QVariantList catalogo();
    QVariantList estadoRed() const;
    QVariantList diagnostico() const;
    QVariantMap eventos() const { return eventos_; }
    QVariantMap programador() const;
    QVariantMap reloj() const;
    QVariantMap latencia() const;
    QVariantMap automixer() const;
    QVariantMap filtrosAudio() const;
    QStringList escenas() const { return escenas_; }
    bool haySuperres() const { return haySuperres_; }
    QVariantList modulosGst() const;
    QVariantList modulosAudio() const;
    QString versionGst() const;
    QVariantMap formularios() const;
    /// Los de un módulo, para el detalle del catálogo (con estos valores puestos).
    Q_INVOKABLE QVariantList propiedadesModulo(const QString& elemento) const;
    /// Las de un módulo con `valores` puestos (texto, como van a GStreamer). Pregunta a
    /// GStreamer (la primera vez crea el elemento): de cualquier hilo. La vista las lee
    /// de un EditorModulo (abajo), que las arma en el fondo.
    static QVariantList propiedadesCon(const video::CatalogoGst& c, const QString& elemento, const QVariantMap& valores);
    static QVariantList virtuales();

    // --- Lo que pide QML ("" siempre: el error llega por notify) ------------------------
    Q_INVOKABLE void agregar(const QString& tipo, const QString& despues);
    Q_INVOKABLE void agregarModulo(const QString& elemento, const QString& despues);
    Q_INVOKABLE void agregarMezcla(const QString& elemento);
    Q_INVOKABLE void ajustarPad(const QString& id, const QString& puerto, const QString& clave, const QVariant& valor);
    Q_INVOKABLE void agregarSuperresUi(const QString& motor, const QString& modelo, const QString& despues);
    Q_INVOKABLE bool crearVirtual(const QString& nombre, const QString& preset, const QString& escena);
    Q_INVOKABLE void quitar(const QString& id);
    Q_INVOKABLE void reubicar(const QString& id, const QString& despues);
    Q_INVOKABLE void correr(const QString& id, int paso);
    Q_INVOKABLE void conectar(const QString& desde, const QString& salida, const QString& hacia, const QString& entrada);
    Q_INVOKABLE void desconectar(const QString& desde, const QString& salida, const QString& hacia, const QString& entrada);
    Q_INVOKABLE void mover(const QString& id, int x, int y);
    Q_INVOKABLE void ajustar(const QString& id, const QVariantMap& params);
    Q_INVOKABLE void controlar(const QString& id);
    Q_INVOKABLE void agregarControlada(const QString& control, const QString& entrada);
    Q_INVOKABLE void soltarControl(const QString& id);
    Q_INVOKABLE void ajustarFiltro(const QString& id, const QString& clave, const QVariant& valor);
    Q_INVOKABLE void aEscena(const QString& id, const QString& escena);
    Q_INVOKABLE void limpiar();
    Q_INVOKABLE void ordenar();
    Q_INVOKABLE bool configurarRelojUi(const QVariantMap& cambios);
    /// Mide (y con `aplicar` corrige) cuánto atrasa `b` de `a`: el resultado llega por `medicion`.
    Q_INVOKABLE void medirUi(const QString& a, const QString& b, bool aplicar);
    Q_INVOKABLE bool desfaseUi(const QString& entrada, double ms);
    Q_INVOKABLE bool superresUi(const QString& entrada, bool activa);
    Q_INVOKABLE bool sincroniaUi(const QString& entrada, const QString& modo);
    Q_INVOKABLE void ponerTopeEstudioUi(int tope);
    Q_INVOKABLE bool claseSincroniaUi(const QString& entrada, const QString& clase);
    Q_INVOKABLE bool bajarLatenciaUi();
    Q_INVOKABLE void automixerPonerUi(const QString& entrada, const QString& grupo);
    Q_INVOKABLE void automixerAjustesUi(const QString& grupo, const QVariantMap& ajustes);
    Q_INVOKABLE bool configurarProgramadorUi(const QVariantMap& cambios);
    Q_INVOKABLE bool agregarFiltroAudioUi(const QString& entrada, const QString& cual);
    Q_INVOKABLE bool quitarFiltroAudioUi(const QString& entrada, int indice);
    Q_INVOKABLE bool ponerFiltrosAudioUi(const QString& entrada, const QVariantList& filtros);
    Q_INVOKABLE void superresMotorUi(const QString& entrada, const QString& motor, const QString& modelo);
    Q_INVOKABLE bool nuevaEntradaRed(const QString& nombre, const QString& uri, const QString& escena, int latenciaMs,
                                        bool normalizar);
    Q_INVOKABLE void presetPanoramicaUi(const QString& id, const QString& preset, double campo);
    Q_INVOKABLE void aplicarDisposicionUi(const QString& compositor, const QString& disposicion);
    /// Que revise las entradas ya (no espera a la próxima revisión).
    Q_INVOKABLE void sincronizar();

    // --- Para los demás modelos de la interfaz (de la foto) -----------------------------
    /// El estado de una entrada (VideoGrafos::estadoEntrada, el de la última vista), o {error}.
    /// Copia SOLO el de esa entrada (para recorrer todas, `estadoDe`, que no copia nada).
    nlohmann::json estadoEntrada(const std::string& entrada) const;
    /// El estado de una entrada en la foto que tiene la interfaz, sin copiarlo (nullptr si
    /// no hay). Vale hasta la próxima foto: usarlo y soltarlo en la misma vuelta del hilo.
    const nlohmann::json* estadoDe(const std::string& entrada) const;
    /// Los tamaños (ancho, alto) distintos que LLEGAN de las entradas (gstreamer.origen de
    /// su estado, antes de los filtros): los arma `releer` mirando solo las entradas cuyo
    /// estado cambió (video/TamanosOrigen), y avisa `tamanosOrigenCambio` solo si cambió la lista.
    const std::vector<std::pair<int, int>>& tamanosOrigen() const { return tamanosOrigen_.distintos(); }
    /// Cuántos estados de entrada se recalcularon con la última foto (con una entrada que
    /// cambia, uno; no las N).
    int estadosRecalculados() const { return tamanosOrigen_.recalculados(); }
    bool esEntrada(const std::string& nombre) const;
    /// La dirección de una entrada de red ("" si no es de red).
    std::string uriDe(const std::string& entrada) const;
    /// Las entradas de red que ya toman video de `host`: [{entrada, url}].
    nlohmann::json entradasDeHost(const std::string& host) const;
    /// El programador de superresolución (como VideoGrafos::programador).
    nlohmann::json programadorJson() const;
    /// Los tamaños (ancho, alto) de las entradas (para el banco de superresolución).
    std::vector<std::pair<int, int>> tamanosDeEntradas() const;
    /// Los nodos del grafo: [{id, tipo, etiqueta, grupo}]; y todo el estudio (describir).
    nlohmann::json nodos() const;
    nlohmann::json estudioJson() const;
    /// El legalizador que corre en las entradas (no | pal | ntsc | rango).
    std::string legalizar() const;
    /// El catálogo de GStreamer (vacío hasta que está listo).
    std::shared_ptr<const video::CatalogoGst> gst() const;

    /// Una orden sobre VideoGrafos, en el dueño (con el turno); después, la vista al día.
    /// `listo` llega en este hilo con lo que devolvió ("" o el error); sin `listo`, el
    /// error va a `notify`.
    void pedir(const char* que, std::function<std::string(video::VideoGrafos&)> orden,
               std::function<void(const std::string&)> listo = {});
    /// Un cuadro de la entrada tal como sale de su grafo: `listo` en este hilo (nula si no dio).
    void capturarCuadroAparte(const std::string& entrada, std::function<void(QImage)> listo);

   signals:
    void cambio();
    void vistaCambio();
    // Lo que se mueve, cada cosa con su aviso (sale solo si cambió lo suyo).
    void redCambio();
    void diagnosticoCambio();
    void eventosCambio();
    void programadorCambio();
    void relojCambio();
    void latenciaCambio();
    void automixerCambio();
    void filtrosAudioCambio();
    void escenasCambio();
    void haySuperresCambio();
    void formulariosCambio();
    /// Cambió la lista de tamaños que llegan de las entradas (`tamanosOrigen`).
    void tamanosOrigenCambio();
    void gstListo();
    /// El resultado de medirUi.
    void medicion(const QVariantMap& resultado);
    /// La superresolución de una entrada no llega a tiempo (la avisa VideoGrafos).
    void superresTarde(const QString& entrada, double ms, double cuadroMs, bool bypass);
    /// Para el operador (ya traducido).
    void notify(const QString& texto, const QString& nivel);

   private:
    /// Llegó una foto: relee la vista si cambió la parte Video.
    void alCambiar(unsigned partes);
    void releer();
    /// Los controles de cada formulario de la vista (ver `formularios`).
    QVariantMap armarFormularios(const nlohmann::json& formularios) const;
    /// Pedir con la entrada elegida (las ediciones de la página sin entrada explícita).
    QString pedirEnLaElegida(const char* que, std::function<QString(video::VideoGrafos&)> orden);
    QString pedirQ(const char* que, std::function<QString(video::VideoGrafos&)> orden);
    const nlohmann::json& v() const;

    VentanillaMotor& ventanilla_;
    video::VideoGrafos& grafos_;
    QString entrada_, vista_ = QStringLiteral("racks");
    QStringList entradas_, escenas_;
    QVariantList entradasInfo_;
    QVariantMap grafo_, eventos_;
    /// Lo que se convierte recién al leerlo (vacío = hay que convertirlo de `antes_`, la
    /// vista de la última foto): ver `releer`.
    mutable std::optional<QVariantList> estadoRed_, diagnostico_;
    mutable std::optional<QVariantMap> estudio_, programador_, reloj_, latencia_, automixer_, filtrosAudio_, formularios_;
    bool haySuperres_ = false;
    /// Lo que se mostró la última vez (para avisar solo lo que cambió): la vista de la foto
    /// anterior, compartida (un puntero; copiarla entera en cada foto era O(tamaño)).
    Compartido<nlohmann::json> antes_;
    /// El tamaño que llega de cada entrada, al día solo con las que cambiaron (ver `tamanosOrigen`).
    video::TamanosOrigen tamanosOrigen_;
};

/// Las propiedades de un módulo de GStreamer mientras se lo arma (el editor de un filtro de
/// audio, el de un elemento propio de superresolución): `elemento` y `valores` (lo que
/// cambió, en texto) son lo que se está editando; `propiedades` (el formulario, con esos
/// valores puestos) se arma en el fondo —preguntarle a GStreamer crea el elemento— y
/// llega cuando está. En QML: EditorModulo { video: vg; elemento: …; valores: … }.
class EditorModulo : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* video READ video WRITE setVideo NOTIFY entradaCambio)
    Q_PROPERTY(QString elemento READ elemento WRITE setElemento NOTIFY entradaCambio)
    Q_PROPERTY(QVariantMap valores READ valores WRITE setValores NOTIFY entradaCambio)
    /// [{nombre, tipo, …, valor}] (ver CatalogoGst::propiedades); vacío sin elemento.
    Q_PROPERTY(QVariantList propiedades READ propiedades NOTIFY propiedadesCambio)
   public:
    explicit EditorModulo(QObject* parent = nullptr) : QObject(parent) {}
    QObject* video() const { return video_; }
    void setVideo(QObject* v);
    QString elemento() const { return elemento_; }
    void setElemento(const QString& e);
    QVariantMap valores() const { return valores_; }
    void setValores(const QVariantMap& v);
    QVariantList propiedades() const { return propiedades_; }

   signals:
    void entradaCambio();
    void propiedadesCambio();

   private:
    void armar();
    void poner(const QVariantList& p);
    QPointer<VideoViewModel> video_;
    QString elemento_;
    QVariantMap valores_;
    QVariantList propiedades_;
    unsigned pedido_ = 0; ///< lo que vuelve de un pedido anterior se descarta
};

} // namespace ba::app
