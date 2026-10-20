// test_modos.cpp — los modos de un dispositivo de video y cuál conviene (ModosVideo)
//
// - con las caps REALES de una webcam USB 2 en Windows (Logitech C910 por Media
//   Foundation, datos/c910-mediafoundation.caps): a 1080p el NV12 a 30 cps lo fabrica
//   el sistema (los otros crudos de ese tamaño llegan a 5) y conviene el MJPEG;
//   a 640×480 el crudo es de verdad y conviene ese;
// - un dispositivo cualquiera (caps escritas a mano): crudo grande que cubre, sin
//   proporción, H.264, rango de cps;
// - lo guardado: «1920x1080@30» (de antes, sin formato) y «…:mjpeg»;
// - la cadena de un modo MJPEG arma y decodifica de verdad (videotestsrc ! jpegenc).

#include "ModosVideo.hpp"
#include "Reloj.hpp"

#include <gst/gst.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace ba::motor;

namespace {
int fallos = 0;
void check(bool ok, const std::string& que) {
    std::printf("%s: %s\n", ok ? "ok" : "FALLA", que.c_str());
    if (!ok) ++fallos;
}
GstCaps* capsDeArchivo(const std::string& ruta) {
    std::ifstream f(ruta);
    std::stringstream t;
    std::string linea;
    while (std::getline(f, linea))
        if (!linea.empty()) t << linea << ";";
    return gst_caps_from_string(t.str().c_str());
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
    const bool hayJpeg = !decodificadorPara("image/jpeg").first.empty();
    std::printf("decodificador de MJPEG: %s; de H.264: %s\n", decodificadorPara("image/jpeg").first.c_str(),
                decodificadorPara("video/x-h264").first.c_str());

    // 1) La webcam real.
    {
        GstCaps* caps = capsDeArchivo(BA_TEST_DATOS "/c910-mediafoundation.caps");
        check(caps != nullptr && gst_caps_get_size(caps) > 100, "se leyeron las caps de la C910");
        const auto modos = modosDe(caps);
        gst_caps_unref(caps);
        for (const auto& m : modos)
            if (m.ancho == 1920 || m.ancho == 640) std::printf("   %s\n", m.etiqueta().c_str());
        bool nv12conv = false, crudo5 = false, jpeg30 = false;
        for (const auto& m : modos) {
            if (m.ancho != 1920 || m.alto != 1080) continue;
            if (m.formato == "crudo" && m.convertido && m.pixel.find("NV12") != std::string::npos && m.fps >= 29.9) nv12conv = true;
            if (m.formato == "crudo" && !m.convertido && m.fps <= 5.1) crudo5 = true;
            if (m.formato == "mjpeg" && m.fps >= 29.9) jpeg30 = true;
        }
        check(nv12conv, "1080p: el NV12 a 30 lo fabrica el sistema (convertido)");
        check(crudo5, "1080p: crudo de verdad, a 5 cps");
        check(jpeg30, "1080p: MJPEG a 30");
        std::string porque;
        const ModoVideo m = elegirModo(modos, {1920, 1080, 30}, &porque);
        std::printf("   elegido para 1080p30: %s — %s\n", m.etiqueta().c_str(), porque.c_str());
        if (hayJpeg)
            check(m.ancho == 1920 && m.formato == "mjpeg" && m.fps >= 29.9, "para un lienzo 1080p30: 1920×1080 MJPEG a 30");
        const ModoVideo chico = elegirModo(modos, {640, 480, 30});
        check(chico.ancho == 640 && chico.formato == "crudo" && !chico.convertido, "para 640×480: el crudo de verdad");
        const ModoVideo hd = elegirModo(modos, {1280, 720, 30});
        std::printf("   elegido para 720p30: %s\n", hd.etiqueta().c_str());
        if (hayJpeg) check(hd.ancho == 1280 && hd.formato == "mjpeg", "para 720p30: MJPEG (el crudo a 30 lo convierte el sistema)");
        const ModoVideo v = modoGuardado(modos, "1920x1080@30");
        check(v.ancho == 1920 && v.fps >= 29.9 && v.formato != "crudo" || (!hayJpeg && v.convertido),
              "«1920x1080@30» guardado de antes: el más barato que da 30 (" + v.etiqueta() + ")");
        const ModoVideo g = modoGuardado(modos, "1280x720@30:mjpeg");
        check(g.ancho == 1280 && g.formato == "mjpeg" && g.valor() == "1280x720@30:mjpeg", "«1280x720@30:mjpeg» guardado");
        check(modoGuardado(modos, "4000x3000@30").ancho == 0, "un modo que no da: ninguno");
    }

    // 2) Un dispositivo cualquiera, escrito a mano.
    {
        GstCaps* caps = gst_caps_from_string(
            "video/x-raw,format=YUY2,width=3840,height=2160,framerate=[1/1,30/1];"
            "video/x-raw,format=YUY2,width=1920,height=1080,framerate=60/1;"
            "video/x-raw,format=YUY2,width=1440,height=1080,framerate=60/1;"
            "video/x-h264,width=3840,height=2160,framerate=30/1;"
            "video/x-raw(memory:GLMemory),format=RGBA,width=1920,height=1080,framerate=60/1");
        const auto modos = modosDe(caps);
        gst_caps_unref(caps);
        const ModoVideo m = elegirModo(modos, {1920, 1080, 25});
        check(m.ancho == 1920 && m.formato == "crudo" && m.fps >= 59.9,
              "1080p25 en una capturadora: 1080p crudo (pide 60: el más cercano arriba de 25) — " + m.etiqueta());
        const ModoVideo r = elegirModo(modos, {3840, 2160, 25});
        check(r.ancho == 3840 && r.formato == "crudo" && std::abs(r.fps - 25) < 0.01, "4K25: el crudo con rango, a 25 justos");
        bool sinGl = true;
        for (const auto& x : modos) sinGl = sinGl && !(x.pixel == "RGBA");
        check(sinGl, "la memoria de la placa no suma modos repetidos");
        const std::string c = cadenaDeModo(r);
        check(c.find("video/x-raw,width=3840,height=2160,framerate=[2450/100,2550/100]") != std::string::npos, "la cadena del crudo: " + c);
    }

    // 3) La cadena de un MJPEG decodifica de verdad.
    if (hayJpeg) {
        ModoVideo m;
        m.ancho = 320;
        m.alto = 240;
        m.fps = 30;
        m.formato = "mjpeg";
        m.decodificador = decodificadorPara("image/jpeg").first;
        const std::string cadena = "videotestsrc num-buffers=10 ! video/x-raw,width=320,height=240,framerate=30/1 ! jpegenc" +
                                   cadenaDeModo(m) + " ! videoconvert ! fakesink";
        GError* e = nullptr;
        GstElement* pl = gst_parse_launch(cadena.c_str(), &e);
        check(e == nullptr && pl != nullptr, "arma: " + cadena);
        if (pl != nullptr) {
            gst_element_set_state(pl, GST_STATE_PLAYING);
            GstBus* bus = gst_element_get_bus(pl);
            GstMessage* msg = gst_bus_timed_pop_filtered(bus, 5 * GST_SECOND, static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
            check(msg != nullptr && GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS, "decodifica los 10 cuadros hasta el final");
            if (msg != nullptr) gst_message_unref(msg);
            gst_object_unref(bus);
            gst_element_set_state(pl, GST_STATE_NULL);
            gst_object_unref(pl);
        }
        if (e != nullptr) g_error_free(e);
    }
    std::printf("test_modos: %s\n", fallos == 0 ? "OK" : "FALLAS");
    return fallos == 0 ? 0 : 1;
}
