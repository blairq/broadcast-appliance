// ============================================================================
// src/app/evento/EventoStore.hpp — los eventos en el disco
//
// Un evento es una CARPETA, no un registro en un archivo compartido:
//
//     Eventos/<nombre>/
//     ├── evento.json         el guion
//     ├── Medios/             música, videos y placas propias del evento
//     ├── Discursos/          los textos del teleprompter
//     └── Respaldos/          copias del guion antes de cada guardado
//
// Así se lo lleva entero a otra máquina o al equipo de respaldo, y si algo
// sale mal se puede abrir con un editor de texto y ver qué dice.
//
// GUARDAR NO PISA. Antes de escribir, el guion que estaba se copia a
// Respaldos/ con la fecha. Un evento es el trabajo de días y se edita el mismo
// día del acto, con gente alrededor: perderlo por un clic no es una opción.
//
// El nombre de la carpeta sale del nombre del evento, pero no es lo mismo: se
// limpia para que sea un nombre de carpeta válido en Windows y en Linux.
// ============================================================================
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "evento/Evento.hpp"

namespace ba::evento {

/// Lo que se sabe de un evento sin abrirlo entero (para la lista).
struct Ficha {
    std::string carpeta; ///< nombre de la carpeta dentro de Eventos/
    std::string nombre;
    std::string inicio;
    int duracionSeg = 0;
    std::size_t bloques = 0;
    std::size_t cues = 0;
};

/// Convierte un nombre cualquiera en algo que sirva de nombre de carpeta en
/// todos los sistemas. Vacío si no queda nada usable.
std::string nombreDeCarpeta(const std::string& nombre);

class EventoStore {
  public:
    /// :param raiz: la carpeta Eventos/ (se crea si no está).
    explicit EventoStore(std::filesystem::path raiz);

    const std::filesystem::path& raiz() const { return raiz_; }
    std::filesystem::path carpetaDe(const std::string& carpeta) const { return raiz_ / carpeta; }

    /// Los eventos que hay, ordenados por nombre.
    std::vector<Ficha> listar() const;

    /// Crea la carpeta con su esqueleto y un guion vacío. Devuelve el nombre
    /// de la carpeta creada, o vacío con el motivo en `err`. Si ya existe una
    /// con ese nombre, le agrega un sufijo en vez de pisarla.
    std::string crear(const std::string& nombre, std::string* err = nullptr);

    /// Carga el guion. `ok` dice si se pudo (una carpeta sin evento.json, o
    /// con un JSON roto, devuelve un evento vacío).
    Evento cargar(const std::string& carpeta, bool* ok = nullptr, std::string* err = nullptr) const;

    /// Guarda el guion, respaldando antes el que estaba.
    bool guardar(const std::string& carpeta, const Evento& e, std::string* err = nullptr) const;

    /// Borra la carpeta entera. Es lo único que borra: se pide explícito.
    bool borrar(const std::string& carpeta, std::string* err = nullptr) const;

  private:
    std::filesystem::path raiz_;
};

} // namespace ba::evento
