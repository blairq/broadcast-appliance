// ============================================================================
// src/app/sala/ParticipantesSala.hpp — lo que se le hace a una persona de la sala (modelo)
//
// Hacer pasar, no dejar pasar, sacar, hablarle, pedirle algo a su navegador,
// cambiarle la ficha, la prueba técnica y su foto. La GUI (la página Sala, el
// lobby) y el MCP (participant_*, invitado_prueba, foto_invitado) pasan por
// acá: el mismo pedido a la API y la misma respuesta.
//
// «Admitir a todos» también es de acá y no un bucle en cada lado: los que
// esperan, en orden de llegada, de a uno (si se mandaran todos juntos, el
// orden en que entran lo decidiría la red), y se corta al primer «sala llena»
// (los que siguen tampoco entrarían).
//
// Las personas van por su id; buscar por nombre es cosa de quien lo pide.
// ============================================================================
#pragma once

#include <QByteArray>

#include <functional>
#include <optional>
#include <string>

#include "sala/Pedido.hpp"

namespace ba::app::sala {

class ParticipantesSala {
   public:
    /// pedidos = cuántos esperaban; admitidos = cuántos pasaron; error = el
    /// primero que contestó la sala ("" si pasaron todos).
    using ListoTodos = std::function<void(int pedidos, int admitidos, const std::string& error)>;
    using ListoFoto = std::function<void(bool ok, const QByteArray& jpeg)>;

    explicit ParticipantesSala(ConferenceClient& sala);

    void admitir(const std::string& id, Listo listo = {});
    /// Hace pasar a todos los que esperan en el lobby, en orden de llegada.
    void admitirATodos(ListoTodos listo = {});
    void rechazar(const std::string& id, Listo listo = {});
    void sacar(const std::string& id, Listo listo = {});
    /// Lobby: hablarle en privado; adentro: IFB.
    void hablar(const std::string& id, bool on, Listo listo = {});
    /// Al navegador: mute-mic, mute-cam, unmute-mic, unmute-cam, stop-screen,
    /// lower-hand, calidad-max, calidad-720p, calidad-360p. Devuelve el error inmediato o "".
    std::string pedirle(const std::string& id, const std::string& que, Listo listo = {});
    /// La ficha: solo cambia los campos que vienen.
    void perfil(const std::string& id, const std::optional<std::string>& nombre, const std::optional<std::string>& info,
                const std::optional<std::string>& contacto, Listo listo = {});
    /// Prueba técnica (eco y red, ~5 s). La respuesta trae `recomendacion`.
    void prueba(const std::string& id, Listo listo);
    /// La última foto que mandó su navegador (JPEG).
    void foto(const std::string& id, ListoFoto listo);

   private:
    void accion(const std::string& id, const std::string& accion, const nlohmann::json& cuerpo, Listo listo);

    ConferenceClient& sala_;
};

/// La ruta de una persona en la API ("/api/v1/participants/<id>/<accion>"), con el id escapado.
std::string rutaDe(const std::string& id, const std::string& accion);

} // namespace ba::app::sala
