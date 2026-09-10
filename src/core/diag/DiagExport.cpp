// DiagExport.cpp
//
// Implementacion del exportador de diagnostico (spec §11.4). Dos ideas de
// diseno:
//
//   1) Bundle autocontenido en un solo archivo: un header JSON (nlohmann
//      ordered_json, determinista) con metadatos (host, timestamp, counts) y
//      despues el contenido de cada log en secciones delimitadas por
//      "=== <nombre> ===". Un solo archivo facilita el envio (correo, ticket)
//      y evita depender de una lib de zip; el orden es del mas nuevo al mas
//      antiguo para que lo relevante este arriba.
//
//   2) No-fallible por contrato: exportBundle() envuelve el trabajo de disco en
//      un try/catch y devuelve ExportResult{ok=false} ante cualquier fallo, en
//      vez de lanzar. El export es una accion de ingenieria, no critica para el
//      flujo de emision: no debe tumbar al launcher.

#include "DiagExport.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

#if defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#else
#    include <unistd.h> // gethostname
#endif

namespace ba::diag {

namespace {

// Nombre de cabecera del bundle (permite a un futuro lector detectar el
// formato y su version).
constexpr const char* kBundleHeader = "#BA-DIAG v1";

// Prefix de nombre de los archivos de log que se empaquetan (coincide con el
// de LogRotator: ba.log y ba.log.N).
constexpr const char* kLogPrefix = "ba.log";

// Nombre del host (para el metadato "host" del bundle). En Windows usa
// GetComputerNameW; en el resto, gethostname(). Best-effort: si falla, "unknown".
std::string hostName() {
#if defined(_WIN32)
    wchar_t buf[256]{};
    DWORD n = sizeof(buf) / sizeof(buf[0]);
    if (GetComputerNameW(buf, &n)) {
        // UTF-16 -> UTF-8 (best-effort; los nombres de host suelen ser ASCII).
        int need = WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(n),
                                       nullptr, 0, nullptr, nullptr);
        std::string out;
        if (need > 0) {
            out.resize(static_cast<std::size_t>(need));
            WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(n),
                                out.data(), need, nullptr, nullptr);
        }
        if (!out.empty()) {
            return out;
        }
    }
    return "unknown";
#else
    char buf[256]{};
    if (::gethostname(buf, sizeof(buf) - 1) == 0) {
        return std::string(buf);
    }
    return "unknown";
#endif
}

// Timestamp UTC ISO-8601 con milisegundos (mismo formato que LogRotator), para
// correlacionar el bundle con las lineas de log.
std::string timestampUtc() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto tt = system_clock::to_time_t(now);
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;

    std::tm tm{};
#if defined(_WIN32)
    // MinGW sigue la firma de MSVC: gmtime_s(tm*, const time_t*) — tm primero.
    // (La firma GNU/Linux gmtime_s(const time_t*, tm*) no existe en MinGW.)
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                  tm.tm_min, tm.tm_sec, static_cast<int>(ms));
    return std::string(buf);
}

// Lee un archivo completo a memoria; devuelve false si no existe / no se lee.
bool readFileAll(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

// Reune las rutas de los logs presentes en `logDir` (ba.log y ba.log.N),
// ordenadas de mas nuevo a mas antiguo (activo primero, luego .1, .2, ...).
// No lanza: si no se puede listar, devuelve vacio.
std::vector<std::string> collectLogFiles(const std::string& logDir) {
    std::vector<std::string> out;
    std::error_code ec;
    if (!std::filesystem::is_directory(logDir, ec) || ec) {
        return out;
    }

    std::vector<std::pair<int, std::string>> found; // (indice, ruta); -1 = activo
    for (const auto& entry : std::filesystem::directory_iterator(logDir, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (name == kLogPrefix) {
            found.emplace_back(-1, entry.path().string());
        } else if (name.rfind(kLogPrefix, 0) == 0 && name.size() > std::char_traits<char>::length(kLogPrefix)) {
            const std::string suffix = name.substr(std::char_traits<char>::length(kLogPrefix));
            if (suffix.front() == '.') {
                // "ba.log.<n>" -> n
                const std::string digits = suffix.substr(1);
                bool allDigits = !digits.empty();
                for (char c : digits) {
                    if (c < '0' || c > '9') {
                        allDigits = false;
                        break;
                    }
                }
                if (allDigits) {
                    found.emplace_back(std::stoi(digits), entry.path().string());
                }
            }
        }
    }

    // Orden: activo (-1) primero, luego por indice ascendente (.1, .2, ...).
    std::sort(found.begin(), found.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [idx, path] : found) {
        out.push_back(path);
    }
    return out;
}

} // namespace

// --- exportBundle -----------------------------------------------------------

ExportResult DiagExport::exportBundle(const std::string& logDir,
                                      const std::string& outPath) {
    ExportResult res{false, outPath};

    try {
        // Crea el directorio padre de outPath si no existe.
        std::error_code ec;
        const std::filesystem::path parent =
            std::filesystem::path(outPath).parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, ec);
            if (ec) {
                return res; // no se pudo crear el destino
            }
        }

        const std::vector<std::string> logs = collectLogFiles(logDir);

        // Metadatos (nlohmann ordered_json: determinista, orden de insercion).
        std::size_t totalBytes = 0;
        for (const auto& p : logs) {
            std::error_code ec2;
            const auto sz = std::filesystem::file_size(p, ec2);
            if (!ec2) {
                totalBytes += sz;
            }
        }

        nlohmann::ordered_json meta = nlohmann::ordered_json::object();
        meta["host"] = hostName();
        meta["timestamp"] = timestampUtc();
        meta["logFiles"] = static_cast<std::uint64_t>(logs.size());
        meta["totalBytes"] = static_cast<std::uint64_t>(totalBytes);

        // Escribe el bundle: header + metadatos + secciones de logs.
        std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            return res; // no se pudo abrir el destino
        }

        auto writeAll = [&out](const std::string& s) {
            out.write(s.data(), static_cast<std::streamsize>(s.size()));
        };

        writeAll(std::string(kBundleHeader) + "\n");
        writeAll(meta.dump() + "\n");

        for (const auto& p : logs) {
            const std::string name = std::filesystem::path(p).filename().string();
            writeAll("\n=== " + name + " ===\n");
            std::string content;
            if (readFileAll(p, content)) {
                writeAll(content);
                // Garantiza que la seccion termine en newline aunque el log no.
                if (content.empty() || content.back() != '\n') {
                    writeAll("\n");
                }
            }
        }

        out.flush();
        if (!out) {
            return res; // fallo al escribir
        }
        res.ok = true;
        return res;
    } catch (...) {
        // No-fallible: cualquier excepcion de disco/json se traga.
        return res;
    }
}

// --- writeMinidump (stub, GATED:Win) ----------------------------------------

#if defined(_WIN32)
// STUB (GATED:Win): la implementacion real usaria MiniDumpWriteDump de
// dbghelp.dll. Se cargaria la lib a runtime (LoadLibraryW(L"dbghelp.dll") +
// GetProcAddress "MiniDumpWriteDump") para no enlazar estaticamente, y se
// llamaria con MiniDumpType = MiniDumpWithHeapSnapshot | MiniDumpWithThreadInfo
// | MiniDumpWithStackWalk, escribiendo en un HANDLE abierto a `path`
// (CreateFileW). El proceso se obtiene con GetCurrentProcess(). Por ahora no
// esta cableado: devuelve false (el dump se genera desde el filtro de
// excepciones / el launcher, que lo implementan en su componente).
bool DiagExport::writeMinidump(const std::string& /*path*/) {
    return false;
}
#else
// No-Windows: no hay minidump nativo (MiniDumpWriteDump es de Windows).
bool DiagExport::writeMinidump(const std::string& /*path*/) {
    return false;
}
#endif

} // namespace ba::diag
