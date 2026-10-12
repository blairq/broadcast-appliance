// src/app/emision/RetornosInvitados.cpp — ver el .hpp

#include "emision/RetornosInvitados.hpp"

#include "arranque/AppRuntime.hpp"
#include "arranque/ArchivoConfig.hpp"

namespace ba::app::emision {

RetornosInvitados::RetornosInvitados(AppRuntime& rt) : rt_(rt) {}

std::string RetornosInvitados::poner(int slot, const std::string& modo) {
    if (modo != "mix_minus" && modo != "programa") return "retorno: mix_minus o programa";
    if (slot < 1 || slot > rt_.config().guests) return "no hay un invitado " + std::to_string(slot);
    rt_.engine().setRetornoInvitado(slot, modo);
    const auto archivo = rt_.dataRoot() / "Config" / "retornos.json";
    nlohmann::json j = config::leerObjeto(archivo);
    j[std::to_string(slot)] = modo;
    if (!config::guardar(archivo, j)) return "el retorno rige, pero no se pudo guardar retornos.json";
    return {};
}

std::string RetornosInvitados::modo(int slot) const { return rt_.engine().retornoInvitado(slot); }

nlohmann::json RetornosInvitados::todos() const {
    nlohmann::json out = nlohmann::json::object();
    for (int s = 1; s <= rt_.config().guests; ++s) out[std::to_string(s)] = modo(s);
    return out;
}

} // namespace ba::app::emision
