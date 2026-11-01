// test_formulario.cpp
//
// ui/Formulario: la conversión de lo que escribió el operador al tipo de cada campo (la
// configuración de un plugin, los parámetros de la acción de un equipo). Antes se hacía
// en QML y los errores de JSON se tragaban.
//   1. Números (entero si es entero), sí/no, opciones, JSON y texto.
//   2. Los errores se dicen (JSON roto, letra en un número, opción que no es, falta).
//   3. convertirTodos: lo que no se tocó queda como estaba (aunque ya no sea una opción).
//   4. deFila: la fila de un modelo (la configuración, las propiedades de una fuente) al
//      esquema del campo: los kind de antes, el número que se escribe o se desliza, la
//      insignia, lo modificado y lo apagado.

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

    // 4) deFila.
    {
        f::Fila puerto{"gateway.metricsPort", "Puerto", "int", 9099, {}, 1024, 65535};
        puerto.numeroEscrito = true;
        puerto.badge = "gateway";
        puerto.modified = true;
        const QVariantMap c = f::deFila(puerto);
        CHECK(c.value("kind") == "number" && c.value("decimales").toInt() == 0);
        CHECK(c.value("min").toDouble() == 1024 && c.value("max").toDouble() == 65535);
        CHECK(c.value("badge") == "gateway" && c.value("modified").toBool() && !c.contains("enabled"));
        // Una propiedad con rango chico: deslizador; sin rango o enorme: se escribe.
        const QVariantMap op = f::deFila({"opacidad", "Opacidad", "float", 0.5, {}, 0, 1, 0.01});
        CHECK(op.value("kind") == "float" && op.value("step").toDouble() == 0.01);
        CHECK(f::deFila({"x", "", "int", 3, {}, -1e9, 1e9}).value("kind") == "number");
        CHECK(f::deFila({"x", "", "float", 3.5, {}, 0, 0}).value("kind") == "number");
        CHECK(!f::deFila({"x", "", "float", 3.5, {}, 0, 0}).contains("decimales"));
        // Los kind de antes.
        CHECK(f::deFila({"modo", "", "choice", "a", f::opcionesDe({"a"})}).value("kind") == "list");
        CHECK(f::deFila({"k", "", "password", ""}).value("kind") == "clave");
        CHECK(f::deFila({"k", "", "secret", ""}).value("kind") == "clave");
        f::Fila apagada{"b", "", "button", {}};
        apagada.enabled = false;
        CHECK(f::deFila(apagada).value("enabled") == false && f::deFila(apagada).value("kind") == "button");
        // El número escrito y borrado no se manda (vacío, no obligatorio).
        CHECK(f::convertir(f::deFila(puerto), "", &v).isEmpty() && !v.isValid());
        CHECK(f::convertir(f::deFila(puerto), 8080.0, &v).isEmpty() && v.toLongLong() == 8080);
    }

    if (g_failures == 0) std::printf("test_formulario: OK\n");
    return g_failures == 0 ? 0 : 1;
}
