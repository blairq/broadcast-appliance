// SPDX-License-Identifier: GPL-2.0-or-later
//
// Cuerpo: los integrantes de un cuerpo legislativo (o cualquier grupo con
// bancas): la FUENTE ÚNICA de nombre, cargo, bloque y banca para los zócalos,
// la fila de oradores (bancas presenciales), el PTZ de cada banca, el micrófono
// de cada banca y el sistema de conferencia del recinto.
//
// Config/cuerpo.json: {nombre, integrantes: [{id, persona_id?, nombre,
// cargo_oficial, bloque, distrito, banca, preset_ptz: {camara, posicion},
// microfono: {fuente}, foto, validado_por, validado_en}]}.
//
// Lo que sale al aire de acá está VALIDADO por una persona (validado_por):
// un integrante sin validar se marca y los zócalos lo avisan.
#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

namespace ba::app::legislatura {

class Cuerpo {
   public:
    using json = nlohmann::json;
    explicit Cuerpo(std::filesystem::path config);

    json todo() const { return datos_; }
    json integrantes() const { return datos_.value("integrantes", json::array()); }
    std::string nombre() const { return datos_.value("nombre", std::string()); }
    /// Crea (o renombra) el cuerpo. No borra integrantes.
    void setNombre(const std::string& n);

    /// Busca por id, banca o nombre (sin distinguir mayúsculas). {} si no está.
    json buscar(const std::string& idBancaONombre) const;
    /// Crea o cambia un integrante (con `id` existente lo cambia; sin `id`, lo
    /// crea). Solo los campos conocidos. Devuelve el id, o "" con el error en `error`.
    std::string guardar(const json& campos, std::string* error = nullptr);
    bool borrar(const std::string& id);
    /// Marca a un integrante como validado por `quien` (ahora).
    bool validar(const std::string& id, const std::string& quien);

    /// Importa una planilla CSV (separada por coma o punto y coma, con
    /// encabezado: banca, nombre, cargo, bloque, distrito, camara, posicion,
    /// microfono; los nombres de columna se reconocen sin tildes ni mayúsculas).
    /// `reemplazar`: borra los de antes. Devuelve {importados, actualizados, errores[]}.
    json importarCsv(const std::filesystem::path& archivo, bool reemplazar);
    /// Escribe una planilla de ejemplo con el encabezado y dos filas.
    static bool escribirPlantilla(const std::filesystem::path& archivo);

   private:
    void guardarArchivo() const;
    std::filesystem::path archivo_;
    json datos_;
};

} // namespace ba::app::legislatura
