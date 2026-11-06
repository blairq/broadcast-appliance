// test_archivos_prueba.cpp — el disco y el probador de las pruebas de sala y de asamblea
// (sala/ArchivosPrueba): carpetas, credenciales solo para el dueño, el informe de hinchada,
// borrar, lo que quedó de antes por la cola, y cortar el probador sin esperarlo (sin que
// quede zombi ni huérfano).

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QPointer>
#include <QProcess>

#include <cstdio>
#include <filesystem>
#include <fstream>

#ifndef _WIN32
#include <signal.h>
#include <sys/stat.h>
#endif

#include "persist/ColaEscritura.hpp"
#include "sala/ArchivosPrueba.hpp"
#include "sala/HistorialPruebas.hpp"

namespace fs = std::filesystem;
namespace ar = ba::app::sala::archivos;
using json = nlohmann::json;

namespace {
int g_fallas = 0;
#define CHECK(c, m)                                             \
    do {                                                        \
        if (!(c)) {                                             \
            std::printf("FALLO: %s (linea %d)\n", m, __LINE__); \
            ++g_fallas;                                         \
        } else {                                                \
            std::printf("ok: %s\n", m);                         \
        }                                                       \
    } while (0)
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const fs::path base = fs::temp_directory_path() / ("ba_test_archivos_prueba_" + std::to_string(QCoreApplication::applicationPid()));
    fs::remove_all(base);

    // Carpetas.
    CHECK(ar::crearCarpetas({base / "a" / "b", base / "grabado", fs::path()}).empty(), "crea las carpetas (y las de arriba)");
    CHECK(fs::is_directory(base / "a" / "b") && fs::is_directory(base / "grabado"), "están");
    {
        std::ofstream(base / "archivo") << "x";
        CHECK(!ar::crearCarpetas({base / "archivo" / "adentro"}).empty(), "donde no se puede: el error");
    }

    // Credenciales: con su carpeta, solo para el dueño.
    const fs::path cred = base / "prueba-asamblea-1" / "integrantes.json";
    CHECK(ar::escribirCredenciales(cred, R"({"integrantes":[]})").empty(), "escribe las credenciales");
#ifndef _WIN32
    struct stat st {};
    CHECK(::stat(cred.c_str(), &st) == 0 && (st.st_mode & 0077) == 0, "solo las lee el dueño (0600)");
#endif
    ar::borrar(cred);
    CHECK(!fs::exists(cred), "borrar");
    ar::borrar(cred);
    ar::borrar(fs::path());
    CHECK(true, "borrar lo que no está (o nada): sin error");

    // El informe de hinchada.
    CHECK(ar::informeHinchada(base / "nada").empty(), "sin informe: objeto vacío");
    CHECK(ar::informeHinchada(fs::path()).empty(), "sin carpeta: objeto vacío");
    ar::crearCarpetas({base / "salida" / "hinchada"});
    std::ofstream(base / "salida" / "hinchada" / "informe.json") << "no es json";
    CHECK(ar::informeHinchada(base / "salida").empty(), "un informe roto: objeto vacío");
    std::ofstream(base / "salida" / "hinchada" / "informe.json")
        << R"({"asamblea":{"entraron":4},"integrantes":[{"id":"p1"}],"resumen":{"ms":3},"otro":1})";
    const json h = ar::informeHinchada(base / "salida");
    CHECK(h.value("asamblea", json::object()).value("entraron", 0) == 4 && h["integrantes"].size() == 1 &&
              h.contains("resumen") && h["notas"].is_array() && !h.contains("otro") &&
              h.value("carpeta", std::string()) == (base / "salida" / "hinchada").string(),
          "el informe: lo que va al de la app");

    // Lo nuevo de una consola que otro va escribiendo: hasta el último fin de línea.
    {
        const fs::path consola = base / "consola.txt";
        CHECK(ar::leerLineasDesde(consola, 0).texto.empty() && ar::leerLineasDesde(consola, 0).hasta == 0, "sin archivo: nada");
        std::ofstream(consola, std::ios::binary) << "uno\ndos\ntr"; // binario: en Windows el modo texto escribe \r\n
        ar::Tramo t = ar::leerLineasDesde(consola, 0);
        CHECK(t.texto == "uno\ndos\n" && t.hasta == 8, "lee las líneas enteras (la de la mitad, la próxima)");
        std::ofstream(consola, std::ios::binary | std::ios::app) << "es\ncuatro\n";
        t = ar::leerLineasDesde(consola, t.hasta);
        CHECK(t.texto == "tres\ncuatro\n", "sigue desde donde quedó");
        const ar::Tramo nada = ar::leerLineasDesde(consola, t.hasta);
        CHECK(nada.texto.empty() && nada.hasta == t.hasta, "sin nada nuevo: nada");
        std::ofstream(consola, std::ios::binary) << "otra\n";
        CHECK(ar::leerLineasDesde(consola, t.hasta).texto == "otra\n", "si lo volvieron a empezar, desde el principio");
        std::ofstream(consola) << "1234567890";
        CHECK(ar::leerLineasDesde(consola, 0, 4).texto == "1234", "una línea más larga que el tope: se toma igual");
    }

    // Lo que quedó de antes, por la cola (lo encolado se ve aunque no esté en el disco).
    const fs::path enCurso = base / "Runtime" / "prueba-en-curso.json";
    CHECK(ar::leerJsonEncolado(enCurso).is_null(), "sin archivo: null");
    ba::persist::cola().escribir(enCurso, R"({"inicioMs":5,"archivo":"x"})");
    CHECK(ar::leerJsonEncolado(enCurso).value("inicioMs", 0) == 5, "lo encolado");
    ba::persist::cola().borrar(enCurso);
    CHECK(ar::leerJsonEncolado(enCurso).is_null(), "borrado encolado: null");
    ba::persist::cola().vaciar();

    // El historial de pruebas de la máquina (sala/HistorialPruebas): lo común, sin lo privado,
    // la lista (con los de capacidad de antes), leer uno, comparar y el CSV.
    {
        namespace hi = ba::app::sala::historial;
        json sala{{"inicioMs", 1700000000000LL}, {"pedidos", 40}, {"conVideo", 40}, {"maquina", "mac-a"},
                  {"credencial", "SECRETO"}, {"reparto", {{"codigo", "BAGEN1.x"}, {"comando", "HINCHADA_CLAVE=x"}}},
                  {"picos", {{"cpuApp", 50}, {"cpuGenerador", 20}}}, {"probador", {{"decodificar", {{"modo", "nada"}}}}},
                  {"fichas", {{"local", {{"nombre", "mac-a"}, {"cpu", {{"modelo", "M1 Pro"}}}}}}}};
        const json s1 = hi::sobre(sala, "sala", "2.1.0", 5);
        CHECK(s1["tipo"] == "sala" && s1["programa"]["version"] == "2.1.0" && s1["fecha"] == 1700000000000LL, "lo común: tipo, versión, fecha");
        CHECK(!s1.contains("credencial") && !s1["reparto"].contains("codigo") && !s1["reparto"].contains("comando"),
              "sin credenciales, códigos ni líneas con la clave");
        CHECK(hi::nombreValido("20261105-101010-mac-a-sala.json") && !hi::nombreValido("../x.json") && !hi::nombreValido("a/b.json") &&
                  !hi::nombreValido("x.txt"),
              "los nombres: sin carpetas ni «..»");
        const fs::path datos = base / "datos";
        ba::persist::cola().escribir(datos / hi::kCarpeta / "20231114-221320-mac-a-sala.json", s1.dump());
        json cap{{"tipo", "local"}, {"fecha", 1600000000000LL}, {"maquina", "mac-a"}, {"tope", 80},
                 {"resultado", {{"conMargen", 60}, {"maximo", 80}}}, {"frase", "Aguanta 60"}};
        ba::persist::cola().escribir(datos / hi::kCarpetaVieja / "20200913-122640-mac-a-local.json", cap.dump());
        ba::persist::cola().escribir(datos / hi::kCarpeta / "roto.json", "no es json");
        ba::persist::cola().vaciar();
        const json lista = hi::listar(datos);
        CHECK(lista.size() == 2 && lista[0]["tipo"] == "sala" && lista[1]["tipo"] == "local", "la lista: los dos (el roto no), lo nuevo primero");
        CHECK(lista[0]["participantes"] == 40 && lista[0]["decodificar"] == "nada" && lista[1]["conMargen"] == 60,
              "con lo que se muestra de cada uno");
        CHECK(hi::leer(datos, "20200913-122640-mac-a-local.json").value("tope", 0) == 80, "leer uno (de la carpeta vieja también)");
        CHECK(hi::leer(datos, "../secreto.json").is_null(), "leer: un nombre que no vale, nada");
        const json cmp = hi::comparar(s1, cap);
        bool hayCpu = false, hayTipo = false;
        for (const json& f : cmp) {
            if (f["clave"] == "cpuAppPico" && f["a"] == 50) hayCpu = true;
            if (f["clave"] == "tipo" && f["a"] == "sala" && f["b"] == "local") hayTipo = true;
        }
        CHECK(hayCpu && hayTipo, "comparar: lado a lado");
        const std::string c = hi::csv(lista);
        CHECK(c.rfind("fecha,tipo,maquina", 0) == 0 && c.find("Aguanta 60") != std::string::npos, "el CSV");
    }

#ifndef _WIN32
    // El probador: se corta sin esperarlo, y no queda ni zombi ni huérfano.
    {
        auto* dueno = new QObject;
        auto* p = new QProcess(dueno);
        bool avisoAlDueno = false;
        QObject::connect(p, &QProcess::finished, dueno, [&]() { avisoAlDueno = true; });
        p->start("/bin/sleep", {"30"});
        CHECK(p->waitForStarted(5000), "arrancó el hijo");
        const qint64 pid = p->processId();
        QPointer<QProcess> vivo(p);
        QElapsedTimer t;
        t.start();
        ar::soltarProceso(p, dueno);
        delete dueno; // el dueño se va (como el modelo al cerrar el programa)
        const qint64 ms = t.elapsed();
        CHECK(ms < 200, "soltar + destruir al dueño: sin esperar al hijo");
        CHECK(vivo, "el QProcess no se destruyó con el dueño (~QProcess esperaría)");
        // Con el bucle de eventos andando: el hijo termina, se recoge y el QProcess se borra solo.
        QElapsedTimer espera;
        espera.start();
        while (espera.elapsed() < 5000 && (vivo || ::kill(static_cast<pid_t>(pid), 0) == 0)) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
        CHECK(!vivo, "el QProcess se borró solo al terminar");
        CHECK(::kill(static_cast<pid_t>(pid), 0) != 0, "el hijo no existe más (ni zombi)");
        CHECK(!avisoAlDueno, "al dueño no le llegó nada después de soltarlo");
    }
#endif
    ar::soltarProceso(nullptr, nullptr);

    fs::remove_all(base);
    std::printf(g_fallas ? "\n%d fallas\n" : "\ntodo ok\n", g_fallas);
    return g_fallas ? 1 : 0;
}
