// test_catalogo_gst.cpp — el catálogo de módulos de GStreamer (video/CatalogoGst)
//
// Con el GStreamer vendoreado: hay módulos de video, videobalance está con su
// clase y su licencia, sus propiedades salen con tipo, rango y valor, y un
// enum (videoflip method) trae sus opciones. Con `--volcar` imprime el
// catálogo entero (para escribir la documentación de datos/gst-modulos.json).

#include <filesystem>
#include "video/CatalogoGst.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

using ba::app::video::CatalogoGst;
using json = nlohmann::json;

namespace {
int fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++fallos;                                                                                        \
        }                                                                                                    \
    } while (0)
} // namespace

int main(int argc, char** argv) {
#if defined(BA_TEST_GST_RAIZ)
#if defined(_WIN32)
    _putenv_s("BA_GST_RAIZ", BA_TEST_GST_RAIZ);
#else
    setenv("BA_GST_RAIZ", BA_TEST_GST_RAIZ, 1);
#endif
#endif
    CatalogoGst c(std::filesystem::path(BA_TEST_GST_DOCS));
    if (!c.disponible()) {
        std::printf("SKIP: sin GStreamer (%s)\n", c.error().c_str());
        return 77;
    }
    const json ms = c.modulos();
    if (argc > 1 && std::strcmp(argv[1], "--volcar") == 0) {
        json out = json::array();
        for (const auto& m : ms) {
            json x = m;
            x["propiedades"] = c.propiedades(m.value("elemento", ""), json::object());
            out.push_back(x);
        }
        std::cout << out.dump(1) << "\n";
        return 0;
    }
    std::printf("GStreamer %s: %zu módulos de video\n", c.version().c_str(), ms.size());
    CHECK(ms.size() > 80, "hay muchos módulos de video");
    const json vb = c.modulo("videobalance");
    CHECK(vb.value("rol", "") == "filtro" && vb.value("licencia", "") == "LGPL" && vb.value("categoria", "") == "Color",
          "videobalance: filtro, LGPL, de color");
    CHECK(c.modulo("videotestsrc").value("rol", "") == "fuente", "videotestsrc es una fuente");
    CHECK(c.modulo("compositor").value("rol", "") == "mezcla", "compositor es de mezcla");
    const json ps = c.propiedades("videobalance", json{{"saturation", 0.5}});
    bool sat = false;
    for (const auto& p : ps)
        if (p.value("nombre", "") == "saturation")
            sat = p.value("tipo", "") == "double" && p.value("max", 0.0) == 2.0 && p.value("valor", 0.0) == 0.5 &&
                  p.value("defecto", 0.0) == 1.0;
    CHECK(sat, "videobalance.saturation: double 0..2, de fábrica 1, con el valor actual");
    bool metodo = false;
    for (const auto& p : c.propiedades("videoflip", json::object()))
        if (p.value("nombre", "") == "method") metodo = p.value("tipo", "") == "enum" && p["opciones"].size() >= 8;
    CHECK(metodo, "videoflip.method: un enum con sus opciones");
    CHECK(c.propiedades("no-existe", json::object()).empty(), "un módulo que no existe: sin propiedades");
    // La documentación en castellano, y si va como tarjeta.
    CHECK(vb.value("titulo", "") == "Balance de video" && !vb.value("para", "").empty() && vb.value("tarjeta", false),
          "videobalance: documentado y va como tarjeta");
    CHECK(!c.modulo("videotestsrc").value("tarjeta", true), "videotestsrc no va como tarjeta (es una fuente)");
    CHECK(c.modulo("glcolorbalance").value("tarjeta", false), "glcolorbalance va como tarjeta (OpenGL)");
    int sinDoc = 0;
    for (const auto& m : ms)
        if (!m.contains("titulo")) {
            std::printf("   sin documentar: %s\n", m.value("elemento", "").c_str());
            ++sinDoc;
        }
    CHECK(sinDoc == 0, "todos los módulos de video tienen su documentación en castellano");
    std::printf(fallos == 0 ? "test_catalogo_gst: OK\n" : "test_catalogo_gst: %d fallos\n", fallos);
    return fallos == 0 ? 0 : 1;
}
