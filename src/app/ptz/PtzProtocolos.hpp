// src/app/ptz/PtzProtocolos.hpp — lo que se manda por el cable a cada cámara
//
// Funciones puras (sin red ni estado): arman los bytes, el XML o la URL de
// cada orden y leen las respuestas. Los backends (PtzVisca, PtzOnvif,
// PtzPanasonic) las usan para hablar; los tests las prueban byte a byte.
//
// Escrito desde las especificaciones de cada protocolo (Sony VISCA y VISCA
// over IP, ONVIF Core/PTZ/Media 2.x, WS-Security UsernameToken 1.0,
// WS-Discovery 2005/04, Panasonic AW «HD Integrated Camera Interface»), no
// desde código de terceros: el proyecto es GPLv3 y la referencia más usada
// (obs-ptz) es GPLv2 sin «o posterior», incompatible.
//
// Convención de todos: velocidades normalizadas -1..1 (pan: + derecha,
// tilt: + arriba, zoom: + acercar, foco: + lejos); 0 = parar ese eje.

#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <cstdint>

namespace ba::app::ptz {

// ---------------------------------------------------------------------------
// VISCA (Sony; también PTZOptics, BirdDog, Avonic, Marshall, AVer, Lumens…)
// ---------------------------------------------------------------------------
namespace visca {

/// Pan/tilt continuo: 8x 01 06 01 VV WW XX YY FF. Velocidad de pan 01..18h,
/// de tilt 01..14h (lo estándar de Sony; casi todas aceptan eso).
QByteArray panTilt(double pan, double tilt, int direccion = 1);
/// Zoom continuo: tele 2p, wide 3p (p 0..7), 00 = parar.
QByteArray zoom(double v, int direccion = 1);
/// Foco continuo: lejos 2p, cerca 3p, 00 = parar.
QByteArray foco(double v, int direccion = 1);
QByteArray focoAuto(bool automatico, int direccion = 1);
QByteArray focoUnToque(int direccion = 1);
/// Presets: 8x 01 04 3F 0a pp FF (a: 1 guardar, 2 ir). pp 0..127.
QByteArray preset(bool guardar, int numero, int direccion = 1);
/// Velocidad con la que se va a una memoria (Sony): 8x 01 7E 01 0B pp vv FF,
/// vv 01..18 (velocidad 0..1). La cámara que no la conoce contesta error y
/// va a la suya.
QByteArray velocidadPreset(int numero, double velocidad, int direccion = 1);
/// Borra una memoria: 8x 01 04 3F 00 pp FF.
QByteArray borrarPreset(int numero, int direccion = 1);
QByteArray home(int direccion = 1);

/// Encabezado VISCA over IP de Sony (UDP 52381): tipo (0x0100 orden,
/// 0x0110 consulta, 0x0200 control), largo y número de secuencia.
QByteArray conEncabezado(std::uint16_t tipo, const QByteArray& visca, std::uint32_t secuencia);
/// Reinicia la secuencia (control 0x0200, carga 01).
QByteArray reiniciarSecuencia();

/// Una respuesta VISCA: 90 4z FF (recibida), 90 5z FF (hecha), 90 6z ee FF (error).
struct Respuesta {
    enum Tipo { Ninguna, Recibida, Hecha, Error } tipo = Ninguna;
    int codigo = 0; ///< el ee de un error
};
/// Lee una respuesta (con o sin el encabezado de Sony).
Respuesta leer(const QByteArray& datos);
/// Texto de un código de error VISCA.
QString textoError(int codigo);

} // namespace visca

// ---------------------------------------------------------------------------
// Panasonic AW (HTTP: /cgi-bin/aw_ptz?cmd=%23...&res=1)
// ---------------------------------------------------------------------------
namespace panasonic {

/// #PTSxxyy: 01..99, 50 = quieto (pan 99 = derecha, tilt 99 = arriba).
QString panTilt(double pan, double tilt);
/// #Zxx: 99 = acercar lo más rápido, 01 = alejar, 50 = quieto.
QString zoom(double v);
/// #Fxx: 99 = lejos, 01 = cerca, 50 = quieto.
QString foco(double v);
QString focoAuto(bool automatico);
/// #Rnn ir / #Mnn guardar: nn 00..99 (el preset 1 es 00).
QString preset(bool guardar, int numero);
/// #Cnn: borra la memoria.
QString borrarPreset(int numero);
/// Al centro: #APC80008000.
QString home();
/// La URL completa de una orden.
QString url(const QString& host, int puerto, const QString& orden);
/// La cámara contesta la orden con la primera letra en minúscula; «er1/2/3» es error.
bool respuestaOk(const QString& orden, const QByteArray& respuesta);

} // namespace panasonic

// ---------------------------------------------------------------------------
// ONVIF (SOAP 1.2 sobre HTTP)
// ---------------------------------------------------------------------------
namespace onvif {

/// PasswordDigest de WS-Security: Base64(SHA1(nonce + created + clave)).
QByteArray digest(const QByteArray& nonce, const QString& creado, const QString& clave);

/// Sobre SOAP con el cuerpo dado. Con usuario, lleva el UsernameToken (la
/// hora `creado` es la de la cámara: la nuestra + el desfase medido). Los
/// tests pasan nonce y creado fijos; vacíos = al azar / ahora.
QByteArray sobre(const QString& cuerpo, const QString& usuario = {}, const QString& clave = {},
                 qint64 desfaseSeg = 0, const QByteArray& nonce = {}, const QString& creado = {});

// Cuerpos (el sobre los envuelve).
QString getSystemDateAndTime();
QString getCapabilities();
QString getProfiles();
QString continuousMove(const QString& perfil, double pan, double tilt, double zoom);
QString stop(const QString& perfil);
QString getPresets(const QString& perfil);
QString gotoPreset(const QString& perfil, const QString& preset, double velocidad = 0);
/// Guarda en un preset nuevo (sin token) o pisa uno existente.
QString setPreset(const QString& perfil, const QString& nombre, const QString& preset = {});
QString gotoHomePosition(const QString& perfil);
/// Dónde está el motor (y si se mueve).
QString getStatus(const QString& perfil);
/// Para mapear lo que la cámara ofrece de verdad (ver PtzOnvif::funciones).
QString getNodes();
QString getConfigurations();
QString getConfigurationOptions(const QString& configPtz);
QString imagingGetOptions(const QString& fuenteVideo);
QString imagingGetMoveOptions(const QString& fuenteVideo);
/// El cuerpo de una respuesta aplanado: "Nodo.Hijo.Hoja" -> texto y "Nodo.Hijo@attr" -> valor
/// (si un camino se repite, los valores se juntan con " | ").
QVariantMap aplanar(const QByteArray& xml);
/// De las respuestas aplanadas (nodos, configuraciones, opciones, imagen, imagen_mover):
/// qué eje se mueve cómo, límites, foco, home, presets, auxiliares; con el `crudo` adentro.
QVariantMap resumirFunciones(const QVariantMap& crudo);
/// Zoom relativo (TranslationGenericSpace, -1..1) y absoluto (PositionGenericSpace, 0..1).
QString relativeZoom(const QString& perfil, double dz);
QString absoluteZoom(const QString& perfil, double z);
QString removePreset(const QString& perfil, const QString& preset);
/// Foco por el servicio de imagen (timg:Move continuo / timg:Stop).
QString focoMover(const QString& fuenteVideo, double v);
QString focoParar(const QString& fuenteVideo);

/// Lo que importa de GetCapabilities.
struct Servicios {
    QString ptz, media, imaging;
};
Servicios leerCapacidades(const QByteArray& xml);
/// La hora de la cámara (GetSystemDateAndTime, en UTC). Inválida si no vino.
QDateTime leerHora(const QByteArray& xml);

struct Perfil {
    QString token, nombre, fuenteVideo;
    QString configPtz; ///< el token de su PTZConfiguration
    bool tienePtz = false;
};
QList<Perfil> leerPerfiles(const QByteArray& xml);

struct Preset {
    QString token, nombre;
};
QList<Preset> leerPresets(const QByteArray& xml);
/// El token que devuelve SetPreset.
QString leerTokenPreset(const QByteArray& xml);
/// Si la respuesta es un Fault, su texto (vacío si no).
QString leerFalla(const QByteArray& xml);
struct EstadoPtz {
    bool ok = false;
    double pan = 0, tilt = 0, zoom = 0;
    bool hayZoom = false;
    QString movPanTilt, movZoom; ///< IDLE | MOVING | UNKNOWN
};
EstadoPtz leerEstadoPtz(const QByteArray& xml);

/// WS-Discovery: el Probe (multicast 239.255.255.250:3702).
QByteArray sondeo(const QString& idMensaje);
struct Encontrada {
    QString direccion; ///< el device_service (el primer XAddr)
    QString nombre;    ///< del scope onvif://www.onvif.org/name/...
    QString modelo;    ///< del scope .../hardware/...
    QStringList todas; ///< todos los XAddr (puede anunciar una 169.254 además de la de la red)
    QString origen;    ///< la IP de la que vino la respuesta (la de verdad)
};
/// Lee un ProbeMatch (vacío si no lo es).
Encontrada leerEncontrada(const QByteArray& xml);

} // namespace onvif

} // namespace ba::app::ptz
