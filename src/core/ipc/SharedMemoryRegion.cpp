// SharedMemoryRegion.cpp
//
// Implementacion del RAII de memoria compartida descrito en el header.
// Dos backends:
//   - _WIN32: CreateFileMappingW/MapViewOfFile, namespace Local\, DACL
//     restringida al usuario actual.
//   - resto:  asignacion anonima (mmap) + registro de proceso name -> buffer,
//     para que create/open compartan semantica y las pruebas corran en Linux.
//
// Ninguna de las funciones publicas lanza: los fallos se expresan con Status.

#include "SharedMemoryRegion.hpp"

#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace ba::ipc {

#if defined(_WIN32)

namespace detail {

// Prepara una DACL que otorga acceso completo solo al usuario actual
// (spec 4: "La DACL es explicita y restringida al usuario actual").
//
// :param sa: Security Descriptor a inicializar.
///:returns: true si la DACL se construyo correctamente.
bool buildCurrentUserDacl(SECURITY_ATTRIBUTES& sa) {
    HANDLE tokenHandle = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tokenHandle)) {
        return false;
    }
    DWORD needed = 0;
    if (!GetTokenInformation(tokenHandle, TokenUser, nullptr, 0, &needed) || needed == 0) {
        CloseHandle(tokenHandle);
        return false;
    }
    std::vector<BYTE> buf(needed);
    if (!GetTokenInformation(tokenHandle, TokenUser, buf.data(), needed, &needed)) {
        CloseHandle(tokenHandle);
        return false;
    }
    CloseHandle(tokenHandle);
    const PSID userSid = const_cast<PSID>(reinterpret_cast<const TOKEN_USER*>(buf.data())->User.Sid);

    // Tamano conservador para SD + DACL + 1 ACE + SID del usuario.
    // Nota MinGW: el SDK de MSVC define `SECDACL` (un envoltorio de `ACL`), pero
    // los headers de MinGW no lo exponen. Como `sizeof(SECDACL) == sizeof(ACL)`,
    // se usa `ACL` directamente: el tamano resultante es identico.
    const DWORD sdSize = sizeof(ACL) + sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) + GetLengthSid(userSid);
    PSECURITY_DESCRIPTOR sd = static_cast<PSECURITY_DESCRIPTOR>(LocalAlloc(LPTR, sdSize));
    if (sd == nullptr) {
        return false;
    }
    if (!InitializeSecurityDescriptor(sd, SECURITY_DESCRIPTOR_REVISION)) {
        LocalFree(sd);
        return false;
    }
    // AddAccessAllowedAce reserva y enlaza la DACL dentro del bloque sd.
    // En MinGW, PSECURITY_DESCRIPTOR es `void*` y AddAccessAllowedAce pide
    // `PACL`; el cast es el mismo truco del patron MSDN (el buffer empieza por
    // la estructura ACL). En C++ la conversion implicito no existe.
    if (!AddAccessAllowedAce(static_cast<PACL>(sd), ACL_REVISION, GENERIC_ALL, userSid)) {
        LocalFree(sd);
        return false;
    }
    // bDaclDefaulted es un WINBOOL (no un puntero): FALSE = la DACL no es
    // heredada por defecto.
    if (!SetSecurityDescriptorDacl(sd, TRUE, FALSE, FALSE)) {
        LocalFree(sd);
        return false;
    }
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = sd;
    sa.bInheritHandle = FALSE;
    return true;
}

} // namespace detail

struct SharedMemoryRegion::Impl {
    HANDLE mapping = nullptr;
    void* view = nullptr;
    size_t size = 0;
    // Retiene la SD para que la DACL viva mientras el mapping exista.
    PSECURITY_DESCRIPTOR sd = nullptr;

    void* viewPtr() const { return view; }

    void release() {
        if (view != nullptr) {
            UnmapViewOfFile(view);
            view = nullptr;
        }
        if (mapping != nullptr) {
            CloseHandle(mapping);
            mapping = nullptr;
        }
        if (sd != nullptr) {
            LocalFree(sd);
            sd = nullptr;
        }
    }
};

Status SharedMemoryRegion::create(const wchar_t* name, size_t bytes, SharedMemoryRegion& out) {
    if (name == nullptr || bytes == 0) {
        return Status::fail(static_cast<int>(ErrorCode::kInvalidArgument));
    }
    if (out.valid()) {
        return Status::fail(static_cast<int>(ErrorCode::kAlreadyMapped));
    }

    // Namespace Local\: evita el privilegio que exige Global\ (spec 4).
    std::wstring full = L"Local\\";
    full.append(name);

    auto impl = new Impl();
    impl->size = bytes;

    SECURITY_ATTRIBUTES sa;
    if (detail::buildCurrentUserDacl(sa)) {
        impl->sd = static_cast<PSECURITY_DESCRIPTOR>(sa.lpSecurityDescriptor);
        impl->mapping =
            CreateFileMappingW(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, static_cast<DWORD>(bytes >> 32),
                               static_cast<DWORD>(bytes & 0xFFFFFFFF), full.c_str());
        if (impl->mapping == nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
            impl->mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, full.c_str());
        }
    } else {
        // Sin DACL (raro): seguimos con el acceso por defecto.
        impl->mapping =
            CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, static_cast<DWORD>(bytes >> 32),
                               static_cast<DWORD>(bytes & 0xFFFFFFFF), full.c_str());
    }

    if (impl->mapping == nullptr) {
        delete impl;
        return Status::fail(static_cast<int>(ErrorCode::kPlatform));
    }

    impl->view = MapViewOfFile(impl->mapping, FILE_MAP_ALL_ACCESS, 0, 0, bytes);
    if (impl->view == nullptr) {
        impl->release();
        delete impl;
        return Status::fail(static_cast<int>(ErrorCode::kPlatform));
    }

    out.impl_ = impl;
    return Status::success();
}

Status SharedMemoryRegion::open(const wchar_t* name, SharedMemoryRegion& out) {
    if (name == nullptr) {
        return Status::fail(static_cast<int>(ErrorCode::kInvalidArgument));
    }
    if (out.valid()) {
        return Status::fail(static_cast<int>(ErrorCode::kAlreadyMapped));
    }

    std::wstring full = L"Local\\";
    full.append(name);

    auto impl = new Impl();
    impl->mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, full.c_str());
    if (impl->mapping == nullptr) {
        delete impl;
        return Status::fail(static_cast<int>(ErrorCode::kNotFound));
    }

    LARGE_INTEGER sz;
    GetFileSizeEx(impl->mapping, &sz);
    impl->size = static_cast<size_t>(sz.QuadPart);

    impl->view = MapViewOfFile(impl->mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (impl->view == nullptr) {
        impl->release();
        delete impl;
        return Status::fail(static_cast<int>(ErrorCode::kPlatform));
    }

    out.impl_ = impl;
    return Status::success();
}

#else // POSIX (Linux/macOS): shm_open + mmap(MAP_SHARED)

namespace detail {

// El backend anterior era mmap(MAP_PRIVATE|MAP_ANONYMOUS) mas un registro
// name->puntero dentro del proceso. Eso NO compartia nada entre procesos: era
// un simulacro para que los tests corrieran, con dos consecuencias malas. Una,
// `open()` devolvia el MISMO puntero que `create()`, asi que cuando el creador
// se destruia el resto quedaba apuntando a memoria liberada (y el registro
// seguia publicando la direccion muerta). Dos, y mas importante: los 20 tests
// validaban una semantica que no era la de produccion, de modo que el ring, el
// bloque de salud y el seqlock nunca se ejercitaban cruzando un limite de
// proceso real.
//
// Ahora se usa el objeto de memoria compartida POSIX de verdad (shm_open +
// ftruncate + mmap MAP_SHARED), que es el equivalente directo del
// CreateFileMappingW/MapViewOfFile de Windows.

/// Convierte el nombre logico (UTF-16, estilo Win32) al nombre de un objeto
/// shm POSIX: "/" + ASCII, sin barras internas y acotado a NAME_MAX.
///
/// Los nombres del proyecto son ASCII ("BA.Audio.Guest.1"), asi que la
/// conversion byte a byte es exacta; cualquier caracter fuera de ASCII o una
/// barra se sustituyen por '_' para que el nombre sea valido.
std::string toShmName(const wchar_t* name) {
    std::string out = "/";
    for (const wchar_t* p = name; *p != L'\0'; ++p) {
        const wchar_t c = *p;
        if (c == L'/' || c == L'\\' || c > 127 || c < 32) {
            out.push_back('_');
        } else {
            out.push_back(static_cast<char>(c));
        }
    }
    // NAME_MAX suele ser 255; se deja margen para el '/' inicial.
    if (out.size() > 200) {
        out.resize(200);
    }
    return out;
}

} // namespace detail

struct SharedMemoryRegion::Impl {
    void* addr = nullptr;
    size_t size = 0;
    std::string shmName;   // nombre del objeto POSIX (para shm_unlink)
    bool ownsName = false; // true solo en el creador: es quien hace unlink

    void* viewPtr() const { return addr; }

    void release() {
        if (addr != nullptr) {
            munmap(addr, size);
            addr = nullptr;
        }
        // Solo el creador retira el nombre del sistema. Los lectores que sigan
        // mapeados conservan su mapeo (POSIX mantiene vivo el objeto mientras
        // haya mapeos), pero ningun open() nuevo lo encontrara.
        if (ownsName && !shmName.empty()) {
            shm_unlink(shmName.c_str());
            ownsName = false;
        }
    }
};

Status SharedMemoryRegion::create(const wchar_t* name, size_t bytes, SharedMemoryRegion& out) {
    if (name == nullptr || bytes == 0) {
        return Status::fail(static_cast<int>(ErrorCode::kInvalidArgument));
    }
    if (out.valid()) {
        return Status::fail(static_cast<int>(ErrorCode::kAlreadyMapped));
    }

    const std::string shmName = detail::toShmName(name);
    // O_EXCL primero para detectar un objeto huerfano de una corrida anterior
    // (un proceso que murio sin hacer unlink): en ese caso se retira y se
    // reintenta, en vez de heredar su contenido y su tamaño.
    int fd = ::shm_open(shmName.c_str(), O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd < 0 && errno == EEXIST) {
        ::shm_unlink(shmName.c_str());
        fd = ::shm_open(shmName.c_str(), O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
    }
    if (fd < 0) {
        return Status::fail(static_cast<int>(ErrorCode::kPlatform));
    }
    if (::ftruncate(fd, static_cast<off_t>(bytes)) != 0) {
        ::close(fd);
        ::shm_unlink(shmName.c_str());
        return Status::fail(static_cast<int>(ErrorCode::kPlatform));
    }
    void* addr = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    // El descriptor no hace falta despues del mmap: el mapeo mantiene vivo al
    // objeto por su cuenta.
    ::close(fd);
    if (addr == MAP_FAILED) {
        ::shm_unlink(shmName.c_str());
        return Status::fail(static_cast<int>(ErrorCode::kOutOfMemory));
    }

    auto impl = new Impl();
    impl->addr = addr;
    impl->size = bytes;
    impl->shmName = shmName;
    impl->ownsName = true;

    out.impl_ = impl;
    return Status::success();
}

Status SharedMemoryRegion::open(const wchar_t* name, SharedMemoryRegion& out) {
    if (name == nullptr) {
        return Status::fail(static_cast<int>(ErrorCode::kInvalidArgument));
    }
    if (out.valid()) {
        return Status::fail(static_cast<int>(ErrorCode::kAlreadyMapped));
    }

    const std::string shmName = detail::toShmName(name);
    const int fd = ::shm_open(shmName.c_str(), O_RDWR, 0);
    if (fd < 0) {
        return Status::fail(static_cast<int>(ErrorCode::kNotFound));
    }
    // El tamaño se toma del objeto, no del caller: el creador es la autoridad.
    struct stat sb {};
    if (::fstat(fd, &sb) != 0 || sb.st_size <= 0) {
        ::close(fd);
        return Status::fail(static_cast<int>(ErrorCode::kPlatform));
    }
    const size_t bytes = static_cast<size_t>(sb.st_size);
    void* addr = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ::close(fd);
    if (addr == MAP_FAILED) {
        return Status::fail(static_cast<int>(ErrorCode::kPlatform));
    }

    auto impl = new Impl();
    impl->addr = addr;
    impl->size = bytes;
    impl->shmName = shmName;
    impl->ownsName = false; // un lector nunca retira el nombre

    out.impl_ = impl;
    return Status::success();
}

#endif // _WIN32

// --- Miembros comunes (independientes de la plataforma) -------------------

SharedMemoryRegion::~SharedMemoryRegion() { close(); }

SharedMemoryRegion::SharedMemoryRegion(SharedMemoryRegion&& other) noexcept : impl_(other.impl_) {
    other.impl_ = nullptr;
}

SharedMemoryRegion& SharedMemoryRegion::operator=(SharedMemoryRegion&& other) noexcept {
    if (this != &other) {
        close();
        impl_ = other.impl_;
        other.impl_ = nullptr;
    }
    return *this;
}

void* SharedMemoryRegion::data() const { return impl_ ? impl_->viewPtr() : nullptr; }

size_t SharedMemoryRegion::size() const { return impl_ ? impl_->size : 0; }

bool SharedMemoryRegion::valid() const { return impl_ != nullptr && impl_->viewPtr() != nullptr; }

void SharedMemoryRegion::close() {
    if (impl_ == nullptr) {
        return;
    }
    impl_->release();
    delete impl_;
    impl_ = nullptr;
}

} // namespace ba::ipc
