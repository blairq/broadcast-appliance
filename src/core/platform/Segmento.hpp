// ============================================================================
// src/core/platform/Segmento.hpp — Un bloque de memoria entre DOS procesos
//
// Proposito:
//   Pasar muchos bytes de un proceso a otro sin copiarlos por un pipe: hoy,
//   los cuadros de una animacion que dibuja `ba-grafico` (crudos, sin PNG).
//   Uno lo CREA con un nombre corto y lo llena; el otro lo ABRE por ese nombre
//   (solo lectura) y copia lo que necesita.
//
//   No es `ipc::SharedMemoryRegion`: en Linux/macOS ese vive en un registro
//   del propio proceso (para las pruebas) y no cruza a otro. Tampoco
//   QSharedMemory: en Qt 6.6 con POSIX en macOS no crea ni con un nombre de
//   seis letras («bad name»), y System V en macOS se queda en 4 MB.
//
// Implementaciones:
//   - Linux/macOS: shm_open + mmap. En macOS el nombre no pasa de 31 letras:
//     usar nombres cortos. El que abre borra el nombre apenas lo mapea, asi no
//     queda colgado si el que lo creo muere (el mapa sigue valido).
//   - Windows: CreateFileMappingW/OpenFileMappingW en `Local\`.
//
// Modelo de threading: move-only, de un solo dueño. No lanza.
// ============================================================================

#pragma once

#include <cstddef>
#include <string>

namespace ba::platform {

class Segmento {
   public:
    Segmento() = default;
    ~Segmento();
    Segmento(Segmento&& o) noexcept;
    Segmento& operator=(Segmento&& o) noexcept;
    Segmento(const Segmento&) = delete;
    Segmento& operator=(const Segmento&) = delete;

    /// Crea `bytes` de memoria con `nombre` (ASCII, corto, sin barras), para escribir.
    bool crear(const std::string& nombre, size_t bytes, std::string* err = nullptr);
    /// Abre, para leer, uno que creó otro proceso.
    bool abrir(const std::string& nombre, std::string* err = nullptr);

    void* datos() const { return datos_; }
    size_t tam() const { return tam_; }
    bool valido() const { return datos_ != nullptr; }
    void cerrar();

   private:
    void* datos_ = nullptr;
    size_t tam_ = 0;
    std::string nombre_;
    bool dueno_ = false;
    void* handle_ = nullptr; // Windows: el HANDLE del mapping
};

} // namespace ba::platform
