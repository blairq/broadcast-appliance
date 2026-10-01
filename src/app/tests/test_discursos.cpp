// test_discursos.cpp — los textos del teleprompter, en carpetas
//
//   1. Contar palabras: la MISMA cuenta que el gateway (de ella sale la
//      duracion estimada que se muestra antes de mandar el texto).
//   2. Nombres que sirvan en Windows y en Linux, y nombres que no sirven.
//   3. Nada sale de la raiz: "..", rutas absolutas, enlaces raros.
//   4. Crear, listar (solo la carpeta que se mira, no las de adentro),
//      leer, escribir, renombrar, mover, borrar.
//   5. Los choques de nombre se avisan en vez de pisar un discurso.
//   6. Sin raiz (sin evento abierto) no se toca el disco.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cstdio>

#include "ui/Discursos.hpp"

using ba::app::Discursos;

namespace {

int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (0)

bool existe(const QString& p) { return QFile::exists(p); }

QString nombreDe(const QVariantList& l, int i) { return l.at(i).toMap().value("nombre").toString(); }

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    std::printf("== test_discursos ==\n");

    // --- 1) contar palabras ---------------------------------------------------
    CHECK(Discursos::contarPalabras("") == 0, "vacio: cero");
    CHECK(Discursos::contarPalabras("   \n\t ") == 0, "solo espacios: cero");
    CHECK(Discursos::contarPalabras("hola") == 1, "una");
    CHECK(Discursos::contarPalabras("  hola   mundo  ") == 2, "los espacios de mas no cuentan");
    CHECK(Discursos::contarPalabras("uno\ndos\n\ntres") == 3, "los renglones separan");

    // --- 2) nombres ------------------------------------------------------------
    CHECK(Discursos::limpiarNombre("intendente") == "intendente.txt", "le pone .txt");
    CHECK(Discursos::limpiarNombre("intendente.txt") == "intendente.txt", "no lo repite");
    CHECK(Discursos::limpiarNombre("notas.md") == "notas.md", "markdown tambien vale");
    CHECK(Discursos::limpiarNombre("  apertura  ") == "apertura.txt", "se recorta");
    CHECK(Discursos::limpiarNombre(R"(a/b\c:d?e*f)") == "abcdef.txt", "saca lo que Windows no admite");
    CHECK(Discursos::limpiarNombre("carpeta", false) == "carpeta", "una carpeta no lleva extension");
    // Un punto al final: Windows lo tira en silencio y el archivo queda con
    // otro nombre que el que se pidio.
    CHECK(Discursos::limpiarNombre("apertura...", false) == "apertura", "saca los puntos del final");
    CHECK(Discursos::limpiarNombre("").isEmpty(), "vacio no sirve");
    CHECK(Discursos::limpiarNombre("///").isEmpty(), "si no queda nada, no sirve");
    CHECK(Discursos::limpiarNombre("...").isEmpty(), "solo puntos, tampoco");

    QTemporaryDir tmp;
    const QString raiz = tmp.path() + "/Discursos";
    QDir().mkpath(raiz);
    Discursos d(raiz);

    // --- 3) nada sale de la raiz ------------------------------------------------
    CHECK(d.ruta("intendente.txt").startsWith(raiz), "adentro, bien");
    CHECK(d.ruta("../secreto.txt").isEmpty(), "'..' no");
    CHECK(d.ruta("Apertura/../../secreto.txt").isEmpty(), "'..' escondido en el medio, tampoco");
    CHECK(d.ruta("/etc/passwd").isEmpty(), "una ruta absoluta no");
    CHECK(d.ruta("").isEmpty(), "vacio no");
    CHECK(d.borrar("../../algo") == "No encuentro ese discurso.", "borrar afuera no hace nada");
    CHECK(!d.escribir("../afuera.txt", "x").isEmpty(), "escribir afuera se niega");
    CHECK(!existe(tmp.path() + "/afuera.txt"), "y no escribio nada");

    // --- 4) crear, listar, leer, escribir ---------------------------------------
    QString rel;
    CHECK(d.crear("", "intendente", &rel).isEmpty() && rel == "intendente.txt", "crear en la raiz");
    CHECK(existe(raiz + "/intendente.txt"), "el archivo esta");
    CHECK(d.crearCarpeta("", "Apertura", &rel).isEmpty() && rel == "Apertura", "crear una carpeta");
    CHECK(d.crear("Apertura", "palabras", &rel).isEmpty() && rel == "Apertura/palabras.txt",
          "crear adentro de una carpeta");

    CHECK(d.escribir("Apertura/palabras.txt", "Buenas noches a todos.\n\nGracias por venir.").isEmpty(),
          "escribir");
    bool ok = false;
    CHECK(d.leer("Apertura/palabras.txt", &ok).startsWith("Buenas noches") && ok, "leer lo escrito");
    d.leer("no-existe.txt", &ok);
    CHECK(!ok, "leer lo que no esta avisa");

    const QStringList carpetas = d.carpetas();
    CHECK(carpetas.size() == 2 && carpetas.contains("") && carpetas.contains("Apertura"),
          "la raiz y la carpeta");

    // Listar da SOLO lo de esa carpeta: si la raiz mostrara lo de adentro, un
    // evento con varias carpetas seria una sola lista larga.
    QVariantList l = d.listar("");
    CHECK(l.size() == 1 && nombreDe(l, 0) == "intendente.txt", "la raiz no muestra lo de las subcarpetas");
    l = d.listar("Apertura");
    CHECK(l.size() == 1 && nombreDe(l, 0) == "palabras.txt", "la carpeta muestra lo suyo");
    CHECK(l.at(0).toMap().value("palabras").toInt() == 7, "con su cuenta de palabras");
    CHECK(l.at(0).toMap().value("rel").toString() == "Apertura/palabras.txt", "y su ruta relativa");
    CHECK(d.listar("../..").isEmpty(), "listar afuera no devuelve nada");

    // Lo que no es un discurso no aparece: la tablet muestra el texto tal cual
    // y un .docx ahi no se puede leer.
    QFile otro(raiz + "/planilla.xlsx");
    otro.open(QIODevice::WriteOnly);
    otro.close();
    CHECK(d.listar("").size() == 1, "un archivo que no es texto no aparece");

    // --- 5) choques de nombre ----------------------------------------------------
    CHECK(d.crear("", "intendente") == "Ya hay un discurso que se llama así.", "no pisa uno que ya estaba");
    CHECK(d.crearCarpeta("", "Apertura") == "Ya hay una carpeta que se llama así.", "ni una carpeta");
    CHECK(d.crear("", "") == "Ponele un nombre.", "sin nombre avisa");

    // --- renombrar y mover -------------------------------------------------------
    CHECK(d.renombrar("Apertura/palabras.txt", "apertura-final", &rel).isEmpty() &&
              rel == "Apertura/apertura-final.txt",
          "renombrar deja el archivo en su carpeta");
    CHECK(existe(raiz + "/Apertura/apertura-final.txt"), "y con el nombre nuevo");
    CHECK(d.renombrar("Apertura/apertura-final.txt", "apertura-final").isEmpty(),
          "renombrar al mismo nombre no es un error");
    CHECK(d.renombrar("no-existe.txt", "x") == "No encuentro ese discurso.", "renombrar lo que no esta");

    CHECK(d.mover("intendente.txt", "Apertura", &rel).isEmpty() && rel == "Apertura/intendente.txt",
          "mover a una carpeta");
    CHECK(existe(raiz + "/Apertura/intendente.txt") && !existe(raiz + "/intendente.txt"), "se movio");
    CHECK(d.mover("Apertura/intendente.txt", "", &rel).isEmpty() && rel == "intendente.txt",
          "y de vuelta a la raiz");
    d.crear("Apertura", "intendente");
    CHECK(d.mover("intendente.txt", "Apertura") == "Allá ya hay un discurso que se llama así.",
          "mover sobre uno que ya esta se niega");
    CHECK(existe(raiz + "/intendente.txt"), "y el original sigue donde estaba");

    CHECK(d.borrar("intendente.txt").isEmpty() && !existe(raiz + "/intendente.txt"), "borrar");
    CHECK(d.borrar("intendente.txt") == "No encuentro ese discurso.", "borrar dos veces avisa");

    // --- 6) sin raiz --------------------------------------------------------------
    Discursos vacio;
    CHECK(!vacio.listo(), "sin raiz no esta listo");
    CHECK(vacio.carpetas().isEmpty() && vacio.listar("").isEmpty(), "no lista nada");
    CHECK(vacio.ruta("x.txt").isEmpty(), "no resuelve rutas");
    CHECK(!vacio.crear("", "x").isEmpty(), "no crea");
    CHECK(!vacio.escribir("x.txt", "y").isEmpty(), "no escribe");

    if (g_fallos == 0) {
        std::printf("test_discursos: OK\n");
        return 0;
    }
    std::printf("test_discursos: %d fallo(s)\n", g_fallos);
    return 1;
}
