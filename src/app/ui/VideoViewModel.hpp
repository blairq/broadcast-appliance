// ============================================================================
// src/app/ui/VideoViewModel.hpp — el grafo de video, visto y pedido desde la interfaz (F3-03)
//
// video::VideoGrafos es del dueño del motor (vive en el hilo del plano de control, ver
// su .hpp y docs/arquitectura/05-motor-del-plano.md). Esto es lo que la interfaz ve de él
// (Studio.video) y cómo le pide, sin tocarlo:
//
//   LEER: su vista viaja en la foto del motor (FotoMotor::video, parte Video). Al llegar,
//         se avisa: `cambio` si cambiaron las entradas o los grafos (lo que redibuja el
//         patchbay) y `estadoCambio` si cambió lo que se mueve (estados, programador,
//         latencia…). Lo que antes se sondeaba (estadoRed(), programadorUi()…) son
//         propiedades.
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

#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

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
    /// Las entradas de video que hay.
    Q_PROPERTY(QStringList entradas READ entradas NOTIFY cambio)
    /// Las mismas, agrupadas como en el audio: [{nombre, grupo («Cámaras», «Red», «NDI»),
    /// tarjetas (cuántas tiene)}].
    Q_PROPERTY(QVariantList entradasInfo READ entradasInfo NOTIFY cambio)
    /// La que se está editando.
    Q_PROPERTY(QString entrada READ entrada WRITE seleccionar NOTIFY cambio)
    /// Su sub-grafo: {nodos:[{id,tipo,titulo,etiqueta,grupo,x,y,params,externo,
    /// entradas:[{id,tipo,pedido}],salidas:[…]}], cables:[{desde,salida,hacia,entrada}], trivial}
    Q_PROPERTY(QVariantMap grafo READ grafo NOTIFY cambio)
    /// Todo el estudio (mismo formato).
    Q_PROPERTY(QVariantMap estudio READ estudio NOTIFY cambio)
    /// Qué muestra la página: "racks" (una entrada) o "estudio" (todo).
    Q_PROPERTY(QString vista READ vista WRITE setVista NOTIFY vistaCambio)
    /// Las tarjetas propias: [{tipo,titulo,descripcion,params,ambito}].
    Q_PROPERTY(QVariantList catalogo READ catalogo CONSTANT)
    /// Las entradas de GStreamer (red y plugins) con su estado y diagnóstico.
    Q_PROPERTY(QVariantList estadoRed READ estadoRed NOTIFY estadoCambio)
    /// Las entradas con algo que decir: [{entrada, diagnostico}].
    Q_PROPERTY(QVariantList diagnostico READ diagnostico NOTIFY estadoCambio)
    /// Los eventos «ba/*» de las tarjetas de la entrada elegida: {tarjeta: [{evento,
    /// direccion, n, hace_ms}]}.
    Q_PROPERTY(QVariantMap eventos READ eventos NOTIFY estadoCambio)
    /// El programador de superresolución (su configuración y el estado por entrada).
    Q_PROPERTY(QVariantMap programador READ programador NOTIFY estadoCambio)
    /// El reloj del estudio.
    Q_PROPERTY(QVariantMap reloj READ reloj NOTIFY estadoCambio)
    /// La latencia del estudio: {latencia_ms, tope_ms, automatico, tarde}.
    Q_PROPERTY(QVariantMap latencia READ latencia NOTIFY estadoCambio)
    /// La mesa del automixer: {mesas, microfonos}.
    Q_PROPERTY(QVariantMap automixer READ automixer NOTIFY estadoCambio)
    /// Los filtros de audio de cada entrada de GStreamer: {entrada: [{id, elemento, propiedades}]}.
    Q_PROPERTY(QVariantMap filtrosAudio READ filtrosAudio NOTIFY estadoCambio)
    /// Las escenas (para elegir dónde poner algo).
    Q_PROPERTY(QStringList escenas READ escenas NOTIFY estadoCambio)
    /// Si este sistema tiene superresolución de Apple.
    Q_PROPERTY(bool haySuperres READ haySuperres NOTIFY estadoCambio)
    /// Los módulos de video de GStreamer (CatalogoGst::modulos), los de audio y la versión.
    Q_PROPERTY(QVariantList modulosGst READ modulosGst NOTIFY gstListo)
    Q_PROPERTY(QVariantList modulosAudio READ modulosAudio NOTIFY gstListo)
    Q_PROPERTY(QString versionGst READ versionGst NOTIFY gstListo)

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
    QVariantMap estudio() const { return estudio_; }
    QString vista() const { return vista_; }
    void setVista(const QString& v);
    static QVariantList catalogo();
    QVariantList estadoRed() const { return estadoRed_; }
    QVariantList diagnostico() const { return diagnostico_; }
    QVariantMap eventos() const { return eventos_; }
    QVariantMap programador() const { return programador_; }
    QVariantMap reloj() const { return reloj_; }
    QVariantMap latencia() const { return latencia_; }
    QVariantMap automixer() const { return automixer_; }
    QVariantMap filtrosAudio() const { return filtrosAudio_; }
    QStringList escenas() const { return escenas_; }
    bool haySuperres() const { return haySuperres_; }
    QVariantList modulosGst() const;
    QVariantList modulosAudio() const;
    QString versionGst() const;
    /// Los controles de una tarjeta de GStreamer (como PropertiesModel): [{name, label,
    /// kind, value, min, max, step, options, suffix, help}].
    Q_INVOKABLE QVariantList propiedades(const QString& id) const;
    /// Los del pad `puerto` de una Mezcla de GStreamer.
    Q_INVOKABLE QVariantList propiedadesPad(const QString& id, const QString& puerto) const;
    /// Los de un módulo, para el detalle del catálogo (con estos valores puestos).
    Q_INVOKABLE QVariantList propiedadesModulo(const QString& elemento) const;
    Q_INVOKABLE QVariantList propiedadesModuloCon(const QString& elemento, const QVariantMap& valores) const;
    /// Las entradas virtuales que se pueden crear: [{id, nombre, descripcion, pipeline}].
    Q_INVOKABLE static QVariantList virtuales();

    // --- Lo que pide QML ("" siempre: el error llega por notify) ------------------------
    Q_INVOKABLE QString agregar(const QString& tipo, const QString& despues);
    Q_INVOKABLE QString agregarFiltro(const QString& filtroId, const QString& despues);
    Q_INVOKABLE QString agregarModulo(const QString& elemento, const QString& despues);
    Q_INVOKABLE QString agregarMezcla(const QString& elemento);
    Q_INVOKABLE QString ajustarPad(const QString& id, const QString& puerto, const QString& clave, const QVariant& valor);
    Q_INVOKABLE QString agregarSuperresUi(const QString& motor, const QString& modelo, const QString& despues);
    Q_INVOKABLE QString crearVirtual(const QString& nombre, const QString& preset, const QString& escena);
    Q_INVOKABLE QString quitar(const QString& id);
    Q_INVOKABLE QString reubicar(const QString& id, const QString& despues);
    Q_INVOKABLE QString correr(const QString& id, int paso);
    Q_INVOKABLE QString conectar(const QString& desde, const QString& salida, const QString& hacia, const QString& entrada);
    Q_INVOKABLE QString desconectar(const QString& desde, const QString& salida, const QString& hacia, const QString& entrada);
    Q_INVOKABLE QString mover(const QString& id, int x, int y);
    Q_INVOKABLE QString ajustar(const QString& id, const QVariantMap& params);
    Q_INVOKABLE QString controlar(const QString& id);
    Q_INVOKABLE QString agregarControlada(const QString& control, const QString& entrada);
    Q_INVOKABLE QString soltarControl(const QString& id);
    Q_INVOKABLE QString ajustarFiltro(const QString& id, const QString& clave, const QVariant& valor);
    Q_INVOKABLE QString aEscena(const QString& id, const QString& escena);
    Q_INVOKABLE QString limpiar();
    Q_INVOKABLE QString ordenar();
    Q_INVOKABLE QString configurarRelojUi(const QVariantMap& cambios);
    /// Mide (y con `aplicar` corrige) cuánto atrasa `b` de `a`: el resultado llega por `medicion`.
    Q_INVOKABLE void medirUi(const QString& a, const QString& b, bool aplicar);
    Q_INVOKABLE QString desfaseUi(const QString& entrada, double ms);
    Q_INVOKABLE QString superresUi(const QString& entrada, bool activa);
    Q_INVOKABLE QString sincroniaUi(const QString& entrada, const QString& modo);
    Q_INVOKABLE void ponerTopeEstudioUi(int tope);
    Q_INVOKABLE QString claseSincroniaUi(const QString& entrada, const QString& clase);
    Q_INVOKABLE QString bajarLatenciaUi();
    Q_INVOKABLE QString automixerPonerUi(const QString& entrada, const QString& grupo);
    Q_INVOKABLE QString automixerAjustesUi(const QString& grupo, const QVariantMap& ajustes);
    Q_INVOKABLE QString configurarProgramadorUi(const QVariantMap& cambios);
    Q_INVOKABLE QString agregarFiltroAudioUi(const QString& entrada, const QString& cual);
    Q_INVOKABLE QString quitarFiltroAudioUi(const QString& entrada, int indice);
    Q_INVOKABLE QString ponerFiltrosAudioUi(const QString& entrada, const QVariantList& filtros);
    Q_INVOKABLE QString superresMotorUi(const QString& entrada, const QString& motor, const QString& modelo);
    Q_INVOKABLE QString nuevaEntradaRed(const QString& nombre, const QString& uri, const QString& escena, int latenciaMs,
                                        bool normalizar);
    Q_INVOKABLE QString presetPanoramicaUi(const QString& id, const QString& preset, double campo);
    Q_INVOKABLE QString aplicarDisposicionUi(const QString& compositor, const QString& disposicion);
    /// Que revise las entradas ya (no espera a la próxima revisión).
    Q_INVOKABLE void sincronizar();

    // --- Para los demás modelos de la interfaz (de la foto) -----------------------------
    /// El estado de una entrada (VideoGrafos::estadoEntrada, el de la última vista), o {error}.
    nlohmann::json estadoEntrada(const std::string& entrada) const;
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
    void estadoCambio();
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
    /// Pedir con la entrada elegida (las ediciones de la página sin entrada explícita).
    QString pedirEnLaElegida(const char* que, std::function<QString(video::VideoGrafos&)> orden);
    QString pedirQ(const char* que, std::function<QString(video::VideoGrafos&)> orden);
    const nlohmann::json& v() const;

    VentanillaMotor& ventanilla_;
    video::VideoGrafos& grafos_;
    QString entrada_, vista_ = QStringLiteral("racks");
    QStringList entradas_, escenas_;
    QVariantList entradasInfo_, estadoRed_, diagnostico_;
    QVariantMap grafo_, estudio_, eventos_, programador_, reloj_, latencia_, automixer_, filtrosAudio_;
    bool haySuperres_ = false;
    /// Lo que se mostró la última vez (para avisar solo lo que cambió).
    nlohmann::json antes_ = nlohmann::json::object();
};

} // namespace ba::app
