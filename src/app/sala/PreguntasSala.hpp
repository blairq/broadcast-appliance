// ============================================================================
// src/app/sala/PreguntasSala.hpp — la cola de preguntas del público (modelo)
//
// Ver, moderar y sumar preguntas. La GUI (Público, el chat) y el MCP
// (preguntas_ver, pregunta_moderar, pregunta_sumar, pregunta_desde_chat) pasan
// por acá, con las mismas reglas: las acciones que van sobre una pregunta
// necesitan su id; una pregunta vacía no se suma.
// ============================================================================
#pragma once

#include <functional>
#include <optional>
#include <string>

#include "sala/Pedido.hpp"

namespace ba::app::sala {

class PreguntasSala {
   public:
    /// Un mensaje del chat (YouTube, Twitch, Facebook o la sala) por su clave.
    struct MensajeChat {
        std::string texto;
        std::string autor;
        std::string deDonde; ///< sala | youtube | twitch | facebook
    };
    using BuscarEnChat = std::function<std::optional<MensajeChat>(const std::string& clave)>;

    PreguntasSala(ConferenceClient& sala, BuscarEnChat buscarEnChat);

    void ver(Listo listo);
    /// elegir, des_elegir, al_aire, responder, descartar (con `id`); limpiar, vaciar.
    /// Devuelve el error inmediato o "".
    std::string moderar(const std::string& accion, const std::string& id, Listo listo);
    /// `deDonde`: por dónde llegó (sala, publico, youtube, twitch, facebook).
    std::string sumar(const std::string& texto, const std::string& nombre, const std::string& deDonde, Listo listo);
    /// Suma un mensaje del chat por su clave.
    std::string sumarDesdeChat(const std::string& clave, Listo listo);

   private:
    ConferenceClient& sala_;
    BuscarEnChat buscarEnChat_;
};

} // namespace ba::app::sala
