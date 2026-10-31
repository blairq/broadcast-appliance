// src/app/sala/CalidadPorCalor.cpp — ver el .hpp

#include "sala/CalidadPorCalor.hpp"

#include <iterator>

namespace ba::app::sala {

bool CalidadPorCalor::bajar(bool on) {
    if (on == baja_) return true;
    baja_ = on;
    if (on) {
        revisar();
        return true;
    }
    // Se alivió: a los que se les bajó y siguen adentro, la calidad completa.
    const std::vector<std::string> ahora = adentro_ ? adentro_() : std::vector<std::string>();
    const std::set<std::string> siguen(ahora.begin(), ahora.end());
    for (const std::string& id : bajados_)
        if (siguen.count(id) > 0 && pedir_) pedir_(id, "calidad-max");
    bajados_.clear();
    return true;
}

void CalidadPorCalor::revisar() {
    if (!baja_ || !adentro_) return;
    const std::vector<std::string> ahora = adentro_();
    // Los que se fueron no cuentan (si vuelven, se les pide de nuevo).
    const std::set<std::string> siguen(ahora.begin(), ahora.end());
    for (auto it = bajados_.begin(); it != bajados_.end();) it = siguen.count(*it) > 0 ? std::next(it) : bajados_.erase(it);
    for (const std::string& id : ahora)
        if (bajados_.insert(id).second && pedir_) pedir_(id, "calidad-720p");
}

} // namespace ba::app::sala
