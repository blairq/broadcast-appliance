// ============================================================================
// src/app/sala/LugaresConGente.cpp — ver LugaresConGente.hpp
// ============================================================================

#include "sala/LugaresConGente.hpp"

#include "Motor.hpp"
#include "arranque/AppRuntime.hpp"
#include "emision/InputSpecs.hpp"
#include "emision/VentanillaMotor.hpp"
#include "ipc/LugaresSala.hpp"
#include "ui/ConferenceClient.hpp"

#include <QJsonArray>
#include <QJsonValue>

namespace ba::app::sala {

LugaresConGente::LugaresConGente(ConferenceClient& sala, VentanillaMotor& ventanilla, AppRuntime& rt, QObject* parent)
    : QObject(parent), sala_(sala), ventanilla_(ventanilla), rt_(rt) {
    connect(&sala_, &ConferenceClient::snapshotChanged, this, &LugaresConGente::mirar);
    // Sin gateway no hay nadie (y cuando vuelve, su primer snapshot dice quién está).
    connect(&sala_, &ConferenceClient::onlineChanged, this, &LugaresConGente::mirar);
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

void LugaresConGente::mirar() {
    // Antes del primer snapshot del gateway no se sabe: todo sigue andando.
    if (sala_.online() && !sala_.snapshot().contains("participants")) return;
    std::map<std::string, bool> ahora = deSnapshot(sala_.snapshot(), sala_.online());
    if (ahora == mandado_) return;
    mandado_ = ahora;
    ventanilla_.pedir("lugares_con_gente", [&rt = rt_, lugares = std::move(ahora)]() {
        if (motor::Motor* m = rt.engine().motor()) m->ponerLugares(lugares);
    });
}

} // namespace ba::app::sala
