// src/app/red/Rtsp.hpp — preguntarle a un equipo si tiene un stream RTSP en una ruta
//
// Un DESCRIBE (lo que hace cualquier reproductor antes de mirar): 200 = la ruta
// existe y se ve; 401 = existe pero pide usuario y clave (y dice cómo:
// Basic o Digest); 404 o 400 = no es esa. Con usuario y clave se repite
// con la autorización (Digest MD5, RFC 2617, o Basic). Del SDP de la
// respuesta salen el códec y, si los declara, el tamaño.
#pragma once

#include <QByteArray>
#include <QString>

namespace ba::app::red::rtsp {

struct Url {
    QString host, ruta; ///< ruta con lo que sigue al host (incluye ?query)
    int puerto = 554;
    QString usuario, clave;
    bool ok = false;
};
/// rtsp://[usuario:clave@]host[:puerto]/ruta
Url partir(const QString& url);
/// La URL sin usuario ni clave (para mostrar).
QString sinClave(const QString& url);
/// La URL con usuario y clave (para conectarse).
QString conClave(const QString& url, const QString& usuario, const QString& clave);

struct Respuesta {
    int codigo = 0;          ///< 0 = no se entendió
    QString esquema;         ///< Basic | Digest (de WWW-Authenticate)
    QString realm, nonce;
    QByteArray sdp;          ///< el cuerpo, si vino
    QString codec;           ///< del SDP: H264, H265, JPEG, MP4V…
    int ancho = 0, alto = 0; ///< del SDP, si lo dice (x-dimensions / framesize)
    bool completa = false;   ///< ya llegó todo (encabezados + cuerpo)
};
/// Lee lo que contestó (puede llegar en pedazos: `completa` dice si alcanza).
Respuesta leer(const QByteArray& datos);

/// El pedido DESCRIBE para `url` (sin credenciales en la línea), con la
/// autorización si se pasa.
QByteArray describe(const QString& url, int cseq, const QString& autorizacion = {});
/// La línea Authorization para un DESCRIBE de `url` con esas credenciales.
QString autorizacion(const Respuesta& desafio, const QString& url, const QString& usuario, const QString& clave);

/// Lo que se dice de una prueba: "se ve", "pide usuario y clave", "no existe", "no contesta".
QString estadoDe(int codigo);

} // namespace ba::app::red::rtsp
