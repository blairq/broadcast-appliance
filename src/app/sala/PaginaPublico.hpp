// ============================================================================
// src/app/sala/PaginaPublico.hpp — cómo se ve la página del público (modelo)
//
// El título, el renglón de cuándo y el fondo de la página donde el público
// pregunta y vota. Lo piden la GUI (Público) y el MCP (publico_pagina), con las
// mismas reglas:
//
//   - el fondo es una imagen del disco (JPEG o PNG) de hasta 2 MB: la bajan
//     TODOS los teléfonos del salón, por la misma red que suele llevar la
//     transmisión; vacío lo saca;
//   - lo que no viene queda como está.
//
// ES DEL EVENTO: lo pedido se guarda en <evento>/publico.json (la ruta del fondo,
// no la imagen) y se le vuelve a dar a una sala que se reinició (republicador
// «publico») y al abrir el evento. La sala solo lo repite.
// ============================================================================
#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

#include <QObject>

#include "sala/Pedido.hpp"

namespace ba::app::sala {

class PaginaPublico {
   public:
    /// Tope del fondo (ver arriba).
    static constexpr long long kMaxFondo = 2LL * 1024 * 1024;

    explicit PaginaPublico(ConferenceClient& sala);

    /// `fondo`: la ruta de la imagen; "" lo saca. Devuelve el error inmediato (no hay nada
    /// que cambiar) o ""; la imagen se lee en otro hilo y lo que falle (no se puede leer,
    /// pesa demasiado, la sala no lo tomó) va a `listo`, ya dicho para mostrarlo tal cual.
    std::string cambiar(const std::optional<std::string>& titulo, const std::optional<std::string>& cuando,
                        const std::optional<std::string>& fondo, Listo listo = {});

    /// De dónde sale la carpeta del evento abierto ("" si no hay).
    void setCarpetaEvento(std::function<std::filesystem::path()> carpeta);
    /// Se abrió otro evento: se lee lo suyo (en otro hilo) y va a la sala.
    void eventoCambio();

    /// Lo que se le manda a la sala para `pedido` ({titulo?, cuando?, fondo?}): el
    /// cuerpo de /api/v1/publico-info, o el error (la imagen no se lee o pesa de más).
    /// Lee el disco.
    static std::string armar(const nlohmann::json& pedido, nlohmann::json& cuerpo);

   private:
    /// Lo guardado, a la sala (lee la imagen en otro hilo).
    void mandarGuardado();
    /// Arma el cuerpo en el pool (la imagen es disco) y lo manda desde el hilo de la interfaz.
    /// `sinFondoSiFalla`: si la imagen ya no está, va lo demás sin ella (al republicar).
    void enviar(const nlohmann::json& pedido, bool sinFondoSiFalla, Listo listo);

    ConferenceClient& sala_;
    std::function<std::filesystem::path()> carpeta_;
    std::filesystem::path deEste_;
    nlohmann::json guardado_ = nlohmann::json::object(); ///< {titulo?, cuando?, fondo?} del evento
    int lectura_ = 0;
    QObject vida_; ///< mientras vive, la sala le pide que republique
};

} // namespace ba::app::sala
