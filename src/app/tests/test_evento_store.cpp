// ============================================================================
// src/app/tests/test_evento_store.cpp — los eventos en el disco
//
//   1. Crear deja la carpeta con su esqueleto y un guion cargable.
//   2. Dos eventos con el mismo nombre no se pisan.
//   3. Guardar respalda el guion anterior antes de escribir.
//   4. Un guion dañado se reporta como tal, no como un evento vacio.
//   5. Nombres de carpeta: los caracteres que Windows no acepta se sacan.
//   6. Borrar no se deja llevar fuera de Eventos/.
//   7. Revisar los archivos: los que faltan, los vacios y los que se escapan
//      de la carpeta del evento.
//   8. Carpetas anidadas: crear adentro, listar en todas, mover, no borrar
//      una carpeta que agrupa.
//   9. El evento en curso: iniciar, terminar, uno solo a la vez, sobrevive a
//      mover; el automático del día y el cambio de día.
// ============================================================================

#include <algorithm>
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

void revisar_archivos() {
    EventoStore s(carpetaDePrueba());
    const std::string carpeta = s.crear("Acto");
    const fs::path dir = s.carpetaDe(carpeta);

    Evento e;
    Bloque b;
    b.nombre = "Todo";
    const auto cue = [](Tipo t, const char* clave, const char* valor) {
        Cue c;
        c.tipo = t;
        c.duracionSeg = 10;
        c.texto = "algo";
        c.datos = nlohmann::json{{clave, valor}};
        return c;
    };
    b.cues.push_back(cue(Tipo::Musica, "archivo", "himno.wav"));               // 0: existe
    b.cues.push_back(cue(Tipo::Video, "archivo", "Medios/institucional.mp4")); // 1: falta
    b.cues.push_back(cue(Tipo::Teleprompter, "discurso", "intendente.txt"));   // 2: vacio
    b.cues.push_back(cue(Tipo::Musica, "archivo", "../../se-escapa.wav"));     // 3: se escapa
    e.bloques.push_back(b);

    {
        std::ofstream(dir / "Medios" / "himno.wav") << "no es audio pero pesa";
        std::ofstream(dir / "Discursos" / "intendente.txt"); // queda vacio
    }

    const auto rs = revisarArchivos(e, dir);
    CHECK(rs.size() == 3, "encuentra los tres problemas y deja pasar el que esta");
    CHECK(rs[0].cue == 1 && rs[0].texto.find("falta el archivo") != std::string::npos,
          "avisa del archivo que no esta");
    CHECK(rs[1].cue == 2 && rs[1].texto.find("vacio") != std::string::npos,
          "un archivo de cero bytes es un problema, no un archivo");
    CHECK(rs[2].cue == 3 && rs[2].texto.find("fuera de") != std::string::npos,
          "un guion no puede hacer que se lea fuera de su carpeta");
    for (const Reparo& r : rs)
        CHECK(r.grave, "los tres impiden correr el guion");

    // Parar la musica no necesita archivo: no se revisa.
    Evento e2;
    Bloque b2;
    b2.nombre = "Parar";
    Cue parar = cue(Tipo::Musica, "accion", "parar");
    b2.cues.push_back(parar);
    e2.bloques.push_back(b2);
    CHECK(revisarArchivos(e2, dir).empty(), "parar la musica no busca ningun archivo");
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

void carpetas_anidadas() {
    EventoStore s(carpetaDePrueba());
    std::string err;
    CHECK(s.crearCarpeta("2026/Octubre", &err) == "2026/Octubre", "crea carpetas anidadas");
    CHECK(s.crearCarpeta("Consejo/Ordi:narias", &err) == "Consejo/Ordinarias", "limpia cada parte");
    CHECK(s.crearCarpeta("../afuera", &err) == "afuera", "«..» no sale de Eventos/");
    const std::string e = s.crear("Sesión ordinaria", &err, "2026/Octubre");
    CHECK(e == "2026/Octubre/Sesión-ordinaria", "crea el evento adentro de la carpeta");
    const std::string raiz = s.crear("Ensayo", &err);
    const auto fichas = s.listar();
    CHECK(fichas.size() == 2, "lista los eventos de todas las carpetas");
    bool vio = false;
    for (const auto& f : fichas)
        if (f.carpeta == e) vio = f.grupo == "2026/Octubre" && f.nombre == "Sesión ordinaria";
    CHECK(vio, "con su grupo");
    const auto cs = s.carpetas();
    CHECK(std::find(cs.begin(), cs.end(), "2026/Octubre") != cs.end(), "las carpetas que agrupan");
    CHECK(std::find(cs.begin(), cs.end(), e) == cs.end(), "un evento no es una carpeta que agrupa");
    CHECK(s.crearCarpeta(e + "/adentro", &err).empty(), "no se agrupa adentro de un evento");
    const std::string movido = s.mover(raiz, "Consejo/Ordinarias", &err);
    CHECK(movido == "Consejo/Ordinarias/Ensayo", "mueve un evento a otra carpeta");
    bool ok = false;
    s.cargar(movido, &ok);
    CHECK(ok, "y se sigue pudiendo cargar");
    CHECK(!s.borrar("2026", &err), "no borra una carpeta que agrupa");
    CHECK(s.borrar(e, &err), "borra un evento anidado");
}

void en_curso() {
    EventoStore s(carpetaDePrueba());
    std::string err;
    const std::string a = s.crear("Uno", &err);
    const std::string b = s.crear("Dos", &err, "Otros");
    CHECK(s.enCurso().empty(), "al principio no hay ninguno en curso");
    CHECK(s.iniciar(a, 1000, &err), "inicia uno");
    CHECK(s.enCurso() == a, "queda en curso");
    CHECK(s.datos(a).estado == "en_curso" && s.datos(a).iniciadoMs == 1000, "con su estado y hora");
    CHECK(s.iniciar(b, 2000, &err), "inicia otro");
    CHECK(s.datos(a).estado == "terminado" && s.datos(a).terminadoMs == 2000, "el anterior se termina solo");
    // Lo que no conoce se conserva.
    DatosDelEvento d = s.datos(b);
    d.resto["futuro"] = 7;
    d.etiquetas = {"con votación"};
    s.guardarDatos(b, d, &err);
    const std::string movido = s.mover(b, "2026", &err);
    CHECK(s.enCurso() == movido, "sigue en curso después de moverlo");
    CHECK(s.datos(movido).resto.value("futuro", 0) == 7, "ficha.json conserva las claves que no conoce");
    CHECK(s.datos(movido).etiquetas.size() == 1, "y las etiquetas");
    {
        EventoStore otra(s.raiz()); // la app que vuelve después de un corte de luz
        CHECK(otra.enCurso() == movido, "el en curso sobrevive a reabrir");
    }
    CHECK(s.terminar(3000, &err), "termina");
    CHECK(s.enCurso().empty() && s.datos(movido).estado == "terminado", "y no queda ninguno");
    CHECK(!s.terminar(3001, &err), "terminar sin ninguno avisa");
}

void del_dia() {
    EventoStore s(carpetaDePrueba());
    std::string err;
    const std::string hoy = s.asegurarDelDia("2026-10-07", 1000, &err);
    CHECK(hoy == "2026/Octubre/2026-10-07-jornada", "crea el del día en su carpeta");
    CHECK(s.enCurso() == hoy, "y lo pone en curso");
    CHECK(s.cargar(hoy).nombre == "Jornada del 07/10/2026", "con nombre legible");
    CHECK(s.datos(hoy).automatico && s.datos(hoy).dia == "2026-10-07", "marcado automático");
    CHECK(s.asegurarDelDia("2026-10-07", 2000, &err) == hoy, "el mismo día no crea otro");
    const std::string manana = s.asegurarDelDia("2026-10-08", 3000, &err);
    CHECK(manana == "2026/Octubre/2026-10-08-jornada", "a la medianoche pasa al de hoy");
    CHECK(s.datos(hoy).estado == "terminado", "y termina el de ayer");
    const std::string elegido = s.crear("Sesión especial", &err);
    s.iniciar(elegido, 4000, &err);
    CHECK(s.asegurarDelDia("2026-10-09", 5000, &err) == elegido, "uno elegido sigue aunque cambie el día");
    s.terminar(6000, &err);
    CHECK(s.asegurarDelDia("2026-10-08", 7000, &err) == manana, "vuelve a abrir el del día si ya existía");
    CHECK(s.datos(manana).iniciadoMs == 3000, "conservando cuándo empezó");
}

int main() {
    crear_y_listar();
    no_se_pisan();
    guardar_respalda();
    guion_danado();
    ultimo_tocado();
    revisar_archivos();
    nombres();
    borrar_no_se_escapa();
    carpetas_anidadas();
    en_curso();
    del_dia();
    std::error_code ec;
    fs::remove_all(fs::temp_directory_path() / "ba-test-eventos", ec);
    if (g_fallos == 0) std::printf("test_evento_store: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
