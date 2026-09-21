// ============================================================================
// src/app/tests/test_evento_store.cpp — los eventos en el disco
//
//   1. Crear deja la carpeta con su esqueleto y un guion cargable.
//   2. Dos eventos con el mismo nombre no se pisan.
//   3. Guardar respalda el guion anterior antes de escribir.
//   4. Un guion dañado se reporta como tal, no como un evento vacio.
//   5. Nombres de carpeta: los caracteres que Windows no acepta se sacan.
//   6. Borrar no se deja llevar fuera de Eventos/.
// ============================================================================

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "evento/EventoStore.hpp"

namespace fs = std::filesystem;
using namespace ba::evento;

namespace {

int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (0)

fs::path carpetaDePrueba() {
    const fs::path p = fs::temp_directory_path() / "ba-test-eventos";
    std::error_code ec;
    fs::remove_all(p, ec);
    return p;
}

Evento conUnBloque() {
    Evento e;
    e.nombre = "Acto";
    Bloque b;
    b.id = "b1";
    b.nombre = "Apertura";
    Cue c;
    c.id = "c1";
    c.tipo = Tipo::Escena;
    c.duracionSeg = 30;
    c.datos = nlohmann::json{{"escena", "Placa"}};
    b.cues.push_back(c);
    e.bloques.push_back(b);
    return e;
}

void crear_y_listar() {
    EventoStore s(carpetaDePrueba());
    CHECK(s.listar().empty(), "al principio no hay eventos");

    std::string err;
    const std::string carpeta = s.crear("Acto del 25 de mayo", &err);
    CHECK(!carpeta.empty(), "se crea el evento");
    CHECK(err.empty(), "sin error");

    const fs::path dir = s.carpetaDe(carpeta);
    CHECK(fs::exists(dir / "evento.json"), "deja el guion escrito");
    CHECK(fs::is_directory(dir / "Medios"), "y la carpeta de medios");
    CHECK(fs::is_directory(dir / "Discursos"), "y la de discursos");
    CHECK(fs::is_directory(dir / "Respaldos"), "y la de respaldos");

    bool ok = false;
    const Evento e = s.cargar(carpeta, &ok);
    CHECK(ok, "el guion recien creado se puede cargar");
    CHECK(e.nombre == "Acto del 25 de mayo", "guarda el nombre tal cual se escribio");

    const auto fichas = s.listar();
    CHECK(fichas.size() == 1, "aparece en la lista");
    CHECK(fichas[0].nombre == "Acto del 25 de mayo", "con su nombre");
    CHECK(fichas[0].cues == 0, "recien creado no tiene cues");

    // Una carpeta sin guion no es un evento.
    std::error_code ec;
    fs::create_directories(s.raiz() / "carpeta-cualquiera", ec);
    CHECK(s.listar().size() == 1, "una carpeta sin guion no se cuenta como evento");
}

void no_se_pisan() {
    EventoStore s(carpetaDePrueba());
    const std::string a = s.crear("Acto");
    const std::string b = s.crear("Acto");
    CHECK(!a.empty() && !b.empty(), "los dos se crean");
    CHECK(a != b, "el segundo no pisa al primero");
    CHECK(s.listar().size() == 2, "quedan los dos");
}

void guardar_respalda() {
    EventoStore s(carpetaDePrueba());
    const std::string carpeta = s.crear("Acto");

    CHECK(s.guardar(carpeta, conUnBloque()), "guarda el guion con un bloque");
    bool ok = false;
    Evento leido = s.cargar(carpeta, &ok);
    CHECK(ok && leido.bloques.size() == 1, "se lee lo que se guardo");
    CHECK(leido.duracionSeg() == 30, "con sus tiempos");

    // El segundo guardado respalda el primero.
    Evento otro = conUnBloque();
    otro.bloques[0].cues[0].duracionSeg = 60;
    CHECK(s.guardar(carpeta, otro), "guarda de nuevo");
    leido = s.cargar(carpeta, &ok);
    CHECK(leido.duracionSeg() == 60, "queda el nuevo");

    int respaldos = 0;
    std::error_code ec;
    for (const auto& d : fs::directory_iterator(s.carpetaDe(carpeta) / "Respaldos", ec)) {
        (void)d;
        ++respaldos;
    }
    CHECK(respaldos >= 1, "el guion anterior quedo respaldado");

    CHECK(!s.guardar("no-existe", otro), "no guarda en una carpeta que no existe");
}

void guion_danado() {
    EventoStore s(carpetaDePrueba());
    const std::string carpeta = s.crear("Acto");
    {
        std::ofstream out(s.carpetaDe(carpeta) / "evento.json", std::ios::trunc);
        out << "{ esto no es json";
    }
    bool ok = true;
    std::string err;
    const Evento e = s.cargar(carpeta, &ok, &err);
    CHECK(!ok, "un guion roto no se carga como si nada");
    CHECK(!err.empty(), "y dice por que");
    CHECK(e.bloques.empty(), "no inventa contenido");
    CHECK(s.listar().empty(), "tampoco aparece en la lista como evento sano");
}

void ultimo_tocado() {
    EventoStore s(carpetaDePrueba());
    CHECK(s.ultimoTocado().empty(), "sin eventos no hay ultimo");

    const std::string viejo = s.crear("Acto viejo");
    // La fecha del archivo tiene la resolucion del sistema: se la fuerza para
    // que la prueba no dependa de cuanto tarda en correr.
    std::error_code ec;
    const auto ahora = fs::last_write_time(s.carpetaDe(viejo) / "evento.json", ec);
    const std::string nuevo = s.crear("Acto nuevo");
    fs::last_write_time(s.carpetaDe(viejo) / "evento.json", ahora - std::chrono::hours(48), ec);

    // Ojo: los tics del reloj de archivos son NEGATIVOS en libstdc++ (epoca
    // 2174). Comparar contra cero o contra -1 hacia que no ganara ninguno y
    // no se abriera nada.
    CHECK(s.ultimoTocado() == nuevo, "el ultimo tocado es el que se guardo despues");

    s.guardar(viejo, conUnBloque());
    CHECK(s.ultimoTocado() == viejo, "guardar el otro lo pone primero");
}

void nombres() {
    CHECK(nombreDeCarpeta("Acto del 25 de mayo") == "Acto-del-25-de-mayo", "los espacios pasan a guiones");
    CHECK(nombreDeCarpeta("Con/barra\\y:dos*puntos") == "Conbarraydospuntos",
          "saca lo que Windows no acepta");
    CHECK(nombreDeCarpeta("  ").empty(), "un nombre sin nada usable queda vacio");
    CHECK(nombreDeCarpeta("punto final.").back() != '.', "no termina en punto (Windows no lo crea)");

    EventoStore s(carpetaDePrueba());
    std::string err;
    CHECK(s.crear("///", &err).empty(), "no crea un evento con un nombre inservible");
    CHECK(!err.empty(), "y dice por que");
}

void borrar_no_se_escapa() {
    EventoStore s(carpetaDePrueba());
    const std::string carpeta = s.crear("Acto");
    std::string err;
    CHECK(!s.borrar("../otra-cosa", &err), "no sale de Eventos/ por arriba");
    CHECK(!s.borrar("sub/carpeta", &err), "ni por abajo");
    CHECK(!s.borrar("", &err), "ni con un nombre vacio");
    CHECK(s.borrar(carpeta, &err), "borra el que se le pide");
    CHECK(s.listar().empty(), "y deja de estar");
}

} // namespace

int main() {
    crear_y_listar();
    no_se_pisan();
    guardar_respalda();
    guion_danado();
    ultimo_tocado();
    nombres();
    borrar_no_se_escapa();
    std::error_code ec;
    fs::remove_all(fs::temp_directory_path() / "ba-test-eventos", ec);
    if (g_fallos == 0) std::printf("test_evento_store: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
