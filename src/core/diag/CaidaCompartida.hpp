// ============================================================================
// src/core/diag/CaidaCompartida.hpp — la app avisa que se cae; el launcher la vuelca
//
// Un volcado bien hecho necesita el REGISTRO DE LA EXCEPCIÓN (qué hilo, qué
// código, en qué dirección) y se escribe mejor DESDE AFUERA: adentro de un
// proceso que se está cayendo (con el heap roto, por ejemplo) MiniDumpWriteDump
// puede fallar o trabarse. Así lo hacen Crashpad y Breakpad, y así se hace acá:
//
//   1. El launcher, antes de lanzar un hijo, crea una memoria compartida y dos
//      eventos (el canal, abajo), se lo pasa en el entorno y espera en un hilo.
//   2. El manejador de caídas del hijo (diag/Caidas.cpp) llena el Pedido (hilo
//      y puntero a sus EXCEPTION_POINTERS), señala «pide» y espera «hecho».
//   3. El launcher escribe el volcado con la excepción (ClientPointers=TRUE:
//      el puntero es del hijo) y un .json al lado; señala «hecho».
//   4. El hijo sigue cayéndose como siempre (WER, código de salida).
//
// Sin launcher (la app suelta, los tests), el hijo se vuelca a sí mismo.
// ============================================================================
#pragma once

#include <cstdint>
#include <string>

namespace ba::diag::caida {

inline constexpr std::uint32_t kMagic = 0x41434142; // "BACA"
inline constexpr std::uint32_t kVersion = 1;

/// Código propio para lo que no es una excepción de Windows (abort, terminate,
/// llamada pura, parámetro inválido): va como ExceptionCode del registro armado.
inline constexpr std::uint32_t kCodigoAbort = 0xE0000BAB;

struct Pedido {
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint32_t hilo = 0;      ///< el que se cayó
    std::uint32_t codigo = 0;    ///< ExceptionCode
    std::uint64_t punteros = 0;  ///< EXCEPTION_POINTERS* en el espacio del hijo
    std::uint64_t direccion = 0; ///< ExceptionAddress
    char tipo[48] = {};          ///< "excepción", "corrupción del heap", "abort"…
};

/// El canal: lo elige el launcher (por su pid y el hijo, «Local\\BA.Caida.<pid>.A»)
/// y se lo pasa al hijo en esta variable. Así existe ANTES de que el hijo
/// arranque (por el pid del hijo, el launcher lo crearía tarde).
inline constexpr const char* kVariable = "BA_CAIDA_CANAL";
inline std::wstring nombrePide(const std::wstring& canal) { return canal + L".pide"; }
inline std::wstring nombreHecho(const std::wstring& canal) { return canal + L".hecho"; }

/// Lo que se le pide a MiniDumpWriteDump (en los dos lados): pilas y lo que
/// apuntan (para seguir punteros y desenrollar), info de hilos y de memoria,
/// handles y módulos descargados. Unos MB, no el proceso entero.
///   MiniDumpWithHandleData 0x4 | WithUnloadedModules 0x20 |
///   WithIndirectlyReferencedMemory 0x40 | WithProcessThreadData 0x100 |
///   WithFullMemoryInfo 0x800 | WithThreadInfo 0x1000 | IgnoreInaccessibleMemory 0x20000
inline constexpr std::uint32_t kTipoVolcado = 0x4 | 0x20 | 0x40 | 0x100 | 0x800 | 0x1000 | 0x20000;

/// Una descripción para el registro y el cartel.
inline const char* nombreCodigo(std::uint32_t c) {
    switch (c) {
        case 0xC0000005: return "violación de acceso";
        case 0xC0000374: return "corrupción del heap";
        case 0xC0000409: return "desborde de búfer en la pila (o fail-fast)";
        case 0xC00000FD: return "desbordamiento de pila";
        case 0xC000001D: return "instrucción inválida";
        case 0xC0000094: return "división por cero";
        case 0x80000003: return "punto de interrupción";
        case 0xE06D7363: return "excepción de C++ sin atrapar";
        case kCodigoAbort: return "abort (o terminate)";
        default: return "excepción";
    }
}

} // namespace ba::diag::caida
