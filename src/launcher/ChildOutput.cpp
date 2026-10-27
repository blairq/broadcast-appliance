// ChildOutput.cpp
//
// Implementacion de la salida capturada de los hijos (ver ChildOutput.hpp).
// Solo std (filesystem + fstream): compila igual en Windows, Linux y macOS.

#include "ChildOutput.hpp"
#include "i18n/I18n.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <system_error>

namespace ba::launcher {

namespace fs = std::filesystem;

namespace {

/// Pedazos de nombre de flag que delatan un valor que no debe quedar escrito:
/// secretos, y URLs (un servidor RTMP puede llevar la clave pegada).
const char* const kSensitive[] = {"pin", "token", "key", "secret", "pass", "clave", "server", "url"};

bool isSensitive(const std::string& flag) {
    std::string low(flag);
    std::transform(low.begin(), low.end(), low.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const char* word : kSensitive) {
        if (low.find(word) != std::string::npos) {
            return true;
        }
    }
    return false;
}

/// <archivo>.<n>. La generacion 0 es el nombre de paso durante la rotacion.
fs::path rotatedPath(const fs::path& file, int n) {
    fs::path p = file;
    p += "." + std::to_string(n);
    return p;
}

/// Hora local, como Logs/launcher.log (se leen uno al lado del otro).
std::string timestamp() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    return buf;
}

} // namespace

bool rotateChildOutput(const fs::path& file, std::uintmax_t maxBytes, int keepFiles) {
    std::error_code ec;
    const std::uintmax_t size = fs::file_size(file, ec);
    if (ec || size <= maxBytes) {
        return false; // no existe, no se pudo medir o todavia es chico
    }
    if (keepFiles < 1) {
        keepFiles = 1;
    }
    // Primero el activo, a un nombre de paso: si no se deja renombrar no se
    // toca nada. Corriendo antes las generaciones, un archivo trabado las iba
    // tirando de a una en cada relanzamiento (justo en un bucle de crashes).
    const fs::path moving = rotatedPath(file, 0);
    fs::rename(file, moving, ec);
    if (ec) {
        return false;
    }
    // La mas vieja se pierde y el resto se corre un lugar, de atras hacia
    // adelante para no pisar el destino del siguiente. Un hueco en la cadena
    // no es un error.
    fs::remove(rotatedPath(file, keepFiles), ec);
    for (int n = keepFiles - 1; n >= 1; --n) {
        fs::rename(rotatedPath(file, n), rotatedPath(file, n + 1), ec);
    }
    fs::rename(moving, rotatedPath(file, 1), ec);
    return true;
}

std::string redactedArgs(const std::vector<std::string>& args) {
    std::string out;
    bool hideNext = false; // el anterior fue un "--pin" suelto: este es su valor
    for (const std::string& a : args) {
        const bool isFlag = !a.empty() && a[0] == '-';
        std::string shown = a;
        if (isFlag) {
            const std::size_t eq = a.find('=');
            const bool sensitive = isSensitive(a.substr(0, eq));
            hideNext = sensitive && eq == std::string::npos;
            if (sensitive && eq != std::string::npos) {
                shown = a.substr(0, eq + 1) + "***";
            }
        } else if (hideNext) {
            shown = "***";
            hideNext = false;
        }
        if (!out.empty()) {
            out += ' ';
        }
        out += shown;
    }
    return out;
}

bool prepareChildOutput(const fs::path& file, const std::string& name, const fs::path& exe,
                        const std::vector<std::string>& args, std::uintmax_t maxBytes, int keepFiles) {
    if (file.empty()) {
        return false;
    }
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec); // si falla lo dice el open de abajo
    rotateChildOutput(file, maxBytes, keepFiles);

    // Modo texto a proposito: en Windows el hijo escribe "\r\n" y el
    // encabezado queda igual.
    std::ofstream out(file, std::ios::app);
    if (!out) {
        return false;
    }
    const std::u8string exeName = exe.filename().u8string();
    const std::string shownArgs = redactedArgs(args);
    out << ba::i18n::llenar(ba::i18n::traducir("child", "==== %1 | el launcher lanza %2 (%3) | args: %4 ===="),
                            {timestamp(), name, std::string(exeName.begin(), exeName.end()),
                             shownArgs.empty() ? ba::i18n::traducir("child", "(ninguno)") : shownArgs})
        << "\n";
    out.flush();
    return out.good();
}

} // namespace ba::launcher
