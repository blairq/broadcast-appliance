// ============================================================================
// src/app/dispositivos/Dispositivos.hpp — los equipos: detectarlos y manejarlos
//
// Un solo lugar para todo lo que se puede encontrar y manejar: cámaras de la
// red (ONVIF, RTSP), cámaras y micrófonos de la máquina (GStreamer), equipos
// USB, HID y serie…
//
// TRES PIEZAS, y nada más:
//   - DETECTOR: sabe buscar un tipo de equipos (`escanear` devuelve la lista).
//   - EQUIPO: lo que se encontró: {id, tipo, nombre, detalle, datos}. El id es
//     estable («red:192.168.1.90», «local:video:0», «usb:046d:0881»).
//   - ACCIÓN: algo que se le puede hacer a un tipo de equipo, DECLARADA con
//     título, descripción y parámetros (texto, clave, número, sí/no, opción,
//     json). `correr` hace el trabajo y contesta con {texto, datos} o {error}.
//
// POR QUÉ ASÍ. Una acción se registra UNA vez (dispositivos/Registro.cpp) y
// aparece sola en el asistente (equipo_accion, con sus parámetros en la
// descripción del equipo) y en la interfaz (la página Equipos arma el botón y
// su formulario con los parámetros). Sumar «explorar el UVC de un USB» o
// «pedir la clave de una cámara» no es tocar MCP ni QML: es registrar una
// acción.
// ============================================================================
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <map>
#include <vector>

namespace ba::app::dispositivos {

/// Lo que contesta una acción o un detector: {texto, datos} o {error}.
using Listo = std::function<void(QVariantMap resultado)>;

struct Parametro {
    QString clave, etiqueta;
    QString tipo = "texto"; ///< texto | clave | numero | si_no | opcion | json
    QStringList opciones;   ///< con tipo opcion
    bool obligatorio = false;
    QString ayuda;
    QVariant defecto;
    QVariantMap mapa() const;
};

struct Accion {
    QString id, titulo, descripcion;
    std::vector<Parametro> parametros;
    bool modifica = false; ///< cambia algo (no solo mira)
    /// Si aplica a ESTE equipo (p. ej. «activar PTZ» solo si ya es entrada). Vacío: a todos los del tipo.
    std::function<bool(const QVariantMap& equipo)> aplica;
    std::function<void(const QVariantMap& equipo, const QVariantMap& args, Listo listo)> correr;
    QVariantMap mapa() const;
};

struct Detector {
    QString id, nombre, descripcion;
    /// Busca y devuelve {equipos: [{clave, tipo, nombre, detalle, datos}]} o {error}.
    std::function<void(Listo listo)> escanear;
};

class Dispositivos : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList equipos READ equiposQml NOTIFY cambio)
    Q_PROPERTY(QVariantList detectores READ detectoresQml NOTIFY cambio)
   public:
    explicit Dispositivos(QObject* parent = nullptr);

    void registrar(Detector d);
    void registrar(const QString& tipo, Accion a);

    /// Busca con un detector (o con todos, `detector` vacío). `listo` cuando terminan todos.
    void escanear(const QString& detector, Listo listo);
    QVariantList equipos(const QString& tipo = {}, const QString& buscar = {}) const;
    QVariantMap equipo(const QString& id) const;
    /// Las acciones que aplican a un equipo (con sus parámetros).
    QVariantList accionesDe(const QVariantMap& equipo) const;
    void correr(const QString& id, const QString& accion, const QVariantMap& args, Listo listo);
    /// «tipo: accion (params), …» de todo lo registrado: para la descripción de las herramientas.
    QString resumenAcciones() const;
    QString resumenDetectores() const;
    /// Suma o actualiza un equipo a mano (lo usan las acciones que descubren algo nuevo).
    void anotar(const QString& detector, QVariantMap equipo);

    // --- La interfaz (pages/EquiposPage.qml) ---------------------------------------
    QVariantList equiposQml() const { return equipos(); }
    QVariantList detectoresQml() const;
    Q_INVOKABLE void escanearUi(const QString& detector);
    Q_INVOKABLE void accionUi(const QString& id, const QString& accion, const QVariantMap& args);

   signals:
    void cambio();
    void resultado(const QString& id, const QString& accion, const QVariantMap& resultado);

   private:
    QVariantMap conAcciones(QVariantMap e) const;
    std::vector<Detector> detectores_;
    std::map<QString, std::vector<Accion>> acciones_; ///< tipo -> acciones
    std::map<QString, QVariantMap> equipos_;          ///< id -> equipo
    std::map<QString, QVariantMap> estadoDetector_;   ///< id -> {escaneando, cuando, cuantos, error}
};

} // namespace ba::app::dispositivos
