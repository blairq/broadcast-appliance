// ============================================================================
// src/app/tests/test_medios.cpp — los archivos del operador
//
//   1. El tipo sale de la extension, y los documentos (pdf, pptx) existen.
//   2. Aplanar: sin tildes, sin guiones bajos, en minusculas.
//   3. Listar: recorre las carpetas de medios y deja afuera lo que no sabemos
//      mostrar.
//   4. Filtrar: todas las palabras, en cualquier orden y en cualquier parte.
//   5. Resolver adentro: un `..` no puede salirse de la carpeta.
// ============================================================================
#include <cstdio>

#include <QDir>
#include <QFile>

#include "medios/Medios.hpp"

using namespace ba::app::medios;

namespace {

int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        }                                                                                                    \
    } while (false)

void tocar(const QString& ruta) {
    QDir().mkpath(QFileInfo(ruta).absolutePath());
    QFile f(ruta);
    f.open(QIODevice::WriteOnly);
    f.write("x");
}

// --- 1) Tipos ---------------------------------------------------------------
void tipos() {
    CHECK(tipoDe("logo.png") == Tipo::Imagen, "png es imagen");
    CHECK(tipoDe("clip.MP4") == Tipo::Video, "las mayusculas no importan");
    CHECK(tipoDe("tema.flac") == Tipo::Audio, "flac es audio");
    // Antes no estaban en ninguna lista: un orador mandaba su presentacion y
    // para el aparato ese archivo no existia.
    CHECK(tipoDe("charla.pdf") == Tipo::Documento, "pdf es documento");
    CHECK(tipoDe("charla.pptx") == Tipo::Documento, "pptx tambien");
    CHECK(tipoDe("notas.txt") == Tipo::Desconocido, "un texto no es medio");
    CHECK(tipoDe("sinextension") == Tipo::Desconocido, "sin extension tampoco");

    CHECK(sePuedeDesarmar("charla.pdf"), "de un pdf sale un carrusel");
    CHECK(sePuedeDesarmar("charla.pptx"), "de un pptx tambien (DeckCraft)");
    CHECK(!sePuedeDesarmar("charla.ppt"), "del ppt viejo no: DeckCraft no lo lee (se exporta a .pptx)");
    CHECK(!sePuedeDesarmar("logo.png"), "de una imagen sola no");
}

// --- 2) Aplanar -------------------------------------------------------------
void aplanar_() {
    CHECK(aplanar("Logo_Camión.png") == "logo camion png", "sin tildes, sin guion bajo, minuscula");
    CHECK(aplanar("  Dos   espacios  ") == "dos espacios", "los espacios de mas se van");
    CHECK(aplanar("a-b_c.d") == "a b c d", "guiones, guiones bajos y puntos separan");
    CHECK(aplanar("").isEmpty(), "vacio sigue vacio");
}

// --- 3) Listar --------------------------------------------------------------
void listar_() {
    const QString raiz = QDir::tempPath() + "/ba_test_medios";
    QDir(raiz).removeRecursively();
    tocar(raiz + "/Media/logos/Logo_Camión.png");
    tocar(raiz + "/Media/musica/tema.mp3");
    tocar(raiz + "/Media/charla.pdf");
    tocar(raiz + "/Media/notas.txt"); // no es medio
    tocar(raiz + "/Shares/de-un-invitado.jpg");
    tocar(raiz + "/AfueraDeTodo/x.png"); // fuera de las carpetas de medios

    const auto todos = listar(raiz);
    CHECK(todos.size() == 4, "encuentra los cuatro medios y deja el resto");
    bool hayTxt = false, hayAfuera = false;
    for (const Archivo& a : todos) {
        if (a.nombre == "notas.txt") hayTxt = true;
        if (a.relativa.contains("AfueraDeTodo")) hayAfuera = true;
    }
    CHECK(!hayTxt, "un .txt no entra: llenaria la grilla de ruido");
    CHECK(!hayAfuera, "lo que no esta en las carpetas de medios no se mira");

    const auto soloShares = listar(raiz, "Shares");
    CHECK(soloShares.size() == 1, "se puede pedir una sola carpeta");
    CHECK(soloShares[0].carpeta == "Shares", "y es la que se pidio");
    CHECK(listar("").isEmpty(), "sin raiz no explota");

    // --- 4) Filtrar ---------------------------------------------------------
    CHECK(filtrar(todos, "").size() == todos.size(), "sin consulta estan todos");
    // Buscar como habla una persona: sin tilde y con espacio donde hay guion.
    const auto camion = filtrar(todos, "logo camion");
    CHECK(camion.size() == 1, "«logo camion» encuentra «Logo_Camión.png»");
    // Todas las palabras tienen que estar: es lo que uno espera al ir
    // agregando palabras para achicar la lista.
    CHECK(filtrar(todos, "logo tema").isEmpty(), "si falta una palabra, no aparece");
    CHECK(filtrar(todos, "musica").size() == 1, "busca tambien por la carpeta");
    CHECK(filtrar(todos, "MUSICA").size() == 1, "sin importar mayusculas");

    QDir(raiz).removeRecursively();
}

// --- 5) No salirse ----------------------------------------------------------
void no_salirse() {
    const QString raiz = QDir::tempPath() + "/ba_test_medios2";
    QDir().mkpath(raiz);
    CHECK(!resolverAdentro(raiz, "Media/x.png").isEmpty(), "una ruta de adentro resuelve");
    CHECK(resolverAdentro(raiz, "../../etc/passwd").isEmpty(), "un .. no puede salirse");
    CHECK(resolverAdentro(raiz, "/etc/passwd").isEmpty(), "una absoluta de afuera tampoco");
    CHECK(!resolverAdentro(raiz, raiz + "/Media/x.png").isEmpty(), "una absoluta de adentro si");
    QDir(raiz).removeRecursively();
}

// --- 6) El arbol de carpetas ------------------------------------------------
void arbol_() {
    const QString raiz = QDir::tempPath() + "/ba_test_medios3";
    QDir(raiz).removeRecursively();
    QDir().mkpath(raiz + "/Media/logos/sponsors");
    QDir().mkpath(raiz + "/Media/musica");
    QDir().mkpath(raiz + "/Media/vacia");
    QDir().mkpath(raiz + "/Media/.oculta");
    tocar(raiz + "/Media/portada.png");
    tocar(raiz + "/Media/logos/uno.png");
    tocar(raiz + "/Media/logos/sponsors/dos.png");
    tocar(raiz + "/Media/logos/sponsors/tres.png");

    const QVector<Archivo> todos = listar(raiz);
    const QVector<Carpeta> a = arbol(raiz, todos);

    auto buscar = [&](const QString& rel) -> const Carpeta* {
        for (const Carpeta& c : a)
            if (c.relativa == rel) return &c;
        return nullptr;
    };
    CHECK(buscar("Media") != nullptr, "la carpeta del sistema esta");
    CHECK(buscar("Media")->raiz, "y esta marcada como raiz");
    // Una carpeta vacia TIENE que aparecer: si no, nadie puede arrastrarle
    // nada para empezar a ordenar.
    CHECK(buscar("Media/vacia") != nullptr, "una carpeta vacia aparece igual");
    CHECK(buscar("Media/.oculta") == nullptr, "una carpeta oculta no ensucia el arbol");
    CHECK(buscar("Media/logos/sponsors") != nullptr, "baja de nivel");
    CHECK(buscar("Media/logos/sponsors")->nivel == 2, "y sabe a que profundidad esta");
    // Propios contra total: es la diferencia entre «esta carpeta tiene uno» y
    // «adentro de esta carpeta hay tres».
    CHECK(buscar("Media/logos")->propios == 1, "cuenta los suyos");
    CHECK(buscar("Media/logos")->total == 3, "y los de sus hijas");
    CHECK(buscar("Media")->total == 4, "la raiz cuenta todo");

    // El orden es el de dibujo: cada carpeta DESPUES de su padre.
    int iLogos = -1, iSponsors = -1;
    for (int i = 0; i < a.size(); ++i) {
        if (a[i].relativa == "Media/logos") iLogos = i;
        if (a[i].relativa == "Media/logos/sponsors") iSponsors = i;
    }
    CHECK(iLogos >= 0 && iSponsors > iLogos, "una hija va despues de su madre");

    QDir(raiz).removeRecursively();
}

// --- 7) Mover, crear, renombrar, papelera -----------------------------------
void tocar_archivos() {
    const QString raiz = QDir::tempPath() + "/ba_test_medios4";
    QDir(raiz).removeRecursively();
    QDir().mkpath(raiz + "/Media/logos");
    tocar(raiz + "/Media/a.png");
    tocar(raiz + "/Media/logos/a.png"); // mismo nombre a proposito
    QString err;

    // Lo que NO se puede, que es la mitad del valor de esto.
    CHECK(!mover(raiz, "../afuera.png", "Media", &err), "no se mueve algo de afuera");
    CHECK(!mover(raiz, "Media", "Media/logos", &err), "una carpeta del sistema no se mueve");
    CHECK(!mover(raiz, "Media/logos", "Media/logos", &err), "ni una carpeta adentro de si misma");
    CHECK(!aPapelera(raiz, "Media", &err), "ni se borra una carpeta del sistema");
    CHECK(!crearCarpeta(raiz, "Media", "con/barra", &err), "un nombre con barra no vale");
    CHECK(!crearCarpeta(raiz, "Media", ".oculta", &err), "ni uno que empieza con punto");

    // Un nombre repetido se numera, no pisa: traer dos veces el mismo logo
    // corregido es normal, perder el anterior no.
    CHECK(mover(raiz, "Media/a.png", "Media/logos", &err), "mueve");
    CHECK(QFileInfo::exists(raiz + "/Media/logos/a.png"), "el que estaba sigue");
    CHECK(QFileInfo::exists(raiz + "/Media/logos/a (2).png"), "y el nuevo entro numerado");

    CHECK(crearCarpeta(raiz, "Media", "nueva", &err), "crea una carpeta");
    CHECK(!crearCarpeta(raiz, "Media", "nueva", &err), "no dos veces la misma");
    CHECK(renombrar(raiz, "Media/nueva", "otra", &err), "renombra");
    CHECK(QDir(raiz + "/Media/otra").exists(), "con el nombre nuevo");

    // Borrar es mover a la papelera: un archivo puede estar en una escena.
    CHECK(aPapelera(raiz, "Media/logos/a (2).png", &err), "a la papelera");
    CHECK(!QFileInfo::exists(raiz + "/Media/logos/a (2).png"), "ya no esta donde estaba");
    CHECK(QFileInfo::exists(raiz + "/Papelera/a (2).png"), "pero no se perdio");

    QDir(raiz).removeRecursively();
}

// --- 8) Ordenar -------------------------------------------------------------
void ordenar_() {
    QVector<Archivo> l;
    Archivo x;
    x.nombre = "zeta.png";
    x.bytes = 10;
    x.modificadoMs = 300;
    x.tipo = Tipo::Imagen;
    l.push_back(x);
    x.nombre = "alfa.mp4";
    x.bytes = 50;
    x.modificadoMs = 100;
    x.tipo = Tipo::Video;
    l.push_back(x);
    x.nombre = "beta.png";
    x.bytes = 30;
    x.modificadoMs = 200;
    x.tipo = Tipo::Imagen;
    l.push_back(x);

    QVector<Archivo> a = l;
    ordenar(a, "nombre");
    CHECK(a[0].nombre == "alfa.mp4", "por nombre");
    ordenar(a, "tamano", true);
    CHECK(a[0].bytes == 50, "por tamaño, el mas grande primero");
    ordenar(a, "fecha", true);
    CHECK(a[0].modificadoMs == 300, "por fecha, lo mas nuevo primero");
    a = l;
    ordenar(a, "loquesea");
    CHECK(a[0].nombre == "zeta.png", "un criterio que no existe no revuelve nada");
}

} // namespace

int main() {
    tipos();
    aplanar_();
    listar_();
    no_salirse();
    arbol_();
    tocar_archivos();
    ordenar_();
    if (g_fallos == 0) std::printf("test_medios: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
