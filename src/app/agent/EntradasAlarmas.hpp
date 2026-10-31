// ============================================================================
// src/app/agent/EntradasAlarmas.hpp — lo que mira el vigía de las alarmas, por hilo (F3-03)
//
// AlertEngine evalúa en el hilo del plano de control. Cada entrada que lee
// (AlertInputs) viene de uno de dos lados:
//
//   - DIRECTAS: seguras entre hilos, se leen en el acto desde el control (el programa
//     en imagen y su audio, la foto de las salidas del motor, el loudness, la
//     persistencia, los recursos). Son las alarmas de lo que sale al aire: siguen con
//     la interfaz trabada.
//   - DE LA INTERFAZ: objetos de ella (la sala, los chats, las entradas de video, la
//     energía). La interfaz las lee en su hilo con `refrescar` y las deja en una foto;
//     el vigía lee la foto. Con la interfaz trabada se quedan quietas, y por eso nada
//     de lo que sale al aire va por acá.
//
// El audio junta las dos: las fuentes (de la interfaz) y el programa (directo).
// ============================================================================
#pragma once

#include "agent/AlertEngine.hpp"
#include "comun/FotoEntreHilos.hpp"

#include <memory>

namespace ba::app::agent {

class EntradasAlarmas {
   public:
    EntradasAlarmas(AlertInputs deLaInterfaz, AlertInputs directas);
    /// En el hilo de la interfaz: copia lo suyo a la foto.
    void refrescar();
    /// Lo que recibe AlertEngine: cada entrada, directa o de la foto.
    AlertInputs paraElVigia() const;
    /// Cuánto hace que la interfaz copió por última vez (-1 si nunca).
    std::int64_t edadMs() const { return f_->vuelta.edadMs(); }

   private:
    struct Fotos {
        FotoEntreHilos<std::vector<AlertInputs::Audio>> audio;
        FotoEntreHilos<std::vector<AlertInputs::Media>> media;
        FotoEntreHilos<json> room, roomChat, disco, loudness, superres, programaVideo, recursos, energia;
        FotoEntreHilos<std::vector<json>> chat, textos;
        FotoEntreHilos<AlertInputs::Status> status;
        FotoEntreHilos<std::vector<std::pair<std::string, bool>>> senales;
        FotoEntreHilos<std::vector<std::string>> persistencia;
        FotoEntreHilos<bool> vuelta; ///< cuándo copió la interfaz por última vez
    };
    AlertInputs ui_, directas_;
    std::shared_ptr<Fotos> f_ = std::make_shared<Fotos>();
};

} // namespace ba::app::agent
