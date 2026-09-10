// SharedMemoryRegion.hpp
//
// Proposito:
//   RAII wrapper sobre un buffer contiguo de bytes respaldado por memoria
//   compartida. Es la primitiva de transporte comun que usan los contratos de
//   IPC de ba_core (rings de audio 4.3 y el bloque de salud 4.4). No depende
//   de Qt ni de libobs.
//
// Modelo de threading:
//   El objeto en si NO es thread-safe: cada instancia representa un mapa
//   (view) del buffer y se usa desde un solo owner. La seguridad entre
//   procesos/threads la aporta el layout del contenido (p. ej. los atomic_ref
//   del ring), no esta clase. La instancia es move-only: moverla transfiere
//   la propiedad del mapa; copiarla esta prohibida.
//
// Estrategia de errores:
//   Las operaciones de create/open devuelven un Status {ok, code} y NUNCA
//   lanzan. code == 0 es exito; en fallo, code es un valor negativo estable
//   (ver enum ErrorCode) que el caller puede loguear de forma estructurada.
//   El destructor/assign/close nunca lanzan: liberan lo que puedan y dejan el
//   objeto en estado vacio.
//
// Backend por plataforma:
//   - _WIN32: CreateFileMappingW/MapViewOfFile sobre el namespace Local\, con
//     una DACL explicita restringida al usuario actual (spec 4: "Global\ exige
//     un privilegio que un usuario estandar no tiene").
//   - resto (Linux/macOS): asignacion anonima (mmap MAP_ANONYMOUS) + un
//     registro de proceso name -> buffer, para que create/open tengan la misma
//     semantica de "buscar por nombre" dentro del proceso y las pruebas corran
//     en cualquier lugar sin dependencias.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cwchar>

namespace ba::ipc {

/// Resultado de una operacion de memoria compartida.
///
/// `ok` es false cuando la operacion fallo; `code` es 0 en exito y un valor
/// negativo de :type:`ErrorCode` en fallo. Pequeno y trivialmente copiable para
/// cruzar el borde hacia el logging estructurado sin overhead.
struct Status {
    bool ok = false;
    int code = 0;

    static Status success() { return Status{true, 0}; }
    static Status fail(int code) { return Status{false, code}; }
};

/// Codigos de error estables para logueo estructurado (componente, codigo,
/// contexto). Son negativos para que `code == 0` signifique siempre "ok".
enum class ErrorCode : int {
    kOk = 0,
    kInvalidArgument = -1,   // name nulo, bytes == 0, out ya valido, etc.
    kOutOfMemory = -2,       // la asignacion anonima fallo
    kNotFound = -3,          // open() de un nombre que no existe
    kPlatform = -4,          // fallo del backend (CreateFileMappingW, mmap, ...)
    kAlreadyMapped = -5,     // create/open sobre un objeto ya valido
};

/// RAII sobre un buffer contiguo de bytes en memoria compartida.
///
/// Move-only. El destructor (o `close()`, o una asignacion move) desmapa el
/// buffer. `data()` devuelve el puntero al inicio del buffer; `size()` el
/// tamano en bytes. `create` reserva y (en Windows) publica el nombre; `open`
/// reencuentra un nombre ya publicado.
class SharedMemoryRegion {
public:
    SharedMemoryRegion() = default;

    ~SharedMemoryRegion();

    // Move-only: transferir la propiedad del mapa es barato y seguro.
    SharedMemoryRegion(SharedMemoryRegion&& other) noexcept;
    SharedMemoryRegion& operator=(SharedMemoryRegion&& other) noexcept;

    SharedMemoryRegion(const SharedMemoryRegion&) = delete;
    SharedMemoryRegion& operator=(const SharedMemoryRegion&) = delete;

    /// Reserva `bytes` de memoria compartida y publica el `name`.
    ///
    /// En Windows crea el file mapping en `Local\<name>` con una DACL
    /// restringida al usuario actual. En otras plataformas reserva una
    /// asignacion anonima y la registra por `name` dentro del proceso.
    ///
    /// :param name: nombre logico del buffer (UTF-16). En Windows se antepone
    ///              el namespace `Local\`. Debe ser distinto de nulo.
    /// :param bytes: tamano en bytes. Debe ser > 0.
    /// :param out: recibe el objeto valido en exito; se deja vacio en fallo.
    /// :returns: Status con ok=true y code=0 en exito.
    static Status create(const wchar_t* name, size_t bytes, SharedMemoryRegion& out);

    /// Reencuentra un buffer ya publicado con `name` y lo mapea.
    ///
    /// En Windows abre el file mapping existente (sin crearlo). En otras
    /// plataformas busca el nombre en el registro de proceso.
    ///
    /// :param name: nombre logico publicado previamente por `create`.
    /// :param out: recibe el objeto valido en exito; se deja vacio en fallo.
    /// :returns: Status con ok=true y code=0 en exito.
    static Status open(const wchar_t* name, SharedMemoryRegion& out);

    /// Puntero al inicio del buffer mapeado.
    ///
    /// :returns: puntero valido si `valid()`, si no nullptr.
    void* data() const;

    /// Tamano del buffer en bytes.
    ///
    /// :returns: tamano si `valid()`, si no 0.
    size_t size() const;

    /// Indica si el objeto posee un mapa vigente.
    bool valid() const;

    /// Desmapa el buffer de forma explicita. Idempotente.
    void close();

private:
    struct Impl;
    Impl* impl_ = nullptr; // pimpl: oculta los detalles de la plataforma
};

} // namespace ba::ipc
