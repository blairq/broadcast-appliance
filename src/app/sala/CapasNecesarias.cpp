// src/app/sala/CapasNecesarias.cpp — la cuenta de CapasRemotos, sin la sala (se prueba sola)

#include <functional>
#include <set>
#include <string>

#include "emision/FotoMotor.hpp"
#include "sala/CapasRemotos.hpp"

namespace ba::app::sala {

nlohmann::json capasNecesarias(const FotoMotor& f) {
    std::set<std::string> aire, siguiente, visibles;
    // Las cámaras visibles de una escena, entrando en las escenas que contiene (una escena
    // puesta como fuente de otra se ve con ella).
    const auto deEscena = [&f](const std::string& nombre, std::set<std::string>& out) {
        std::set<std::string> vistas;
        const std::function<void(const std::string&)> mirar = [&](const std::string& n) {
            const FotoMotor::Escena* e = f.escena(n);
            if (e == nullptr || !vistas.insert(n).second) return; // no es escena, o ya recorrida
            for (const FotoMotor::Fuente& x : e->fuentes) {
                if (!x.visible) continue;
                if (!x.remota.empty()) out.insert(x.remota);
                mirar(x.nombre);
            }
        };
        mirar(nombre);
    };
    // Una capa DSK es una fuente por nombre: su cámara, la de esa fuente en cualquier escena.
    const auto deFuente = [&f](const std::string& nombre, std::set<std::string>& out) {
        for (const FotoMotor::Escena& e : f.escenas)
            for (const FotoMotor::Fuente& x : e.fuentes)
                if (x.nombre == nombre && !x.remota.empty()) out.insert(x.remota);
    };
    for (const FotoMotor::Salida& s : f.salidas) {
        deEscena(s.alAire, aire); // también es su retorno (BA.Salida.<id>)
        for (const std::string& d : s.dsk) deFuente(d, aire);
        deEscena(s.armada, siguiente);
    }
    // Lo que sale por un canal de escena (pantallas de la sala, retornos de escena) sale de
    // la máquina como el aire: alta.
    for (const std::string& e : f.escenasEnCanales) deEscena(e, aire);
    // Lo que muestra la interfaz (monitores, ventanas, multiview, distribuciones de
    // pantallas): se ve, aunque chico. Con esto la sala no pausa a nadie que esté a la vista.
    for (const std::string& e : f.escenasEnVistas) deEscena(e, visibles);
    for (const std::string& a : aire) {
        siguiente.erase(a);
        visibles.erase(a);
    }
    for (const std::string& s : siguiente) visibles.erase(s);
    return {{"aire", aire}, {"siguiente", siguiente}, {"visibles", visibles}};
}

} // namespace ba::app::sala
