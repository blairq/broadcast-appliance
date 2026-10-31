// ============================================================================
// src/gateway/HttpIo.hpp — E/S de sockets, HTTP/1.1 minimo y frames WebSocket
//
// Proposito:
//   Utilidades sin estado que comparten las dos superficies del gateway: la de
//   los invitados (SignalingServer: estatico, join, WebSocket de sala) y la de
//   control del operador (AdminApi: REST + eventos SSE). Antes vivian en un
//   namespace anonimo de SignalingServer.cpp; se separan para no duplicarlas.
//
//   Los descriptores viajan como `long long` para que el header no dependa de
//   winsock2.h / sys/socket.h (SOCKET en Windows es un entero sin signo).
//
// Estrategia de errores:
//   No lanza. Las funciones de E/S devuelven bool/int; un fallo cierra la
//   conexion en el caller.
// ============================================================================
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

namespace ba::gateway::http {

/// Tamano maximo de la cabecera de una peticion HTTP/WS (64 KiB).
inline constexpr size_t kMaxHeaderBytes = 64 * 1024;
/// Tamano maximo de un cuerpo HTTP leido a memoria (1 MiB). Las subidas de
/// archivos NO pasan por aca: se leen en streaming directo a disco.
inline constexpr size_t kMaxBodyBytes = 1u << 20;

bool socketInit();
void socketCleanup();
void closeFd(long long fd);
/// shutdown(SHUT_RDWR): desbloquea un recv de otro thread sin cerrar el fd.
void shutdownFd(long long fd);

/// Socket de escucha TCP en `bindAddress:port` (IPv4). -1 si fallo.
long long makeListen(const std::string& bindAddress, uint16_t port);
/// Puerto local de un socket ya enlazado (para cuando se pidio el puerto 0).
uint16_t localPort(long long fd);
/// 1 si hay algo para leer/aceptar, 0 si vencio el timeout, -1 en error.
int waitReadable(long long fd, int ms);
/// Fija (o quita, con ms == 0) el timeout de recepcion y envio.
void setSocketTimeout(long long fd, int ms);
bool setNoDelay(long long fd);

/// Sin plazo total: cada recv lo acota el timeout del socket (setSocketTimeout).
using Plazo = std::chrono::steady_clock::time_point;
inline constexpr Plazo kSinPlazo = Plazo::max();

/// Lee exactamente `n` bytes. false en EOF/error o si vence `hasta`.
bool readExact(long long fd, uint8_t* out, size_t n, Plazo hasta = kSinPlazo);
/// Lee hasta `n` bytes. :returns: >0 leidos, <=0 EOF/error.
int recvSome(long long fd, uint8_t* out, size_t n);
bool sendAll(long long fd, const void* data, size_t n);
/// Lee hasta el fin de cabecera (`\r\n\r\n`). Con `hasta`, el plazo es para TODA la
/// cabecera y no por recv: el que la manda de a un byte cada tanto (slowloris) no
/// retiene la conexión más que eso.
bool readHeaders(long long fd, std::string& headers, Plazo hasta = kSinPlazo);

/// Valor de una cabecera (busqueda case-insensitive, valor intacto).
std::string getHeader(const std::string& headers, const std::string& name);
std::map<std::string, std::string> parseQuery(const std::string& q);
/// Entero decimal sin lanzar; `fallback` si no es valido.
long long parseLong(const std::string& s, long long fallback);
std::string urlDecode(const std::string& s);

std::array<uint8_t, 20> sha1(const std::string& s);
std::string base64(const uint8_t* data, size_t n);
/// Al reves. Lo que no sea del alfabeto se SALTEA (los saltos de linea de un
/// base64 partido en renglones son legales), y si lo que queda no cierra en
/// grupos de cuatro devuelve vacio en vez de bytes a medias: media imagen es
/// peor que ninguna, porque parece que anduvo.
std::string deBase64(const std::string& s);

/// Texto de estado estandar ("OK", "Not Found", ...).
const char* statusText(int code);

/// Respuesta completa con `Connection: close`. `extraHeaders` va tal cual
/// (cada linea terminada en \r\n).
bool sendResponse(long long fd, int code, const std::string& contentType, const std::string& body,
                  const std::string& extraHeaders = {});

/// Lee un frame WS de cliente (enmascarado), reensamblando fragmentos.
bool readWsFrame(long long fd, std::string& payload, int& opcode);
/// Escribe un frame WS de servidor (sin mascara).
bool writeWsFrame(long long fd, int opcode, const std::string& payload);

} // namespace ba::gateway::http
