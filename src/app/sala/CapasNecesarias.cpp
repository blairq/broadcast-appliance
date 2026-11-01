// src/app/sala/CapasNecesarias.cpp — la cuenta de CapasRemotos, sin la sala (se prueba sola)

#include <set>
#include <string>

#include "emision/FotoMotor.hpp"
#include "sala/CapasRemotos.hpp"

namespace ba::app::sala {

nlohmann::json capasNecesarias(const FotoMotor& f) {
    std::set<std::string> aire, siguiente;
    const auto deEscena = [&f](const std::string& nombre, std::set<std::string>& out) {
        if (const FotoMotor::Escena* e = f.escena(nombre))
            for (const FotoMotor::Fuente& x : e->fuentes)
                if (x.visible && !x.remota.empty()) out.insert(x.remota);
    };
    // Una capa DSK es una fuente por nombre: su cámara, la de esa fuente en cualquier escena.
    const auto deFuente = [&f](const std::string& nombre, std::set<std::string>& out) {
        for (const FotoMotor::Escena& e : f.escenas)
            for (const FotoMotor::Fuente& x : e.fuentes)
                if (x.nombre == nombre && !x.remota.empty()) out.insert(x.remota);
    };
    for (const FotoMotor::Salida& s : f.salidas) {
        deEscena(s.alAire, aire);
        for (const std::string& d : s.dsk) deFuente(d, aire);
        deEscena(s.armada, siguiente);
    }
    for (const std::string& a : aire) siguiente.erase(a);
    return {{"aire", aire}, {"siguiente", siguiente}};
}

} // namespace ba::app::sala
