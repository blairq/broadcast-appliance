// ============================================================================
// src/app/tests/test_ndi.cpp — recepción NDI(R) abierta (libNoIDea) por GStreamer
//
//   1. El descubrimiento mDNS arranca, vuelve a tiempo y no se cuelga (sin
//      emisores en la red: lista vacía).
//   2. `bandisrc` se registra, y GStreamer del bundle tiene avdec_speedhq (el
//      video de NDI completo) y acepta las caps que pone bandisrc (con la
//      variante SHQ2 = 4:2:2, lo que manda NDI).
//   3. Una cadena con una fuente NDI que no está arranca, queda esperando y se
//      cierra limpia (sin colgarse ni romper).
// ============================================================================

#include "NdiSource.hpp"
#include "video/CatalogoGst.hpp"

#include <gst/gst.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

int main() {
#if defined(BA_TEST_GST_RAIZ)
    g_setenv("BA_GST_RAIZ", BA_TEST_GST_RAIZ, FALSE);
#endif
    int fallos = 0;
    const auto t0 = std::chrono::steady_clock::now();
    const auto found = ba::app::ndi::discover(1500);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    std::printf("descubrimiento: %zu emisor(es) en %lld ms\n", found.size(), static_cast<long long>(ms));
    if (ms > 6000) {
        std::printf("FALLO: el descubrimiento no volvió a tiempo\n");
        ++fallos;
    }
    std::string e;
    if (!ba::app::video::iniciarGStreamer(&e)) {
        std::printf("SKIP: sin GStreamer (%s)\n", e.c_str());
        return fallos == 0 ? 77 : 1;
    }
    if (!ba::app::ndi::registrarElemento()) {
        std::printf("FALLO: no se registró bandisrc\n");
        ++fallos;
    }
    GstElementFactory* dec = gst_element_factory_find("avdec_speedhq");
    if (dec == nullptr) {
        std::printf("FALLO: el GStreamer del bundle no tiene avdec_speedhq\n");
        ++fallos;
    } else {
        GstCaps* caps = gst_caps_from_string("video/x-speedhq, variant=(string)SHQ2, width=(int)1920, height=(int)1080, framerate=(fraction)30/1");
        if (!gst_element_factory_can_sink_any_caps(dec, caps)) {
            std::printf("FALLO: avdec_speedhq no acepta las caps de bandisrc\n");
            ++fallos;
        }
        gst_caps_unref(caps);
        gst_object_unref(dec);
    }
    GError* err = nullptr;
    GstElement* p = gst_parse_launch("bandisrc name=n fuente=\"NO EXISTE (prueba)\" n.video ! avdec_speedhq ! fakesink async=false "
                                     "n.audio ! audioconvert ! fakesink async=false",
                                     &err);
    if (err != nullptr) {
        std::printf("FALLO: la cadena no se arma: %s\n", err->message);
        g_error_free(err);
        ++fallos;
    } else {
        const GstStateChangeReturn r = gst_element_set_state(p, GST_STATE_PLAYING);
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        const auto st = ba::app::ndi::statuses();
        std::printf("estado: %zu receptor(es)%s\n", st.size(), st.empty() ? "" : (", " + st[0].error).c_str());
        if (r == GST_STATE_CHANGE_FAILURE || st.size() != 1 || st[0].connected) {
            std::printf("FALLO: tenía que quedar un receptor esperando\n");
            ++fallos;
        }
        gst_element_set_state(p, GST_STATE_NULL);
        gst_object_unref(p);
        if (!ba::app::ndi::statuses().empty()) {
            std::printf("FALLO: el receptor no se cerró\n");
            ++fallos;
        }
    }
    if (fallos == 0) std::printf("test_ndi: OK\n");
    return fallos == 0 ? 0 : 1;
}
