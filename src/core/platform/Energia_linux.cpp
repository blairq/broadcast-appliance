// ============================================================================
// src/core/platform/Energia_linux.cpp — ver Energia.hpp
//
// systemd-logind: Manager.Inhibit("sleep:idle", quién, por qué, "block") devuelve
// un descriptor; mientras esté abierto, ni suspensión ni el reposo por inactividad.
// libdbus-1 es parte de la base del sistema (la pide Qt; tools/deps/linux-baseline):
// se carga con dlopen al usarla, sin enlazarla ni traer sus headers (los tipos que
// hacen falta son pocos y estables: el ABI de libdbus no cambia desde 2006).
// ============================================================================
#include "platform/Energia.hpp"
#include "i18n/I18n.hpp"

#include <dlfcn.h>
#include <sys/vfs.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace ba::platform {

namespace {
// Lo mínimo de libdbus-1 (dbus/dbus-protocol.h, dbus/dbus-errors.h).
constexpr int kBusSistema = 1;           // DBUS_BUS_SYSTEM
constexpr int kTipoString = 's';         // DBUS_TYPE_STRING
constexpr int kTipoFd = 'h';             // DBUS_TYPE_UNIX_FD
constexpr int kTipoInvalido = 0;         // DBUS_TYPE_INVALID
struct ErrorDbus {                       // DBusError: dos punteros y relleno
    const char* name;
    const char* message;
    unsigned int bits;
    void* relleno;
};
using FnErrorInit = void (*)(ErrorDbus*);
using FnErrorFree = void (*)(ErrorDbus*);
using FnBusGet = void* (*)(int, ErrorDbus*);
using FnNuevo = void* (*)(const char*, const char*, const char*, const char*);
using FnArgs = int (*)(void*, int, ...);
using FnEnviar = void* (*)(void*, void*, int, ErrorDbus*);
using FnGetArgs = int (*)(void*, ErrorDbus*, int, ...);
using FnUnref = void (*)(void*);
} // namespace

struct Despierta::Impl {
    int fd = -1;
    std::string detalle;
};

Despierta::Despierta(const std::string& motivo, bool /*pantalla*/) : d_(std::make_unique<Impl>()) {
    // La pantalla la maneja el escritorio (no logind): con «idle» no se suspende por
    // inactividad; apagar el monitor queda en manos del escritorio de la máquina.
    void* lib = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL);
    if (lib == nullptr) {
        d_->detalle = ba::i18n::traducir("energia", "no está libdbus-1 (sin D-Bus no se puede pedir que no suspenda)");
        return;
    }
    auto errorInit = reinterpret_cast<FnErrorInit>(dlsym(lib, "dbus_error_init"));
    auto errorFree = reinterpret_cast<FnErrorFree>(dlsym(lib, "dbus_error_free"));
    auto busGet = reinterpret_cast<FnBusGet>(dlsym(lib, "dbus_bus_get"));
    auto nuevo = reinterpret_cast<FnNuevo>(dlsym(lib, "dbus_message_new_method_call"));
    auto args = reinterpret_cast<FnArgs>(dlsym(lib, "dbus_message_append_args"));
    auto enviar = reinterpret_cast<FnEnviar>(dlsym(lib, "dbus_connection_send_with_reply_and_block"));
    auto getArgs = reinterpret_cast<FnGetArgs>(dlsym(lib, "dbus_message_get_args"));
    auto unref = reinterpret_cast<FnUnref>(dlsym(lib, "dbus_message_unref"));
    if (errorInit == nullptr || errorFree == nullptr || busGet == nullptr || nuevo == nullptr || args == nullptr ||
        enviar == nullptr || getArgs == nullptr || unref == nullptr) {
        d_->detalle = ba::i18n::traducir("energia", "libdbus-1 sin las funciones esperadas");
        return;
    }
    ErrorDbus err{};
    errorInit(&err);
    void* bus = busGet(kBusSistema, &err); // compartida: no se cierra (es de libdbus)
    if (bus == nullptr) {
        d_->detalle = ba::i18n::llenar(ba::i18n::traducir("energia", "no hay bus de sistema: %1"),
                                       {err.message != nullptr ? err.message : ""});
        errorFree(&err);
        return;
    }
    void* m = nuevo("org.freedesktop.login1", "/org/freedesktop/login1", "org.freedesktop.login1.Manager", "Inhibit");
    const char* que = "sleep:idle";
    const char* quien = "Broadcast Appliance";
    const char* porque = motivo.c_str();
    const char* modo = "block";
    args(m, kTipoString, &que, kTipoString, &quien, kTipoString, &porque, kTipoString, &modo, kTipoInvalido);
    void* r = enviar(bus, m, 5000, &err);
    unref(m);
    if (r == nullptr) {
        d_->detalle = ba::i18n::llenar(ba::i18n::traducir("energia", "logind no aceptó el pedido: %1"),
                                       {err.message != nullptr ? err.message : ""});
        errorFree(&err);
        return;
    }
    int fd = -1;
    if (getArgs(r, &err, kTipoFd, &fd, kTipoInvalido) == 0) fd = -1;
    unref(r);
    errorFree(&err);
    d_->fd = fd;
    d_->detalle = fd >= 0 ? ba::i18n::traducir("energia", "sin suspender ni reposo por inactividad (systemd-inhibit --list)")
                          : ba::i18n::traducir("energia", "logind no devolvió el bloqueo");
}

Despierta::~Despierta() {
    if (d_ != nullptr && d_->fd >= 0) close(d_->fd);
}

bool Despierta::activa() const { return d_ != nullptr && d_->fd >= 0; }
const std::string& Despierta::detalle() const { return d_->detalle; }

std::string sinAhorroDeEnergia() { return {}; } // Linux no frena procesos por su cuenta

// --- El corte de luz (F1-11) -------------------------------------------------------------------

EstadoEnergia leerEnergia() { return leerPowerSupply("/sys/class/power_supply"); }

std::string sistemaDeArchivos(const std::filesystem::path& ruta) {
    std::filesystem::path p = ruta;
    std::error_code ec;
    while (!p.empty() && !std::filesystem::exists(p, ec) && p != p.parent_path()) p = p.parent_path();
    struct statfs s {};
    if (statfs(p.c_str(), &s) != 0) return {};
    // Los números mágicos de linux/magic.h (sin incluirlo: no está en todas las bases).
    switch (static_cast<unsigned long>(s.f_type)) {
        case 0xEF53: return "ext4"; // ext2/3/4 comparten el número: ext2 sin diario es rarísimo hoy
        case 0x58465342: return "xfs";
        case 0x9123683E: return "btrfs";
        case 0x2FC12FC1: return "zfs";
        case 0xF2F52010: return "f2fs";
        case 0x4d44: return "vfat";
        case 0x2011BAB0: return "exfat";
        case 0x5346544e: return "ntfs";
        case 0x7366746e: return "ntfs3";
        case 0x65735546: return "fuse"; // ntfs-3g, exfat-fuse…: no se sabe
        case 0x01021994: return "tmpfs";
        default: return "0x" + [](unsigned long v) {
            char b[20];
            std::snprintf(b, sizeof b, "%lx", v);
            return std::string(b);
        }(static_cast<unsigned long>(s.f_type));
    }
}

namespace {

std::string leerTodo(const std::filesystem::path& p) {
    std::ifstream f(p);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

/// ¿Algún gestor de inicio de sesión tiene el inicio automático? (GDM, LightDM, SDDM.)
std::string usuarioAutomatico() {
    namespace fs = std::filesystem;
    std::vector<fs::path> archivos = {"/etc/gdm3/custom.conf", "/etc/gdm/custom.conf", "/etc/lightdm/lightdm.conf",
                                      "/etc/sddm.conf"};
    std::error_code ec;
    for (const char* dir : {"/etc/lightdm/lightdm.conf.d", "/etc/sddm.conf.d"})
        for (const auto& e : fs::directory_iterator(dir, ec)) archivos.push_back(e.path());
    for (const auto& a : archivos) {
        std::istringstream in(leerTodo(a));
        std::string l;
        bool gdmSi = false;
        std::string gdmUsuario;
        while (std::getline(in, l)) {
            if (l.empty() || l[0] == '#') continue;
            const auto valor = [&](const char* clave) -> std::optional<std::string> {
                const std::string k = std::string(clave) + "=";
                return l.rfind(k, 0) == 0 ? std::optional<std::string>(l.substr(k.size())) : std::nullopt;
            };
            if (auto v = valor("AutomaticLoginEnable")) gdmSi = *v == "true" || *v == "True" || *v == "1";
            if (auto v = valor("AutomaticLogin")) gdmUsuario = *v;
            if (auto v = valor("autologin-user"); v && !v->empty()) return *v; // LightDM
            if (auto v = valor("User"); v && !v->empty() && a.string().find("sddm") != std::string::npos) return *v;
        }
        if (gdmSi && !gdmUsuario.empty()) return gdmUsuario;
    }
    return {};
}

} // namespace

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
        arr.detalle = traducir("preparacion", "Lo decide el BIOS/UEFI y Linux no lo puede leer.");
        arr.como = traducir("preparacion", "En el BIOS/UEFI: «Restore on AC power loss» (o «AC Power Recovery») = Power On.");
    }
    out.push_back(arr);

    ItemPreparacion ses;
    ses.id = "sesion_automatica";
    ses.titulo = traducir("preparacion", "Iniciar sesión sola");
    const std::string u = usuarioAutomatico();
    ses.estado = u.empty() ? "falta" : "ok";
    ses.detalle = u.empty() ? traducir("preparacion", "Al encender, el equipo espera que alguien inicie sesión.")
                            : llenar(traducir("preparacion", "La sesión de «%1» se inicia sola."), {u});
    if (u.empty())
        ses.como = traducir("preparacion", "En la configuración de usuarios del escritorio: «Inicio de sesión automático» "
                                           "(GDM: AutomaticLoginEnable en /etc/gdm3/custom.conf; LightDM: autologin-user).");
    out.push_back(ses);

    out.push_back(itemSistemaDeArchivos(datos));
    out.push_back(itemRespaldo(e));

    // La suspensión selectiva de USB: una placa de captura o una interfaz de audio
    // que el núcleo «duerme» se cae a mitad del evento.
    ItemPreparacion usb;
    usb.id = "usb";
    usb.titulo = traducir("preparacion", "USB sin suspensión automática");
    const std::string a = leerTodo("/sys/module/usbcore/parameters/autosuspend");
    const int seg = a.empty() ? 0 : std::atoi(a.c_str());
    usb.estado = a.empty() ? "revisar" : seg < 0 ? "ok" : "falta";
    usb.detalle = a.empty() ? traducir("preparacion", "No se pudo leer /sys/module/usbcore/parameters/autosuspend.")
                  : seg < 0 ? traducir("preparacion", "El núcleo no suspende los dispositivos USB.")
                            : llenar(traducir("preparacion", "El núcleo suspende un USB quieto a los %1 s."), {std::to_string(seg)});
    if (seg >= 0)
        usb.como = traducir("preparacion", "Agregá usbcore.autosuspend=-1 a la línea del núcleo (GRUB_CMDLINE_LINUX en "
                                           "/etc/default/grub y después update-grub).");
    out.push_back(usb);
    return out;
}

} // namespace ba::platform
