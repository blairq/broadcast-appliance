// ============================================================================
// src/app/sala/ControlSala.hpp — la sala en sí: entrada, PIN, chat, proxy y red (modelo)
//
// Lo que no es de una persona ni de los oradores: cómo se entra (lobby, sala
// cerrada, atención automática del asistente), el PIN y sus bloqueos, el chat
// del operador, los archivos compartidos, el proxy por nombre y la red. La GUI
// (la página Sala, Configuración) y el MCP (room_set, room_pin,
// room_pin_bloqueos, chat_send, share_view, share_take, share_delete, proxy_*,
// network_status) pasan por acá, con las mismas reglas:
//
//   - la atención automática se guarda en la configuración y la sala se entera
//     también cuando arranca después que el programa;
//   - un mensaje vacío no se manda; sin destinatario va a todos;
//   - un archivo compartido se toma con copia propia en Media/ (la carpeta de
//     la sala se limpia al reiniciarla) y entra a la escena al aire;
//   - el proxy: solo las claves que entiende, prendido pide dominio, se guarda
//     y la sala se reinicia (aparte: esperar el cierre trababa la interfaz).
// ============================================================================
#pragma once

#include <QByteArray>
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
    /// Un archivo compartido: sus datos (como los da la sala) y, si es una
    /// imagen, sus bytes.
    using Contenido =
        std::function<void(const std::string& error, const nlohmann::json& archivo, const QByteArray& bytes)>;
    /// Lo que quedó de tomar un archivo compartido.
    struct Tomado {
        std::string error;  ///< "" = entró a la escena
        std::string nombre; ///< el que le puso el invitado
        std::string fuente; ///< la fuente nueva
        std::string escena; ///< la escena al aire, donde entró
    };

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
    /// Los datos de un archivo compartido y, si es una imagen, sus bytes (para
    /// revisarla antes de ponerla al aire). Videos y audios: solo los datos.
    void verArchivo(const std::string& id, Contenido listo);
    /// Lo copia a Media/ y lo agrega a la escena al aire (imagen, video o audio).
    Tomado tomarArchivo(const std::string& id);
    /// Avisa cada fuente nueva que entra por tomarArchivo (para que la vista
    /// recargue sus listas, venga de la GUI o del MCP).
    void setAlTomarArchivo(std::function<void()> f) { alTomarArchivo_ = std::move(f); }

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
    /// El estado de los rótulos para el celular que los encarga (también es su
    /// latido); `listo` recibe los encargos pendientes ({encargos: […]}).
    void publicarRotulos(const nlohmann::json& estado, Listo listo);
    /// El estado público del evento para su página (/evento/).
    void publicarEventoWeb(const nlohmann::json& estado);
    /// Los códigos de votante acreditados en la puerta desde la última vez ({codigos: […]}).
    void acreditaciones(Listo listo);
    /// El nivel de registro del gateway (otro proceso): verlo y cambiarlo.
    void nivelLog(Listo listo);
    void setNivelLog(const std::string& nivel, Listo listo);
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
    std::function<void()> alTomarArchivo_;
};

} // namespace ba::app::sala
