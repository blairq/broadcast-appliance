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
    Q_INVOKABLE QString mover(const QString& entrada, double pan, double tilt, double zoom);
    Q_INVOKABLE QString parar(const QString& entrada);
    Q_INVOKABLE QString enfocar(const QString& entrada, double velocidad);
    Q_INVOKABLE QString autoFoco(const QString& entrada, bool automatico);
    Q_INVOKABLE QString irPreset(const QString& entrada, int numero);
    Q_INVOKABLE QString guardarPreset(const QString& entrada, int numero);
    Q_INVOKABLE QString home(const QString& entrada);
    /// Encuadre digital directo: centro (x, y 0..1) y zoom (1..8).
    Q_INVOKABLE QString encuadre(const QString& entrada, double x, double y, double zoom, double segundos);
    /// {x, y, zoom, moviendo} del PTZ digital (vacío si está apagado).
    Q_INVOKABLE QVariantMap posicion(const QString& entrada) const;

    // --- Configurar ------------------------------------------------------------
    /// backend: "ninguno" | "visca" | "onvif" | "panasonic"; conf: host,
    /// puerto, transporte, direccion, usuario, clave, perfil. Una clave vacía
    /// deja la que estaba guardada. Devuelve "" o el error.
    Q_INVOKABLE QString configurar(const QString& entrada, const QString& backend, const QVariantMap& conf);
    Q_INVOKABLE QString setDigital(const QString& entrada, bool prendido);
    /// "general" | "si" | "no".
    Q_INVOKABLE QString setMejora(const QString& entrada, const QString& modo);
    /// Qué mueve el joystick cuando hay hardware: "optico" | "digital".
    Q_INVOKABLE QString setModo(const QString& entrada, const QString& modo);
    Q_INVOKABLE void buscarOnvif();
    /// Revisa entradas y filtros ya (también corre solo cada 1,5 s).
    Q_INVOKABLE void sincronizar();

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
    void crearHardware(Camara& c);
    void aplicarFiltros(Camara& c, bool mejoraGeneral);
    QVariantMap fila(const Camara& c) const;
    void leer();
    void guardarPronto();
    void guardar();
    std::string nombreClave(const std::string& entrada) const;

    AppRuntime& rt_;
    std::filesystem::path archivo_;
    QString credenciales_;
    std::map<std::string, std::unique_ptr<Camara>> camaras_; ///< todas las conocidas (por nombre)
    std::map<std::string, QString> tipoEntrada_;              ///< las presentes ahora: hardware | red | ndi
    std::unique_ptr<OnvifBusqueda> busqueda_;
    QTimer revision_;
    QTimer guardado_;
};

} // namespace ba::app::ptz
