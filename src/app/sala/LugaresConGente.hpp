// ============================================================================
// src/app/sala/LugaresConGente.hpp — qué lugares de la sala tienen a alguien (al motor)
//
// Cada lugar del cupo tiene armadas sus entradas (la cámara BA.Guest.<n>, la pantalla
// BA.GuestShare.<n> y el audio BA.Audio.Guest.<n>) aunque no haya nadie. Con esto el motor
// las deja en reposo (READY: sin hilos ni trabajo) mientras el lugar está vacío y las pone
// a andar apenas llega alguien (Motor::ponerLugares, Entrada::ponerSinGente); la pantalla,
// solo mientras comparte.
//
// Quién está: los admitidos con lugar (state «admitted», slot > 0) del snapshot de la sala
// (ConferenceClient). Se manda solo lo que cambió de la lista, apenas llega el snapshot: el
// gateway asigna el lugar ANTES de que lleguen los medios, así la entrada ya anda cuando
// llega el primer cuadro. Sin gateway, nadie. Antes del primer snapshot no se manda nada
// (todo anda, como siempre).
// ============================================================================
#pragma once

#include <QJsonObject>
#include <QObject>

#include <map>
#include <string>

namespace ba::app {
class AppRuntime;
class ConferenceClient;
class VentanillaMotor;
} // namespace ba::app

namespace ba::app::sala {

class LugaresConGente : public QObject {
    Q_OBJECT
   public:
    LugaresConGente(ConferenceClient& sala, VentanillaMotor& ventanilla, AppRuntime& rt, QObject* parent = nullptr);

    /// canal → hay alguien (todos los lugares posibles, ipc::kMaxInvitados), de un snapshot.
    static std::map<std::string, bool> deSnapshot(const QJsonObject& snap, bool online);

   private:
    void mirar();

    ConferenceClient& sala_;
    VentanillaMotor& ventanilla_;
    AppRuntime& rt_;
    std::map<std::string, bool> mandado_; ///< lo último que se le mandó al motor
};

} // namespace ba::app::sala
