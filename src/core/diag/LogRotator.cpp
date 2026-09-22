// LogRotator.cpp
//
// Implementacion del rotador de logs por proceso (spec §11.4). Dos ideas de
// diseno:
//
//   1) Rotacion por desplazamiento con tope: antes de escribir, si el archivo
//      activo ya tiene maxBytes o mas (y no esta vacio), se desplaza la cadena
//      de rotados hacia indices mayores (ba.log.(n-1) -> ba.log.n) y se borra
//      el que cae fuera del tope (ba.log.keepFiles). El activo se renombra a
//      ba.log.1. Es O(keepFiles) por rotacion, que es pequeno (tipicamente 3-5),
//      y no hay que reescribir el contenido: solo renames y un remove.
//
//   2) No-fallible por contrato: cada llamada a write() envuelve el trabajo de
//      disco en un try/catch (std::filesystem y fstream pueden lanzar) y, ante
//      cualquier fallo, descarta la linea en vez de propagar. El diagnostico es
//      una via secundaria: nunca debe tumbar al proceso que lo usa.

#include "LogRotator.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace ba::diag {

namespace {

// Nombre fijo del archivo activo (spec §11.4: "Logs rotativos por proceso").
constexpr const char* kActiveName = "ba.log";


// Formatea un timestamp UTC con milisegundos: AAAA-MM-DDTHH:MM:SS.mmmZ.
// Usa system_clock (wall clock) porque el objetivo es correlacionar logs de
// varios procesos por hora real, no medir duraciones.
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

} // namespace

// --- constructor ------------------------------------------------------------

LogRotator::LogRotator(std::string dir, std::size_t maxBytes, std::size_t keepFiles,
                       std::string nombre, bool conSelloDeTiempo)
    : dir_(std::move(dir)), maxBytes_(maxBytes > 0 ? maxBytes : 1),
      keepFiles_(keepFiles > 0 ? keepFiles : 1),
      nombre_(nombre.empty() ? kActiveName : std::move(nombre)), conSello_(conSelloDeTiempo) {
    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);
    if (ec) {
        // Directorio no creable: el rotador queda inactivo (write() no-op).
        active_ = false;
        return;
    }
    activePath_ = (std::filesystem::path(dir_) / nombre_).string();
    active_ = true;
}

// --- helpers ----------------------------------------------------------------

std::size_t LogRotator::activeSize() const {
    std::error_code ec;
    const auto sz = std::filesystem::file_size(activePath_, ec);
    if (ec) {
        return 0; // no existe o no se pudo medir
    }
    return sz;
}

std::string LogRotator::rotatedPath(std::size_t n) const {
    // Los rotados son el nombre del activo mas el indice: app.log.1, app.log.2.
    // Asi dos procesos que comparten Logs/ no se pisan las rotaciones.
    return (std::filesystem::path(dir_) / (nombre_ + "." + std::to_string(n))).string();
}

// --- rotate -----------------------------------------------------------------

void LogRotator::rotate() {
    std::error_code ec;

    // Borra el mas antiguo si va a caer fuera del tope (keepFiles).
    const std::string oldest = rotatedPath(keepFiles_);
    std::filesystem::remove(oldest, ec); // ec se ignora: si no existe, nada.

    // Desplaza la cadena hacia indices mayores, de atras hacia adelante, para
    // no pisar el destino del siguiente. Solo se mueve lo que existe.
    for (std::size_t n = keepFiles_ - 1; n >= 1; --n) {
        const std::string src = rotatedPath(n);
        const std::string dst = rotatedPath(n + 1);
        std::filesystem::rename(src, dst, ec);
        if (ec) {
            // Si no existe `src` (hueco en la cadena) se ignora; si fallo por
            // otro motivo, se deja y se sigue: la proxima rotacion reintenta.
            ec.clear();
        }
    }

    // El activo pasa a ser el rotado mas nuevo (.1).
    std::filesystem::rename(activePath_, rotatedPath(1), ec);
    // Si fallo, el activo sigue ahi; la proxima write reintenta la rotacion.
}

// --- write ------------------------------------------------------------------

void LogRotator::write(const std::string& line) {
    if (!active_) {
        return; // directorio no creable: no-op
    }

    // Todo el ciclo "mirar tamaño -> rotar -> escribir" va bajo el lock: son
    // tres pasos que dependen entre si, y dos hilos intercalandolos se pisan
    // los renombres.
    std::lock_guard<std::mutex> lock(m_);
    try {
        // Rota si el activo ya llego al umbral (y no esta vacio).
        const std::size_t cur = activeSize();
        if (cur > 0 && cur >= maxBytes_) {
            rotate();
        }

        std::ofstream out(activePath_, std::ios::app | std::ios::binary);
        if (!out) {
            return; // no se pudo abrir: se descarta la linea
        }
        const std::string stamped =
            conSello_ ? "[" + timestampUtc() + "] " + line + "\n" : line + "\n";
        out.write(stamped.data(), static_cast<std::streamsize>(stamped.size()));
        // El destructor de `out` cierra y flusha; no se fuerza fsync (control,
        // no RT): un corte de energia puede perder las ultimas lineas, aceptable.
    } catch (...) {
        // No-fallible: cualquier excepcion de disco se traga.
    }
}

// --- files ------------------------------------------------------------------

std::vector<std::string> LogRotator::files() const {
    std::lock_guard<std::mutex> lock(m_);
    std::vector<std::string> out;
    if (!active_) {
        return out;
    }

    std::error_code ec;
    if (!std::filesystem::is_directory(dir_, ec) || ec) {
        return out;
    }

    // Reune los rotados presentes (ba.log.1 .. ba.log.keepFiles) mas el activo.
    std::vector<std::pair<std::size_t, std::string>> rotated;
    for (std::size_t n = 1; n <= keepFiles_; ++n) {
        const std::string p = rotatedPath(n);
        std::error_code ec2;
        if (std::filesystem::is_regular_file(p, ec2) && !ec2) {
            rotated.emplace_back(n, p);
        }
    }

    // Orden de mas antiguo a mas nuevo: el rotado de MAYOR indice es el mas
    // antiguo; el activo (ba.log) es el mas nuevo y va al final.
    std::sort(rotated.begin(), rotated.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (const auto& [n, p] : rotated) {
        out.push_back(p);
    }
    std::error_code ec3;
    if (std::filesystem::is_regular_file(activePath_, ec3) && !ec3) {
        out.push_back(activePath_);
    }
    return out;
}

} // namespace ba::diag
