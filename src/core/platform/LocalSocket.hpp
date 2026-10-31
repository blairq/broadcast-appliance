// ============================================================================
// src/core/platform/LocalSocket.hpp — Canal local entre procesos (control A/B)
//
// Proposito:
//   Transporte real del canal de control (spec §4.1) entre el launcher, A y B.
//   `LocalStream` implementa `ba::ipc::DuplexStream`, asi que `ControlEndpoint`
//   lo usa sin cambios.
//
// Implementaciones:
//   - Windows: named pipe `\\.\pipe\BroadcastAppliance.<sesion>.<nombre>`, en
//     modo byte, con DACL que solo admite al usuario actual (y SYSTEM),
//     PIPE_REJECT_REMOTE_CLIENTS y FILE_FLAG_FIRST_PIPE_INSTANCE (nadie puede
//     "ocupar" el nombre antes que el servidor).
//   - Linux/macOS: socket Unix de tipo stream en `runtimeDir()/<nombre>.sock`.
//     El directorio es 0700 del usuario y, ademas, cada conexion aceptada
//     verifica que el UID del otro extremo sea el propio (SO_PEERCRED en
//     Linux, getpeereid en macOS). No se usa el namespace abstracto de Linux:
//     no tiene permisos y cualquier usuario podria conectarse.
//
// Semantica (igual en todas):
//   - `write` es bloqueante hasta escribir todo; false si el otro lado cerro.
//   - `read` es NO bloqueante: >0 bytes, 0 sin datos ahora, <0 cerrado/error
//     (contrato de `DuplexStream`).
//   - `waitReadable` bloquea hasta que haya datos o cierre, con timeout, para
//     que un hilo de control no tenga que hacer polling.
//   - `connect` reintenta hasta el timeout si el servidor todavia no existe:
//     A y B arrancan en paralelo y el orden no debe importar.
//
// Modelo de threading:
//   Objetos move-only de un solo dueño. Un stream admite UN lector y UN
//   escritor en hilos distintos en POSIX; en Windows, usarlo desde un solo
//   hilo (las operaciones solapadas comparten el evento del stream).
//
// Estrategia de errores:
//   No lanza. Los metodos que pueden fallar devuelven bool y, si se pasa,
//   dejan el motivo en `err`.
// ============================================================================

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "ipc/ControlProtocol.hpp"

namespace ba::platform {

/// Extremo conectado de un canal local.
class LocalStream final : public ba::ipc::DuplexStream {
   public:
    LocalStream() = default;
    ~LocalStream() override;
    LocalStream(LocalStream&& other) noexcept;
    LocalStream& operator=(LocalStream&& other) noexcept;
    LocalStream(const LocalStream&) = delete;
    LocalStream& operator=(const LocalStream&) = delete;

    /// Conecta con el servidor `name`, reintentando hasta `timeoutMs`.
    ///
    /// :param name: nombre logico del canal (p. ej. "control.gateway");
    ///              caracteres [A-Za-z0-9._-].
    /// :param timeoutMs: tiempo maximo total (>= 0).
    /// :param out: recibe el stream conectado.
    /// :param err: motivo del fallo (opcional).
    /// :returns: true si conecto.
    static bool connect(const std::string& name, int timeoutMs, LocalStream& out, std::string* err = nullptr);

    bool write(const std::uint8_t* data, std::size_t len) override;
    int read(std::uint8_t* buf, std::size_t cap) override;

    /// Espera hasta que haya datos para leer o el otro lado cierre.
    ///
    /// :param timeoutMs: <0 sin limite.
    /// :returns: true si `read()` ya no devolveria 0; false por timeout.
    bool waitReadable(int timeoutMs);

    /// true si el stream esta abierto (no implica que el otro lado siga).
    bool isOpen() const;

    /// Cierra el extremo. Idempotente.
    void close();

   private:
    friend class LocalServer;
#if defined(_WIN32)
    void* pipe_ = nullptr;  // HANDLE
    void* event_ = nullptr; // HANDLE del evento de las operaciones solapadas
#else
    int fd_ = -1;
#endif
};

/// Servidor de un canal local: acepta conexiones de `LocalStream::connect`.
class LocalServer {
   public:
    LocalServer() = default;
    ~LocalServer();
    LocalServer(LocalServer&& other) noexcept;
    LocalServer& operator=(LocalServer&& other) noexcept;
    LocalServer(const LocalServer&) = delete;
    LocalServer& operator=(const LocalServer&) = delete;

    /// Empieza a escuchar en `name`.
    ///
    /// Falla si otro servidor VIVO ya escucha en ese nombre. Un socket
    /// huerfano de un proceso que murio (POSIX) se detecta y se reemplaza.
    static bool listen(const std::string& name, LocalServer& out, std::string* err = nullptr);

    /// Espera una conexion hasta `timeoutMs` (<0 sin limite).
    ///
    /// :returns: true y `out` conectado; false por timeout, error o si el
    ///           cliente no es del mismo usuario (se rechaza y se cierra).
    bool accept(int timeoutMs, LocalStream& out, std::string* err = nullptr);

    /// Deja de escuchar (y en POSIX borra el archivo del socket). Idempotente.
    void close();

    bool isListening() const;

   private:
    std::string name_;
#if defined(_WIN32)
    bool createInstance(bool first, std::string* err);
    void* pending_ = nullptr; // HANDLE de la instancia que espera cliente
    void* event_ = nullptr;
    void* security_ = nullptr; // PSECURITY_DESCRIPTOR (LocalAlloc)
#else
    int fd_ = -1;
    std::string path_;
#endif
};

} // namespace ba::platform
