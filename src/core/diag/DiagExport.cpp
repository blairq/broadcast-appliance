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
#define WIN32_LEAN_AND_MEAN
// `windows.h` ANTES que `dbghelp.h`: el segundo arrastra `_dbg_LOAD_IMAGE.h`
// y `_dbg_common.h`, que usan tipos base de Windows (PSTR, HANDLE, ULONG,
// CALLBACK) solo definidos una vez procesado `windows.h`. El orden inverso
// rompe el cross-compile MinGW.
// clang-format off: el ORDEN de estos dos includes es obligatorio (ver
// comentario anterior); clang-format querría reordenarlos alfabéticamente
// (dbghelp.h antes que windows.h) y eso rompería la compilación.
#include <windows.h>
#include <dbghelp.h> // MiniDumpWriteDump (se resuelve a runtime, no se enlaza)
// clang-format on
#else
#include <unistd.h> // gethostname
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
        int need = WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(n), nullptr, 0, nullptr, nullptr);
        std::string out;
        if (need > 0) {
            out.resize(static_cast<std::size_t>(need));
            WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(n), out.data(), need, nullptr, nullptr);
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
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900, tm.tm_mon + 1,
                  tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms));
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
        } else if (name.rfind(kLogPrefix, 0) == 0 &&
                   name.size() > std::char_traits<char>::length(kLogPrefix)) {
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
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [idx, path] : found) {
        out.push_back(path);
    }
    return out;
}

} // namespace

// --- exportBundle -----------------------------------------------------------

ExportResult DiagExport::exportBundle(const std::string& logDir, const std::string& outPath) {
    ExportResult res{false, outPath};

    try {
        // Crea el directorio padre de outPath si no existe.
        std::error_code ec;
        const std::filesystem::path parent = std::filesystem::path(outPath).parent_path();
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

// --- writeMinidump (GATED:Win) ----------------------------------------------

#if defined(_WIN32)
namespace {

// dbghelp.h se incluye solo para los tipos (MINIDUMP_TYPE, PMINIDUMP_*) y la
// firma de MiniDumpWriteDump; el simbolo se resuelve a runtime con
// GetProcAddress y NO se enlaza estaticamente.
//
// MinGW trae un enum MINIDUMP_TYPE incompleto: solo define MiniDumpNormal y
// MiniDumpWithThreadInfo de los tres que usamos. MiniDumpWithHeapSnapshot y
// MiniDumpWithStackWalk no existen en su dbghelp.h, asi que se los declara
// localmente con #ifndef para que el codigo compile tanto contra el header de
// MinGW como contra uno completo (MSVC/WDK).
#ifndef MiniDumpWithHeapSnapshot
constexpr DWORD MiniDumpWithHeapSnapshot = 0x00040000;
#endif
#ifndef MiniDumpWithStackWalk
constexpr DWORD MiniDumpWithStackWalk = 0x04000000;
#endif

// MiniDumpWriteDump de dbghelp.dll (se resuelve a runtime).
typedef BOOL(WINAPI* MiniDumpWriteDumpFn)(HANDLE hProcess, DWORD ProcessId, HANDLE hFile,
                                          MINIDUMP_TYPE DumpType,
                                          CONST PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
                                          CONST PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
                                          CONST PMINIDUMP_CALLBACK_INFORMATION CallbackParam);

} // namespace

// Escribe un minidump del proceso actual en `path` (spec §11.4).
//
// Proposito:
//   Captura el estado del proceso (heap, hilos y stack) como un .dmp
//   autocontenido para post-mortem. dbghelp.dll se carga a runtime
//   (LoadLibraryW + GetProcAddress) y no se enlaza estaticamente: asi el
//   binario no depende de dbghelp en el import table y sigue arrancando en
//   sistemas donde no este presente (devuelve false en vez de fallar al
//   resolver el simbolo al cargar).
//
// Threading:
//   Camino de CONTROL, no de RT: MiniDumpWriteDump recorre la memoria del
//   proceso y escribe un archivo, por lo que puede bloquear; debe llamarse
//   desde un hilo de control (el filtro de excepciones o el launcher), nunca
//   desde el bucle de audio.
//
// Errores:
//   No-fallible por contrato: NUNCA lanza. Devuelve false ante CUALQUIER fallo
//   (dbghelp.dll ausente, simbolo no resuelto, archivo no abre, o
//   MiniDumpWriteDump devuelve 0) y true solo si se escribio el dump.
bool DiagExport::writeMinidump(const std::string& path) {
    // dbghelp.dll: cargar a runtime; si no esta, no hay minidump.
    HMODULE dbghelp = ::LoadLibraryW(L"dbghelp.dll");
    if (!dbghelp) {
        return false;
    }

    // Doble cast via `void*`: GetProcAddress devuelve FARPROC (funcion), y
    // castear directamente funcion->funcion dispara `-Wcast-function-type`.
    // (Mismo patron que Vst3Host.cpp.)
    auto fn = reinterpret_cast<MiniDumpWriteDumpFn>(
        reinterpret_cast<void*>(::GetProcAddress(dbghelp, "MiniDumpWriteDump")));
    if (!fn) {
        ::FreeLibrary(dbghelp);
        return false;
    }

    // Convertir la ruta UTF-8 a UTF-16 para CreateFileW.
    const int need =
        ::MultiByteToWideChar(CP_UTF8, 0, path.c_str(), static_cast<int>(path.size()), nullptr, 0);
    std::wstring wpath(static_cast<std::size_t>(need), L'\0');
    if (need > 0) {
        ::MultiByteToWideChar(CP_UTF8, 0, path.c_str(), static_cast<int>(path.size()), wpath.data(), need);
    }

    // Abrir el destino (crea o sobreescribe). Si no se abre, no hay dump.
    HANDLE hFile = ::CreateFileW(wpath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        ::FreeLibrary(dbghelp);
        return false;
    }

    const BOOL ok = fn(
        ::GetCurrentProcess(), ::GetCurrentProcessId(), hFile,
        static_cast<MINIDUMP_TYPE>(MiniDumpWithHeapSnapshot | MiniDumpWithThreadInfo | MiniDumpWithStackWalk),
        nullptr, nullptr, nullptr);
    ::CloseHandle(hFile);
    ::FreeLibrary(dbghelp);
    return ok != 0;
}
#else
// No-Windows: no hay minidump nativo (MiniDumpWriteDump es de Windows).
bool DiagExport::writeMinidump(const std::string& /*path*/) { return false; }
#endif

} // namespace ba::diag
