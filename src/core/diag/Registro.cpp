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
#include <condition_variable>
#include <mutex>
#include <thread>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

#include "diag/LogRotator.hpp"
#include "platform/Hilo.hpp"

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
/// Qt o de GStreamer.
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

namespace {

/// Lo que deja el que registra: los datos crudos. Pasarlo a UTF-8 válido, a una
/// línea y a texto con hora es trabajo del escritor, no del hilo que llama.
struct Pendiente {
    std::int64_t ms = 0;
    Nivel nivel = Nivel::Info;
    int codigo = 0;
    std::string componente;
    std::string texto;
};

/// Cola acotada de muchos productores y un consumidor, sin candado para el que
/// mete (la de Vyukov: cada casilla con su número de vuelta). Meter es un
/// compare_exchange y mover dos strings; si está llena NO espera: dice que no,
/// y el que registra cuenta la línea como perdida.
class ColaDeLineas {
   public:
    static constexpr std::size_t kCasillas = 4096; // potencia de dos

    ColaDeLineas() : casillas_(std::make_unique<Casilla[]>(kCasillas)) {
        for (std::size_t i = 0; i < kCasillas; ++i) casillas_[i].vuelta.store(i, std::memory_order_relaxed);
    }

    bool meter(Pendiente&& p) {
        std::size_t pos = cola_.load(std::memory_order_relaxed);
        for (;;) {
            Casilla& c = casillas_[pos & (kCasillas - 1)];
            const std::size_t v = c.vuelta.load(std::memory_order_acquire);
            const auto dif = static_cast<std::intptr_t>(v) - static_cast<std::intptr_t>(pos);
            if (dif == 0) {
                if (cola_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    c.dato = std::move(p);
                    c.vuelta.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (dif < 0) {
                return false; // llena: el escritor está trabado
            } else {
                pos = cola_.load(std::memory_order_relaxed);
            }
        }
    }

    /// Solo el escritor (un consumidor).
    bool sacar(Pendiente& out) {
        Casilla& c = casillas_[cabeza_ & (kCasillas - 1)];
        if (c.vuelta.load(std::memory_order_acquire) != cabeza_ + 1) return false;
        out = std::move(c.dato);
        c.vuelta.store(cabeza_ + kCasillas, std::memory_order_release);
        ++cabeza_;
        return true;
    }

    /// Solo el escritor: hay algo listo para sacar.
    bool hayAlgo() const {
        return casillas_[cabeza_ & (kCasillas - 1)].vuelta.load(std::memory_order_acquire) == cabeza_ + 1;
    }

   private:
    struct Casilla {
        std::atomic<std::size_t> vuelta{0};
        Pendiente dato;
    };
    std::unique_ptr<Casilla[]> casillas_;
    alignas(64) std::atomic<std::size_t> cola_{0};
    alignas(64) std::size_t cabeza_ = 0;
};

} // namespace

// CÓMO VIAJA UNA LÍNEA. El que registra (que puede ser el hilo de un mezclador
// con plazo de 10 ms) solo mete los datos crudos en la cola sin candado y, si
// el escritor duerme, lo despierta. El escritor —un hilo propio, «registro»—
// arma la línea y hace todo lo que puede trabarse: stderr, el archivo
// rotativo, el anillo y los oyentes. Antes todo eso pasaba bajo un candado
// global en el hilo que llamaba, y con tiempo real en los hilos de audio
// (Prioridad.cpp) eso es inversión de prioridad: el mezclador esperando el
// fflush de un hilo cualquiera. Si el escritor se traba (un disco que no
// contesta, un oyente colgado) la cola se llena y lo que sigue se descarta y
// se cuenta (perdidas()): mejor perder una línea que un bloque de audio.
struct Registro::Impl {
    ColaDeLineas cola;

    // Del escritor y de quien configura; el que registra nunca lo toma.
    mutable std::mutex m;
    std::unique_ptr<LogRotator> rotador;
    std::string componente = "app";
    bool espejo = true;
    std::deque<Registrado> anillo;
    std::function<void(const Registrado&)> oyente;
    std::map<int, std::function<void(const Registrado&)>> oyentes;
    int proximo = 1;

    std::atomic<int> minimo{static_cast<int>(Nivel::Info)};
    std::atomic<std::uint64_t> encoladas{0};
    std::atomic<std::uint64_t> cuenta{0}; // las que ya pasó el escritor
    std::atomic<std::uint64_t> descartadas{0};
    std::atomic<std::uint64_t> perdidas{0};

    // Para despertar al escritor. El que registra toma `mDormir` SOLO si el
    // escritor está durmiendo (la primera línea después de un rato quieto) y
    // lo suelta enseguida: bajo este candado nunca hay E/S.
    std::mutex mDormir;
    std::condition_variable cvDormir, cvHecho;
    std::atomic<bool> durmiendo{false};
    bool aviso = false;
    bool parar = false;
    std::thread hilo;

    void despertar() {
        if (!durmiendo.load()) return;
        {
            std::lock_guard<std::mutex> g(mDormir);
            aviso = true;
        }
        cvDormir.notify_one();
    }

    void procesar(Pendiente& p) {
        Registrado r;
        r.ms = p.ms;
        r.nivel = p.nivel;
        r.codigo = p.codigo;
        // En UTF-8 válido: lo leen el visor y el MCP como JSON, y un mensaje de un
        // tercero en cp1252 (Windows) hacía tirar una excepción (ver diag/Utf8.hpp).
        r.texto = enUnaLinea(utf8Valido(p.texto));
        std::function<void(const Registrado&)> uno;
        std::vector<std::function<void(const Registrado&)>> otros;
        {
            std::lock_guard<std::mutex> g(m);
            r.componente = p.componente.empty() ? componente : std::move(p.componente);
            const std::string linea = r.aLinea();
            // A stderr y al archivo desde el mismo hilo y en el mismo orden:
            // ninguna línea se entrelaza con otra.
            if (espejo) {
                std::fprintf(stderr, "%s\n", linea.c_str());
                std::fflush(stderr);
            }
            if (rotador) rotador->write(linea);
            anillo.push_back(r);
            if (anillo.size() > kEnMemoria) anillo.pop_front();
            uno = oyente;
            for (const auto& [id, fn] : oyentes) otros.push_back(fn);
        }
        cuenta.fetch_add(1);
        // Los oyentes, sin el candado: si la interfaz se cuelga dibujando, frena
        // al escritor (y la cola se llena), nunca al que registra.
        if (uno) uno(r);
        for (const auto& fn : otros) fn(r);
    }

    void escritor() {
        ba::platform::nombrarHilo("registro");
        Pendiente p;
        for (;;) {
            bool alguna = false;
            while (cola.sacar(p)) {
                procesar(p);
                alguna = true;
            }
            std::unique_lock<std::mutex> l(mDormir);
            if (alguna) cvHecho.notify_all();
            if (parar) return;
            // Dormido ANTES de mirar la cola otra vez: o el que mete ve
            // `durmiendo` y avisa, o acá se ve lo que metió.
            durmiendo.store(true);
            if (cola.hayAlgo()) {
                durmiendo.store(false);
                continue;
            }
            cvDormir.wait(l, [&] { return aviso || parar; });
            aviso = false;
            durmiendo.store(false);
        }
    }
};

Registro::Registro() : d_(std::make_unique<Impl>()) {
    // El nivel por entorno se lee ACÁ y no en el arranque de cada proceso:
    // así vale también para los que no tienen línea de comandos propia, y
    // sirve cuando lo que falla es justamente el arranque.
    if (const char* v = std::getenv("BA_LOG_NIVEL")) {
        Nivel n{};
        if (nivelDeNombre(v, n)) d_->minimo.store(static_cast<int>(n));
    }
    d_->hilo = std::thread([d = d_.get()] { d->escritor(); });
}

Registro::~Registro() {
    {
        std::lock_guard<std::mutex> g(d_->mDormir);
        d_->parar = true;
    }
    d_->cvDormir.notify_one();
    if (d_->hilo.joinable()) d_->hilo.join();
    // Lo que se metió entre la última vuelta del escritor y el parar.
    Pendiente p;
    while (d_->cola.sacar(p)) d_->procesar(p);
}

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
    Pendiente p;
    p.ms = ahoraMs();
    p.nivel = n;
    p.codigo = codigo;
    p.componente = componente;
    p.texto = texto;
    if (!d_->cola.meter(std::move(p))) {
        d_->perdidas.fetch_add(1);
        return;
    }
    d_->encoladas.fetch_add(1);
    d_->despertar();
    // Un fatal es lo último que dice el proceso: se espera a que llegue al
    // archivo (ahí trabarse ya no le hace mal a nadie).
    if (n == Nivel::Fatal) vaciar(1000);
}

bool Registro::vaciar(int ms) {
    if (std::this_thread::get_id() == d_->hilo.get_id()) return false; // un oyente: se esperaría a sí mismo
    const std::uint64_t objetivo = d_->encoladas.load();
    std::unique_lock<std::mutex> l(d_->mDormir);
    if (d_->durmiendo.load()) {
        d_->aviso = true;
        d_->cvDormir.notify_one();
    }
    return d_->cvHecho.wait_for(l, std::chrono::milliseconds(ms), [&] { return d_->cuenta.load() >= objetivo; });
}

std::vector<Registrado> Registro::ultimas(std::size_t cuantas) const {
    std::lock_guard<std::mutex> g(d_->m);
    const std::size_t n = std::min(cuantas, d_->anillo.size());
    return std::vector<Registrado>(d_->anillo.end() - static_cast<std::ptrdiff_t>(n), d_->anillo.end());
}

std::uint64_t Registro::cuenta() const { return d_->cuenta.load(); }
std::uint64_t Registro::descartadas() const { return d_->descartadas.load(); }
std::uint64_t Registro::perdidas() const { return d_->perdidas.load(); }

Registro& registro() {
    // Nunca se destruye: su escritor sigue vivo hasta el final, y lo que se
    // registra desde un destructor estático o un atexit no cae sobre un objeto
    // muerto. Lo pendiente al salir lo baja `vaciar()` (en el atexit de main).
    static Registro* r = new Registro();
    return *r;
}

} // namespace ba::diag
