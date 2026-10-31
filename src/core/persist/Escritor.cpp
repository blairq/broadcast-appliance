// src/core/persist/Escritor.cpp — ver el .hpp

#include "persist/Escritor.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <sys/mount.h>
#include <sys/param.h>
#else
#include <sys/vfs.h>
#endif
#endif

namespace ba::persist {

namespace fs = std::filesystem;

namespace {

void anotar(std::string* error, const std::string& que, const fs::path& ruta, const std::string& por) {
    if (error != nullptr) *error = que + " " + ruta.string() + ": " + por;
}

#if defined(_WIN32)
std::string ultimoError() { return std::system_category().message(static_cast<int>(GetLastError())); }
#else
std::string ultimoError() { return std::strerror(errno); }

/// Baja el descriptor al disco: F_FULLFSYNC en macOS si es crítico (si el sistema de
/// archivos no lo soporta, como uno de red, cae al fsync de siempre).
bool bajar(int fd, Durabilidad d) {
#if defined(__APPLE__)
    if (d == Durabilidad::Critica && ::fcntl(fd, F_FULLFSYNC) == 0) return true;
#else
    (void)d;
#endif
    while (::fsync(fd) != 0)
        if (errno != EINTR) return false;
    return true;
}

/// La carpeta también: el rename (y el borrado) son una entrada de la carpeta.
void bajarCarpeta(const fs::path& carpeta, Durabilidad d) {
    const int fd = ::open(carpeta.empty() ? "." : carpeta.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return;
    bajar(fd, d); // algunos sistemas de archivos no dejan: no es un error de la escritura
    ::close(fd);
}
#endif

} // namespace

bool escribir(const fs::path& ruta, std::string_view bytes, Durabilidad d, Permisos p, std::string* error) {
    std::error_code ec;
    const fs::path carpeta = ruta.parent_path();
    if (!carpeta.empty()) fs::create_directories(carpeta, ec);
    fs::path tmp = ruta;
    tmp += ".tmp";

#if defined(_WIN32)
    (void)p;
    (void)d; // en Windows las dos van con FlushFileBuffers + WRITE_THROUGH
    HANDLE h = CreateFileW(tmp.wstring().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        anotar(error, "no se pudo crear", tmp, ultimoError());
        return false;
    }
    std::size_t hecho = 0;
    bool ok = true;
    while (ok && hecho < bytes.size()) {
        const DWORD n = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - hecho, 1u << 30));
        DWORD escritos = 0;
        ok = WriteFile(h, bytes.data() + hecho, n, &escritos, nullptr) != 0 && escritos == n;
        hecho += escritos;
    }
    if (ok) ok = FlushFileBuffers(h) != 0;
    const std::string porQue = ok ? std::string() : ultimoError();
    CloseHandle(h);
    if (!ok) {
        DeleteFileW(tmp.wstring().c_str());
        anotar(error, "no se pudo escribir", tmp, porQue);
        return false;
    }
    if (MoveFileExW(tmp.wstring().c_str(), ruta.wstring().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
        anotar(error, "no se pudo reemplazar", ruta, ultimoError());
        DeleteFileW(tmp.wstring().c_str());
        return false;
    }
    return true;
#else
    const mode_t modo = p == Permisos::Privados ? (S_IRUSR | S_IWUSR) : 0644;
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, modo);
    if (fd < 0) {
        anotar(error, "no se pudo crear", tmp, ultimoError());
        return false;
    }
    // Un temporal que quedó de antes (de un corte) conserva sus permisos: se fijan.
    if (p == Permisos::Privados) ::fchmod(fd, S_IRUSR | S_IWUSR);
    std::size_t hecho = 0;
    bool ok = true;
    while (hecho < bytes.size()) {
        const ssize_t n = ::write(fd, bytes.data() + hecho, bytes.size() - hecho);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            ok = false;
            break;
        }
        hecho += static_cast<std::size_t>(n);
    }
    if (ok) ok = bajar(fd, d);
    const std::string porQue = ok ? std::string() : ultimoError();
    if (::close(fd) != 0 && ok) ok = false;
    if (!ok) {
        ::unlink(tmp.c_str());
        anotar(error, "no se pudo escribir", tmp, porQue);
        return false;
    }
    if (::rename(tmp.c_str(), ruta.c_str()) != 0) {
        anotar(error, "no se pudo reemplazar", ruta, ultimoError());
        ::unlink(tmp.c_str());
        return false;
    }
    bajarCarpeta(carpeta, d);
    return true;
#endif
}

bool borrar(const fs::path& ruta) {
    std::error_code ec;
    const bool habia = fs::remove(ruta, ec);
    if (ec) return false;
#if !defined(_WIN32)
    if (habia) bajarCarpeta(ruta.parent_path(), Durabilidad::Normal);
#else
    (void)habia;
#endif
    return true;
}

bool leerArchivo(const fs::path& ruta, std::string& out) {
    std::ifstream in(ruta, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

std::string sistemaDeArchivos(const fs::path& ruta) {
    // La carpeta más cercana que exista (la de datos puede no tener todavía Runtime/).
    std::error_code ec;
    fs::path p = ruta;
    while (!p.empty() && !fs::exists(p, ec) && p != p.parent_path()) p = p.parent_path();
    if (p.empty()) return {};
#if defined(_WIN32)
    wchar_t raiz[MAX_PATH] = {};
    if (!GetVolumePathNameW(fs::absolute(p, ec).wstring().c_str(), raiz, MAX_PATH)) return {};
    wchar_t nombre[MAX_PATH + 1] = {};
    if (!GetVolumeInformationW(raiz, nullptr, 0, nullptr, nullptr, nullptr, nombre, MAX_PATH + 1)) return {};
    return fs::path(nombre).string();
#elif defined(__APPLE__)
    struct statfs s {};
    if (::statfs(p.c_str(), &s) != 0) return {};
    return s.f_fstypename;
#else
    struct statfs s {};
    if (::statfs(p.c_str(), &s) != 0) return {};
    switch (static_cast<unsigned long>(s.f_type)) {
        case 0xEF53UL: return "ext4";
        case 0x9123683EUL: return "btrfs";
        case 0x58465342UL: return "xfs";
        case 0x01021994UL: return "tmpfs";
        case 0x5346544EUL: return "ntfs";
        case 0x4D44UL: return "vfat";
        case 0x2011BAB0UL: return "exfat";
        case 0x65735546UL: return "fuseblk"; // ntfs-3g, exfat-fuse…: no se sabe cuál
        default: return {};
    }
#endif
}

bool sinDiario(const fs::path& ruta) {
    std::string n = sistemaDeArchivos(ruta);
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return n == "msdos" || n == "vfat" || n == "fat" || n == "fat32" || n == "exfat";
}

} // namespace ba::persist
