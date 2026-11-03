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
//
// SIN LA INTERFAZ: se decide en el hilo del lector de la sala (ConferenceClient::alLeerFoto)
// y la orden va a la agenda del dueño del motor (VentanillaMotor::pedir, segura entre
// hilos). Antes corría en la interfaz con cada foto: con la interfaz trabada (3 a 26 s con
// 120 invitados, 2026-11-02) la entrada de audio del que entraba seguía en reposo y su
// audio «llegaba tarde» a la mezcla de retornos y del programa.
// ============================================================================
#pragma once

#include <QJsonObject>
#include <QObject>

#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace ba::app {
class AppRuntime;
class ConferenceClient;
class VentanillaMotor;
} // namespace ba::app

namespace ba::app::sala {

struct ResumenSala;

class LugaresConGente : public QObject {
    Q_OBJECT
   public:
    LugaresConGente(ConferenceClient& sala, VentanillaMotor& ventanilla, AppRuntime& rt, QObject* parent = nullptr);
    ~LugaresConGente() override;

    /// canal → hay alguien (todos los lugares posibles, ipc::kMaxInvitados), de un snapshot.
    static std::map<std::string, bool> deSnapshot(const QJsonObject& snap, bool online);

   private:
    /// En el hilo del lector: `r` nulo = sin B.
    void mirar(const ResumenSala* r);

    ConferenceClient& sala_;
    VentanillaMotor& ventanilla_;
    AppRuntime& rt_;
    int enganche_ = 0;
    std::mutex mu_;
    std::map<std::string, bool> mandado_; ///< lo último que se le mandó al motor (con mu_)
    std::string firma_;                   ///< los lugares con gente de la última foto (con mu_)
};

} // namespace ba::app::sala
