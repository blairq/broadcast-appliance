// ============================================================================
// src/app/sala/ClavesEvento.hpp — las claves de las superficies de la sala, del evento
//
// El teleprompter, la corrección de subtítulos, los rótulos, el tablero y la página del
// público piden una clave que viaja en su enlace (y en su QR). Antes las inventaba la
// sala en cada arranque: el QR del público impreso en el programa dejaba de andar tras
// una caída o un «aplicar configuración». Ahora son del EVENTO:
//
//   - se generan la primera vez que se abre el evento (aleatorias, 16 bytes) y se
//     guardan CIFRADAS en el almacén de credenciales (nunca en la carpeta en claro), con
//     un nombre que sale de la carpeta del evento;
//   - se le pasan a la sala cada vez que conecta una nueva (republicador «claves») y al
//     cambiar de evento: otro evento, otras claves (los QR de ayer no abren lo de hoy);
//   - las del evento en curso quedan además en el almacén (ba::ipc::kClavesEnCurso): una
//     sala que arranca ya las tiene, sin esperar a que A reconecte;
//   - al cerrar el evento se le dice a la sala «sin evento» ({sinEvento}): claves nuevas
//     y olvida la urna, las preguntas y la página del público del anterior.
//
// Leer y escribir el almacén es disco: va en otro hilo; la sala las recibe cuando están.
// ============================================================================
#pragma once

#include <QObject>

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>

#include <nlohmann/json.hpp>

namespace ba::app {
class ConferenceClient;
}

namespace ba::app::sala {

class ClavesEvento : public QObject {
    Q_OBJECT
   public:
    /// `carpeta`: la del evento abierto ("" si no hay; entonces valen las de la sala).
    ClavesEvento(ConferenceClient& sala, std::function<std::filesystem::path()> carpeta, QObject* parent = nullptr);

    /// Se abrió otro evento (o ninguno): se buscan (o crean) sus claves y van a la sala.
    void eventoCambio();

    /// El nombre en el almacén para la carpeta de un evento (sin la ruta en claro).
    static std::string nombreEnAlmacen(const std::filesystem::path& carpeta);
    /// Las de esa carpeta: las guardadas o, si no hay, nuevas (y se guardan). Bloquea (disco).
    static nlohmann::json leerOCrear(const std::filesystem::path& carpeta);

   private:
    void mandar();

    ConferenceClient& sala_;
    std::function<std::filesystem::path()> carpeta_;
    std::filesystem::path deEstas_; ///< de qué evento son las que hay
    nlohmann::json claves_;         ///< {prompter, correccion, rotulos, publico, tablero}, {sinEvento} o null
    int pedido_ = 0;                ///< para descartar una lectura vieja que vuelve tarde
    /// El último pedido, visto desde el pool (solo ese escribe las del evento en curso).
    std::shared_ptr<std::atomic<int>> ultimoPedido_ = std::make_shared<std::atomic<int>>(0);
};

} // namespace ba::app::sala
