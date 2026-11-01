// ============================================================================
// src/app/agent/EntradasAlarmas.hpp — lo que mira el vigía de las alarmas, por hilo (F3-03)
//
// AlertEngine evalúa en el hilo del plano de control. Cada entrada que lee
// (AlertInputs) viene de uno de dos lados:
//
//   - DIRECTAS: seguras entre hilos, se leen en el acto desde el control (el programa
//     en imagen y su audio, la foto de las salidas del motor, el loudness, la
//     persistencia, los recursos, la señal de las entradas de video y la superresolución,
//     del grafo de video que es del dueño). Son las alarmas de lo que sale al aire:
//     siguen con la interfaz trabada.
//   - DE LA INTERFAZ: objetos de ella (la sala, los chats, las salidas de texto). La
//     interfaz las lee en su hilo con `refrescar` CUANDO CAMBIAN (sus avisos: quien arma
//     esto los conecta) y las deja en una foto; el vigía lee la foto. Con la interfaz
//     trabada se quedan quietas, y por eso nada de lo que sale al aire va por acá.
//   - SIN LA INTERFAZ (respaldo): lo de la interfaz que también se puede preguntar a su
//     dueño, pero caro para hacerlo siempre (la sala, a B por HTTP). Se usa SOLO cuando
//     la interfaz está trabada: lo dice su LATIDO (cuánto hace de su última vuelta,
//     diag::VigiaBucle::edadMs), no la edad de la foto (que, sin cambios, no se copia).
//     Pasado `quietaMs` sin latir, la sala y su chat siguen avisando desde el respaldo;
//     con la interfaz andando no se le pregunta a B.
//
// La energía y el disco son directas (se miden en el vigía, cada tanto).
//
// El audio junta las dos: las fuentes (de la interfaz) y el programa (directo).
// ============================================================================
#pragma once

#include "agent/AlertEngine.hpp"
#include "comun/FotoEntreHilos.hpp"

#include <functional>
#include <memory>

namespace ba::app::agent {

class EntradasAlarmas {
   public:
    /// Cuánto hace (ms) del último latido de la interfaz (-1 si todavía no latió). Desde
    /// cualquier hilo.
    using Latido = std::function<std::int64_t()>;
    /// Sin `latido` no hay forma de saber si la interfaz se trabó: el respaldo no se usa.
    EntradasAlarmas(AlertInputs deLaInterfaz, AlertInputs directas, AlertInputs sinLaInterfaz = {}, Latido latido = {},
                    int quietaMs = 1000);
    /// En el hilo de la interfaz: copia lo suyo a la foto (al arrancar y cada vez que algo
    /// de lo que da cambia).
    void refrescar();
    /// Lo que recibe AlertEngine: cada entrada, directa o de la foto.
    AlertInputs paraElVigia() const;

   private:
    struct Fotos {
        FotoEntreHilos<std::vector<AlertInputs::Audio>> audio;
        FotoEntreHilos<std::vector<AlertInputs::Media>> media;
        FotoEntreHilos<json> room, roomChat, disco, loudness, superres, programaVideo, recursos, energia;
        FotoEntreHilos<std::vector<json>> chat, textos;
        FotoEntreHilos<AlertInputs::Status> status;
        FotoEntreHilos<std::vector<std::pair<std::string, bool>>> senales;
        FotoEntreHilos<std::vector<std::string>> persistencia;
    };
    AlertInputs ui_, directas_, respaldo_;
    Latido latido_;
    int quietaMs_ = 1000;
    std::shared_ptr<Fotos> f_ = std::make_shared<Fotos>();
};

} // namespace ba::app::agent
