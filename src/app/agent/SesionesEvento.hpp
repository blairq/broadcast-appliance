// ============================================================================
// src/app/agent/SesionesEvento.hpp — UNA conversación del asistente por evento
//
// Antes cada arranque del asistente empezaba una conversación nueva (opencode
// sin `--session`, Claude con un `--session-id` nuevo): al reiniciar la app, o
// al volver a abrir el evento, el asistente no sabía nada de lo que ya había
// pasado, y en OpenCode cada arranque dejaba otra sesión (otra pestaña) con un
// solo aviso adentro. Ahora cada evento recuerda la suya, por asistente («evento»
// y «automatizacion») y por motor (un id de opencode no le sirve a Claude):
//
//   <evento>/Config/asistente-sesiones.json
//   {"evento": "<carpeta del evento>",
//    "sesiones": {"evento": {"opencode": "ses_…", "claude": "<uuid>"},
//                 "automatizacion": {"opencode": "ses_…"}}}
//
// `evento` es el nombre de la carpeta del evento: un evento COPIADO (la copia es
// total, Config/ incluida) trae el archivo del otro, y sin esto los dos
// escribirían en la misma conversación. Si no coincide, no hay ninguna guardada
// y la primera que se guarde lo pisa.
//
// Se lee y se guarda por el escritor único de Config/ (arranque/ArchivoConfig).
// ============================================================================
#pragma once

#include <filesystem>
#include <string>

namespace ba::app::agent {

class SesionesEvento {
   public:
    /// `archivo`: Config/asistente-sesiones.json del evento; `evento`: su nombre (la carpeta).
    SesionesEvento(std::filesystem::path archivo, std::string evento);

    /// La conversación guardada de ese asistente con ese motor ("" si no hay).
    std::string de(const std::string& instancia, const std::string& motor) const;
    /// La guarda (vacío = la olvida).
    void poner(const std::string& instancia, const std::string& motor, const std::string& id);
    /// Olvida las de ese asistente (todas sus conversaciones: «Conversación nueva»).
    void olvidar(const std::string& instancia);

    /// El archivo de la configuración activa y el nombre del evento abierto.
    static SesionesEvento delEventoAbierto(const std::filesystem::path& raizEvento);

   private:
    std::filesystem::path archivo_;
    std::string evento_;
};

} // namespace ba::app::agent
