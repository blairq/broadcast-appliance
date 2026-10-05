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
//     sigue lo general (Configuración → Video), o se fuerza, o se apaga.
//
// Se guarda en Config/camaras.json (por nombre de entrada: si la entrada se
// borra y se vuelve a agregar con el mismo nombre, recupera su PTZ). Las
// claves de las cámaras van cifradas en Credenciales/, nunca en el json.
//
// Los filtros se ponen y se sacan solos: cada 1,5 s (y cuando cambian las
// entradas) se revisa que cada entrada tenga lo que le corresponde. Así una
// entrada nueva ya tiene su PTZ, y un cambio en Configuración rige enseguida.

#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <filesystem>
#include <map>
#include <memory>
#include <string>

namespace ba::app {
class AppRuntime;
}

namespace ba::app::ptz {

class PtzBackend;
class PtzDigital;
class OnvifBusqueda;
struct Posicion;

class ControlCamaras : public QObject {
    Q_OBJECT
    /// [{entrada, tipoEntrada, backend, conf, tieneClave, digital, mejora,
    ///   mejoraActiva, modo, capacidades, capacidadesDigital, listo, estado,
    ///   presetsDigitales}]
    Q_PROPERTY(QVariantList camaras READ camaras NOTIFY cambio)
    /// Los backends de hardware que se pueden elegir y sus campos.
    Q_PROPERTY(QVariantList backends READ backends CONSTANT)
    Q_PROPERTY(bool buscando READ buscando NOTIFY busquedaCambio)
    /// Cámaras ONVIF encontradas en la red: [{direccion, host, nombre, modelo}]
    Q_PROPERTY(QVariantList encontradas READ encontradas NOTIFY busquedaCambio)
    /// «Mejorar imagen» general (Configuración → Video).
    Q_PROPERTY(bool mejoraGeneral READ mejoraGeneral NOTIFY cambio)

   public:
    ControlCamaras(AppRuntime& rt, std::filesystem::path dataRoot, QObject* parent = nullptr);
    ~ControlCamaras() override;

    QVariantList camaras() const;
    static QVariantList backends();
    bool buscando() const;
    QVariantList encontradas() const;
    bool mejoraGeneral() const;

    // --- Mover (lo del joystick) -------------------------------------------
    /// Velocidades -1..1 (pan + derecha, tilt + arriba, zoom + acercar).
    // --- Interlock: no mover la cámara que está AL AIRE -----------------------
    /// La cámara (su entrada) está en la escena al aire.
    Q_INVOKABLE bool alAire(const QString& entrada) const;
    /// {activo, seguir_suave_permitido} (Config/ptz-interlock.json).
    Q_INVOKABLE QVariantMap interlock() const;
    Q_INVOKABLE void setInterlock(const QVariantMap& cambios);
    Q_INVOKABLE QString mover(const QString& entrada, double pan, double tilt, double zoom);
    Q_INVOKABLE QString parar(const QString& entrada);
    Q_INVOKABLE QString enfocar(const QString& entrada, double velocidad);
    Q_INVOKABLE QString autoFoco(const QString& entrada, bool automatico);
    Q_INVOKABLE QString irPreset(const QString& entrada, int numero);
    Q_INVOKABLE QString guardarPreset(const QString& entrada, int numero);
    Q_INVOKABLE QString home(const QString& entrada);
    /// Encuadre digital directo: centro (x, y 0..1) y zoom (1..8).
    Q_INVOKABLE QString encuadre(const QString& entrada, double x, double y, double zoom, double segundos);
    /// Apuntar con el mouse sobre la imagen de la entrada (lo que se ve, ya
    /// recortado): (u, v) 0..1 es el punto que va al centro; con un cuadro
    /// (ancho, alto 0..1 de la imagen), además se acerca hasta que el cuadro
    /// llene la imagen. ancho = 0: solo centrar. Lo hace lo que mueve el
    /// joystick: el digital, exacto; una cámara motorizada, aproximado (un
    /// pulso de movimiento y otro de zoom, porque los protocolos no dicen
    /// cuántos grados hay en el cuadro).
    Q_INVOKABLE QString apuntar(const QString& entrada, double u, double v, double ancho, double alto);
    /// {x, y, zoom, moviendo} del PTZ digital (vacío si está apagado).
    Q_INVOKABLE QVariantMap posicion(const QString& entrada) const;

    // --- Configurar ------------------------------------------------------------
    /// backend: "ninguno" | "visca" | "onvif" | "panasonic"; conf: host,
    /// puerto, transporte, direccion, usuario, clave, perfil. Una clave vacía
    /// deja la que estaba guardada. Devuelve "" o el error.
    Q_INVOKABLE QString configurar(const QString& entrada, const QString& backend, const QVariantMap& conf);
    Q_INVOKABLE QString setDigital(const QString& entrada, bool prendido);
    /// Los PTZ que maneja un plugin: el plugin arrancó, se cayó o volvió.
    void refrescarPlugins();
    /// Qué mueve ahora el PTZ de la entrada: "optico", "digital" o "" (nada).
    QString modoActivo(const QString& entrada);
    /// Todo lo que el protocolo dijo que la cámara puede, con las respuestas crudas.
    QVariantMap funcionesDe(const QString& entrada);
    /// "general" | "si" | "no".
    Q_INVOKABLE QString setMejora(const QString& entrada, const QString& modo);
    /// Qué mueve el joystick cuando hay hardware: "optico" | "digital".
    Q_INVOKABLE QString setModo(const QString& entrada, const QString& modo);
    Q_INVOKABLE void buscarOnvif();
    /// Revisa entradas y filtros ya (también corre solo cada 1,5 s).
    Q_INVOKABLE void sincronizar();

    // --- Posiciones con nombre (ptz/Posiciones) --------------------------------
    /// [{id, nombre, comentario, definicion, digital{x,y,zoom}, camara{backend,memoria|token}, creada,
    /// modificada}]
    Q_INVOKABLE QVariantList posiciones(const QString& entrada) const;
    /// Registra donde está ahora la cámara (cabeza y/o recorte) con ese
    /// nombre. {ok, error, aviso, id}.
    Q_INVOKABLE QVariantMap crearPosicion(const QString& entrada, const QString& nombre,
                                          const QString& comentario);
    /// cambios: {nombre?, comentario?, regrabar?: bool (vuelve a registrar
    /// donde está ahora)}. {ok, error, aviso}.
    Q_INVOKABLE QVariantMap editarPosicion(const QString& entrada, const QString& idONombre,
                                           const QVariantMap& cambios);
    /// Borra la posición (y libera su memoria en la cámara).
    Q_INVOKABLE QString borrarPosicion(const QString& entrada, const QString& idONombre);
    /// Va a la posición: la cámara a su memoria y el recorte a su encuadre.
    /// {ok, error, aviso}.
    Q_INVOKABLE QVariantMap irAPosicion(const QString& entrada, const QString& idONombre,
                                        double segundos = 1.0);

    /// Para MCP: el estado de una entrada (o de todas), como json.
    QJsonObject estadoJson(const QString& entrada = {}) const;

   signals:
    void cambio();
    void busquedaCambio();

   private:
    struct Camara;
    Camara* camara(const QString& entrada, QString* error);
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

    bool interlockActivo_ = true;
    bool seguirSuavePermitido_ = true;
    AppRuntime& rt_;
    std::filesystem::path archivo_;
    QString credenciales_;
    std::map<std::string, std::unique_ptr<Camara>> camaras_; ///< todas las conocidas (por nombre)
    std::map<std::string, int>
        pulsos_; ///< por entrada: el pulso óptico vigente (uno nuevo cancela el anterior)
    std::map<std::string, QString> tipoEntrada_; ///< las presentes ahora: hardware | red | ndi
    std::unique_ptr<OnvifBusqueda> busqueda_;
    QTimer revision_;
    QTimer guardado_;
};

} // namespace ba::app::ptz
