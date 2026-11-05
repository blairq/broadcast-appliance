// src/app/ptz/ControlCamaras.hpp — el PTZ de cada entrada de video (Studio.camaras)
//
// Un solo frontend para todas las cámaras: la página «Cámaras PTZ», las
// herramientas MCP (ptz_*) y el asistente hablan con esto, y esto con el
// backend de cada entrada (PtzBackend):
//
//   * Todas las entradas de video (hardware, red, NDI) tienen PTZ DIGITAL
//     (recorte y zoom en la GPU, filtro ba_ptz_digital), si está prendido en
//     esa entrada (de fábrica sí: sin zoom no cuesta nada).
//   * Si la cámara se mueve de verdad, se le elige un backend de hardware
//     (VISCA, ONVIF, Panasonic) y aparecen sus controles: lo que el backend
//     dice que puede (capacidades).
//   * Con los dos, el joystick mueve uno u otro («modo»: optico | digital).
//   * «Mejorar imagen» (filtro ba_mejora_imagen) también es por entrada:
//     sigue lo general (Configuración → Cámaras y entradas), o se fuerza, o se apaga.
//
// Se guarda en Config/camaras.json (por nombre de entrada: si la entrada se
// borra y se vuelve a agregar con el mismo nombre, recupera su PTZ). Las
// claves de las cámaras van cifradas en Credenciales/, nunca en el json.
//
// Los filtros se ponen y se sacan solos: cada vez que cambian las entradas o la
// configuración se revisa que cada entrada tenga lo que le corresponde. Así una
// entrada nueva ya tiene su PTZ, y un cambio en Configuración rige enseguida.
//
// Es de la interfaz (F3-03): las entradas, su tamaño, lo que está al aire (el interlock)
// y «Mejorar imagen» general los lee de la foto del motor (parte Video, ver
// emision/FotoMotor.hpp), y las fotos de las cámaras se le piden al motor por la
// ventanilla. Mover una cámara es hablar con ella (su backend), no con el motor.
//
// La implementación va en tres archivos (y lo de adentro, en ControlCamarasCamara.hpp):
// ControlCamaras.cpp (lo que se muestra, entradas, órdenes, configuración, disco),
// ControlCamarasPosiciones.cpp (posiciones, presets, miniaturas) y ControlCamarasFoto.cpp
// (la foto del panel y apuntar haciendo click en ella).

#pragma once

#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include "ui/CapturaError.hpp"

namespace ba::app {
class AppRuntime;
class VentanillaMotor;
class UsoPorVistas;
}

namespace ba::app::ptz {

class PtzBackend;
class PtzDigital;
class OnvifBusqueda;
struct Posicion;

class ControlCamaras : public QObject {
    Q_OBJECT
    /// El error de una acción de la interfaz (las Q_INVOKABLE void): a quien mira.
    void avisarError(const QString& e) {
        if (!e.isEmpty() && !::ba::app::CapturaError::tomar(e)) emit aviso(e);
    }
    void avisarError(const std::string& e) { avisarError(QString::fromStdString(e)); }
    /// [{entrada, tipoEntrada, backend, conf, tieneClave, digital, mejora,
    ///   mejoraActiva, modo, capacidades, capacidadesDigital, listo, estado,
    ///   posiciones, red (la cámara de red de la entrada: Red::camaraDeEntrada; {} si no es)}]
    Q_PROPERTY(QVariantList camaras READ camaras NOTIFY cambio)
    /// Las entradas que se ven al aire ahora (el interlock): cambia con cada corte.
    Q_PROPERTY(QStringList alAireEntradas READ alAireEntradas NOTIFY alAireCambio)
    /// {entrada: motivo} de las cámaras que hoy no se mueven sin confirmar (la regla de
    /// chequearMovimiento, para un movimiento común): la vista pregunta si su entrada está acá,
    /// sin llamar a una función (en el tablero web, la respuesta de una función llega tarde).
    /// Cambia con cada corte y con el ajuste del interlock.
    Q_PROPERTY(QVariantMap motivosInterlock READ motivosInterlock NOTIFY alAireCambio)
    /// {entrada: {x, y, zoom, moviendo}}: el encuadre del PTZ digital de las entradas que
    /// alguna vista usa (`usar(vista, si, entrada)`). Se lee mientras se mueve.
    Q_PROPERTY(QVariantMap encuadres READ encuadres NOTIFY encuadresCambio)
    /// {entrada: foto(entrada)}: la foto del panel PTZ de cada cámara (y si se movió desde entonces).
    Q_PROPERTY(QVariantMap fotos READ fotos NOTIFY fotosCambio)
    /// Los backends de hardware que se pueden elegir y sus campos.
    Q_PROPERTY(QVariantList backends READ backends CONSTANT)
    Q_PROPERTY(bool buscando READ buscando NOTIFY busquedaCambio)
    /// Cámaras ONVIF encontradas en la red: [{direccion, host, nombre, modelo}]
    Q_PROPERTY(QVariantList encontradas READ encontradas NOTIFY busquedaCambio)
    /// «Mejorar imagen» general (Configuración → Cámaras y entradas).
    Q_PROPERTY(bool mejoraGeneral READ mejoraGeneral NOTIFY cambio)

   public:
    ControlCamaras(AppRuntime& rt, VentanillaMotor& ventanilla, std::filesystem::path dataRoot, QObject* parent = nullptr);
    ~ControlCamaras() override;

    QVariantList camaras() const;
    static QVariantList backends();
    bool buscando() const;
    QVariantList encontradas() const;
    bool mejoraGeneral() const;
    QStringList alAireEntradas() const { return alAire_; }
    QVariantMap motivosInterlock() const;
    QVariantMap encuadres() const { return encuadres_; }
    QVariantMap fotos() const;
    /// Una vista empieza o deja de mirar el encuadre de `entrada` (la cuenta es por vista:
    /// comun/UsoPorVistas; en QML, `Suscripcion { objetivo: Studio.camaras; que: entrada }`).
    Q_INVOKABLE void usar(QObject* vista, bool si, const QString& entrada = {});
    /// De dónde salen los datos de la cámara de red de una entrada (red::Red, que se arma
    /// después); `revisarRed()` cuando cambian.
    void setDatosDeRed(std::function<QVariantMap(const QString&)> fn) {
        datosDeRed_ = std::move(fn);
        revisarRed();
    }
    void revisarRed();

    // --- Mover (lo del joystick) -------------------------------------------
    /// Velocidades -1..1 (pan + derecha, tilt + arriba, zoom + acercar).
    // --- Interlock: no mover la cámara que está AL AIRE -----------------------
    /// La cámara (su entrada) está en la escena al aire.
    bool alAire(const QString& entrada) const;
    /// {activo, seguir_suave_permitido} (Config/ptz-interlock.json).
    Q_INVOKABLE QVariantMap interlock() const;
    Q_INVOKABLE void setInterlock(const QVariantMap& cambios);
    /// Si mover la cámara de `entrada` necesita que alguien confirme: "" si se
    /// puede mover ya, o el porqué (está al aire y el interlock está prendido).
    /// Lo consultan todos antes de mover (las páginas, que preguntan; el MCP,
    /// que lo devuelve como error salvo `forzar`). `forzar`: ya se confirmó.
    /// `modo`: "" (movimiento común) o "seguir_suave" (ptz/Interlock.hpp).
    Q_INVOKABLE QString chequearMovimiento(const QString& entrada, bool forzar = false,
                                           const QString& modo = QString()) const;
    Q_INVOKABLE void mover(const QString& entrada, double pan, double tilt, double zoom);
    Q_INVOKABLE void parar(const QString& entrada);
    Q_INVOKABLE void enfocar(const QString& entrada, double velocidad);
    Q_INVOKABLE void autoFoco(const QString& entrada, bool automatico);
    /// El preset `numero` es la POSICIÓN de ese número (Posiciones): ir a ella, o
    /// regrabarla (si no hay, se crea «Preset N»). Sin posición con ese número, ir
    /// usa el preset propio de la cámara, si tiene.
    Q_INVOKABLE void irPreset(const QString& entrada, int numero);
    Q_INVOKABLE void guardarPreset(const QString& entrada, int numero);
    Q_INVOKABLE void home(const QString& entrada);
    /// Encuadre digital directo: centro (x, y 0..1) y zoom (1..8).
    Q_INVOKABLE void encuadre(const QString& entrada, double x, double y, double zoom, double segundos);
    /// Apuntar con el mouse sobre la imagen de la entrada (lo que se ve, ya
    /// recortado): (u, v) 0..1 es el punto que va al centro; con un cuadro
    /// (ancho, alto 0..1 de la imagen), además se acerca hasta que el cuadro
    /// llene la imagen. ancho = 0: solo centrar. Lo hace lo que mueve el
    /// joystick: el digital, exacto; una cámara motorizada, aproximado (un
    /// pulso de movimiento y otro de zoom, porque los protocolos no dicen
    /// cuántos grados hay en el cuadro).
    Q_INVOKABLE void apuntar(const QString& entrada, double u, double v, double ancho, double alto);
    /// {x, y, zoom, moviendo} del PTZ digital (vacío si está apagado).
    QVariantMap posicion(const QString& entrada) const;

    // --- Configurar ------------------------------------------------------------
    /// backend: "ninguno" | "visca" | "onvif" | "panasonic"; conf: host,
    /// puerto, transporte, direccion, usuario, clave, perfil. Una clave vacía
    /// deja la que estaba guardada. El error va por la señal del modelo.
    Q_INVOKABLE void configurar(const QString& entrada, const QString& backend, const QVariantMap& conf);
    Q_INVOKABLE void setDigital(const QString& entrada, bool prendido);
    /// Los PTZ que maneja un plugin: el plugin arrancó, se cayó o volvió.
    void refrescarPlugins();
    /// Qué mueve ahora el PTZ de la entrada: "optico", "digital" o "" (nada).
    QString modoActivo(const QString& entrada);
    /// Todo lo que el protocolo dijo que la cámara puede, con las respuestas crudas.
    QVariantMap funcionesDe(const QString& entrada);
    /// "general" | "si" | "no".
    Q_INVOKABLE void setMejora(const QString& entrada, const QString& modo);
    /// Qué mueve el joystick cuando hay hardware: "optico" | "digital".
    Q_INVOKABLE void setModo(const QString& entrada, const QString& modo);
    Q_INVOKABLE void buscarOnvif();
    /// Revisa entradas y filtros ya (también corre solo cada 1,5 s).
    Q_INVOKABLE void sincronizar();

    // --- Posiciones con nombre (ptz/Posiciones) --------------------------------
    /// [{id, nombre, comentario, definicion, digital{x,y,zoom}, camara{backend,memoria|token},
    /// velocidad?, segundos?, miniatura?, miniatura_ruta?, miniatura_url?, creada, modificada}]
    QVariantList posiciones(const QString& entrada) const;
    /// Registra donde está ahora la cámara (cabeza y/o recorte) con ese
    /// nombre. {ok, error, aviso, id}.
    /// `numero`: su número de preset (0 = el primero libre).
    Q_INVOKABLE QVariantMap crearPosicion(const QString& entrada, const QString& nombre,
                                          const QString& comentario, double velocidad = 0, double segundos = -1,
                                          int numero = 0);
    /// cambios: {nombre?, comentario?, numero? (su número de preset, libre), velocidad? (0..1, 0 = la de la cámara),
    /// segundos? (-1 = sin fijar), regrabar?: bool (vuelve a registrar donde
    /// está ahora, con foto nueva), miniatura?: bool (solo la foto)}. {ok, error, aviso}.
    Q_INVOKABLE QVariantMap editarPosicion(const QString& entrada, const QString& idONombre,
                                           const QVariantMap& cambios);
    /// Borra la posición (y libera su memoria en la cámara).
    Q_INVOKABLE void borrarPosicion(const QString& entrada, const QString& idONombre);
    /// Va a la posición: la cámara a su memoria (con su velocidad) y el
    /// recorte a su encuadre, en `segundos` (-1 = los de la posición, o 1).
    /// {ok, error, aviso}.
    Q_INVOKABLE QVariantMap irAPosicion(const QString& entrada, const QString& idONombre,
                                        double segundos = -1);
    /// De una posición: {existe, id, nombre, digital (tiene encuadre digital), x, y,
    /// zoom, motorizada (tiene memoria en una cámara con motor)}. Para la fuente
    /// «Banca con la palabra»: con digital, recorta la toma entera sin mover nada.
    Q_INVOKABLE QVariantMap datosPosicion(const QString& entrada, const QString& idONombre) const;
    /// Las posiciones con ese nombre (sin tildes ni mayúsculas) en TODAS las cámaras:
    /// [{entrada, id, nombre}]. Para «enfocar Presidencia» sin decir qué cámara.
    Q_INVOKABLE QVariantList buscarPosicion(const QString& nombre) const;
    /// La cámara solo tiene PTZ digital (sin motor).
    Q_INVOKABLE bool soloDigital(const QString& entrada) const;
    /// Deja de seguir con el seguimiento de caras (lo pone quien lo maneja): al ir a
    /// una posición a pedido, el seguimiento no tiene que pelearle.
    void setDejarDeSeguir(std::function<void(const QString&)> fn) { dejarDeSeguir_ = std::move(fn); }

    /// Saca la foto de la posición: lo que muestra la entrada AHORA, 320 px de
    /// ancho, en Miniaturas/ptz/. El error va por la señal del modelo. Se llama sola al crear o
    /// regrabar (un rato después, cuando el recorte ya llegó).
    Q_INVOKABLE void capturarMiniatura(const QString& entrada, const QString& idONombre);

    // --- La foto del panel PTZ (cámaras motorizadas) ----------------------------
    /// Saca una foto de lo que muestra la entrada y anota dónde está la cabeza
    /// (en una memoria interna de la cámara, si tiene): tocar la foto lleva la
    /// cámara ahí (apuntarEnFoto). {ok, error, aviso}.
    Q_INVOKABLE QVariantMap sacarFoto(const QString& entrada);
    /// {hay, url, conMemoria (puede volver a donde se sacó), movida (se movió desde
    /// entonces), cuando}.
    QVariantMap foto(const QString& entrada) const;
    /// Apunta al punto (u, v) de la FOTO (y, con ancho/alto, encuadra ese cuadro):
    /// si la cámara no se movió desde la foto, ya; si se movió, primero vuelve a
    /// donde estaba (su memoria) y después apunta. El error va por la señal del modelo.
    Q_INVOKABLE void apuntarEnFoto(const QString& entrada, double u, double v, double ancho, double alto);

    /// Para MCP: el estado de una entrada (o de todas), como json.
    QJsonObject estadoJson(const QString& entrada = {}) const;

   signals:
    void cambio();
    void busquedaCambio();
    void alAireCambio();
    void encuadresCambio();
    void fotosCambio();
    /// Lo que salió mal en algo que terminó después (la foto de una posición).
    void aviso(const QString& texto);

   private:
    struct Camara;
    Camara* camara(const QString& entrada, QString* error);
    /// Se la movió: la foto puede dejar de valer y el encuadre se relee hasta que pare.
    void movida(Camara& c);
    /// Relee el encuadre de `entrada` (si alguien lo mira) hasta que deje de moverse.
    void despertarEncuadre(const std::string& entrada);
    void leerEncuadres();
    void revisarAlAire();
    QString guardarMiniatura(const QString& entrada, const QString& idONombre, const QImage& img);
    /// El backend que recibe el joystick y los presets.
    PtzBackend* activo(Camara& c) const;
    /// Mueve (o para, con todo en 0) sin cortar el pulso de `apuntar`.
    void pulsar(const QString& entrada, double pan, double tilt, double zoom);
    void crearHardware(Camara& c);
    void aplicarFiltros(Camara& c, bool mejoraGeneral);
    QVariantMap fila(const Camara& c) const;
    void leer();
    void guardarPronto();
    void guardar();
    std::string nombreClave(const std::string& entrada) const;
    /// Toma donde está ahora la cámara y lo pone en `p` (la parte de la
    /// cámara puede llegar después: ONVIF contesta el token). "" o el error;
    /// `aviso` lo que no se pudo.
    QString registrar(Camara& c, Posicion& p, QString& aviso);
    /// La foto, un rato después (el recorte digital tarda en llegar).
    void miniaturaPronto(const QString& entrada, const QString& id);
    std::filesystem::path carpetaMiniaturas() const;
    /// La foto de un preset (de la cámara o digital): Miniaturas/ptz/<clave>-preset-<cam|dig>-<n>.jpg.
    QString nombreFotoPreset(const Camara& c, bool digital, int n) const;
    /// Los presets de antes (los digitales del filtro y los de la cámara con foto)
    /// pasan a ser posiciones con su número. true si migró alguno.
    bool migrarPresets(Camara& c, const QJsonObject& digitales);
    /// Los presets que la cámara trae con nombre (ONVIF) y todavía no son posiciones.
    void importarPresetsDeLaCamara(Camara& c);
    /// Un nombre que no esté en la cámara: `base`, o «base (2)»...
    QString nombreLibre(const Camara& c, const QString& base) const;

    std::function<void(const QString&)> dejarDeSeguir_;
    bool interlockActivo_ = true;
    bool seguirSuavePermitido_ = true;
    /// Un cuadro de `fuente` (lo pide el dueño del motor): `listo` en este hilo, nula si no dio.
    void capturar(const std::string& fuente, int ancho, std::function<void(QImage)> listo);
    /// El tamaño de la entrada ahora (de la foto).
    void tamano(const std::string& entrada, int& w, int& h) const;
    /// Mejorar imagen general (de la foto): {activa, ruido, color, nitidez}.
    bool mejoraActiva() const;

    AppRuntime& rt_;
    VentanillaMotor& ventanilla_;
    std::filesystem::path archivo_;
    QString credenciales_;
    std::map<std::string, std::unique_ptr<Camara>> camaras_; ///< todas las conocidas (por nombre)
    std::map<std::string, int>
        pulsos_; ///< por entrada: el pulso óptico vigente (uno nuevo cancela el anterior)
    std::map<std::string, QString> tipoEntrada_; ///< las presentes ahora: hardware | red | ndi
    std::unique_ptr<OnvifBusqueda> busqueda_;
    QTimer guardado_;
    QStringList alAire_;
    std::function<QVariantMap(const QString&)> datosDeRed_;
    std::map<std::string, QVariantMap> red_; ///< por entrada presente: lo de la red (vacío si no es)
    std::unique_ptr<UsoPorVistas> usoEncuadre_;
    QVariantMap encuadres_;
    std::map<std::string, qint64> despiertos_; ///< las que se releen: hasta cuándo aunque no se muevan
    QTimer lecturaEncuadres_;
};

} // namespace ba::app::ptz
