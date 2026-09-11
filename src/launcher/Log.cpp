// Log.cpp
//
// Implementacion del logger estructurado del launcher (ver Log.hpp).
// Escribe a un archivo (Logs/launcher.log) y espeja a stderr. Thread-safe
// mediante una unica std::mutex; nunca lanza.

#include "Log.hpp"

#include <chrono>
#include <ctime>
#include <iostream>

namespace ba::launcher {

namespace {
std::mutex g_mutex;
std::FILE* g_file = nullptr;

const char* levelTag(Log::Level level) {
    switch (level) {
        case Log::Level::Info: return "INFO";
        case Log::Level::Warn: return "WARN";
        case Log::Level::Error: return "ERROR";
    }
    return "?";
}

std::string timestamp() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const std::time_t t = system_clock::to_time_t(now);
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    char full[48];
    std::snprintf(full, sizeof(full), "%s.%03d", buf, static_cast<int>(ms.count()));
    return full;
}
} // namespace

void Log::init(const std::string& path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) {
        std::fclose(g_file);
        g_file = nullptr;
    }
#if defined(_WIN32)
    // En Windows el launcher es GUI (sin consola); el archivo es el canal
    // primario. std::ofstream con ruta UTF-8 no siempre funciona, asi que se
    // usa fopen con la ruta ANSI (suficiente para Logs/launcher.log relativo).
    g_file = std::fopen(path.c_str(), "a");
#else
    g_file = std::fopen(path.c_str(), "a");
#endif
    if (!g_file) {
        std::fprintf(stderr, "[log] no se pudo abrir %s; solo stderr\n", path.c_str());
    }
}

void Log::close() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) {
        std::fclose(g_file);
        g_file = nullptr;
    }
}

void Log::write(Level level, const char* component, int code, const std::string& context) {
    const std::string line = timestamp() + " " + std::string(levelTag(level)) + " | " + component +
                             " | code=" + std::to_string(code) + " | " + context;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) {
        std::fputs(line.c_str(), g_file);
        std::fputc('\n', g_file);
        std::fflush(g_file);
    }
    // Espejo a stderr para desarrollo / debugging.
    std::fprintf(stderr, "%s\n", line.c_str());
}

} // namespace ba::launcher
