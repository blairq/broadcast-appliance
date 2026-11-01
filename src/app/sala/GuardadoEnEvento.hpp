// ============================================================================
// src/app/sala/GuardadoEnEvento.hpp — lo que llega a la sala y guarda el evento
//
// La urna (votación o encuesta) y la cola de preguntas del público LLEGAN a la sala
// (B): ahí están los teléfonos. Pero son del EVENTO: tienen que sobrevivir a que la sala
// se reinicie (una caída, un «aplicar configuración») y no mezclarse con las de otro
// evento. A es la fuente de verdad, B el borde (gateway/Sesion.hpp):
//
//   - cada cambio en B llega como evento (votacion | preguntas) con la `foto` entera y
//     de qué evento es: se guarda en la carpeta del evento (votacion.json,
//     preguntas.json; por el escritor único, sin trabar la interfaz);
//   - al abrir un evento se lee lo suyo y se le da a la sala (`restaurar`): los de otro
//     evento no quedan colgados;
//   - a una sala que vuelve (republicador) se le pregunta qué tiene: si ya es de este
//     evento (se reinició la app, no la sala) manda lo de ella, que es lo más nuevo; si
//     no (se reinició la sala), se le devuelve lo guardado.
//
// Una votación SECRETA llega sin quién votó qué (gateway/Votacion.hpp: foto()): eso no
// toca nunca el disco.
// ============================================================================
#pragma once

#include <QObject>
#include <QTimer>

#include <filesystem>
#include <functional>
#include <string>

#include <nlohmann/json.hpp>

namespace ba::app {
class ConferenceClient;
}

namespace ba::app::sala {

class GuardadoEnEvento : public QObject {
    Q_OBJECT
   public:
    /// `que`: "votacion" o "preguntas" (el evento de la sala, el republicador, la ruta
    /// /api/v1/<que> y el archivo <evento>/<que>.json). `carpeta`: la del evento abierto.
    GuardadoEnEvento(ConferenceClient& sala, std::string que, std::function<std::filesystem::path()> carpeta,
                     QObject* parent = nullptr);

    /// Se abrió otro evento (o ninguno): se lee lo suyo y va a la sala.
    void eventoCambio();
    /// Lo último que se sabe (la foto), o null si todavía no se leyó.
    const nlohmann::json& foto() const { return foto_; }
    /// La etiqueta de un evento para la sala (no la ruta en claro).
    static std::string etiquetaDe(const std::filesystem::path& carpeta);

   private:
    void alEvento(const nlohmann::json& ev);
    /// Con lo leído y la sala en línea: se compara con lo que la sala tiene (ver arriba).
    void sincronizar();
    void restaurar();
    void guardarPronto();

    ConferenceClient& sala_;
    std::string que_;
    std::function<std::filesystem::path()> carpeta_;
    std::filesystem::path deEste_;
    std::string etiqueta_;
    nlohmann::json foto_; ///< null = todavía no se leyó del disco
    int lectura_ = 0;     ///< para descartar una lectura vieja
    QTimer guardar_;      ///< junta los cambios (un voto tras otro) en una escritura
};

} // namespace ba::app::sala
