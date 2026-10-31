// src/core/platform/Hilo.cpp — ver Hilo.hpp
//
// Un solo archivo para los tres sistemas (y no Hilo_win/Hilo_posix): lo compilan
// adentro también los plugins de GStreamer, que no enlazan ba_core.

#include "platform/Hilo.hpp"

#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace ba::platform {

namespace {

/// `s` cortado a `max` bytes sin partir un carácter UTF-8.
std::string cortar(std::string_view s, std::size_t max) {
    if (s.size() <= max) return std::string(s);
    std::size_t n = max;
    while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n; // s[n] es la continuación de uno partido
    return std::string(s.substr(0, n));
}

#if defined(_WIN32)
std::wstring ancho(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

template <typename F>
F deKernel32(const char* funcion) {
    return reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), funcion)));
}
#endif

} // namespace

void nombrarHilo(std::string_view nombre) {
#if defined(_WIN32)
    using Poner = HRESULT(WINAPI*)(HANDLE, PCWSTR);
    static const auto poner = deKernel32<Poner>("SetThreadDescription");
    if (poner != nullptr) poner(GetCurrentThread(), ancho(cortar(nombre, 255)).c_str());
#elif defined(__APPLE__)
    pthread_setname_np(cortar(nombre, 63).c_str());
#else
    pthread_setname_np(pthread_self(), cortar(nombre, 15).c_str());
#endif
}

std::string nombreDelHilo() {
#if defined(_WIN32)
    using Leer = HRESULT(WINAPI*)(HANDLE, PWSTR*);
    static const auto leer = deKernel32<Leer>("GetThreadDescription");
    PWSTR w = nullptr;
    if (leer == nullptr || FAILED(leer(GetCurrentThread(), &w)) || w == nullptr) return {};
    std::string out;
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n > 1) {
        out.resize(static_cast<std::size_t>(n - 1));
        WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n, nullptr, nullptr);
    }
    LocalFree(w);
    return out;
#else
    char buf[64] = {0};
    if (pthread_getname_np(pthread_self(), buf, sizeof buf) != 0) return {};
    return buf;
#endif
}

} // namespace ba::platform
