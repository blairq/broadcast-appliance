// SPDX-License-Identifier: GPL-2.0-or-later
// ============================================================================
// src/app/evento/Documentos.hpp — la documentación del evento (PDF, sobre todo)
//
// Cada evento guarda la suya en su carpeta (la copia total la lleva):
//
//   <evento>/docs/documentos.json   el índice
//   <evento>/docs/archivos/         los documentos (con su nombre, sin pisar otro)
//   <evento>/docs/miniaturas/<id>.jpg  la carátula, hecha UNA vez al agregarlo (la
//                                      web la sirve directo, sin abrir el PDF)
//
// Cada documento: {id, archivo, titulo, descripcion, paginas, bytes, agregado_ms,
// publico, puntos: [n del orden del día], secciones: [la clave de cada sección del
// guion: el id del bloque, o su número]}. Relacionado con nada: es del evento en general.
//
// Sin Qt: el índice y los archivos. Leer el PDF y hacer la carátula es de
// DocumentosViewModel (Qt PDF). Ver docs/planes/PLAN-DOCUMENTOS-EVENTO.md.
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <string>

namespace ba::evento {

class Documentos {
   public:
    using json = nlohmann::json;
    /// `evento`: la carpeta del evento (la de su configuración: Config/, Media/, docs/…).
    explicit Documentos(std::filesystem::path evento);

    std::filesystem::path carpeta() const { return docs_; }
    std::filesystem::path archivos() const { return docs_ / "archivos"; }
    std::filesystem::path miniaturas() const { return docs_ / "miniaturas"; }
    std::filesystem::path miniatura(const std::string& id) const { return miniaturas() / (id + ".jpg"); }
    /// La ruta del documento `id` ("" si no existe).
    std::filesystem::path ruta(const std::string& id) const;

    /// Todos, en el orden en que se agregaron.
    json lista() const;
    json documento(const std::string& id) const;
    /// Los públicos, como los ve la web (con las direcciones del PDF y la miniatura).
    json publicos() const;

    /// Copia `origen` a archivos/ y lo anota. `datos`: {titulo, descripcion, paginas,
    /// publico, puntos, secciones}. Devuelve el id nuevo ("" y el motivo en `error`).
    std::string agregar(const std::filesystem::path& origen, const json& datos, std::int64_t ahoraMs, std::string* error);
    /// Cambia lo que venga en `cambios` (titulo, descripcion, publico, puntos, secciones,
    /// paginas). "" o el error.
    std::string editar(const std::string& id, const json& cambios);
    /// Lo saca del índice y manda el archivo y su miniatura a <evento>/Papelera/docs/.
    std::string quitar(const std::string& id);

   private:
    json leer() const;
    bool guardar(const json& l) const;
    std::filesystem::path evento_, docs_;
};

} // namespace ba::evento
