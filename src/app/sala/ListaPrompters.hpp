// ============================================================================
// src/app/sala/ListaPrompters.hpp — los teleprompters del evento, con su nombre
//
// El gateway tiene los teleprompters (gateway/Prompter.hpp: Prompters), cada uno
// con su texto y su página (/prompter/<id>/), y los crea al primer uso. Lo que no
// sabe es cómo se llaman para el operador («Atril del presidente»): eso es del
// evento y vive acá, en <evento>/prompters.json. «principal» está siempre y es el
// de /prompter/ (el de antes de que hubiera varios).
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

namespace ba::app::sala {

class ListaPrompters {
   public:
    static constexpr const char* kPrincipal = "principal";

    /// Lee la lista de `archivo` (vacío: solo el principal, sin guardar).
    void cargar(const std::filesystem::path& archivo);
    /// [{id, nombre}], el principal primero.
    nlohmann::json lista() const;
    bool existe(const std::string& id) const;
    std::string nombreDe(const std::string& id) const;
    /// Uno nuevo con ese nombre; su id sale del nombre (minúsculas, números y
    /// guiones, como los acepta el gateway). Devuelve el id, o "" y `error`.
    std::string agregar(const std::string& nombre, std::string* error = nullptr);
    /// "" o el error. El principal no se saca (sí se le puede cambiar el nombre).
    std::string quitar(const std::string& id);
    std::string renombrar(const std::string& id, const std::string& nombre);

   private:
    bool guardar() const;
    std::filesystem::path archivo_;
    nlohmann::json lista_ = nlohmann::json::array(); ///< los agregados y, si se renombró, el principal
};

} // namespace ba::app::sala
