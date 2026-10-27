// src/app/emision/RetornosInvitados.cpp — ver el .hpp

#include "emision/RetornosInvitados.hpp"

#include "arranque/AppRuntime.hpp"
#include "arranque/ArchivoConfig.hpp"
#include "i18n/I18n.hpp"

#include <cstdlib>

namespace ba::app::emision {

RetornosInvitados::RetornosInvitados(AppRuntime& rt) : rt_(rt) {}

std::string RetornosInvitados::poner(int slot, const std::string& modo) {
    if (modo != "mix_minus" && modo != "programa") return ba::i18n::traducir("emision", "retorno: mix_minus o programa");
    if (slot < 1 || slot > rt_.config().guests) return ba::i18n::llenar(ba::i18n::traducir("emision", "no hay un invitado %1"), {std::to_string(slot)});
    rt_.engine().setRetornoInvitado(slot, modo);
    const auto archivo = rt_.dataRoot() / "Config" / "retornos.json";
    nlohmann::json j = config::leerObjeto(archivo);
    j[std::to_string(slot)] = modo;
    if (!config::guardar(archivo, j)) return ba::i18n::traducir("emision", "el retorno rige, pero no se pudo guardar retornos.json");
    return {};
}

std::string RetornosInvitados::modo(int slot) const { return rt_.engine().retornoInvitado(slot); }

void RetornosInvitados::cargar() {
    const nlohmann::json j = config::leerObjeto(rt_.dataRoot() / "Config" / "retornos.json");
    for (const auto& [slot, modo] : j.items())
        if (modo.is_string() && (modo == "mix_minus" || modo == "programa"))
            rt_.engine().setRetornoInvitado(std::atoi(slot.c_str()), modo.get<std::string>());
}

nlohmann::json RetornosInvitados::todos() const {
    nlohmann::json out = nlohmann::json::object();
    for (int s = 1; s <= rt_.config().guests; ++s) out[std::to_string(s)] = modo(s);
    return out;
}

} // namespace ba::app::emision
