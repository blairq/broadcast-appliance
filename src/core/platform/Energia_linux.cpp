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
#include <unistd.h>

#include <cstdint>
#include <string>

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

} // namespace ba::platform
