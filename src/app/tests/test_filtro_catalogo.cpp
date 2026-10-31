// test_filtro_catalogo.cpp
//
// FiltroCatalogo (ui/FiltroCatalogo): la única búsqueda de los catálogos (filtros de
// audio, módulos de video, herramientas de la Botonera).
//   1. Sin buscar: todo lo que cumple `exigir`, en el orden del catálogo; la categoría
//      acota; `categorias`, `total` y `colores` salen de lo que cumple.
//   2. Buscando: todas las palabras, sin tildes ni mayúsculas, en los campos (también
//      listas); `cuentas` por categoría; la cabeza suma más.
//   3. Con puntajes (el índice del asistente): entran esos, con su porqué; agrupando,
//      la categoría con el mejor primero.

#include <QCoreApplication>

#include <cstdio>

#include "ui/FiltroCatalogo.hpp"

using ba::app::FiltroCatalogo;

namespace {
int g_failures = 0;
#define CHECK(cond)                                                                                          \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (linea %d)\n", #cond, __LINE__);                                          \
            ++g_failures;                                                                                    \
        }                                                                                                    \
    } while (0)

QString nombres(const FiltroCatalogo& f) {
    QStringList l;
    for (const QVariant& v : f.lista()) l << v.toMap().value("nombre").toString();
    return l.join(',');
}
void vuelta() { QCoreApplication::processEvents(); }
QVariantMap item(const char* n, const char* cat, const char* desc, bool vivo, QVariantList etiquetas = {}) {
    return {{"nombre", n}, {"categoria", cat}, {"descripcion", desc}, {"vivo", vivo}, {"etiquetas", etiquetas},
            {"color", QString(cat) == "Dinámica" ? "#ff6a2b" : "#0ea5e9"}};
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    FiltroCatalogo f;
    f.setClave("nombre");
    f.setProperty("campos", QStringList{"descripcion", "etiquetas", "categoria"});
    f.setProperty("camposCabeza", QStringList{"nombre"});
    f.setFuente({item("compresor", "Dinámica", "Achica el rango", true, {"voz"}),
                 item("compuerta", "Dinámica", "Corta el ruido de fondo", true),
                 item("ecualizador", "Ecualizadores", "Graves y agudos de la voz", false),
                 item("limitador", "Dinámica", "El techo: nada pasa de ahí", false)});
    vuelta();
    CHECK(nombres(f) == "compresor,compuerta,ecualizador,limitador");
    CHECK(f.total() == 4 && f.categorias() == QStringList({"Dinámica", "Ecualizadores"}));
    CHECK(f.colores().value("Dinámica").toString() == "#ff6a2b");

    f.setProperty("exigir", QStringList{"vivo"});
    vuelta();
    CHECK(nombres(f) == "compresor,compuerta" && f.total() == 2 && f.categorias() == QStringList({"Dinámica"}));
    f.setProperty("exigir", QStringList{});
    f.setProperty("categoria", "Ecualizadores");
    vuelta();
    CHECK(nombres(f) == "ecualizador");

    // --- 2) Buscando
    f.setProperty("categoria", "");
    f.setProperty("texto", "VOZ");
    vuelta();
    CHECK(nombres(f) == "compresor,ecualizador"); // en las etiquetas y en la descripción
    CHECK(f.cuentas().value("Dinámica").toInt() == 1 && f.cuentas().value("Ecualizadores").toInt() == 1);
    f.setProperty("texto", "dinamica ruido");
    vuelta();
    CHECK(nombres(f) == "compuerta"); // sin tilde, todas las palabras
    f.setProperty("texto", "compu");
    vuelta();
    CHECK(f.lista().value(0).toMap().value("puntos").toDouble() == 3); // en la cabeza
    CHECK(FiltroCatalogo::plano("Cámara_Uno") == "camara uno");

    // --- 3) Con puntajes y agrupando
    f.setProperty("agrupar", true);
    f.setProperty("texto", "algo");
    f.setProperty("puntajes", QVariantMap{{"limitador", QVariantMap{{"puntaje", 2.0}, {"porque", QVariantList{"techo"}}}},
                                          {"ecualizador", QVariantMap{{"puntaje", 9.0}, {"porque", QVariantList{"eq"}}}},
                                          {"compresor", QVariantMap{{"puntaje", 5.0}}}});
    vuelta();
    CHECK(nombres(f) == "ecualizador,compresor,limitador"); // Ecualizadores (9) antes que Dinámica (5)
    CHECK(f.lista().value(0).toMap().value("porque").toString() == "eq");

    if (g_failures == 0) std::printf("test_filtro_catalogo: OK\n");
    return g_failures == 0 ? 0 : 1;
}
