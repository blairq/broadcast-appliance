// test_sondas.cpp — Sondas: cuánto agrega cada elemento y dónde cruza GPU ↔ memoria
//
// - un `identity sleep-time=20000` (20 ms por cuadro) mide ≈20 ms en su fila
//   (docs/planes/PLAN-TIEMPOS.md, T3);
// - un elemento que retiene cuadros (`queue` lleno a propósito con min-threshold-time
//   de 300 ms) mide lo que RETIENE, aunque no procese nada;
// - con GL: glupload «sube» y gldownload «baja» (los cruces de memoria);
// - al terminar no quedan sondas: lo que pase después no se anota.

#include "Reloj.hpp"
#include "Sondas.hpp"

#include <gst/gst.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

using namespace ba::motor;
using json = nlohmann::json;

namespace {
int fallos = 0;
void check(bool ok, const std::string& que) {
    std::printf("%s: %s\n", ok ? "ok" : "FALLA", que.c_str());
    if (!ok) ++fallos;
}
json fila(const json& r, const std::string& nombre) {
    for (const auto& f : r.value("elementos", json::array()))
        if (f.value("nombre", std::string()) == nombre) return f;
    return json::object();
}
json correr(const std::string& cadena, int ms) {
    GError* e = nullptr;
    GstElement* pl = gst_parse_launch(cadena.c_str(), &e);
    if (e != nullptr) {
        std::printf("cadena: %s\n", e->message);
        g_error_free(e);
        return nullptr;
    }
    gst_element_set_state(pl, GST_STATE_PLAYING);
    gst_element_get_state(pl, nullptr, nullptr, 5 * GST_SECOND);
    std::this_thread::sleep_for(std::chrono::milliseconds(500)); // que arranque
    json r;
    {
        Sondas s(pl);
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        r = s.terminar();
    }
    gst_element_set_state(pl, GST_STATE_NULL);
    gst_object_unref(pl);
    return r;
}
} // namespace

int main() {
#if defined(BA_TEST_GST_RAIZ)
    g_setenv("BA_GST_RAIZ", BA_TEST_GST_RAIZ, FALSE);
#endif
    std::string err;
    if (!iniciarGStreamer(&err)) {
        std::printf("SKIP: sin GStreamer (%s)\n", err.c_str());
        return 77;
    }
    // 1) Lo que tarda en procesar: 20 ms por cuadro, a 25 fps (entra en el período).
    {
        const json r = correr("videotestsrc is-live=true ! video/x-raw,width=320,height=180,framerate=25/1 ! "
                              "identity name=lento sleep-time=20000 ! fakesink sync=true",
                              2500);
        std::printf("%s\n", r.dump(1).c_str());
        const json f = fila(r, "lento");
        const double ms = f.value("agrega_ms", -1.0);
        check(ms > 15 && ms < 30, "identity sleep-time=20 ms mide ≈20 ms (midió " + std::to_string(ms) + ")");
        check(f.value("cuadros", 0) > 30, "anotó cuadros");
        check(f.value("memoria_entrada", std::string()) == "sistema", "memoria del sistema");
        check(f.value("caps_salida", std::string()).find("25 fps") != std::string::npos, "los cuadros por segundo en las caps");
        // identity no le declara al pipeline lo que duerme: declara 0 y mide 20 (lo que ve el diagnóstico).
        check(f.contains("declara_ms") && f.value("declara_ms", -1.0) < 1, "identity declara 0 ms (midió " + f.value("declara_ms", json()).dump() + ")");
        check(f.value("puede_entrada", json::array()).dump().find("cualquiera") != std::string::npos, "identity acepta cualquier memoria (caps ANY)");
    }
    // 2) Lo que RETIENE: una cola que no suelta nada hasta tener 300 ms adentro.
    {
        const json r = correr("videotestsrc is-live=true ! video/x-raw,width=160,height=90,framerate=30/1 ! "
                              "queue name=retiene min-threshold-time=300000000 max-size-time=0 max-size-buffers=0 "
                              "max-size-bytes=0 ! fakesink sync=false",
                              2500);
        const double ms = fila(r, "retiene").value("agrega_ms", -1.0);
        check(ms > 250 && ms < 400, "una cola que retiene 300 ms mide ≈300 ms (midió " + std::to_string(ms) + ")");
    }
    // 3) GL: los cruces de memoria (si hay GL en esta máquina).
    if (GstElementFactory* f = gst_element_factory_find("glupload")) {
        gst_object_unref(f);
        const json r = correr("videotestsrc is-live=true ! video/x-raw,width=320,height=180,framerate=25/1 ! "
                              // gldownload deja pasar la memoria GL si lo que sigue la acepta: se pide la del sistema.
                              "glupload name=sube ! glcolorconvert ! gldownload name=baja ! video/x-raw,format=RGBA ! "
                              "fakesink sync=true",
                              1500);
        const json sube = fila(r, "sube"), baja = fila(r, "baja");
        std::printf("GL: %s\n    %s\n", sube.dump().c_str(), baja.dump().c_str());
        if (sube.value("memoria_salida", std::string()).empty()) {
            std::printf("SKIP GL: no negoció (sin contexto GL en esta sesión)\n");
        } else {
            check(sube.value("cruce", std::string()) == "sube" && sube.value("memoria_salida", std::string()) == "gl",
                  "glupload: sube a la GPU (memory:GLMemory)");
            check(baja.value("cruce", std::string()) == "baja" && baja.value("memoria_salida", std::string()) == "sistema",
                  "gldownload: baja a la memoria del sistema");
            check(r.value("cruces", json::array()).size() == 2, "dos cruces en la cadena");
            check(sube.value("puede_salida", json::array()).dump().find("cualquiera") != std::string::npos,
                  "glupload saca cualquier memoria (video/x-raw(ANY))");
        }
    }
    std::printf("test_sondas: %s\n", fallos == 0 ? "OK" : "FALLAS");
    return fallos == 0 ? 0 : 1;
}
