// ============================================================================
// src/app/sala/CapasRemotos.hpp — qué cámaras de remotos tiene la app al aire (modelo)
//
// La cámara de cada remoto (invitado de la conferencia, integrante de la asamblea) llega
// en dos calidades y la sala elige cuál viaja (src/gateway/Capas.hpp): la ALTA para el
// que está al aire y para el que va a salir, la BAJA para las miniaturas. Lo que la sala
// no sabe es qué está al aire: eso lo dice la app. Acá se arma, de la foto del motor,
// la lista de entradas de remotos ("guest:<n>", "remoto:<k>") en la escena al aire de
// cada salida (y sus capas DSK) y en la armada (el preview: va a salir, se precarga la
// alta), y se le manda a la sala cuando cambia (POST /api/v1/capas). Si la sala se
// reinicia, se le vuelve a decir (republicador «capas»).
//
// Sin Qt en la cuenta (capasNecesarias): se prueba sola (test_capas_remotos).
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <QObject>

namespace ba::app {
class ConferenceClient;
struct FotoMotor;
} // namespace ba::app

namespace ba::app::sala {

/// {aire: [...], siguiente: [...], visibles: [...]}: las entradas de remotos al aire (en la
/// escena al aire o una capa DSK de alguna salida —que es también su retorno—, o en una
/// escena que sale por un canal de escena: pantallas de la sala, retornos de escena), armadas
/// (en la escena armada de alguna salida y no al aire) y visibles (en una escena que muestra
/// la interfaz: monitores, ventanas, multiview, distribuciones de pantallas; ni al aire ni
/// armadas). Las escenas metidas en otra cuentan con ella. Ordenadas y sin repetir. La
/// sala no pausa a nadie de estas listas (src/gateway/Capas.hpp).
nlohmann::json capasNecesarias(const FotoMotor& f);

class CapasRemotos {
   public:
    explicit CapasRemotos(ConferenceClient& sala);
    /// Cambió la foto (escenas o salidas): si cambió la lista, se le manda a la sala.
    void actualizar(const FotoMotor& f);

   private:
    ConferenceClient& sala_;
    nlohmann::json ultima_; ///< la última que se mandó (se republica)
    QObject vida_;          ///< mientras vive, la sala le pide que republique
};

} // namespace ba::app::sala
