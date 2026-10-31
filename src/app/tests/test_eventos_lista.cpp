// ============================================================================
// src/app/tests/test_eventos_lista.cpp — la caché de la lista de eventos (E-A7)
//
//   1. Leer las propiedades no va al disco: Eventos/ se leyó UNA vez (al crear el
//      modelo), por más que la vista pregunte cien veces.
//   2. Un evento nuevo aparece en `eventos` en el momento (y avisa con listaChanged), sin
//      contadores ni «refrescar».
//   3. Uno que aparece por otro lado (copiado a mano en la carpeta) también: lo ve el
//      QFileSystemWatcher y se relee en el fondo.
//   4. El automático se guarda por persist (Config/historico.json) sin leerlo de nuevo.
// ============================================================================
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>

#include "evento/EventoStore.hpp"
#include "persist/ColaEscritura.hpp"
#include "ui/EventosListaViewModel.hpp"

namespace fs = std::filesystem;
using ba::app::EventosListaViewModel;

namespace {
int g_fallos = 0;
#define CHECK(cond, msg)                                                                 \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);             \
            ++g_fallos;                                                                  \
        }                                                                                \
    } while (0)

/// Corre el bucle hasta que `listo` o pasen `ms`.
bool esperar(const std::function<bool()>& listo, int ms = 5000) {
    QElapsedTimer t;
    t.start();
    while (!listo() && t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    return listo();
}

bool hay(const EventosListaViewModel& v, const QString& nombre) {
    for (const QVariant& x : v.eventos())
        if (x.toMap().value("nombre").toString() == nombre) return true;
    return false;
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    const fs::path raiz = fs::path(tmp.path().toStdString()) / "Eventos";
    const fs::path ajustes = fs::path(tmp.path().toStdString()) / "Config" / "historico.json";
    fs::create_directories(ajustes.parent_path());
    {
        std::ofstream o(ajustes);
        o << R"({"evento_automatico": false, "registrar": {"bus": false}})";
    }
    ba::evento::EventoStore st(raiz);
    st.crear("Uno");
    st.crear("Dos");

    EventosListaViewModel v(raiz, ajustes);
    int avisos = 0;
    QObject::connect(&v, &EventosListaViewModel::listaChanged, [&avisos]() { ++avisos; });

    // 1. Una lectura, por más que se pregunte.
    CHECK(v.lecturas() == 1, "Eventos/ se lee una vez al crear el modelo");
    CHECK(v.eventos().size() == 2, "están los dos eventos");
    for (int i = 0; i < 100; ++i) {
        (void)v.eventos();
        (void)v.carpetas();
        (void)v.enCurso();
        (void)v.datos("Uno");
    }
    CHECK(v.lecturas() == 1, "leer las propiedades no vuelve al disco");

    // 2. Uno nuevo, por acá: está ya y avisó.
    std::string err;
    const std::string c = v.crear("Tres", {}, &err);
    CHECK(!c.empty(), "se crea");
    CHECK(hay(v, "Tres"), "el nuevo está en la lista en el momento");
    CHECK(avisos >= 1, "avisó listaChanged");
    CHECK(esperar([&v]() { return v.lecturas() >= 2; }), "y se relee en el fondo una vez");

    // 3. Uno que aparece por otro lado.
    const int antes = v.lecturas();
    ba::evento::EventoStore(raiz).crear("Copiado a mano");
    CHECK(esperar([&v]() { return hay(v, "Copiado a mano"); }), "lo ve el vigía de la carpeta");
    CHECK(v.lecturas() > antes, "releyó por el aviso de la carpeta");

    // 4. El automático: la caché cambia y el archivo lo escribe persist (lo demás queda).
    CHECK(!v.eventoAutomatico(), "el automático viene del archivo");
    v.setEventoAutomatico(true);
    CHECK(v.eventoAutomatico(), "cambió");
    ba::persist::cola().vaciar();
    {
        std::ifstream f(ajustes);
        const nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
        CHECK(j.value("evento_automatico", false), "quedó escrito");
        CHECK(j.contains("registrar"), "lo demás del archivo se conserva");
    }
    CHECK(!v.carpetaEnCurso().empty(), "con el automático hay uno en curso (la jornada del día)");
    v.esperarLecturas();

    if (g_fallos == 0) std::printf("test_eventos_lista: OK\n");
    return g_fallos == 0 ? 0 : 1;
}
