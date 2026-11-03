// ============================================================================
// src/app/sala/LugaresConGente.cpp — ver LugaresConGente.hpp
// ============================================================================

#include "sala/LugaresConGente.hpp"

#include "Motor.hpp"
#include "arranque/AppRuntime.hpp"
#include "emision/InputSpecs.hpp"
#include "emision/VentanillaMotor.hpp"
#include "ipc/LugaresSala.hpp"
#include "sala/ResumenSala.hpp"
#include "ui/ConferenceClient.hpp"

#include <QJsonArray>
#include <QJsonValue>

namespace ba::app::sala {

LugaresConGente::LugaresConGente(ConferenceClient& sala, VentanillaMotor& ventanilla, AppRuntime& rt, QObject* parent)
    : QObject(parent), sala_(sala), ventanilla_(ventanilla), rt_(rt) {
    // En el hilo del lector, con cada foto (y con nulo cuando se corta B: sin gateway no hay
    // nadie; cuando vuelve, su primera foto dice quién está).
    enganche_ = sala_.alLeerFoto([this](const ResumenSala* r) { mirar(r); });
}

LugaresConGente::~LugaresConGente() {
    // Espera a que termine una vuelta en curso del lector: después ya no se la llama.
    sala_.soltarAlLeerFoto(enganche_);
}

std::map<std::string, bool> LugaresConGente::deSnapshot(const QJsonObject& snap, bool online) {
    std::map<std::string, bool> out;
    for (int slot = 1; slot <= ipc::kMaxInvitados; ++slot) {
        const inputs::CanalesLugar c = inputs::canalesDeLugar(slot);
        out[c.camara] = out[c.pantalla] = out[c.audio] = false;
    }
    if (!online) return out;
    for (const QJsonValue& v : snap.value("participants").toArray()) {
        const QJsonObject p = v.toObject();
        const int slot = p.value("slot").toInt();
        // Admitido con lugar: el gateway ya le asignó los canales (aunque se esté
        // reconectando: el lugar sigue siendo suyo y vuelve enseguida).
        if (p.value("state").toString() != "admitted" || slot < 1 || slot > ipc::kMaxInvitados) continue;
        const inputs::CanalesLugar c = inputs::canalesDeLugar(slot);
        out[c.camara] = out[c.audio] = true;
        if (p.value("screen").toBool()) out[c.pantalla] = true; // la pantalla, solo mientras comparte
    }
    return out;
}

void LugaresConGente::mirar(const ResumenSala* r) {
    // Antes del primer snapshot del gateway no se sabe: todo sigue andando.
    if (r != nullptr && !r->foto.contains("participants")) return;
    // Lo que decide (los lugares ocupados y quién comparte), en una firma: si es la misma de
    // la foto anterior (casi siempre: cambió la red de alguien), no se arma nada más.
    std::string firma = r == nullptr ? std::string("-") : std::string();
    if (r != nullptr)
        for (const QJsonValue& v : r->foto.value("participants").toArray()) {
            const QJsonObject p = v.toObject();
            const int slot = p.value("slot").toInt();
            if (p.value("state").toString() != "admitted" || slot < 1) continue;
            firma += std::to_string(slot) + (p.value("screen").toBool() ? "p," : ",");
        }
    std::lock_guard<std::mutex> l(mu_);
    if (firma == firma_) return;
    firma_ = firma;
    std::map<std::string, bool> ahora = deSnapshot(r != nullptr ? r->foto : QJsonObject(), r != nullptr);
    if (ahora == mandado_) return;
    mandado_ = ahora;
    // A la agenda del dueño del motor (segura entre hilos): no pasa por la interfaz.
    ventanilla_.pedir("lugares_con_gente", [&rt = rt_, lugares = std::move(ahora)]() {
        if (motor::Motor* m = rt.engine().motor()) m->ponerLugares(lugares);
    });
}

} // namespace ba::app::sala
