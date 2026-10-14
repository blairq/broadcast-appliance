// src/core/platform/Segmento_win.cpp — Segmento con un file mapping en Local\ (ver Segmento.hpp).
#include "platform/Segmento.hpp"

#include <utility>
#include <windows.h>

namespace ba::platform {

namespace {
std::wstring nombreLocal(const std::string& n) { return L"Local\\" + std::wstring(n.begin(), n.end()); }
void poner(std::string* err, const char* que) {
    if (err != nullptr) *err = std::string(que) + " (error " + std::to_string(GetLastError()) + ")";
}
} // namespace

Segmento::~Segmento() { cerrar(); }

Segmento::Segmento(Segmento&& o) noexcept { *this = std::move(o); }

Segmento& Segmento::operator=(Segmento&& o) noexcept {
    if (this != &o) {
        cerrar();
        std::swap(datos_, o.datos_);
        std::swap(tam_, o.tam_);
        std::swap(nombre_, o.nombre_);
        std::swap(dueno_, o.dueno_);
        std::swap(handle_, o.handle_);
    }
    return *this;
}

bool Segmento::crear(const std::string& nombre, size_t bytes, std::string* err) {
    cerrar();
    const unsigned long long t = bytes;
    HANDLE h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, static_cast<DWORD>(t >> 32),
                                  static_cast<DWORD>(t & 0xFFFFFFFFull), nombreLocal(nombre).c_str());
    if (h == nullptr) {
        poner(err, "CreateFileMappingW");
        return false;
    }
    void* p = MapViewOfFile(h, FILE_MAP_WRITE, 0, 0, bytes);
    if (p == nullptr) {
        poner(err, "MapViewOfFile");
        CloseHandle(h);
        return false;
    }
    datos_ = p;
    tam_ = bytes;
    nombre_ = nombre;
    dueno_ = true;
    handle_ = h;
    return true;
}

bool Segmento::abrir(const std::string& nombre, std::string* err) {
    cerrar();
    HANDLE h = OpenFileMappingW(FILE_MAP_READ, FALSE, nombreLocal(nombre).c_str());
    if (h == nullptr) {
        poner(err, "OpenFileMappingW");
        return false;
    }
    void* p = MapViewOfFile(h, FILE_MAP_READ, 0, 0, 0);
    if (p == nullptr) {
        poner(err, "MapViewOfFile");
        CloseHandle(h);
        return false;
    }
    MEMORY_BASIC_INFORMATION mi{};
    VirtualQuery(p, &mi, sizeof mi);
    datos_ = p;
    tam_ = mi.RegionSize;
    nombre_ = nombre;
    dueno_ = false;
    handle_ = h;
    return true;
}

void Segmento::cerrar() {
    if (datos_ != nullptr) UnmapViewOfFile(datos_);
    if (handle_ != nullptr) CloseHandle(static_cast<HANDLE>(handle_));
    datos_ = nullptr;
    tam_ = 0;
    handle_ = nullptr;
    nombre_.clear();
    dueno_ = false;
}

} // namespace ba::platform
