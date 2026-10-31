// test_formulario.cpp
//
// ui/Formulario: la conversión de lo que escribió el operador al tipo de cada campo (la
// configuración de un plugin, los parámetros de la acción de un equipo). Antes se hacía
// en QML y los errores de JSON se tragaban.
//   1. Números (entero si es entero), sí/no, opciones, JSON y texto.
//   2. Los errores se dicen (JSON roto, letra en un número, opción que no es, falta).
//   3. convertirTodos: lo que no se tocó queda como estaba (aunque ya no sea una opción).

#include <QCoreApplication>

#include <cstdio>

#include "ui/Formulario.hpp"

namespace f = ba::app::formulario;

namespace {
int g_failures = 0;
#define CHECK(cond)                                                                                          \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (linea %d)\n", #cond, __LINE__);                                          \
            ++g_failures;                                                                                    \
        }                                                                                                    \
    } while (0)
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QVariant v;
    // 1) Cada tipo.
    const QVariantMap num = f::campo("puerto", "Puerto", "number", 0);
    CHECK(f::convertir(num, "8080", &v).isEmpty() && v.typeId() == QMetaType::LongLong && v.toLongLong() == 8080);
    CHECK(f::convertir(num, "0,5", &v).isEmpty() && v.toDouble() == 0.5);
    CHECK(f::convertir(num, 12.0, &v).isEmpty() && v.typeId() == QMetaType::LongLong);
    CHECK(f::convertir(f::campo("x", "", "bool", false), true, &v).isEmpty() && v.toBool());
    const QVariantMap op = f::campo("modo", "Modo", "list", "a", {}, f::opcionesDe({"a", "b"}));
    CHECK(f::convertir(op, "b", &v).isEmpty() && v.toString() == "b");
    const QVariantMap js = f::campo("lista", "Lista", "json", QVariant());
    CHECK(f::convertir(js, "[1, 2]", &v).isEmpty() && v.toList().size() == 2);
    CHECK(f::convertir(js, "\"hola\"", &v).isEmpty() && v.toString() == "hola");
    CHECK(f::convertir(f::campo("t", "", "text", ""), "  hola ", &v).isEmpty() && v.toString() == "  hola ");
    CHECK(f::kindDeTipo("si_no") == "bool" && f::kindDeTipo("opcion") == "list" && f::kindDeTipo("raro") == "text");

    // 2) Los errores.
    CHECK(!f::convertir(num, "ocho", &v).isEmpty());
    CHECK(!f::convertir(js, "[1, 2", &v).isEmpty());
    CHECK(!f::convertir(op, "c", &v).isEmpty());
    QVariantMap obligatorio = f::campo("ip", "IP", "text", QVariant());
    obligatorio["obligatorio"] = true;
    CHECK(!f::convertir(obligatorio, "", &v).isEmpty());
    CHECK(f::convertir(num, "", &v).isEmpty() && !v.isValid()); // vacío y no obligatorio: no se manda

    // 3) convertirTodos.
    const QVariantMap escena = f::campo("escena", "", "list", "Vieja", {}, f::opcionesDe({"Principal"}));
    QVariantMap out;
    CHECK(f::convertirTodos({num, escena}, {{"puerto", "9000"}}, &out).isEmpty());
    CHECK(out.value("puerto").toLongLong() == 9000 && out.value("escena").toString() == "Vieja");
    out.clear();
    CHECK(!f::convertirTodos({num, obligatorio}, {}, &out).isEmpty()); // falta la IP

    if (g_failures == 0) std::printf("test_formulario: OK\n");
    return g_failures == 0 ? 0 : 1;
}
