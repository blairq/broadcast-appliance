// Integrity.cpp
//
// Implementacion de la verificacion de integridad (ver Integrity.hpp).
// Parsea manifest.json de forma minima (sin dependencias: un parser JSON
// ultra-reducido para el formato conocido { "files": [ { "path", "sha256" } ] })
// y hashea en paralelo con Sha256.

#include "Integrity.hpp"

#include "Sha256.hpp"

#include <algorithm>
#include <chrono>
#include <execution>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>

namespace ba::launcher {

namespace {

// --- Parser JSON minimo ------------------------------------------------------
// El manifest tiene un formato fijo y conocido (lo genera tools/deploy). No se
// enlaza una libreria JSON: se extraen pares "path" y "sha256" con un escaneo
// simple. Suficiente y determinista para el layout de 5.1.

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Extrae el valor de una clave "key" que precede a un string, a partir de pos.
// Devuelve el string y avanza pos al final del valor.
bool extractString(const std::string& json, std::size_t pos, const std::string& key, std::string& out) {
    const std::string needle = "\"" + key + "\"";
    const std::size_t k = json.find(needle, pos);
    if (k == std::string::npos) {
        return false;
    }
    const std::size_t colon = json.find(':', k + needle.size());
    if (colon == std::string::npos) {
        return false;
    }
    const std::size_t q1 = json.find('"', colon + 1);
    if (q1 == std::string::npos) {
        return false;
    }
    const std::size_t q2 = json.find('"', q1 + 1);
    if (q2 == std::string::npos) {
        return false;
    }
    out = json.substr(q1 + 1, q2 - q1 - 1);
    return true;
}

struct ManifestEntry {
    std::wstring path;
    std::string sha256;
};

std::vector<ManifestEntry> parseManifest(const std::wstring& manifestPath) {
    std::vector<ManifestEntry> entries;
    std::ifstream in = abrirParaLeer(manifestPath);
    if (!in) {
        return entries;
    }
    std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    // Recorre objetos { "path": ..., "sha256": ... }.
    std::size_t pos = 0;
    while (pos < json.size()) {
        const std::size_t obj = json.find('{', pos);
        if (obj == std::string::npos) {
            break;
        }
        std::string path, sha;
        if (extractString(json, obj, "path", path) && extractString(json, obj, "sha256", sha)) {
            ManifestEntry e;
            e.path = fromUtf8Path(path);
            e.sha256 = toLower(sha);
            entries.push_back(std::move(e));
        }
        pos = obj + 1;
    }
    return entries;
}

std::wstring join(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) {
        return b;
    }
    if (a.back() == L'\\' || a.back() == L'/') {
        return a + b;
    }
#ifdef _WIN32
    return a + L"\\" + b;
#else
    return a + L"/" + b; // en Linux/macOS "\\" es parte del nombre: el manifest nunca se encontraba
#endif
}

// --- Caché de lo ya verificado -------------------------------------------------
// Una línea por archivo: sha256 \t tamaño \t fecha (ticks del reloj del sistema
// de archivos) \t ruta relativa. Texto plano a propósito: se lee en un ms y si
// está roto o es de otra versión, simplemente no coincide y se hashea.
struct Visto {
    std::string sha256;
    std::uintmax_t tamano = 0;
    long long fecha = 0;
};

std::filesystem::path rutaFs(const std::wstring& p) {
#ifdef _WIN32
    return std::filesystem::path(p);
#else
    return std::filesystem::path(toUtf8Path(p));
#endif
}

bool estado(const std::wstring& full, std::uintmax_t& tamano, long long& fecha) {
    std::error_code ec;
    const std::filesystem::path f = rutaFs(full);
    tamano = std::filesystem::file_size(f, ec);
    if (ec) return false;
    const auto t = std::filesystem::last_write_time(f, ec);
    if (ec) return false;
    fecha = static_cast<long long>(t.time_since_epoch().count());
    return true;
}

std::unordered_map<std::wstring, Visto> leerCache(const std::filesystem::path& cachePath) {
    std::unordered_map<std::wstring, Visto> out;
    if (cachePath.empty()) return out;
    // Uno de más de una semana no se usa: todo se vuelve a leer (en el fondo).
    {
        std::error_code ec;
        const auto cuando = std::filesystem::last_write_time(cachePath, ec);
        if (!ec && std::filesystem::file_time_type::clock::now() - cuando > std::chrono::hours(24 * 7)) return out;
    }
    std::ifstream in(cachePath, std::ios::binary);
    if (!in) return out;
    std::string linea;
    while (std::getline(in, linea)) {
        std::istringstream ls(linea);
        Visto v;
        std::string tam, fecha, ruta;
        if (!std::getline(ls, v.sha256, '\t') || !std::getline(ls, tam, '\t') || !std::getline(ls, fecha, '\t') ||
            !std::getline(ls, ruta))
            continue;
        try {
            v.tamano = std::stoull(tam);
            v.fecha = std::stoll(fecha);
        } catch (...) {
            continue;
        }
        out[fromUtf8Path(ruta)] = std::move(v);
    }
    return out;
}

// Lo verificado en este arranque, para guardarCache() al cierre ordenado.
std::mutex gVistoMu;
std::vector<std::pair<std::wstring, Visto>> gVisto;

} // namespace

int Integrity::verifyOne(const std::wstring& path, const std::string& expectedHex) {
    const std::string actual = Sha256::hashFile(path);
    if (actual.empty()) {
        return -1; // ausente / no leible.
    }
    return (toLower(actual) == toLower(expectedHex)) ? 0 : -2;
}

IntegrityReport Integrity::verify(const std::wstring& root, int workers) {
    return verify(root, std::filesystem::path(), workers);
}

void Integrity::guardarCache(const std::filesystem::path& cachePath) {
    if (cachePath.empty()) return;
    std::lock_guard<std::mutex> lock(gVistoMu);
    if (gVisto.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(cachePath.parent_path(), ec);
    const std::filesystem::path tmp = cachePath.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return;
        for (const auto& [ruta, v] : gVisto) {
            out << v.sha256 << '\t' << v.tamano << '\t' << v.fecha << '\t';
            out << toUtf8Path(ruta); // en UTF-8, como en el manifest
            out << '\n';
        }
        if (!out) return;
    }
    std::filesystem::rename(tmp, cachePath, ec);
}

IntegrityReport Integrity::verify(const std::wstring& root, const std::filesystem::path& cachePath, int workers,
                                  Modo modo, const std::atomic<bool>* parar, bool bajaPrioridad) {
    IntegrityReport report;
    const std::unordered_map<std::wstring, Visto> cache = leerCache(cachePath);
    std::vector<std::pair<std::wstring, Visto>> visto;
    const std::wstring manifestPath = join(root, L"manifest.json");
    const std::vector<ManifestEntry> entries = parseManifest(manifestPath);
    if (entries.empty()) {
        // Sin manifest (o vacio): no hay que verificar. No es un fallo.
        report.ok = true;
        report.checked = 0;
        return report;
    }

    if (workers <= 0) {
        workers = static_cast<int>(std::thread::hardware_concurrency());
        if (workers < 1) {
            workers = 1;
        }
        if (workers > 8) {
            workers = 8;
        }
    }

    std::mutex mu;
    std::vector<std::thread> pool;
    pool.reserve(static_cast<std::size_t>(workers));
    const std::size_t total = entries.size();

    // Particiona el rango de entradas entre workers.
    for (int w = 0; w < workers; ++w) {
        const std::size_t begin = total * static_cast<std::size_t>(w) / static_cast<std::size_t>(workers);
        const std::size_t end = total * static_cast<std::size_t>(w + 1) / static_cast<std::size_t>(workers);
        if (begin >= end) {
            continue;
        }
        pool.emplace_back([&, begin, end]() {
#ifdef _WIN32
            // De fondo: el sistema le baja la prioridad de CPU y de disco al hilo.
            if (bajaPrioridad) SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
#endif
            for (std::size_t i = begin; i < end; ++i) {
                if (parar != nullptr && parar->load()) break;
                const std::wstring full = join(root, entries[i].path);
                Visto v;
                const bool hayEstado = estado(full, v.tamano, v.fecha);
                const auto c = hayEstado ? cache.find(entries[i].path) : cache.end();
                const bool igual = c != cache.end() && c->second.sha256 == entries[i].sha256 &&
                                   c->second.tamano == v.tamano && c->second.fecha == v.fecha;
                // Rapida: lo que no coincide con el caché no se lee acá (queda pendiente);
                // si no está, es un fallo ya.
                const bool pendiente = modo == Modo::Rapida && !igual && hayEstado;
                const int rc = igual || pendiente ? 0 : (hayEstado ? verifyOne(full, entries[i].sha256) : -1);
                std::lock_guard<std::mutex> lock(mu);
                report.checked += 1;
                if (pendiente) {
                    report.pendientes += 1;
                    continue;
                }
                if (!igual) report.hashed += 1;
                if (rc == 0 && hayEstado) {
                    v.sha256 = entries[i].sha256;
                    visto.emplace_back(entries[i].path, std::move(v));
                }
                if (rc == -1) {
                    report.missing += 1;
                    report.failures.push_back(entries[i].path);
                } else if (rc == -2) {
                    report.mismatch += 1;
                    report.failures.push_back(entries[i].path);
                }
            }
        });
    }
    for (auto& t : pool) {
        t.join();
    }

    report.ok = report.failures.empty();
    {
        // Solo una verificación sin fallos deja algo para el caché.
        std::lock_guard<std::mutex> lock(gVistoMu);
        if (report.ok) gVisto = std::move(visto);
        else gVisto.clear();
    }
    return report;
}

} // namespace ba::launcher
