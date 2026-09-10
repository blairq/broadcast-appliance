// DeviceIdentity.hpp
//
// Proposito:
//   Estructura de datos pura con la IDENTIDAD FISICA de un dispositivo de
//   hardware (spec 9.2). No contiene logica: es el "registro" que el watcher de
//   dispositivos (GATED:Win, WASAPI/cfgmgr32) llena y que los algoritmos de
//   matching (DeviceMatcher) y de resolucion de arbol (DeviceIdentityResolver)
//   consumen. Al ser solo datos, se modela y se testa en Linux aunque la
//   enumeracion real solo exista en Windows.
//
// Modelo de threading:
//   Trivialmente copiable y const-ref: quien lo llena (el worker de
//   notificaciones de hot-plug, spec 9.6) lo publica por valor y los
//   consumidores lo leen por const-ref. No tiene estado mutable compartido,
//   por lo que no requiere sincronizacion propia; la seguridad entre threads la
//   aporta quien guarda la coleccion (p. ej. el hardware_map, con su lock).
//
// Estrategia de errores:
//   Ninguna: es un plain-old-data. Los campos que "no aplican" se dejan en su
//   valor por defecto (string vacio, 0) y el significado de cada campo lo
//   documenta su docstring; en particular `serial` vacio significa "el
//   dispositivo no reporta un serial unico" (spec 9.4: muchas capturadoras
//   baratas no lo reportan), NO un error.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ba::hw {

/// Identidad fisica completa de un dispositivo de audio/video (spec 9.2).
///
/// Reune los datos que permiten re-identificar el MISMO aparato fisico cuando
/// se reconecta (mismo puerto o no) o cuando se cambia de equipo: el id del
/// endpoint (o device path de dshow para video), el ContainerId (agrupa las
/// funciones del mismo aparato, ej. audio embebido + video de una
/// capturadora), el id de instancia USB padre con VID/PID/MI, el serial (solo
/// si es unico), las rutas de ubicacion, el nombre amigable y el driver.
///
/// Los campos opcionales se dejan vacios/0 cuando el sistema no los expone;
/// el matching (DeviceMatcher) degrada el puntaje segun que campos faltan.
struct DeviceIdentity {
    /// Id del endpoint WASAPI (audio) o device path de dshow (video). Es la
    /// clave de identidad mas fuerte: si coincide, es el mismo endpoint exacto
    /// (spec 9.4, nivel 1). Vacio si aun no se resuelve.
    std::string endpointId;

    /// DEVPKEY_Device_ContainerId: agrupa las funciones del mismo aparato
    /// fisico, lo que permite vincular automaticamente el audio embebido de una
    /// capturadora con su video (spec 9.2). Vacio si no esta disponible.
    std::string containerId;

    /// Id de instancia del padre USB (el nodo USB por encima del endpoint).
    /// Vacio si el dispositivo no es USB (p. ej. placa integrada).
    std::string usbInstanceId;

    /// Vendor ID del dispositivo USB. 0 si no aplica.
    std::uint32_t vid = 0;

    /// Product ID del dispositivo USB. 0 si no aplica.
    std::uint32_t pid = 0;

    /// Interface number (MI) del dispositivo USB. 0 si no aplica.
    std::uint32_t mi = 0;

    /// Numero de serie, SOLO si es unico (capacidad UniqueID, spec 9.2).
    /// Vacio si el dispositivo no reporta serial unico (muy comun en
    /// capturadoras e interfaces baratas, spec 9.4).
    std::string serial;

    /// DEVPKEY_Device_LocationPaths: rutas de ubicacion del dispositivo
    /// (puerto/racon). Dos dispositivos iguales solo se distinguen por el
    /// puerto (spec 9.4). Vacio si no hay.
    std::vector<std::string> locationPaths;

    /// Nombre amigable mostrado por el sistema (p. ej. "USB Audio Device").
    std::string friendlyName;

    /// Nombre del driver en uso.
    std::string driver;
};

} // namespace ba::hw
