// ============================================================================
// src/core/persist/Esquema.cpp — ver Esquema.hpp
// ============================================================================
#include "persist/Esquema.hpp"

#include "diag/Registro.hpp"
#include "i18n/I18n.hpp"

namespace ba::persist {

int esquemaDe(const nlohmann::json& j) {
    if (!j.is_object()) return 1;
    const auto it = j.find(kClaveEsquema);
    return it != j.end() && it->is_number_integer() && it->get<int>() >= 1 ? it->get<int>() : 1;
}

int migrar(nlohmann::json& j, int actual, const std::vector<PasoMigracion>& pasos, const std::string& nombre) {
    const int tenia = esquemaDe(j);
    if (!j.is_object()) return tenia;
    if (tenia > actual) {
        ba::diag::registro().linea(
            ba::diag::Nivel::Aviso, "config", 0,
            ba::i18n::llenar(ba::i18n::traducir("config", "%1: lo escribió una versión más nueva (esquema %2; esta lee hasta el %3): "
                                                          "se lee como está"),
                             {nombre, std::to_string(tenia), std::to_string(actual)}));
        return tenia;
    }
    for (int v = tenia; v < actual; ++v)
        if (static_cast<std::size_t>(v - 1) < pasos.size() && pasos[static_cast<std::size_t>(v - 1)])
            pasos[static_cast<std::size_t>(v - 1)](j);
    if (tenia < actual)
        ba::diag::registro().linea(ba::diag::Nivel::Info, "config", 0,
                                   ba::i18n::llenar(ba::i18n::traducir("config", "%1: esquema %2 → %3"),
                                                    {nombre, std::to_string(tenia), std::to_string(actual)}));
    j[kClaveEsquema] = actual;
    return tenia;
}

} // namespace ba::persist
