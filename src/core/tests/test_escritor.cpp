// test_escritor.cpp — el escritor único durable, las dos generaciones y la cola
//
// docs/arquitectura/04-plan-unificado.md, F1-01..F1-03. Sin framework: main() que
// devuelve 0 si todo pasa.
//
//   1. escribir: reemplaza entero, sin temporales colgados; Privados = 0600.
//   2. Matar al escritor (SIGKILL) en cualquier momento de 10 000 escrituras: el
//      archivo SIEMPRE es un JSON válido (el viejo o el nuevo, nunca uno a medias).
//      Lo mismo con dos generaciones: siempre hay una entera. (POSIX: fork + kill.)
//   3. Dos generaciones: la más nueva válida; rota la nueva → la anterior; rotas
//      las dos → Danada; el archivo viejo de una sola → Vieja (y la primera escritura
//      nueva lo borra).
//   4. La cola: fusiona por archivo (último gana), lo de aire pasa adelante, `leer`
//      ve lo encolado, `borrar` también se encola, y si el disco falla reintenta.

#include "persist/ColaEscritura.hpp"
#include "persist/Escritor.hpp"
#include "persist/Generaciones.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace ba::persist;
using json = nlohmann::json;

namespace {

int g_fallas = 0;
#define CHECK(c, msg)                                                                                        \
    do {                                                                                                     \
        if (!(c)) {                                                                                          \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #c, __LINE__);                                    \
            ++g_fallas;                                                                                      \
        }                                                                                                    \
    } while (0)

fs::path carpetaTemporal(const char* nombre) {
    static std::mt19937_64 r(std::random_device{}());
    const fs::path d = fs::temp_directory_path() / ("ba-escritor-" + std::string(nombre) + "-" + std::to_string(r()));
    fs::create_directories(d);
    return d;
}

std::string leer(const fs::path& p) {
    std::string s;
    leerArchivo(p, s);
    return s;
}

/// Un JSON de tamaño variable (de pocos bytes a ~40 KB): que un corte a medias se note.
std::string documento(int i) {
    json j{{"n", i}, {"relleno", std::string((static_cast<std::size_t>(i) * 7919u) % 40000u, 'x')}};
    return j.dump();
}

bool esJson(const std::string& s) { return !s.empty() && !json::parse(s, nullptr, false).is_discarded(); }

void probarEscribir() {
    const fs::path d = carpetaTemporal("escribir");
    const fs::path f = d / "sub" / "a.json";
    CHECK(escribir(f, "{\"a\":1}"), "escribe (y crea la carpeta)");
    CHECK(leer(f) == "{\"a\":1}", "el contenido");
    CHECK(escribir(f, "{}", Durabilidad::Critica), "reemplaza (crítica)");
    CHECK(leer(f) == "{}", "reemplazado entero");
    CHECK(!fs::exists(f.string() + ".tmp"), "sin temporal colgado");
#if !defined(_WIN32)
    const fs::path s = d / "secreto.json";
    CHECK(escribir(s, "x", Durabilidad::Normal, Permisos::Privados), "privado");
    struct stat st {};
    ::stat(s.c_str(), &st);
    CHECK((st.st_mode & 0777) == 0600, "privado: 0600");
    // Donde no se puede (la «carpeta» es un archivo): false con el porqué, y nada roto.
    std::string error;
    CHECK(!escribir(f / "no.json", "x", Durabilidad::Normal, Permisos::Comunes, &error), "falla sin carpeta");
    CHECK(!error.empty(), "dice por qué");
    CHECK(leer(f) == "{}", "lo de antes sigue");
#endif
    CHECK(!sistemaDeArchivos(d).empty(), "se sabe el sistema de archivos");
    std::printf("  sistema de archivos de %s: %s (sin diario: %d)\n", d.string().c_str(), sistemaDeArchivos(d).c_str(),
                sinDiario(d) ? 1 : 0);
    fs::remove_all(d);
}

#if !defined(_WIN32)
/// Un hijo que escribe sin parar; el padre lo mata en un momento al azar y mira el disco.
template <typename Escribir, typename Revisar>
void matarEnCualquierMomento(const char* que, int rondas, Escribir escribirHijo, Revisar revisar) {
    std::mt19937 r(12345);
    int revisadas = 0;
    for (int ronda = 0; ronda < rondas; ++ronda) {
        std::fflush(stdout); // si no, cada hijo repite lo que el padre tenía sin escribir
        const pid_t hijo = ::fork();
        if (hijo == 0) {
            for (int i = 0; i < 10000; ++i) escribirHijo(ronda * 10000 + i);
            ::_exit(0);
        }
        std::this_thread::sleep_for(std::chrono::microseconds(std::uniform_int_distribution<int>(500, 40000)(r)));
        ::kill(hijo, SIGKILL);
        int estado = 0;
        ::waitpid(hijo, &estado, 0);
        if (revisar(ronda)) ++revisadas;
    }
    CHECK(revisadas == rondas, que);
    std::printf("  %s: %d de %d cortes dejaron todo entero\n", que, revisadas, rondas);
}

void probarMatar() {
    const fs::path d = carpetaTemporal("matar");
    const fs::path f = d / "estado.json";
    escribir(f, documento(0)); // ya había uno: el corte nunca lo puede dejar vacío
    matarEnCualquierMomento(
        "un archivo: siempre JSON válido", 30, [&](int i) { escribir(f, documento(i)); },
        [&](int) { return esJson(leer(f)); });
    const fs::path g = d / "aire.json";
    {
        DosGeneraciones dg(g);
        dg.escribir(documento(0));
    }
    matarEnCualquierMomento(
        "dos generaciones: siempre una entera", 30,
        [&](int i) {
            static DosGeneraciones dg(g); // una por hijo (cada fork empieza de cero)
            dg.escribir(documento(i), Durabilidad::Normal);
        },
        [&](int) {
            const Lectura l = DosGeneraciones(g).leer();
            return l.estado == Lectura::Estado::Valida && esJson(l.cuerpo);
        });
    fs::remove_all(d);
}
#endif

void probarGeneraciones() {
    const fs::path d = carpetaTemporal("generaciones");
    const fs::path f = d / "al-aire.json";
    DosGeneraciones dg(f);
    CHECK(dg.leer().estado == Lectura::Estado::Nada, "nada al principio");

    // El archivo de antes de las generaciones se lee (retrocompatible)…
    escribir(f, "{\"streaming\":true}");
    Lectura l = dg.leer();
    CHECK(l.estado == Lectura::Estado::Vieja && l.cuerpo == "{\"streaming\":true}", "el viejo de una generación");
    // …y la primera escritura nueva lo borra (no vuelve desactualizado).
    CHECK(dg.escribir("{\"v\":1}"), "primera");
    CHECK(!fs::exists(f), "el viejo se fue");
    CHECK(dg.escribir("{\"v\":2}") && dg.escribir("{\"v\":3}"), "segunda y tercera");
    l = dg.leer();
    CHECK(l.estado == Lectura::Estado::Valida && l.cuerpo == "{\"v\":3}" && l.secuencia == 3, "la más nueva");
    CHECK(fs::exists(dg.rutaA()) && fs::exists(dg.rutaB()), "las dos generaciones");

    // Otra instancia (otro arranque) sigue la secuencia y pisa la vieja, no la última.
    DosGeneraciones otra(f);
    CHECK(otra.escribir("{\"v\":4}"), "otra instancia");
    l = dg.leer();
    CHECK(l.secuencia == 4 && l.cuerpo == "{\"v\":4}", "sigue la secuencia");

    // La nueva truncada (corte a mitad, sin el rename de por medio: disco que miente).
    const fs::path nueva = leer(dg.rutaA()).find("seq=4 ") != std::string::npos ? dg.rutaA() : dg.rutaB();
    const fs::path vieja = nueva == dg.rutaA() ? dg.rutaB() : dg.rutaA();
    {
        const std::string c = leer(nueva);
        std::ofstream(nueva, std::ios::binary | std::ios::trunc) << c.substr(0, c.size() - 3);
    }
    l = dg.leer();
    CHECK(l.estado == Lectura::Estado::Valida && l.cuerpo == "{\"v\":3}" && l.descartadas == 1, "rota la nueva: la anterior");
    // Un byte cambiado en la otra (el hash no coincide): las dos rotas.
    {
        std::string c = leer(vieja);
        c[c.size() - 3] = '9';
        std::ofstream(vieja, std::ios::binary | std::ios::trunc) << c;
    }
    l = dg.leer();
    CHECK(l.estado == Lectura::Estado::Danada && l.descartadas == 2, "rotas las dos: dañada (no «nada»)");
    // Escribir de nuevo arregla (pisa una rota).
    DosGeneraciones tercera(f);
    CHECK(tercera.escribir("{\"v\":5}"), "tras el daño");
    l = tercera.leer();
    CHECK(l.estado == Lectura::Estado::Valida && l.cuerpo == "{\"v\":5}", "vuelve a haber una buena");
    CHECK(tercera.borrar() && tercera.leer().estado == Lectura::Estado::Nada, "borrar: las dos");

    // Cabecera: el formato de SnapshotStore.
    std::uint64_t s = 0;
    std::string cuerpo;
    CHECK(sinCabecera(conCabecera(17, "abc"), s, cuerpo) && s == 17 && cuerpo == "abc", "ida y vuelta");
    CHECK(!sinCabecera("{\"a\":1}", s, cuerpo), "sin cabecera: no");
    CHECK(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 de «abc»");
    fs::remove_all(d);
}

void probarCola() {
    const fs::path d = carpetaTemporal("cola");
    ColaEscritura c;
    const fs::path f = d / "ajustes.json";

    // Fusión: mil pedidos al mismo archivo; se ve el último enseguida y al disco va el último.
    for (int i = 0; i < 1000; ++i) c.escribir(f, std::to_string(i));
    std::string v;
    CHECK(c.leer(f, v) && v == "999", "leer ve lo encolado");
    CHECK(c.vaciar(), "vacía");
    CHECK(leer(f) == "999", "en el disco, el último");
    const auto n = c.cuentas();
    CHECK(n.pedidos == 1000 && n.escrituras < 1000 && n.fusionados > 0, "fusiona");
    std::printf("  cola: %llu pedidos, %llu escrituras, %llu fusionados\n", static_cast<unsigned long long>(n.pedidos),
                static_cast<unsigned long long>(n.escrituras), static_cast<unsigned long long>(n.fusionados));

    // Prioridad: con el hilo ocupado en «a», llegan «b» (ajuste) y «c» (aire): «c» primero.
    std::mutex m;
    std::vector<std::string> orden;
    std::atomic<bool> soltar{false};
    const auto anotar = [&](const char* x) {
        return [&, x]() {
            std::lock_guard<std::mutex> l(m);
            orden.push_back(x);
        };
    };
    Pedido pa;
    pa.antes = [&]() {
        while (!soltar) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        std::lock_guard<std::mutex> l(m);
        orden.push_back("a");
    };
    c.escribir(d / "a.json", "a", pa);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    Pedido pb;
    pb.antes = anotar("b");
    c.escribir(d / "b.json", "b", pb);
    Pedido pc{Durabilidad::Critica, Prioridad::Aire, Permisos::Comunes, anotar("c")};
    c.escribir(d / "c.json", "c", pc);
    soltar = true;
    CHECK(c.vaciar(), "vacía");
    CHECK((orden == std::vector<std::string>{"a", "c", "b"}), "lo de aire pasa adelante");

    // Generaciones por la cola, y su lectura antes de que lleguen al disco.
    const fs::path g = d / "linea-aire.json";
    c.escribirGeneracion(g, "{\"estado\":\"corriendo\"}");
    CHECK(c.leerGeneraciones(g).cuerpo == "{\"estado\":\"corriendo\"}", "leerGeneraciones ve lo encolado");
    CHECK(c.vaciar() && DosGeneraciones(g).leer().estado == Lectura::Estado::Valida, "generación escrita");
    c.borrar(g);
    CHECK(c.leerGeneraciones(g).estado == Lectura::Estado::Nada, "el borrado encolado se ve");
    CHECK(c.vaciar() && !fs::exists(DosGeneraciones(g).rutaA()) && !fs::exists(DosGeneraciones(g).rutaB()), "borradas");
    c.borrar(f);
    CHECK(!c.leer(f, v), "borrar un archivo");
    CHECK(c.vaciar() && !fs::exists(f), "borrado");

#if !defined(_WIN32)
    // Disco que falla: el pedido queda (se reintenta) y no traba a los demás.
    const fs::path bloqueo = d / "bloqueo";
    escribir(bloqueo, "soy un archivo");
    c.escribir(bloqueo / "x.json", "x");
    c.escribir(d / "otro.json", "otro");
    CHECK(!c.vaciar(std::chrono::milliseconds(300)), "no vacía: el disco no responde");
    CHECK(leer(d / "otro.json") == "otro", "lo demás se escribió igual");
    CHECK(c.cuentas().errores > 0, "cuenta el error");
    // Vuelve «el disco»: se escribe solo.
    fs::remove(bloqueo);
    CHECK(c.vaciar(std::chrono::milliseconds(3000)), "reintenta y vacía");
    CHECK(leer(bloqueo / "x.json") == "x", "escrito al volver");
#endif
    fs::remove_all(d);
}

} // namespace

int main() {
    probarEscribir();
    probarGeneraciones();
    probarCola();
#if !defined(_WIN32)
    probarMatar();
#endif
    if (g_fallas == 0) std::printf("test_escritor: todo bien\n");
    return g_fallas == 0 ? 0 : 1;
}
