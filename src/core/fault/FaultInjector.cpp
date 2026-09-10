// FaultInjector.cpp
//
// Implementacion del andamiaje generico de inyeccion de fallos (spec §13).
//
//   - El estado es un `std::atomic<FaultKind>`: arm/disarm/armed son cargas y
//     store atomicos; fire() usa compare-exchange para que el consumo sea
//     atomico y que, ante una carrera, solo un thread consuma el fallo.
//   - instance() es un singleton de Meyers (estatico local): inicializacion
//     thread-safe por garantia del standard (C++11+), sin lock a mano.
//   - writeWithFault() es la demostracion autocontenida de ChecksumMismatch:
//     copia un archivo y, si ese fallo esta armado, altera un byte. Solo std
//     (fstream + filesystem), sin dependencias de producto.
//
// RT-safety: es una ayuda de TEST/INTEGRACION. armed()/fire() son atomicos
// baratos, pero el contrato es usarlos en el camino de control o en tests, no
// en la callback de audio RT. writeWithFault() hace E/S de disco (control
// path). Ninguna funcion lanza.

#include "fault/FaultInjector.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>

namespace ba::fault {

// --- FaultInjector ----------------------------------------------------------

FaultInjector& FaultInjector::instance() {
    // Singleton de Meyers: el estatico local se inicializa de forma
    // thread-safe (C++11+). Un unico registro process-wide.
    static FaultInjector theInstance;
    return theInstance;
}

void FaultInjector::arm(FaultKind kind) {
    // Store atomico simple. Armar None equivale a desarmar.
    armed_.store(kind, std::memory_order_release);
}

void FaultInjector::disarm() {
    armed_.store(FaultKind::None, std::memory_order_release);
}

FaultKind FaultInjector::armed() const {
    return armed_.load(std::memory_order_acquire);
}

bool FaultInjector::fire() const {
    // Consumo atomico: solo un thread gana si hay carrera.
    for (;;) {
        // `expected` no es const: compare_exchange_strong lo actualiza por
        // referencia en caso de fallo del CAS.
        FaultKind expected = armed_.load(std::memory_order_acquire);
        if (expected == FaultKind::None) {
            // No habia fallo armado: no hay nada que consumir.
            return false;
        }
        if (armed_.compare_exchange_strong(
                expected, FaultKind::None,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            // Consumimos el fallo: queda en None para la proxima armed().
            return true;
        }
        // CAS fallo: `expected` ya trae el valor nuevo; el bucle lo reevalua
        // (puede que otro thread lo consumio -> None -> false).
    }
}

// --- writeWithFault (demostracion de ChecksumMismatch) ----------------------

bool writeWithFault(const FaultInjector& injector,
                    const std::string& sourcePath,
                    const std::string& outPath) {
    // 1) Leer el archivo original completo (binario).
    std::ifstream in(sourcePath, std::ios::binary);
    if (!in) {
        return false; // no existe / no se puede leer
    }
    std::string data((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    in.close();

    // 2) Si ChecksumMismatch esta armado, alterar EXACTAMENTE un byte (XOR
    //    0x01 sobre el primer byte). Garantiza un cambio (0->1 o 1->0) y, por
    //    tanto, un checksum distinto. Archivo vacio: no hay byte que alterar;
    //    se deja la copia exacta (no se puede corromper un archivo de 0 bytes).
    if (injector.armed() == FaultKind::ChecksumMismatch && !data.empty()) {
        data[0] = static_cast<char>(static_cast<unsigned char>(data[0]) ^ 0x01u);
    }

    // 3) Escribir la copia (binario, truncando el destino previo).
    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false; // no se puede escribir el destino
    }
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    out.close();
    return out.good();
}

} // namespace ba::fault
