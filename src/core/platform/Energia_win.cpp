// ============================================================================
// src/core/platform/Energia_win.cpp — ver Energia.hpp
// ============================================================================
#include "platform/Energia.hpp"
#include "i18n/I18n.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <filesystem>
#include <string>
#include <vector>

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

namespace {
// PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION (processthreadsapi.h, Windows 11).
constexpr ULONG kIgnorarResolucion = 0x4;
} // namespace

std::string sinAhorroDeEnergia() {
    // EcoQoS: Windows 11 (y 10 desde 1709) baja la velocidad de lo que considera de fondo
    // —la notebook con la tapa cerrada contra un monitor, la ventana minimizada—. Un
    // estudio en vivo no es de fondo. StateMask 0 con el bit en ControlMask = «nunca».
    PROCESS_POWER_THROTTLING_STATE e{};
    e.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    // Y que respete la resolución de reloj que pide (el audio la necesita fina: platform/Ritmo).
    // Siempre, aunque el encabezado no traiga el nombre (SDK anteriores a 10.0.22000, mingw):
    // con el #ifdef de antes, compilado así, Windows 11 seguía ignorando el reloj fino.
    e.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | kIgnorarResolucion;
    e.StateMask = 0;
    // Un Windows 10 que no conoce el bit puede rechazar el pedido entero: ahí, sin él (lo
    // de EcoQoS sí lo conoce, y su reloj no ignora la resolución).
    bool ok = SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &e, sizeof(e)) != FALSE;
    if (!ok) {
        e.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        ok = SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &e, sizeof(e)) != FALSE;
    }
    if (!ok)
        return ba::i18n::llenar(ba::i18n::traducir("energia", "no se pudo sacar del modo eficiencia (error %1)"),
                                {std::to_string(GetLastError())});
    return ba::i18n::traducir("energia", "fuera del modo eficiencia (EcoQoS)");
}

// --- El corte de luz (F1-11) -------------------------------------------------------------------

namespace {

// powrprof.dll es del sistema (siempre está): se carga al usarla, sin enlazarla.
using GetPwrCapabilitiesFn = BOOLEAN(WINAPI*)(PSYSTEM_POWER_CAPABILITIES);
using PowerGetActiveSchemeFn = DWORD(WINAPI*)(HKEY, GUID**);
using PowerReadACValueIndexFn = DWORD(WINAPI*)(HKEY, const GUID*, const GUID*, const GUID*, LPDWORD);

HMODULE powrprof() {
    static HMODULE m = LoadLibraryW(L"powrprof.dll");
    return m;
}

template <class F>
F funcion(const char* nombre) {
    return powrprof() != nullptr ? reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(powrprof(), nombre))) : nullptr;
}

// Los GUID de winnt.h, escritos acá (los de winnt.h piden enlazar powrprof.lib).
constexpr GUID kSubReposo{0x238C9FA8, 0x0AAD, 0x41ED, {0x83, 0xF4, 0x97, 0xBE, 0x24, 0x2C, 0x8F, 0x20}};
constexpr GUID kReposoTras{0x29F6C1DB, 0x86DA, 0x48C5, {0x9F, 0xDB, 0xF2, 0xB6, 0x7B, 0x1F, 0x44, 0xDA}};
constexpr GUID kSubUsb{0x2A737441, 0x1930, 0x4402, {0x8D, 0x77, 0xB2, 0xBE, 0xBB, 0xA3, 0x08, 0xA3}};
constexpr GUID kUsbSuspension{0x48E6B7A6, 0x50F5, 0x4782, {0xA5, 0xD4, 0x53, 0xBB, 0x8F, 0x07, 0xE2, 0x26}};

/// Un valor «con corriente» del plan de energía activo; -1 si no se puede leer.
long valorAC(const GUID& sub, const GUID& ajuste) {
    const auto activo = funcion<PowerGetActiveSchemeFn>("PowerGetActiveScheme");
    const auto leer = funcion<PowerReadACValueIndexFn>("PowerReadACValueIndex");
    if (activo == nullptr || leer == nullptr) return -1;
    GUID* plan = nullptr;
    if (activo(nullptr, &plan) != ERROR_SUCCESS || plan == nullptr) return -1;
    DWORD v = 0;
    const DWORD r = leer(nullptr, plan, &sub, &ajuste, &v);
    LocalFree(plan);
    return r == ERROR_SUCCESS ? static_cast<long>(v) : -1;
}

std::string utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring registro(const wchar_t* clave, const wchar_t* valor) {
    wchar_t b[256] = {};
    DWORD tam = sizeof b;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, clave, valor, RRF_RT_REG_SZ, nullptr, b, &tam) != ERROR_SUCCESS) return {};
    return b;
}

} // namespace

EstadoEnergia leerEnergia() {
    EstadoEnergia e;
    SYSTEM_POWER_STATUS s{};
    if (!GetSystemPowerStatus(&s)) return e;
    e.conocido = true;
    e.hayBateria = s.BatteryFlag != 128 && s.BatteryFlag != 255;
    e.enBateria = e.hayBateria && s.ACLineStatus == 0;
    e.porcentaje = s.BatteryLifePercent <= 100 ? s.BatteryLifePercent : -1;
    e.minutos = e.enBateria && s.BatteryLifeTime != static_cast<DWORD>(-1) ? static_cast<int>(s.BatteryLifeTime / 60) : -1;
    if (e.hayBateria) {
        // Un UPS USB HID aparece como batería; las capacidades dicen si es un UPS.
        SYSTEM_POWER_CAPABILITIES c{};
        const auto cap = funcion<GetPwrCapabilitiesFn>("GetPwrCapabilities");
        e.fuente = cap != nullptr && cap(&c) && c.UpsPresent && !c.SystemBatteriesPresent ? "ups" : "bateria";
    }
    return e;
}

std::string sistemaDeArchivos(const std::filesystem::path& ruta) {
    std::filesystem::path p = ruta;
    std::error_code ec;
    while (!p.empty() && !std::filesystem::exists(p, ec) && p != p.parent_path()) p = p.parent_path();
    wchar_t vol[MAX_PATH] = {}, nombre[64] = {};
    if (!GetVolumePathNameW(p.wstring().c_str(), vol, MAX_PATH)) return {};
    if (!GetVolumeInformationW(vol, nullptr, 0, nullptr, nullptr, nullptr, nombre, 64)) return {};
    return utf8(nombre);
}

std::vector<ItemPreparacion> preparacion(const std::filesystem::path& datos) {
    using ba::i18n::llenar;
    using ba::i18n::traducir;
    std::vector<ItemPreparacion> out;
    const EstadoEnergia e = leerEnergia();

    ItemPreparacion arr;
    arr.id = "arranque_al_volver";
    arr.titulo = traducir("preparacion", "Encender sola al volver la luz");
    if (e.hayBateria && e.fuente == "bateria") {
        arr.estado = "no_aplica";
        arr.detalle = traducir("preparacion", "Es una notebook: su batería la mantiene encendida durante el corte.");
    } else {
        arr.estado = "revisar";
        arr.detalle = traducir("preparacion", "Lo decide el BIOS/UEFI y Windows no lo puede leer.");
        arr.como = traducir("preparacion", "En el BIOS/UEFI: «Restore on AC power loss» (o «AC Power Recovery») = Power On.");
    }
    out.push_back(arr);

    ItemPreparacion ses;
    ses.id = "sesion_automatica";
    ses.titulo = traducir("preparacion", "Iniciar sesión sola");
    const wchar_t* kWinlogon = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon";
    const bool auto_ = registro(kWinlogon, L"AutoAdminLogon") == L"1";
    ses.estado = auto_ ? "ok" : "falta";
    ses.detalle = auto_ ? llenar(traducir("preparacion", "La sesión de «%1» se inicia sola."), {utf8(registro(kWinlogon, L"DefaultUserName"))})
                        : traducir("preparacion", "Al encender, Windows espera que alguien inicie sesión.");
    if (!auto_)
        ses.como = traducir("preparacion", "Con netplwiz (destildar «Los usuarios deben escribir su nombre y contraseña») o con "
                                           "Autologon de Sysinternals.");
    out.push_back(ses);

    out.push_back(itemSistemaDeArchivos(datos));
    out.push_back(itemRespaldo(e));

    // El plan de energía: con el programa abierto Windows no se suspende (Despierta),
    // pero hasta que abre, sí.
    ItemPreparacion rep;
    rep.id = "reposo";
    rep.titulo = traducir("preparacion", "Plan de energía sin suspensión");
    const long seg = valorAC(kSubReposo, kReposoTras);
    rep.estado = seg < 0 ? "revisar" : seg == 0 ? "ok" : "falta";
    rep.detalle = seg < 0    ? traducir("preparacion", "No se pudo leer el plan de energía.")
                  : seg == 0 ? traducir("preparacion", "Con corriente, Windows no se suspende solo.")
                             : llenar(traducir("preparacion", "Con corriente, Windows se suspende a los %1 min sin uso."),
                                      {std::to_string(seg / 60)});
    if (seg > 0) rep.como = traducir("preparacion", "Configuración → Sistema → Energía → «Suspender» = Nunca (o: powercfg /change standby-timeout-ac 0).");
    out.push_back(rep);

    ItemPreparacion usb;
    usb.id = "usb";
    usb.titulo = traducir("preparacion", "USB sin suspensión selectiva");
    const long sel = valorAC(kSubUsb, kUsbSuspension);
    usb.estado = sel < 0 ? "revisar" : sel == 0 ? "ok" : "falta";
    usb.detalle = sel < 0    ? traducir("preparacion", "No se pudo leer el ajuste de USB del plan de energía.")
                  : sel == 0 ? traducir("preparacion", "Windows no suspende los dispositivos USB.")
                             : traducir("preparacion", "Windows puede suspender una placa de captura o de audio USB quieta.");
    if (sel > 0)
        usb.como = traducir("preparacion", "Opciones de energía → Cambiar la configuración avanzada → Configuración de USB → "
                                           "«Suspensión selectiva de USB» = Deshabilitado.");
    out.push_back(usb);

    ItemPreparacion red;
    red.id = "red";
    red.titulo = traducir("preparacion", "Placa de red sin ahorro de energía");
    red.estado = "revisar";
    red.detalle = traducir("preparacion", "Windows puede apagar la placa de red para ahorrar energía (se cortan la transmisión "
                                          "y los invitados).");
    red.como = traducir("preparacion", "Administrador de dispositivos → Adaptadores de red → la placa → Administración de "
                                       "energía → destildar «Permitir que el equipo apague este dispositivo».");
    out.push_back(red);
    return out;
}

} // namespace ba::platform
