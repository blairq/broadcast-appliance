// ============================================================================
// src/motor/GpuPerdida.hpp — la GPU que se pierde (F3-09, 03 D7)
//
// Un TDR de Windows (el driver se reinicia: «device removed»), un driver que se cae o
// un contexto GL que se pierde dejan al motor sin la placa de video: cada elemento de
// GPU (d3d12…, gl…) empieza a dar error o deja de entregar, y el programa queda negro o
// congelado (antes solo lo veía la alarma de programa quieto, a los 30 s).
//
// DETECTAR. Tres caminos, el primero que llega:
//   - D3D12: el dispositivo del motor dice que se perdió. GStreamer consulta
//     GetDeviceRemovedReason cuando una operación de D3D12 falla y lo publica en la
//     propiedad «device-removed-reason» del GstD3D12Device (con su notify): el motor
//     se cuelga de ese aviso al guardar el dispositivo (autoprueba de Piezas.cpp) y lo
//     vuelve a mirar con cada error de un elemento.
//   - El texto del error lo dice: DXGI_ERROR_DEVICE_REMOVED/HUNG/RESET (o su código),
//     GL_CONTEXT_LOST, «context lost», los reset de GL_KHR_robustness.
//   - Un elemento de GPU da un error que no dice por qué: se prueba aparte si la GPU
//     del motor todavía entrega cuadros (gpuEntrega, un pipeline chico con el MISMO
//     contexto). Si no entrega, se perdió; si entrega, era otra cosa (una negociación
//     que falló) y no se toca nada. Una prueba a la vez y como mucho una cada 5 s.
// Quien atiende el bus de un pipeline del motor (Composicion, Entrada, Salidas,
// Vistas) le pasa cada ERROR a `revisarErrorDeGpu`.
//
// REARMAR. El motor no se rearma a sí mismo (sus pipelines son de la app, que tiene el
// estado: escenas, fuentes, salidas): queda la marca (`gpuPerdida`, barata) y el estado
// («gpu» en Motor::estado). La app la ve en su tic, apaga el motor, llama a
// `rearmarGpu` —suelta el contexto o el dispositivo viejo y prueba uno nuevo; si no
// anda, el video va a la CPU— y lo vuelve a armar con lo que había (en la CPU, a menor
// resolución) y retoma las salidas (AppRuntime::rearmarPorGpu). Mientras tanto la
// transmisión la sostiene ba-emisor con su placa (F3-01).
//
// La falla se puede inyectar (banco de caos, diag_falla «video»/«gpu»): el mezclador de
// la composición avisa el error que daría una GPU perdida (Fallas.cpp).
// ============================================================================
#pragma once

#include "Piezas.hpp"

#include <gst/gst.h>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace ba::motor {

/// Qué dice un error, sin GStreamer (para las pruebas).
enum class ErrorDeGpu {
    Ajeno,    ///< no es de la GPU
    Sospecha, ///< de un elemento de GPU, sin decir por qué: hay que probar si la GPU entrega
    Perdida,  ///< lo dice el texto (o el dispositivo): la GPU se perdió
};
/// `fabrica`: la fábrica del elemento que avisa (glvideomixer, d3d12convert…); `texto`:
/// el mensaje y el detalle del error.
ErrorDeGpu clasificarErrorDeGpu(const std::string& fabrica, const std::string& texto);

/// Lo mira quien atiende el bus de un pipeline del motor, con cada GST_MESSAGE_ERROR.
/// Barato si no es de la GPU. true si con esto la GPU quedó marcada como perdida (la
/// sospecha se confirma aparte: puede quedar marcada después).
bool revisarErrorDeGpu(GstMessage* m);

/// ¿Se perdió la GPU y todavía no se rearmó? Una carga atómica: lo mira la app en su tic.
bool gpuPerdida();
/// Marca la GPU como perdida (una vez hasta el próximo rearme): `motivo` al registro y
/// al estado. Lo usan la detección y el aviso del dispositivo D3D12.
void marcarGpuPerdida(const std::string& motivo, const std::string& elemento = "");

/// {gpu, perdida, motivo, elemento, hace_s, perdidas, rearmes, ultimo_rearme: {gpu, ms}}.
nlohmann::json estadoGpu();

/// Suelta el contexto GL o el dispositivo D3D12 del motor, prueba uno nuevo (la
/// autoprueba de siempre más gpuEntrega) y, si no anda, deja el video en la CPU. Limpia
/// la marca. Con `probarGpu` false va derecho a la CPU. Devuelve con qué quedó. Lo llama
/// la app con el motor apagado o por apagarse (los pipelines viejos se quedan con lo
/// suyo hasta que se desarman; los nuevos reciben lo nuevo en darContextoGpu).
Gpu rearmarGpu(bool probarGpu = true);

/// Lo que diría un elemento de esa GPU al perderla (la falla inyectada y las pruebas).
std::string textoDeGpuPerdida(Gpu g);

/// D3D12: colgarse del aviso «device-removed-reason» de los dispositivos que traen
/// estos contextos (los de la autoprueba). Suelta los de antes. En GL no hace nada.
void vigilarDispositivos(const std::vector<GstContext*>& contextos);

} // namespace ba::motor
