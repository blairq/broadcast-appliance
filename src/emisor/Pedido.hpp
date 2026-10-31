// ============================================================================
// src/emisor/Pedido.hpp — lo que la app le pide al emisor de una salida (F3-01)
//
// docs/arquitectura/04-plan-unificado.md, F3-01. La app y el `ba-emisor` de una
// salida se hablan por DOS archivos de Runtime/emisor/, no por un canal:
//
//   <id>.json         EL PEDIDO. Lo escribe solo la app (persist::escribir, crítico):
//                     qué transmitir (la principal, los destinos extra, la señal
//                     limpia) y qué grabar (con las pistas 2-6), la repetición, con
//                     la misma configuración de motor::Transmision y motor::Grabacion
//                     que usaría ella. `seq` sube con cada cambio.
//   Credenciales/emisor-<id>.cred   LAS CLAVES. Las URL de transmisión llevan la
//                     clave del canal: no van en el pedido (que queda con "url": "")
//                     sino cifradas en el CredentialStore del equipo (las escribe la
//                     app ANTES del pedido; el emisor las lee al leerlo).
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
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ba::emisor {

/// La repetición (motor::Repeticion): graba sin parar y se queda con los últimos
/// `segundos`. Guardar es pedir con `guardarSeq` uno más y `guardarEn` el clip.
struct PedidoRepeticion {
    int segundos = 30;
    std::string carpeta; ///< los tramos van en <carpeta>/.tramos
    int kbps = 6000;
    std::uint64_t guardarSeq = 0;
    std::string guardarEn;
};

/// La señal limpia (el programa sin DSK, placa ni negro): a un destino o a un archivo.
/// Su video llega por otro canal (CanalesSalida.hpp, canalVideoLimpioEmisor).
struct PedidoLimpio {
    std::optional<motor::Transmision::Config> transmision;
    std::optional<motor::Grabacion::Config> grabacion;
};

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
    /// Las pistas 2-6 que la grabación suma a la del programa (cada una por su anillo,
    /// CanalesSalida.hpp, canalAudioPistaEmisor).
    std::vector<int> pistas;
    /// A qué pasar la grabación cuando se cierra en orden («mp4», «mov»; "" = queda MKV).
    std::string remux;
    /// Los destinos extra, por id (el mismo de la app).
    std::map<std::string, motor::Transmision::Config> extras;
    std::optional<PedidoRepeticion> repeticion;
    std::optional<PedidoLimpio> limpio;

    /// Transmite o graba algo.
    bool hayAire() const {
        return transmision || grabacion || copia || !extras.empty() || (limpio && (limpio->transmision || limpio->grabacion));
    }
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

/// Credenciales/ al lado de `runtime` (el CredentialStore del equipo).
std::filesystem::path credenciales(const std::filesystem::path& runtime);
/// El nombre de la credencial con las claves del pedido de la salida `id`.
std::string nombreCredencial(const std::string& id);

/// Lee el pedido de la salida `id` con sus claves. false si no hay o no se entiende
/// (queda `p` como estaba). Un pedido viejo con la URL adentro también sirve.
bool leer(const std::filesystem::path& runtime, const std::string& id, Pedido& p);
/// Lo escribe entero o nada (crítico: sobrevive a un corte): primero las claves en el
/// CredentialStore (o las borra si no hay ninguna), después el pedido sin ellas.
bool escribir(const std::filesystem::path& runtime, const std::string& id, const Pedido& p, std::string* error = nullptr);

/// Un nombre de grabación que no pise archivos que ya están en `carpeta`: el mismo
/// si no hay ninguno que empiece con él; si no, «<nombre>-sigue2», «-sigue3»… (el
/// emisor que vuelve tras una caída o un corte sigue en otro archivo, no encima).
std::string nombreLibre(const std::filesystem::path& carpeta, const std::string& nombre);

} // namespace ba::emisor
