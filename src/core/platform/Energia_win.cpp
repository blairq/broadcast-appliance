// ============================================================================
// src/core/platform/Energia_win.cpp — ver Energia.hpp
// ============================================================================
#include "platform/Energia.hpp"
#include "i18n/I18n.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

namespace ba::platform {

struct Despierta::Impl {
    HANDLE pedido = INVALID_HANDLE_VALUE;
    bool sistema = false;
    bool pantalla = false;
    std::wstring motivo; // REASON_CONTEXT apunta acá: vive lo que vive el pedido
    std::string detalle;
};

namespace {
std::wstring ancho(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
} // namespace

Despierta::Despierta(const std::string& motivo, bool pantalla) : d_(std::make_unique<Impl>()) {
    d_->motivo = ancho(motivo);
    REASON_CONTEXT rc{};
    rc.Version = POWER_REQUEST_CONTEXT_VERSION;
    rc.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
    rc.Reason.SimpleReasonString = d_->motivo.data();
    d_->pedido = PowerCreateRequest(&rc);
    if (d_->pedido == INVALID_HANDLE_VALUE) {
        d_->detalle = ba::i18n::llenar(ba::i18n::traducir("energia", "Windows no aceptó el pedido de energía (error %1)"),
                                       {std::to_string(GetLastError())});
        return;
    }
    d_->sistema = PowerSetRequest(d_->pedido, PowerRequestSystemRequired) != FALSE;
    if (pantalla) d_->pantalla = PowerSetRequest(d_->pedido, PowerRequestDisplayRequired) != FALSE;
    using ba::i18n::traducir;
    const std::string rechazado = " " + traducir("energia", "(rechazado)");
    d_->detalle = traducir("energia", "sin suspender el sistema") + (d_->sistema ? std::string() : rechazado) +
                  (pantalla ? ", " + traducir("energia", "sin apagar la pantalla") +
                                  (d_->pantalla ? std::string() : rechazado)
                            : std::string()) +
                  " (powercfg /requests)";
}

Despierta::~Despierta() {
    if (d_ == nullptr || d_->pedido == INVALID_HANDLE_VALUE) return;
    if (d_->pantalla) PowerClearRequest(d_->pedido, PowerRequestDisplayRequired);
    if (d_->sistema) PowerClearRequest(d_->pedido, PowerRequestSystemRequired);
    CloseHandle(d_->pedido);
}

bool Despierta::activa() const { return d_ != nullptr && d_->sistema; }
const std::string& Despierta::detalle() const { return d_->detalle; }

std::string sinAhorroDeEnergia() {
    // EcoQoS: Windows 11 (y 10 desde 1709) baja la velocidad de lo que considera de fondo
    // —la notebook con la tapa cerrada contra un monitor, la ventana minimizada—. Un
    // estudio en vivo no es de fondo. StateMask 0 con el bit en ControlMask = «nunca».
    PROCESS_POWER_THROTTLING_STATE e{};
    e.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    e.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
#ifdef PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION
    // Y que respete la resolución de reloj que pide (el audio la necesita fina).
    e.ControlMask |= PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
#endif
    e.StateMask = 0;
    if (SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &e, sizeof(e)) == FALSE)
        return ba::i18n::llenar(ba::i18n::traducir("energia", "no se pudo sacar del modo eficiencia (error %1)"),
                                {std::to_string(GetLastError())});
    return ba::i18n::traducir("energia", "fuera del modo eficiencia (EcoQoS)");
}

} // namespace ba::platform
