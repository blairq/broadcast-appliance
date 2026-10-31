// ============================================================================
// src/emisor/Pedido.hpp — lo que la app le pide al emisor de una salida (F3-01)
//
// docs/arquitectura/04-plan-unificado.md, F3-01. La app y el `ba-emisor` de una
// salida se hablan por DOS archivos de Runtime/emisor/, no por un canal:
//
//   <id>.json         EL PEDIDO. Lo escribe solo la app (persist::escribir, crítico
//                     y privado: lleva la URL con la clave): qué transmitir y qué
//                     grabar, con la misma configuración de motor::Transmision y
//                     motor::Grabacion que usaría ella. `seq` sube con cada cambio.
//   <id>.estado.json  LO QUE PASA. Lo escribe solo el emisor, cada segundo: el
//                     estado de su transmisión y su grabación (el mismo JSON que
//                     dan esas clases), si está en la placa y qué `seq` aplicó.
//
// Por qué un archivo y no el canal de control: el pedido tiene que sobrevivir a la
// app (que es justo lo que se cae) y al emisor mismo. Si la app muere, el emisor
// sigue con lo último pedido; si muere el emisor, el launcher lo relanza y retoma
// del archivo sin esperar a nadie; tras un corte de luz, vuelve todo como estaba.
//
// LA APP QUE VUELVE ADOPTA lo que el emisor está haciendo (su estado dice que graba
// o transmite eso): vuelve a escribir el mismo bloque (con otro `seq`), así la
// grabación sigue en el mismo archivo y la conexión no se toca. Si el emisor no lo
// está haciendo (se cerró en orden, o es otra sesión), lo pedido es nuevo.
//
// EL CIERRE ORDENADO es del emisor: el launcher lo cierra después de A, cierra los
// archivos y deja `cerrado` en su estado con el `seq` que tenía. Al volver a abrir no
// retoma ese pedido: espera uno nuevo (la app que restaura tras un corte de energía
// con batería, F1-11, lo vuelve a pedir; la que se cerró en orden, no).
//
// Sin GStreamer andando: solo los tipos de motor/Salidas.hpp y JSON (lo usan la
// app, el emisor y sus pruebas).
// ============================================================================
#pragma once

#include "Salidas.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace ba::emisor {

struct Pedido {
    std::uint64_t seq = 0;
    /// El cuadro que sale (el canal se escala a esto): el de salida de la app.
    int ancho = 1920, alto = 1080, fpsN = 30, fpsD = 1;
    /// Lo que dice la placa mientras la app no publica («Volvemos enseguida», en el
    /// idioma del evento: lo traduce la app).
    std::string placaTexto;
    std::optional<motor::Transmision::Config> transmision;
    std::optional<motor::Grabacion::Config> grabacion;
    /// La copia de la grabación en otro disco (mismo codificador, otra carpeta).
    std::optional<motor::Grabacion::Config> copia;

    /// Transmite o graba algo.
    bool hayAire() const { return transmision || grabacion || copia; }
};

nlohmann::json aJson(const motor::Transmision::Config& c);
nlohmann::json aJson(const motor::Grabacion::Config& c);
nlohmann::json aJson(const Pedido& p);
/// Lo que haya en `j` (lo que falta queda de fábrica; un tipo malo también).
Pedido pedidoDe(const nlohmann::json& j);

/// Runtime/emisor/ (`runtime` es la Runtime/ de los datos).
std::filesystem::path carpeta(const std::filesystem::path& runtime);
std::filesystem::path rutaPedido(const std::filesystem::path& runtime, const std::string& id);
std::filesystem::path rutaEstado(const std::filesystem::path& runtime, const std::string& id);

/// Lee el pedido. false si no hay o no se entiende (queda `p` como estaba).
bool leer(const std::filesystem::path& ruta, Pedido& p);
/// Lo escribe entero o nada (crítico: sobrevive a un corte; privado: lleva la clave).
bool escribir(const std::filesystem::path& ruta, const Pedido& p, std::string* error = nullptr);

/// Un nombre de grabación que no pise archivos que ya están en `carpeta`: el mismo
/// si no hay ninguno que empiece con él; si no, «<nombre>-sigue2», «-sigue3»… (el
/// emisor que vuelve tras una caída o un corte sigue en otro archivo, no encima).
std::string nombreLibre(const std::filesystem::path& carpeta, const std::string& nombre);

} // namespace ba::emisor
