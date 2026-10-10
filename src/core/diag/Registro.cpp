// ============================================================================
// src/core/diag/Registro.cpp — ver Registro.hpp
// ============================================================================
#include "diag/Registro.hpp"
#include "diag/Utf8.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <map>
#include <mutex>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

#include "diag/LogRotator.hpp"

namespace ba::diag {
namespace {

std::int64_t ahoraMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

/// `AAAA-MM-DDTHH:MM:SS.mmmZ`, en UTC. UTC y no local a propósito: los tres
/// procesos escriben archivos distintos y el visor los entrelaza por hora; con
/// hora local, un cambio de huso o de horario de verano rompe ese orden justo
/// en el archivo que uno va a leer después del incidente.
std::string selloDeTiempo(std::int64_t ms) {
    const std::time_t s = static_cast<std::time_t>(ms / 1000);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &s);
#else
    gmtime_r(&s, &tm);
#endif
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900, tm.tm_mon + 1,
                  tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms % 1000));
    return buf;
}

/// El contexto va en UNA línea: un salto adentro rompería el formato y el
/// visor leería media línea como si fuera otra entrada. Se marcan con «⏎» en
/// vez de perderlos, que es lo que interesa de un mensaje de varias líneas de
/// Qt o de libobs.
std::string enUnaLinea(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        if (c == '\n' || c == '\r') {
            if (!out.empty() && out.compare(out.size() >= 3 ? out.size() - 3 : 0, 3, "⏎") == 0) continue;
            out += "⏎";
        } else {
            out += c;
        }
    }
    return out;
}

/// true si stderr sigue siendo una terminal, o sea que NADIE lo esta
/// capturando a un archivo.
bool stderrEsTerminal() {
#if defined(_WIN32)
    return _isatty(_fileno(stderr)) != 0;
#else
    return isatty(fileno(stderr)) != 0;
#endif
}

} // namespace

const char* nombreDeNivel(Nivel n) {
    switch (n) {
        case Nivel::Traza: return "traza";
        case Nivel::Depuracion: return "depuracion";
        case Nivel::Info: return "info";
        case Nivel::Aviso: return "aviso";
        case Nivel::Error: return "error";
        case Nivel::Fatal: return "fatal";
    }
    return "info";
}

bool nivelDeNombre(const std::string& s, Nivel& out) {
    struct {
        const char* n;
        Nivel v;
    } kTabla[] = {
        {"traza", Nivel::Traza},      {"trace", Nivel::Traza},    {"depuracion", Nivel::Depuracion},
        {"debug", Nivel::Depuracion}, {"info", Nivel::Info},      {"aviso", Nivel::Aviso},
        {"warn", Nivel::Aviso},       {"warning", Nivel::Aviso},  {"error", Nivel::Error},
        {"fatal", Nivel::Fatal},      {"critical", Nivel::Fatal},
    };
    for (const auto& e : kTabla) {
        if (s == e.n) {
            out = e.v;
            return true;
        }
    }
    return false;
}

std::string Registrado::aLinea() const {
    return selloDeTiempo(ms) + " " + nombreDeNivel(nivel) + " | " + componente + " | " +
           std::to_string(codigo) + " | " + texto;
}

struct Registro::Impl {
    mutable std::mutex m;
    std::unique_ptr<LogRotator> rotador;
    std::string componente = "app";
    std::atomic<int> minimo{static_cast<int>(Nivel::Info)};
    bool espejo = true;
    std::deque<Registrado> anillo;
    std::atomic<std::uint64_t> cuenta{0};
    std::atomic<std::uint64_t> descartadas{0};
    std::function<void(const Registrado&)> oyente;
    std::map<int, std::function<void(const Registrado&)>> oyentes;
    int proximo = 1;
};

Registro::Registro() : d_(std::make_unique<Impl>()) {
    // El nivel por entorno se lee ACÁ y no en el arranque de cada proceso:
    // así vale también para los que no tienen línea de comandos propia, y
    // sirve cuando lo que falla es justamente el arranque.
    if (const char* v = std::getenv("BA_LOG_NIVEL")) {
        Nivel n{};
        if (nivelDeNombre(v, n)) d_->minimo.store(static_cast<int>(n));
    }
}

Registro::~Registro() = default;

void Registro::abrir(const std::filesystem::path& dir, const std::string& archivo, std::size_t maxBytes,
                     std::size_t archivos) {
    std::lock_guard<std::mutex> g(d_->m);
    // Sin el sello del rotador: la linea que se le pasa YA trae su hora (ver
    // Registrado::aLinea). Con los dos, cada linea sale con la hora repetida.
    d_->rotador = std::make_unique<LogRotator>(dir.string(), maxBytes, archivos, archivo, false);
}

bool Registro::abrirSegunEntorno(const std::filesystem::path& dir, const std::string& archivo) {
    if (!stderrEsTerminal()) return false;
    abrir(dir, archivo);
    return true;
}

void Registro::setComponentePorDefecto(std::string c) {
    std::lock_guard<std::mutex> g(d_->m);
    d_->componente = std::move(c);
}

void Registro::setNivel(Nivel n) { d_->minimo.store(static_cast<int>(n)); }
Nivel Registro::nivel() const { return static_cast<Nivel>(d_->minimo.load()); }
bool Registro::registra(Nivel n) const { return static_cast<int>(n) >= d_->minimo.load(); }

void Registro::setEspejoEnStderr(bool si) {
    std::lock_guard<std::mutex> g(d_->m);
    d_->espejo = si;
}

void Registro::setOyente(std::function<void(const Registrado&)> fn) {
    std::lock_guard<std::mutex> g(d_->m);
    d_->oyente = std::move(fn);
}

int Registro::agregarOyente(std::function<void(const Registrado&)> fn) {
    std::lock_guard<std::mutex> g(d_->m);
    const int id = d_->proximo++;
    d_->oyentes[id] = std::move(fn);
    return id;
}

void Registro::quitarOyente(int id) {
    std::lock_guard<std::mutex> g(d_->m);
    d_->oyentes.erase(id);
}

void Registro::linea(Nivel n, const std::string& componente, int codigo, const std::string& texto) {
    if (!registra(n)) {
        d_->descartadas.fetch_add(1);
        return;
    }
    Registrado r;
    r.ms = ahoraMs();
    r.nivel = n;
    r.codigo = codigo;
    // En UTF-8 válido: lo leen el visor y el MCP como JSON, y un mensaje de un
    // tercero en cp1252 (Windows) hacía tirar una excepción (ver diag/Utf8.hpp).
    r.texto = enUnaLinea(utf8Valido(texto));

    std::function<void(const Registrado&)> oyente;
    std::vector<std::function<void(const Registrado&)>> otros;
    {
        std::lock_guard<std::mutex> g(d_->m);
        r.componente = componente.empty() ? d_->componente : componente;
        const std::string linea = r.aLinea();
        // A stderr y al archivo bajo el mismo lock, para que las dos salidas
        // queden en el mismo orden y ninguna línea se entrelace con otra.
        if (d_->espejo) {
            std::fprintf(stderr, "%s\n", linea.c_str());
            std::fflush(stderr);
        }
        if (d_->rotador) d_->rotador->write(linea);

        d_->anillo.push_back(r);
        if (d_->anillo.size() > kEnMemoria) d_->anillo.pop_front();
        oyente = d_->oyente;
        for (const auto& [id, fn] : d_->oyentes) otros.push_back(fn);
    }
    d_->cuenta.fetch_add(1);
    // El oyente se llama SIN el lock: si la interfaz se cuelga dibujando, no
    // puede frenar al que está registrando.
    if (oyente) oyente(r);
    for (const auto& fn : otros) fn(r);
}

std::vector<Registrado> Registro::ultimas(std::size_t cuantas) const {
    std::lock_guard<std::mutex> g(d_->m);
    const std::size_t n = std::min(cuantas, d_->anillo.size());
    return std::vector<Registrado>(d_->anillo.end() - static_cast<std::ptrdiff_t>(n), d_->anillo.end());
}

std::uint64_t Registro::cuenta() const { return d_->cuenta.load(); }
std::uint64_t Registro::descartadas() const { return d_->descartadas.load(); }

Registro& registro() {
    static Registro r;
    return r;
}

} // namespace ba::diag
