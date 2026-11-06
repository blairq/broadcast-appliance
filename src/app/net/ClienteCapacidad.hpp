// ============================================================================
// src/app/net/ClienteCapacidad.hpp — lo de red de la prueba de capacidad (fuera de la interfaz)
//
// docs/funciones/CAPACIDAD.md. Dos cosas, las dos con HttpsClient (mbedtls, el del
// programa: nada del sistema) en un hilo del pool, y la respuesta encolada al hilo de
// `quien` (si `quien` se fue, no llega):
//
//   - la MEDIDA de otro BroadcastAppliance que está «recibiendo» una prueba:
//     GET <url>/api/prueba-sala/medida con la credencial en la cabecera X-BA-Prueba
//     (nunca en la URL) y, si la caja es autofirmada, su certificado fijado por la huella
//     que vino en el código;
//   - la conexión «ida y vuelta» por el túnel (pinggy): esta máquina le pide relleno a su
//     propia dirección pública (GET /api/capacidad/relleno con el secreto en X-BA-Medir),
//     así cada byte cruza la subida y la bajada; antes y durante, la demora (GET /healthz).
// ============================================================================
#pragma once

#include <QObject>
#include <QString>

#include <atomic>
#include <functional>
#include <memory>

#include <nlohmann/json.hpp>

namespace ba::app::net {

/// {ok, status, medida?, participantes?, error?}
/// `generador`: lo de esta máquina que genera (CPU, red, ficha), que el otro guarda para su
/// informe; viaja en la cabecera X-BA-Generador (JSON en ASCII), con la credencial.
void pedirMedidaRemota(QObject* quien, const QString& url, const QString& credencial, const QString& huella,
                       std::function<void(const nlohmann::json&)> listo, const nlohmann::json& generador = nlohmann::json());

/// Cuánto dura cada parte de la medición ida y vuelta.
struct IdaYVuelta {
    int pings = 8;          ///< en reposo
    int cargaMs = 8000;     ///< bajando relleno
    int conexiones = 4;     ///< a la vez
    int kbPorPedido = 1024;
};
/// {ok, kbps, rttMs (p50 en reposo), rttP95Ms, rttCargaMs (p50 cargado), perdida (0..1), bytes, error?}
/// `cancelar` corta entre pedidos.
void medirIdaYVuelta(QObject* quien, const QString& urlPublica, const QString& secreto, IdaYVuelta cfg,
                     std::shared_ptr<std::atomic<bool>> cancelar, std::function<void(const nlohmann::json&)> listo);

} // namespace ba::app::net
