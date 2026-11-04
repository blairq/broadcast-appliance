// ============================================================================
// src/core/persist/Lectura.hpp — lo que se encontró al leer un archivo en dos generaciones
//
// Lo devuelven DosGeneraciones::leer (Generaciones.hpp) y ColaEscritura::leerGeneraciones;
// lo guarda quien recuerda cómo encontró su archivo (emision/DocumentoAire). Aparte para
// que eso no arrastre las generaciones ni el escritor.
//
// Sin Qt (ba_core).
// ============================================================================
#pragma once

#include <cstdint>
#include <string>

namespace ba::persist {

/// Lo que se encontró al leer.
struct Lectura {
    enum class Estado {
        Nada,   ///< no hay ningún archivo (primer arranque)
        Valida, ///< una generación entera: la más nueva que sirve
        Vieja,  ///< el archivo de antes de las generaciones (sin verificar: lo valida quien lo usa)
        Danada, ///< hay generaciones pero ninguna sirve
    };
    Estado estado = Estado::Nada;
    std::string cuerpo;
    std::uint64_t secuencia = 0;
    int descartadas = 0; ///< generaciones que estaban y no servían (rotas o truncadas)
};

} // namespace ba::persist
