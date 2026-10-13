// src/app/escenas/AjustesEscenas.cpp — ver el .hpp

#include "escenas/AjustesEscenas.hpp"

#include "arranque/ArchivoConfig.hpp"

namespace ba::app::escenas {

AjustesEscenas::AjustesEscenas(std::filesystem::path dataRoot)
    : archivo_(std::move(dataRoot) / "Config" / "escenas.json") {
    corteDirectoEditor_ = config::leerObjeto(archivo_).value("corte_directo_editor", false);
}

bool AjustesEscenas::setCorteDirectoEditor(bool si) {
    if (si == corteDirectoEditor_) return false;
    corteDirectoEditor_ = si;
    nlohmann::json j = config::leerObjeto(archivo_); // lo demás que haya, se conserva
    j["corte_directo_editor"] = si;
    config::guardar(archivo_, j);
    return true;
}

} // namespace ba::app::escenas
