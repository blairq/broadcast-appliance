// ============================================================================
// src/core/platform/Termica_win.cpp — ver Termica.hpp
// ============================================================================
#include "platform/Termica.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <vector>

namespace ba::platform {

namespace {

// powrprof.h no declara esta estructura (está en la documentación de
// CallNtPowerInformation): se copia tal cual.
struct InfoProcesador {
    ULONG numero, maxMhz, actualMhz, limiteMhz, maxIdleState, currentIdleState;
};
using CallNtPowerInformationFn = LONG(WINAPI*)(int, PVOID, ULONG, PVOID, ULONG);
constexpr int kProcessorInformation = 11;

} // namespace

EstadoTermico leerTermica() {
    // Windows no da la temperatura sin un servicio (el WMI de ACPI pide administrador y
    // casi ninguna placa la publica). Da el tope de frecuencia que se impone el sistema:
    // un tope bajo es la máquina frenándose. powrprof.dll se carga al medir (no se importa).
    EstadoTermico e;
    static HMODULE dll = LoadLibraryW(L"powrprof.dll");
    static auto fn = dll != nullptr ? reinterpret_cast<CallNtPowerInformationFn>(
                                          reinterpret_cast<void*>(GetProcAddress(dll, "CallNtPowerInformation")))
                                    : nullptr;
    if (fn == nullptr) return e;
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    std::vector<InfoProcesador> info(si.dwNumberOfProcessors > 0 ? si.dwNumberOfProcessors : 1);
    if (fn(kProcessorInformation, nullptr, 0, info.data(), static_cast<ULONG>(info.size() * sizeof(InfoProcesador))) != 0)
        return e;
    double minimo = -1;
    for (const auto& p : info)
        if (p.maxMhz > 0) {
            const double pct = 100.0 * p.limiteMhz / p.maxMhz;
            if (minimo < 0 || pct < minimo) minimo = pct;
        }
    e.limiteFrecuenciaPct = minimo;
    e.nivel = nivelPorLimite(minimo);
    e.fuente = "powrprof";
    return e;
}

} // namespace ba::platform
