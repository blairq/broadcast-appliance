// ============================================================================
// src/core/json/ObjetoJson.hpp — un objeto JSON guardado por valor, sin el header entero
//
// Para un miembro de una estructura de configuración que guarda un objeto JSON tal cual
// (los avanzados de ba::ajustes, la sección "proxy" de gateway.json): un
// `nlohmann::json` por valor obliga a incluir <nlohmann/json.hpp> en el header (~1 s de
// frontend por TU) a todos los que nombran la estructura, lo usen o no. Esto lo guarda
// por dentro (en el heap) y se copia, se mueve y se asigna como un valor.
//
// Nace como objeto vacío ({}), como los miembros que reemplaza. Para leerlo o cambiarlo,
// `*o` o `o->`: quien lo hace incluye <nlohmann/json.hpp>.
//
// Sin Qt (ba_core).
// ============================================================================
#pragma once

#include <memory>

#include "json/JsonFwd.hpp"

namespace ba {

class ObjetoJson {
   public:
    ObjetoJson(); ///< {}
    ObjetoJson(const nlohmann::json& j);
    ObjetoJson(nlohmann::json&& j);
    ObjetoJson(const ObjetoJson& o);
    ObjetoJson(ObjetoJson&& o);
    ObjetoJson& operator=(const ObjetoJson& o);
    ObjetoJson& operator=(ObjetoJson&& o) noexcept;
    ObjetoJson& operator=(const nlohmann::json& j);
    ObjetoJson& operator=(nlohmann::json&& j);
    ~ObjetoJson();

    nlohmann::json& operator*() { return *j_; }
    const nlohmann::json& operator*() const { return *j_; }
    nlohmann::json* operator->() { return j_.get(); }
    const nlohmann::json* operator->() const { return j_.get(); }

   private:
    std::unique_ptr<nlohmann::json> j_; ///< nunca nulo (en uno movido, un JSON movido: usable)
};

} // namespace ba
