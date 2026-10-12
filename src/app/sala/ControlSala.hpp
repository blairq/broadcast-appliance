// ============================================================================
// src/app/sala/ControlSala.hpp — la sala en sí: entrada, PIN, chat, proxy y red (modelo)
//
// Lo que no es de una persona ni de los oradores: cómo se entra (lobby, sala
// cerrada, atención automática del asistente), el PIN y sus bloqueos, el chat
// del operador, los archivos compartidos, el proxy por nombre y la red. La GUI
// (la página Sala, Configuración) y el MCP (room_set, room_pin,
// room_pin_bloqueos, chat_send, share_delete, proxy_*, network_status) pasan
// por acá, con las mismas reglas:
//
//   - la atención automática se guarda en la configuración y la sala se entera
//     también cuando arranca después que el programa;
//   - un mensaje vacío no se manda; sin destinatario va a todos;
//   - el proxy: solo las claves que entiende, prendido pide dominio, se guarda
//     y la sala se reinicia (aparte: esperar el cierre trababa la interfaz).
// ============================================================================
#pragma once

#include <QMetaObject>

#include <functional>
#include <optional>
#include <string>

#include "sala/Pedido.hpp"

namespace ba::app {
class AppRuntime;
}

namespace ba::app::sala {

class ControlSala {
   public:
    /// "" = la sala se reinició; si no, por qué no. Llega en el hilo de la interfaz.
    using Reiniciada = std::function<void(const std::string& error)>;

    ControlSala(AppRuntime& rt, ConferenceClient& sala);
    ~ControlSala();
    ControlSala(const ControlSala&) = delete;
    ControlSala& operator=(const ControlSala&) = delete;

    // --- La entrada ------------------------------------------------------------
    /// Lobby (cada uno espera que lo hagan pasar) y/o sala cerrada (nadie nuevo
    /// entra). Lo que no viene queda como está. Devuelve el error inmediato o "".
    std::string entrada(std::optional<bool> lobby, std::optional<bool> cerrada, Listo listo = {});
    /// El asistente atiende solo a los que llegan al lobby. Se guarda.
    bool atencionAutomatica() const;
    void setAtencionAutomatica(bool on);

    // --- El PIN ----------------------------------------------------------------
    /// PIN nuevo (vacío = que la sala genere uno). La respuesta trae `pin`.
    void cambiarPin(const std::string& pin, Listo listo = {});
    /// Quién está bloqueado por poner mal el PIN (la respuesta trae `bloqueos`).
    void verBloqueosPin(Listo listo);
    /// Levanta los bloqueos de esa IP, o (vacía) todos, el global incluido.
    void desbloquearPin(const std::string& ip, Listo listo = {});

    // --- Chat y archivos -------------------------------------------------------
    /// `para`: "all" (o vacío) o el id de una persona. Devuelve el error inmediato o "".
    std::string chatear(const std::string& para, const std::string& texto, Listo listo = {});
    void borrarArchivo(const std::string& id, Listo listo = {});
    /// Un código de un solo uso para entrar a la consola web (la respuesta trae `url`).
    void codigoConsolaWeb(Listo listo);

    // --- Proxy y red -----------------------------------------------------------
    /// Lo que dice la sala del proxy, más la configuración guardada
    /// (`configuracion`), que viene siempre aunque la sala no conteste.
    void verProxy(Listo listo);
    /// Pide ya el certificado de `host` (vacío = todos).
    void renovarProxy(const std::string& host, Listo listo = {});
    /// Cambia la configuración del proxy (ver sala::mezclarProxy), la guarda y
    /// reinicia la sala. Devuelve el error inmediato o ""; el reinicio, después.
    std::string configurarProxy(const nlohmann::json& cambios, Reiniciada reiniciada = {});
    /// HTTPS, túnel WireGuard, IPs y puertos de la sala.
    void verRed(Listo listo);
    /// Arma la configuración del servidor con IP pública (`endpoint` host:puerto)
    /// y deja este equipo apuntando a él con el túnel prendido; la sala se
    /// reinicia. `listo` recibe la configuración para copiar (`config`).
    /// Devuelve el error inmediato o "".
    std::string accesoDirecto(const std::string& endpoint, const std::string& direccionPublica,
                              const std::string& interfaz, Listo listo, Reiniciada reiniciada = {});

   private:
    /// Reinicia la sala sin trabar a quien lo pide.
    void reiniciar(Reiniciada reiniciada);

    AppRuntime& rt_;
    ConferenceClient& sala_;
    QMetaObject::Connection alConectar_;
};

} // namespace ba::app::sala
