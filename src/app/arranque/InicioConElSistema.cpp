// ============================================================================
// src/app/arranque/InicioConElSistema.cpp — ver InicioConElSistema.hpp
// ============================================================================

#include "arranque/InicioConElSistema.hpp"

#include "i18n/I18n.hpp"
#include "platform/Paths.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace ba::app::inicio {

namespace fs = std::filesystem;

namespace {

/// Lo que hay que abrir: la .app en macOS; el lanzador en Windows y Linux.
fs::path queAbrir() {
    const fs::path raiz = ba::platform::bundleRoot();
#if defined(__APPLE__)
    // <X>.app/Contents/Resources
    return raiz.parent_path().parent_path();
#elif defined(_WIN32)
    const fs::path l = raiz / "BroadcastAppliance.exe";
    return fs::exists(l) ? l : ba::platform::executablePath();
#else
    const fs::path l = raiz / "BroadcastAppliance";
    return fs::exists(l) ? l : ba::platform::executablePath();
#endif
}

#if defined(__APPLE__) || (!defined(_WIN32))
fs::path casa() {
    const char* h = std::getenv("HOME");
    return h != nullptr ? fs::path(h) : fs::path();
}
#endif

#if defined(__APPLE__)
fs::path archivo() { return casa() / "Library" / "LaunchAgents" / "ar.agora.broadcast-appliance.plist"; }
std::string contenido(const fs::path& app) {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
           "<plist version=\"1.0\"><dict>\n"
           "  <key>Label</key><string>ar.agora.broadcast-appliance</string>\n"
           "  <key>ProgramArguments</key><array><string>/usr/bin/open</string><string>" + app.string() + "</string></array>\n"
           "  <key>RunAtLoad</key><true/>\n"
           "</dict></plist>\n";
}
#elif !defined(_WIN32)
fs::path archivo() {
    const char* x = std::getenv("XDG_CONFIG_HOME");
    const fs::path base = x != nullptr && *x != '\0' ? fs::path(x) : casa() / ".config";
    return base / "autostart" / "broadcast-appliance.desktop";
}
std::string contenido(const fs::path& exe) {
    return "[Desktop Entry]\nType=Application\nName=Broadcast Appliance\nExec=\"" + exe.string() +
           "\"\nX-GNOME-Autostart-enabled=true\n";
}
#endif

#if defined(_WIN32)
constexpr const wchar_t* kClave = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* kValor = L"BroadcastAppliance";
std::wstring valorEsperado() { return L"\"" + queAbrir().wstring() + L"\""; }
std::wstring leerValor() {
    wchar_t buf[2048];
    DWORD n = sizeof buf;
    if (RegGetValueW(HKEY_CURRENT_USER, kClave, kValor, RRF_RT_REG_SZ, nullptr, buf, &n) != ERROR_SUCCESS) return {};
    return buf;
}
#else
std::string leerArchivo() {
    std::ifstream in(archivo());
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
#endif

/// ¿Hay una entrada (apunte a donde apunte)?
bool hayEntrada() {
#if defined(_WIN32)
    return !leerValor().empty();
#else
    std::error_code ec;
    return fs::exists(archivo(), ec);
#endif
}

} // namespace

bool activo() {
#if defined(_WIN32)
    return leerValor() == valorEsperado();
#else
    return hayEntrada() && leerArchivo() == contenido(queAbrir());
#endif
}

std::string poner(bool on) {
#if defined(_WIN32)
    if (!on) {
        const LSTATUS r = RegDeleteKeyValueW(HKEY_CURRENT_USER, kClave, kValor);
        return r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND ? std::string() : ba::i18n::traducir("inicio", "no se pudo sacar del inicio de Windows");
    }
    const std::wstring v = valorEsperado();
    if (RegSetKeyValueW(HKEY_CURRENT_USER, kClave, kValor, REG_SZ, v.c_str(),
                        static_cast<DWORD>((v.size() + 1) * sizeof(wchar_t))) != ERROR_SUCCESS)
        return ba::i18n::traducir("inicio", "no se pudo poner en el inicio de Windows");
    return {};
#else
    std::error_code ec;
    if (!on) {
        fs::remove(archivo(), ec);
        return ec ? ba::i18n::llenar(ba::i18n::traducir("inicio", "no se pudo borrar %1"), {archivo().string()}) : std::string();
    }
    fs::create_directories(archivo().parent_path(), ec);
    std::ofstream out(archivo(), std::ios::trunc);
    out << contenido(queAbrir());
    return out ? std::string() : ba::i18n::llenar(ba::i18n::traducir("inicio", "no se pudo escribir %1"), {archivo().string()});
#endif
}

void revisar() {
    if (hayEntrada() && !activo()) poner(true); // se movió la carpeta: la ruta nueva
}

nlohmann::json estado() {
    nlohmann::json notas = nlohmann::json::array();
#if defined(__APPLE__)
    notas.push_back(ba::i18n::traducir("inicio", "Que la Mac se encienda al volver la luz: Ajustes del Sistema → Energía → «Arrancar "
                                                 "automáticamente después de un corte de luz» (o, como administrador, `sudo pmset autorestart 1`)."));
    notas.push_back(ba::i18n::traducir("inicio", "Que la sesión se inicie sola: Ajustes del Sistema → Usuarios y grupos → «Iniciar sesión "
                                                 "automáticamente» (no anda con FileVault prendido)."));
#elif defined(_WIN32)
    notas.push_back(ba::i18n::traducir("inicio", "Que la PC se encienda al volver la luz: en la BIOS/UEFI, «Restore on AC power loss» = Power On."));
    notas.push_back(ba::i18n::traducir("inicio", "Que la sesión se inicie sola: inicio de sesión automático de Windows (netplwiz o Autologon)."));
#else
    notas.push_back(ba::i18n::traducir("inicio", "Que la PC se encienda al volver la luz: en la BIOS/UEFI, «Restore on AC power loss» = Power On."));
    notas.push_back(ba::i18n::traducir("inicio", "Que la sesión se inicie sola: inicio de sesión automático del escritorio (GDM/LightDM/SDDM)."));
#endif
    notas.push_back(ba::i18n::traducir("inicio", "Al volver, el programa retoma solo la transmisión y la grabación que estaban andando."));
    return {{"activo", activo()}, {"abre", queAbrir().string()}, {"notas", notas}};
}

} // namespace ba::app::inicio
