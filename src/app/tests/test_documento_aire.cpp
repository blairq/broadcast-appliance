// ============================================================================
// src/app/tests/test_documento_aire.cpp — el documento único de aire (F3-04)
//
//   1. Corte en cada transición (escena, placa, grabar, transmitir, paso de la
//      corrida, HOLD, negro de otra salida): un arranque nuevo lee EXACTAMENTE lo
//      último puesto; con la escritura de esa transición rota a mitad, lee la
//      anterior ENTERA (nunca una mezcla de secciones de dos momentos).
//   2. Daño: las dos generaciones rotas → «dañado», cuenta como corte (la línea
//      vuelve en HOLD y pregunta).
//   3. Lo mismo dos veces no se reescribe; un dueño en otro hilo vuelca lo último.
//   4. La versión 1 (al-aire.json + placa.json + linea-aire.json) se lee como
//      secciones y el primer volcado borra los sueltos.
// ============================================================================
#include "emision/DocumentoAire.hpp"

#include "persist/ColaEscritura.hpp"
#include "persist/Generaciones.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;
using ba::app::salidas::AlAire;
using ba::app::salidas::DocumentoAire;

namespace {
int g_fallos = 0;
#define CHECK(cond, msg)                                                                 \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);             \
            ++g_fallos;                                                                  \
        }                                                                                \
    } while (0)

fs::path carpeta(const char* nombre) {
    const fs::path d = fs::temp_directory_path() / ("ba-test-documento-aire-" + std::string(nombre));
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}
std::string leer(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}
void escribir(const fs::path& p, const std::string& c) { std::ofstream(p, std::ios::binary | std::ios::trunc) << c; }
std::string texto(const AlAire& a) { return ba::app::salidas::aJson(a).dump(); }

/// Lo que ve un arranque nuevo (otro proceso): lo del disco, por un documento nuevo.
DocumentoAire::Anterior arrancar(const fs::path& d) {
    CHECK(ba::persist::cola().vaciar(), "la cola se vacía");
    DocumentoAire otro(d);
    return otro.leerAnterior();
}

/// La generación con la secuencia más alta (la que escribió la última transición).
fs::path laMasNueva(const fs::path& d) {
    std::uint64_t mejor = 0;
    fs::path cual;
    for (const char* g : {"al-aire.A.json", "al-aire.B.json"}) {
        std::uint64_t s = 0;
        std::string cuerpo;
        if (ba::persist::sinCabecera(leer(d / g), s, cuerpo) && s >= mejor) {
            mejor = s;
            cual = d / g;
        }
    }
    return cual;
}
} // namespace

int main() {
    // --- 1) corte en cada transición -------------------------------------------------
    {
        const fs::path d = carpeta("transiciones");
        DocumentoAire doc(d);
        CHECK(doc.leerAnterior().estado == ba::persist::Lectura::Estado::Nada, "primer arranque: nada");
        std::map<std::string, ba::app::salidas::Aire> s;
        s["principal"].escena = "Plano";
        json linea; // null: no corre
        // Cada transición: el nombre y qué cambia.
        struct Paso {
            const char* que;
            std::function<void()> hacer;
        };
        const std::vector<Paso> pasos{
            {"arranque", [&] { doc.ponerSalidas(s, false); }},
            {"escena", [&] { s["principal"].escena = "Entrevista"; doc.ponerSalidas(s, false); }},
            {"placa", [&] {
                 s["principal"].placa.placa = true;
                 s["principal"].placa.texto = "Volvemos enseguida";
                 doc.ponerSalidas(s, false);
             }},
            {"grabar", [&] { s["principal"].grabando = true; doc.ponerSalidas(s, false); }},
            {"transmitir", [&] {
                 s["principal"].transmitiendo = true;
                 s["principal"].destinos = {"respaldo"};
                 s["b"].escena = "Pantalla";
                 s["b"].transmitiendo = true;
                 doc.ponerSalidas(s, false);
             }},
            {"paso de la corrida", [&] {
                 linea = {{"estado", "corriendo"}, {"evento", "ev1"}, {"arranqueMs", 1000},
                          {"salidas", json::array({{{"salida", "principal"}, {"alAire", "p2"}, {"hold", false}}})}};
                 doc.ponerLinea(linea);
             }},
            {"HOLD", [&] { linea["salidas"][0]["hold"] = true; doc.ponerLinea(linea); }},
            {"negro de otra salida", [&] {
                 s["b"].placa.ftb = true;
                 s["b"].placa.ftbAudio = true;
                 doc.ponerSalidas(s, false);
             }},
            {"sacar la placa", [&] { s["principal"].placa = {}; doc.ponerSalidas(s, false); }},
            {"cortar la grabación", [&] { s["principal"].grabando = false; doc.ponerSalidas(s, false); }},
        };
        std::string anterior;
        const fs::path foto = d / "foto";
        for (const Paso& p : pasos) {
            p.hacer();
            const std::string ahora = texto(doc.foto());
            CHECK(ahora != anterior, p.que);
            // b) el corte A MITAD de la escritura. La cola copia la última versión a la otra
            // generación al segundo (persist/ColaEscritura, kIgualar): el corte que importa
            // es el de antes de esa copia. Se saca una foto del disco apenas llega la
            // generación nueva, se le rompe esa y un arranque sobre la foto tiene que leer la
            // transición anterior ENTERA (todas sus secciones de ese momento).
            if (!anterior.empty()) {
                bool llego = false;
                for (int i = 0; i < 400 && !llego; ++i) {
                    std::uint64_t sq = 0;
                    std::string cuerpo;
                    const fs::path n = laMasNueva(d);
                    llego = !n.empty() && ba::persist::sinCabecera(leer(n), sq, cuerpo) && cuerpo == ahora;
                    if (!llego) std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
                CHECK(llego, "la generación nueva llega al disco");
                fs::remove_all(foto);
                fs::create_directories(foto);
                for (const char* g : {"al-aire.A.json", "al-aire.B.json"})
                    if (fs::exists(d / g)) fs::copy_file(d / g, foto / g);
                const fs::path nueva = laMasNueva(foto);
                const std::string bien = leer(nueva);
                escribir(nueva, bien.substr(0, bien.size() / 2));
                const DocumentoAire::Anterior a = DocumentoAire(foto).leerAnterior();
                if (texto(a.aire) != anterior || a.descartadas != 1)
                    std::printf("  %s (corte a mitad, %d descartadas):\n    esperaba %s\n    leyó     %s\n", p.que,
                                a.descartadas, anterior.c_str(), texto(a.aire).c_str());
                CHECK(texto(a.aire) == anterior && a.descartadas == 1, "corte a mitad: la transición anterior entera");
            }
            // a) el corte después de que la escritura llegó (y se igualó): vuelve igual.
            const DocumentoAire::Anterior a = arrancar(d);
            if (texto(a.aire) != ahora) std::printf("  %s:\n    esperaba %s\n    leyó     %s\n", p.que, ahora.c_str(), texto(a.aire).c_str());
            CHECK(texto(a.aire) == ahora, "corte tras la transición: vuelve igual");
            CHECK(!a.aire.cerrado && !a.danado, "una corrida sin cerrar");
            anterior = ahora;
        }
        fs::remove_all(foto);
        // La línea vuelve con su HOLD (lo que lee EventoViewModel::retomarLinea).
        const DocumentoAire::Anterior a = arrancar(d);
        CHECK(a.aire.linea.is_object() && a.aire.linea["salidas"][0].value("hold", false), "el HOLD de la línea vuelve");
        CHECK(a.aire.de("b").placa.ftb && a.aire.de("b").placa.ftbAudio && !a.aire.de("principal").placa.puesta(),
              "el negro de la otra salida vuelve; la placa sacada, no");
        CHECK(a.aire.de("b").transmitiendo && a.aire.de("principal").destinos == std::vector<std::string>{"respaldo"},
              "las salidas vuelven");

        // --- 2) daño: las dos generaciones rotas -------------------------------------
        for (const char* g : {"al-aire.A.json", "al-aire.B.json"}) {
            std::string c = leer(d / g);
            c[c.size() - 2] ^= 0x5;
            escribir(d / g, c);
        }
        const DocumentoAire::Anterior roto = arrancar(d);
        CHECK(roto.danado && !roto.aire.cerrado && roto.aire.salidas.empty() && roto.descartadas == 2,
              "dañado: no se sabe qué había, cuenta como corte (la línea vuelve en HOLD)");
        // Escribir de nuevo arregla.
        DocumentoAire otro(d);
        otro.leerAnterior();
        otro.ponerSalidas(s, true);
        const DocumentoAire::Anterior arreglado = arrancar(d);
        CHECK(!arreglado.danado && arreglado.aire.cerrado, "tras el daño, el próximo volcado vale (cierre ordenado)");
        fs::remove_all(d);
    }

    // --- 3) sin reescribir lo mismo; el dueño en otro hilo ---------------------------
    {
        const fs::path d = carpeta("duenio");
        DocumentoAire doc(d);
        doc.leerAnterior();
        std::map<std::string, ba::app::salidas::Aire> s;
        s["principal"].escena = "A";
        CHECK(doc.ponerSalidas(s, false), "la primera vez cambia");
        const std::uint64_t n = doc.escrituras();
        CHECK(!doc.ponerSalidas(s, false) && doc.escrituras() == n, "lo mismo: ni cambia ni se escribe");
        CHECK(!doc.hayAire(), "sin nada saliendo, no hay aire");
        // El dueño: un hilo que vuelca cuando lo despiertan (como PlanoControl).
        std::mutex m;
        std::condition_variable cv;
        bool pedido = false, parar = false;
        std::atomic<int> despertares{0};
        doc.setDueno([&] {
            ++despertares;
            std::lock_guard<std::mutex> l(m);
            pedido = true;
            cv.notify_one();
        });
        std::thread duenio([&] {
            std::unique_lock<std::mutex> l(m);
            while (!parar) {
                cv.wait(l, [&] { return pedido || parar; });
                pedido = false;
                l.unlock();
                doc.volcar();
                l.lock();
            }
        });
        // Dos hilos ponen a la vez (el del motor y el de la interfaz).
        std::thread a([&] {
            for (int i = 0; i < 200; ++i) {
                std::map<std::string, ba::app::salidas::Aire> x;
                x["principal"].escena = "E" + std::to_string(i);
                x["principal"].grabando = true;
                doc.ponerSalidas(x, false);
            }
        });
        std::thread b([&] {
            for (int i = 0; i < 200; ++i) doc.ponerLinea({{"estado", "corriendo"}, {"escritoMs", i}});
        });
        a.join();
        b.join();
        {
            std::lock_guard<std::mutex> l(m);
            parar = true;
            cv.notify_one();
        }
        duenio.join();
        doc.volcar(); // lo que haya quedado entre el último despertar y el final
        CHECK(despertares > 0 && doc.hayAire(), "el dueño volcó; grabando = aire");
        const DocumentoAire::Anterior r = arrancar(d);
        CHECK(texto(r.aire) == texto(doc.foto()), "en el disco, lo último que se puso (las dos secciones)");
        CHECK(r.aire.de("principal").escena == "E199" && r.aire.linea.value("escritoMs", -1) == 199, "lo último de cada uno");
        fs::remove_all(d);
    }

    // --- 4) la versión 1: secciones sueltas, una versión más ---------------------------
    {
        const fs::path d = carpeta("version1");
        escribir(d / "al-aire.json", R"({"streaming":true,"recording":false,"escena":"Plano","cerrado":false,)"
                                     R"("salidas":{"b":{"escena":"Pantalla","recording":true}}})");
        escribir(d / "placa.json", R"({"ftb":false,"placa":true,"placa_texto":"Cuarto intermedio","placa_imagen":"",)"
                                   R"("salidas":{"b":{"ftb":true,"ftb_audio":true},"../x":{"ftb":true}}})");
        escribir(d / "linea-aire.json", R"({"estado":"corriendo","evento":"ev9","salidas":[]})");
        DocumentoAire doc(d);
        const DocumentoAire::Anterior a = doc.leerAnterior();
        CHECK(a.deVersionVieja && a.aire.version == 1 && !a.aire.cerrado, "la versión 1, de un corte");
        CHECK(a.aire.de("principal").placa.placa && a.aire.de("principal").placa.texto == "Cuarto intermedio",
              "placa.json: la por omisión");
        CHECK(a.aire.de("b").placa.ftb && a.aire.de("b").placa.ftbAudio && a.aire.salidas.count("../x") == 0,
              "placa.json: las demás (un id inválido, afuera)");
        CHECK(a.aire.linea.value("evento", "") == "ev9", "linea-aire.json: la sección de la línea");
        CHECK(a.aire.de("principal").transmitiendo && a.aire.de("b").grabando, "al-aire.json de siempre");
        // El primer volcado: el documento entero (v2) y los sueltos, borrados.
        auto s = a.aire.salidas;
        s["principal"].escena = "Otra";
        doc.ponerSalidas(s, false);
        CHECK(ba::persist::cola().vaciar(), "vaciar");
        CHECK(!fs::exists(d / "placa.json") && !fs::exists(d / "linea-aire.json") && !fs::exists(d / "al-aire.json"),
              "los archivos de la versión 1 se fueron");
        const DocumentoAire::Anterior b = arrancar(d);
        CHECK(b.aire.version == 2 && !b.deVersionVieja && b.aire.de("principal").placa.placa &&
                  b.aire.linea.value("evento", "") == "ev9" && b.aire.de("principal").escena == "Otra",
              "la versión 2 trae todo adentro");
        fs::remove_all(d);
    }
    {
        // Una línea vieja rota (sus dos generaciones): se sabe (la línea vuelve en HOLD).
        const fs::path d = carpeta("version1-linea-rota");
        escribir(d / "al-aire.json", R"({"streaming":false,"escena":"Plano","cerrado":false})");
        escribir(d / "linea-aire.A.json", "#BA-SNAP v1 seq=1 sha256=00\n{}");
        escribir(d / "linea-aire.B.json", "#BA-SNAP v1 seq=2 sha256=00\n{}");
        DocumentoAire doc(d);
        const DocumentoAire::Anterior a = doc.leerAnterior();
        CHECK(a.lineaDanada && !a.aire.linea.is_object() && !a.aire.cerrado, "la línea vieja rota: se sabe");
        fs::remove_all(d);
    }

    std::printf(g_fallos == 0 ? "TODO OK\n" : "%d FALLOS\n", g_fallos);
    return g_fallos == 0 ? 0 : 1;
}
