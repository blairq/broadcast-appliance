// src/app/tests/test_eventos_separar.cpp — cada evento a su carpeta (Configuraciones::separarEventos)
//
// Una carpeta con tres eventos adentro (compartiendo escenas y medios): se queda el
// en curso, los otros dos pasan a su carpeta con una copia de los ajustes y medios,
// sin grabaciones ni papelera, y con su evento en curso. La segunda vez no hace nada.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include <QCoreApplication>
#include <QThread>

#include "arranque/Configuraciones.hpp"
#include "evento/EventoStore.hpp"

namespace fs = std::filesystem;

static int g_failures = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_failures;                                                                                    \
        }                                                                                                    \
    } while (0)

static void escribir(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << s;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const fs::path datos = fs::temp_directory_path() / ("ba-separar-" + std::to_string(QCoreApplication::applicationPid()));
    fs::remove_all(datos);
    const fs::path a = datos / "Configuraciones" / "Principal";
    escribir(a / "Config" / "escenas.json", R"({"escenas": ["Principal"]})");
    escribir(a / "Config" / "orden-del-dia.json", R"({"puntos": [1]})");
    escribir(a / "Media" / "logo.txt", "logo");
    escribir(a / "Plugins" / "p.txt", "plugin");
    escribir(a / "Papelera" / "viejo.txt", "basura");
    escribir(a / "Recordings" / "grabacion.mp4", "video");

    ba::evento::EventoStore st(a / "Eventos");
    std::string err;
    const std::string uno = st.crear("Debate uno", &err);
    const std::string dos = st.crear("Sesión dos", &err);
    const std::string tres = st.crear("Prueba tres", &err);
    CHECK(!uno.empty() && !dos.empty() && !tres.empty(), "tres eventos en la misma carpeta");
    st.iniciar(dos, 1000);

    const auto hecho = ba::app::Configuraciones::separarEventos(datos);
    std::printf("%s\n", hecho.dump().c_str());
    CHECK(hecho.size() == 2, "se separan dos (el en curso se queda)");

    for (const char* n : {"Debate uno", "Prueba tres"}) {
        const fs::path d = datos / "Configuraciones" / n;
        CHECK(fs::exists(d / "Config" / "escenas.json"), "lleva las escenas");
        CHECK(fs::exists(d / "Media" / "logo.txt") && fs::exists(d / "Plugins" / "p.txt"), "lleva medios y plugins");
        CHECK(!fs::exists(d / "Recordings") && !fs::exists(d / "Papelera"), "sin grabaciones ni papelera");
        CHECK(!fs::exists(d / "Config" / "orden-del-dia.json"), "sin la sesión de donde salió");
        ba::evento::EventoStore nuevo(d / "Eventos");
        CHECK(nuevo.listar().size() == 1, "un solo evento adentro");
        CHECK(!nuevo.enCurso().empty(), "su evento queda en curso");
    }
    CHECK(fs::exists(a / "Eventos" / dos) && !fs::exists(a / "Eventos" / uno) && !fs::exists(a / "Eventos" / tres),
          "la de origen se queda con el en curso");
    CHECK(fs::exists(a / "Recordings" / "grabacion.mp4"), "las grabaciones se quedan donde estaban");
    CHECK(fs::exists(datos / "Respaldos"), "respaldó los eventos antes");
    CHECK(ba::app::Configuraciones::separarEventos(datos).empty(), "la segunda vez no hace nada");

    // Copiar un evento es copia TOTAL: la web, el guion, la sesión, el historial y las
    // grabaciones; menos lo de la app andando y el estado al aire.
    {
        qputenv("BA_DATA_ROOT", QByteArray::fromStdString(datos.string()));
        escribir(a / "Eventos" / dos / "Web" / "portada.html", "<h1>Mi portada</h1>");
        escribir(a / "Config" / "al-aire.json", R"({"transmitiendo": true})");
        escribir(a / "Logs" / "app.log", "registro");
        escribir(a / "Runtime" / "x.sock", "");
        ba::app::Configuraciones cf({});
        const std::string e = cf.nuevoEvento("Copia de trabajo", "Principal");
        CHECK(e.empty(), "copia el evento");
        const fs::path c = datos / "Configuraciones" / "Copia de trabajo";
        CHECK(fs::exists(c / "Eventos" / dos / "Web" / "portada.html"), "lleva la página web");
        CHECK(fs::exists(c / "Config" / "orden-del-dia.json"), "lleva la sesión (orden del día)");
        CHECK(fs::exists(c / "Recordings" / "grabacion.mp4") && fs::exists(c / "Papelera" / "viejo.txt"),
              "lleva las grabaciones y todo lo demás");
        CHECK(!fs::exists(c / "Config" / "al-aire.json"), "no sale al aire sola");
        CHECK(!fs::exists(c / "Logs") && !fs::exists(c / "Runtime"), "sin lo de la app andando");
        ba::evento::EventoStore copia(c / "Eventos");
        CHECK(copia.enCurso() == dos, "su evento en curso es el de la copia (no uno nuevo vacío)");

        // Borrar va a la papelera y se recupera con su nombre (o con otro si ya está usado).
        escribir(datos / "configuraciones.json", R"({"activa": "Principal"})");
        CHECK(!cf.borrar("Principal").empty(), "el abierto no se borra");
        CHECK(!cf.borrar("No existe").empty(), "uno que no existe, error");
        CHECK(cf.borrar("Copia de trabajo").empty(), "borra uno");
        CHECK(!fs::exists(c), "ya no está en la lista");
        const auto pap = cf.papelera();
        CHECK(pap.size() == 1 && pap[0]["nombre"] == "Copia de trabajo" && pap[0]["borrado_ms"].get<long long>() > 0,
              "queda en la papelera con su nombre y cuándo");
        const std::string id = pap[0]["id"].get<std::string>();
        CHECK(!cf.recuperar("../Principal").empty() && !cf.recuperar("nada").empty(), "solo lo que está en la papelera");
        fs::create_directories(c);
        CHECK(!cf.recuperar(id).empty(), "si el nombre está usado, no pisa");
        fs::remove_all(c);
        CHECK(cf.recuperar(id).empty() && fs::exists(c / "Recordings" / "grabacion.mp4"), "vuelve con todo lo suyo");
        CHECK(cf.papelera().empty(), "la papelera queda vacía");
        CHECK(cf.borrar("Copia de trabajo").empty() && cf.recuperar(cf.papelera()[0]["id"].get<std::string>(), "Otra").empty() &&
                  fs::exists(datos / "Configuraciones" / "Otra"),
              "se recupera con otro nombre");

        // Vaciar: en otro hilo; lo viejo se va al arrancar; lo que se copia no se borra.
        const auto esperar = [&cf]() {
            for (int i = 0; i < 200 && cf.vaciando(); ++i) {
                QCoreApplication::processEvents();
                QThread::msleep(10);
            }
        };
        CHECK(cf.borrar("Otra").empty(), "borra otra vez");
        const fs::path dirPap = datos / "Configuraciones" / ".papelera";
        fs::create_directories(dirPap / "Viejo-1000");          // borrado en 1970: muy viejo
        fs::create_directories(dirPap / "Medio.borrando");      // un vaciado que se cortó
        CHECK(cf.recuperar("Medio.borrando").size() > 0, "lo que quedó a medio borrar no se recupera");
        CHECK(cf.vaciarViejos(30) == 2, "lo de más de un mes (y lo de a medias) se va");
        esperar();
        CHECK(!fs::exists(dirPap / "Viejo-1000") && !fs::exists(dirPap / "Medio.borrando.borrando") && !fs::exists(dirPap / "Medio.borrando"),
              "borrado de verdad");
        CHECK(cf.papelera().size() == 1, "lo reciente se queda");
        CHECK(cf.vaciarPapelera("nada").size() > 0, "un id que no está, error");
        CHECK(cf.vaciarPapelera().empty(), "vacía todo");
        CHECK(!cf.vaciarPapelera().empty() || cf.vaciando(), "mientras vacía no arranca otra vez");
        esperar();
        CHECK(cf.papelera().empty() && fs::is_empty(dirPap), "la papelera queda vacía");
        CHECK(cf.vaciarPapelera() == "la papelera está vacía", "vacía, avisa");

        // Una copia en curso: ni el origen ni el destino se borran o renombran.
        escribir(a / "Recordings" / "grande.bin", std::string(4 << 20, 'x'));
        CHECK(cf.nuevoEventoAparte("Copia lenta", "Principal", false, {}).isEmpty(), "arranca la copia");
        escribir(datos / "configuraciones.json", R"({"activa": "Otro"})"); // que Principal no sea el abierto
        const bool copiando = !cf.copia().isEmpty();
        if (copiando) {
            CHECK(!cf.borrar("Principal").empty(), "el origen de la copia no se borra");
            CHECK(!cf.renombrar("Principal", "X").empty(), "ni se renombra");
            CHECK(!cf.borrar("Copia lenta").empty(), "ni el que se está creando");
        }
        for (int i = 0; i < 500 && !cf.copia().isEmpty(); ++i) {
            QCoreApplication::processEvents();
            QThread::msleep(10);
        }
        CHECK(cf.copia().isEmpty() && fs::exists(datos / "Configuraciones" / "Copia lenta"), "terminó la copia");
        CHECK(cf.borrar("Principal").empty(), "después sí se borra");
    }

    fs::remove_all(datos);
    if (g_failures == 0) std::printf("OK\n");
    return g_failures == 0 ? 0 : 1;
}
