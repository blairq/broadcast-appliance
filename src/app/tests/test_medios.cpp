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
    CHECK(sePuedeDesarmar("charla.ppt"), "del ppt viejo tambien");
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
    tocar(raiz + "/Media/notas.txt");          // no es medio
    tocar(raiz + "/Shares/de-un-invitado.jpg");
    tocar(raiz + "/AfueraDeTodo/x.png");       // fuera de las carpetas de medios

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

} // namespace

int main() {
    tipos();
    aplanar_();
    listar_();
    no_salirse();
    if (g_fallos == 0) std::printf("test_medios: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
