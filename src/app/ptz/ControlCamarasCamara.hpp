// ============================================================================
// src/app/ptz/ControlCamarasCamara.hpp — lo de adentro de ControlCamaras (privado)
//
// La cámara con todo lo suyo y lo que comparten los archivos de ControlCamaras:
// ControlCamaras.cpp (lo que se muestra, entradas, órdenes, configuración, disco),
// ControlCamarasPosiciones.cpp (posiciones, presets, miniaturas) y
// ControlCamarasFoto.cpp (la foto del panel PTZ y apuntar haciendo click en ella).
// Solo lo incluyen esos tres.
// ============================================================================
#pragma once

#include "ptz/ControlCamaras.hpp"
#include "ptz/Posiciones.hpp"
#include "ptz/PtzBackend.hpp"
#include "ptz/PtzDigital.hpp"

#include <QJsonObject>
#include <QString>

#include <memory>
#include <string>

namespace ba::app::ptz {

namespace camaras {
/// Orden de los filtros extra en la cadena de la entrada: se limpia antes de ampliar.
inline constexpr int kOrdenMejora = 10;
inline constexpr int kOrdenPtz = 20;
/// La posición interna con la memoria de la foto (no se lista ni se numera).
inline constexpr const char* kNombreFoto = "·foto del panel";
inline QString qs(const std::string& s) { return QString::fromStdString(s); }
} // namespace camaras

struct ControlCamaras::Camara {
    std::string entrada;
    QString backend = QStringLiteral("ninguno"); ///< ninguno | visca | onvif | panasonic
    QJsonObject conf;                            ///< sin la clave
    bool digital = true;
    QString mejora = QStringLiteral("general");                  ///< general | si | no
    QString modo = QStringLiteral("optico");                     ///< con hardware: qué mueve el joystick
    QJsonObject posicion{{"x", 0.5}, {"y", 0.5}, {"zoom", 1.0}}; ///< la digital, para volver a ella
    std::unique_ptr<PtzDigital> dig;
    std::unique_ptr<PtzBackend> hw;
    bool filtroPuesto = false; ///< el PTZ digital ya está en la fuente (y repuesto)
    Posiciones posiciones;     ///< las posiciones con nombre
    int movimientos = 0;       ///< cuántas veces se la movió (la foto sabe si sigue valiendo)
    /// La foto del panel PTZ: el archivo (en Miniaturas/ptz), cuántos movimientos
    /// había al sacarla y la posición interna con la memoria para volver.
    struct {
        QString archivo;
        int movimientos = -1;
        QString idMemoria;
        QString cuando;
        int rev = 0;
    } foto;
};

} // namespace ba::app::ptz
