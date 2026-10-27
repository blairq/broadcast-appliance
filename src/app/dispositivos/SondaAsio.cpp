// src/app/dispositivos/SondaAsio.cpp — cada driver ASIO instalado como un
// driver propio (ver SondaReal.hpp). Solo Windows.
//
// La lista sale del registro (HKLM\SOFTWARE\ASIO\<nombre>: CLSID y
// Description), sin cargar nada. Probar uno, en el proceso hijo: crearlo por
// COM (los drivers ASIO se registran como servidor COM con su CLSID), init() y
// preguntarle los canales y la frecuencia. Es lo mismo que hace el proveedor
// de ASIO de GStreamer, pero de a uno y fuera de la app.
//
// La interfaz IASIO se declara acá con lo justo (las primeras entradas de la
// tabla virtual, en el orden del SDK de Steinberg): no hace falta el SDK para
// listar y preguntar canales. En 64 bits la convención de llamada es una sola.

#include "dispositivos/SondaReal.hpp"

#include "i18n/I18n.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>

// Los encabezados viejos de MinGW no lo traen (vale lo mismo en todos).
#ifndef RRF_SUBKEY_WOW6464KEY
#define RRF_SUBKEY_WOW6464KEY 0x00010000
#endif

#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <system_error>
#endif

namespace ba::app::dispositivos::sonda {

using json = nlohmann::json;

#if defined(_WIN32)
namespace {

struct IASIO : public IUnknown {
    virtual long init(void* sysHandle) = 0; ///< ASIOBool: 1 si arrancó
    virtual void getDriverName(char* name) = 0;
    virtual long getDriverVersion() = 0;
    virtual void getErrorMessage(char* string) = 0;
    virtual long start() = 0;
    virtual long stop() = 0;
    virtual long getChannels(long* numInputChannels, long* numOutputChannels) = 0;
    virtual long getLatencies(long* inputLatency, long* outputLatency) = 0;
    virtual long getBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity) = 0;
    virtual long canSampleRate(double sampleRate) = 0;
    virtual long getSampleRate(double* sampleRate) = 0;
};

std::string utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? static_cast<std::size_t>(n - 1) : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}
std::wstring ancho(const std::string& s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? static_cast<std::size_t>(n - 1) : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}
/// El texto local de un driver (los mensajes de los drivers vienen en la página de códigos del sistema).
std::string local(const char* s) {
    const int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    std::wstring w(n > 0 ? static_cast<std::size_t>(n - 1) : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_ACP, 0, s, -1, w.data(), n);
    return utf8(w);
}

std::wstring valorTexto(HKEY raiz, const std::wstring& clave, const wchar_t* nombre) {
    wchar_t buf[1024] = {};
    DWORD n = sizeof buf;
    if (RegGetValueW(raiz, clave.c_str(), nombre, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_SUBKEY_WOW6464KEY, nullptr,
                     buf, &n) != ERROR_SUCCESS)
        return {};
    return buf;
}

/// El archivo del driver y su tamaño/fecha: si se actualiza, se lo vuelve a probar.
std::string firmaDe(const std::wstring& clsid) {
    const std::wstring dll = valorTexto(HKEY_CLASSES_ROOT, L"CLSID\\" + clsid + L"\\InprocServer32", nullptr);
    if (dll.empty()) return "sin-archivo";
    std::error_code ec;
    const std::filesystem::path p(dll);
    const auto tam = std::filesystem::file_size(p, ec);
    std::string f = utf8(p.filename().wstring()) + ":" + (ec ? std::string("?") : std::to_string(tam));
    const auto cuando = std::filesystem::last_write_time(p, ec);
    if (!ec) f += ":" + std::to_string(cuando.time_since_epoch().count());
    return f;
}

std::vector<json> listarAsio() {
    std::vector<json> out;
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO", 0, KEY_READ | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS)
        return out;
    for (DWORD i = 0;; ++i) {
        wchar_t nombre[256] = {};
        DWORD n = 256;
        if (RegEnumKeyExW(k, i, nombre, &n, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        const std::wstring sub = std::wstring(L"SOFTWARE\\ASIO\\") + nombre;
        std::wstring clsid = valorTexto(HKEY_LOCAL_MACHINE, sub, L"CLSID");
        if (clsid.empty()) continue;
        for (auto& c : clsid) c = static_cast<wchar_t>(towupper(c));
        const std::wstring desc = valorTexto(HKEY_LOCAL_MACHINE, sub, L"Description");
        out.push_back({{"id", "asio:" + utf8(clsid)},
                       {"nombre", utf8(desc.empty() ? std::wstring(nombre) : desc)},
                       {"tipo", "asio"},
                       {"firma", firmaDe(clsid)},
                       {"rango", 0}});
    }
    RegCloseKey(k);
    return out;
}

json probarAsio(const std::string& id) {
    CLSID clsid{};
    if (FAILED(CLSIDFromString(ancho(id.substr(5)).c_str(), &clsid))) return {{"ausente", true}};
    // ASIO es de un solo hilo (STA), como lo usan los programas de audio.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IASIO* a = nullptr;
    const HRESULT hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, clsid, reinterpret_cast<void**>(&a));
    if (FAILED(hr) || a == nullptr) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "0x%08lX", static_cast<unsigned long>(hr));
        return {{"ok", false},
                {"motivo", ba::i18n::llenar(ba::i18n::traducir("dispositivos", "no se pudo cargar el driver (COM %1)"), {buf})}};
    }
    json r;
    if (a->init(GetDesktopWindow()) != 1) {
        char msg[256] = {};
        a->getErrorMessage(msg);
        r = {{"ok", false}};
        if (msg[0] != '\0') r["motivo"] = local(msg);
    } else {
        char nombre[64] = {};
        a->getDriverName(nombre);
        long entradas = 0, salidas = 0;
        double frecuencia = 0;
        const long c = a->getChannels(&entradas, &salidas);
        a->getSampleRate(&frecuencia);
        if (c != 0) {
            r = {{"ok", false},
                 {"motivo", ba::i18n::llenar(ba::i18n::traducir("dispositivos", "no dice cuántos canales tiene (error %1)"),
                                             {std::to_string(c)})}};
        } else {
            std::string detalle = ba::i18n::llenar(ba::i18n::traducir("dispositivos", "%1 entradas, %2 salidas"),
                                                   {std::to_string(entradas), std::to_string(salidas)});
            if (frecuencia > 0) detalle += ", " + std::to_string(static_cast<int>(frecuencia + 0.5)) + " Hz";
            r = {{"ok", true},
                 {"detalle", detalle},
                 {"dispositivos", json::array({{{"nombre", local(nombre)},
                                                {"entradas", entradas},
                                                {"salidas", salidas},
                                                {"frecuencia", frecuencia}}})}};
        }
    }
    a->Release();
    return r;
}

} // namespace
#endif

Fuente fuenteAsio() {
    Fuente f;
#if defined(_WIN32)
    f.listar = []() { return listarAsio(); };
    f.esMia = [](const std::string& id) { return id.rfind("asio:", 0) == 0; };
    f.probar = [](const std::string& id) { return probarAsio(id); };
#endif
    return f;
}

} // namespace ba::app::dispositivos::sonda
