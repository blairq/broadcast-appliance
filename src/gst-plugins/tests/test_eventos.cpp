// ============================================================================
// src/gst-plugins/tests/test_eventos.cpp — todos nuestros elementos dejan pasar los eventos
//
// La cadena de una entrada se arma con nuestras tarjetas en el orden que quiera
// el usuario, y lo que va de una a otra (el canal de control «ba/control», las
// caras «ba/rostros», la ventana del PTZ «ba/ptz», lo que agregue un plugin
// mañana) viaja como EVENTOS de GStreamer. Si un elemento se come un evento,
// lo que está después deja de enterarse — y no lo dice nadie.
//
// Por cada elemento: una cadena mínima con él en el medio, y se manda
//   - un evento propio serializado hacia abajo (GST_EVENT_CUSTOM_DOWNSTREAM),
//   - uno fuera de banda hacia abajo (GST_EVENT_CUSTOM_DOWNSTREAM_OOB),
//   - uno propio hacia arriba (GST_EVENT_CUSTOM_UPSTREAM),
// y se mira que lleguen del otro lado. Un elemento que no se puede armar en
// esta máquina (le falta un modelo, un plugin de Python) se informa y se saltea.
// ============================================================================
#include <gst/gst.h>

#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

namespace {

struct Cuenta {
    std::atomic<int> serial{0}, oob{0}, arriba{0};
    std::atomic<bool> enviado{false}, enviadoArriba{false};
};

GstPadProbeReturn alSalirFuente(GstPad* pad, GstPadProbeInfo* info, gpointer d) {
    auto* c = static_cast<Cuenta*>(d);
    if ((info->type & GST_PAD_PROBE_TYPE_EVENT_UPSTREAM) != 0) {
        const GstStructure* s = gst_event_get_structure(GST_PAD_PROBE_INFO_EVENT(info));
        if (s != nullptr && gst_structure_has_name(s, "prueba/arriba")) ++c->arriba;
        return GST_PAD_PROBE_OK;
    }
    if ((info->type & GST_PAD_PROBE_TYPE_BUFFER) != 0 && !c->enviado.exchange(true)) {
        gst_pad_push_event(pad, gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, gst_structure_new_empty("prueba/serial")));
        gst_pad_push_event(pad, gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM_OOB, gst_structure_new_empty("prueba/oob")));
    }
    return GST_PAD_PROBE_OK;
}

GstPadProbeReturn alLlegar(GstPad* pad, GstPadProbeInfo* info, gpointer d) {
    auto* c = static_cast<Cuenta*>(d);
    if ((info->type & (GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM)) != 0) {
        const GstStructure* s = gst_event_get_structure(GST_PAD_PROBE_INFO_EVENT(info));
        if (s != nullptr && gst_structure_has_name(s, "prueba/serial")) ++c->serial;
        if (s != nullptr && gst_structure_has_name(s, "prueba/oob")) ++c->oob;
        return GST_PAD_PROBE_OK;
    }
    if ((info->type & GST_PAD_PROBE_TYPE_BUFFER) != 0 && !c->enviadoArriba.exchange(true))
        gst_pad_push_event(pad, gst_event_new_custom(GST_EVENT_CUSTOM_UPSTREAM, gst_structure_new_empty("prueba/arriba")));
    return GST_PAD_PROBE_OK;
}

enum class Tipo { Video, Gl, Audio };

struct Caso {
    const char* elemento;
    Tipo tipo;
    const char* props = "";
};

/// "ok", "salteado: …" o "FALLA: …".
std::string probar(const Caso& k) {
    if (GstElementFactory* f = gst_element_factory_find(k.elemento))
        gst_object_unref(f);
    else
        return std::string("salteado: no está el elemento");
    std::string d;
    const std::string e = std::string(k.elemento) + " " + k.props;
    switch (k.tipo) {
        case Tipo::Video:
            d = "videotestsrc name=s num-buffers=20 ! video/x-raw,width=320,height=240,framerate=30/1 ! videoconvert ! " + e +
                " ! videoconvert ! fakesink name=k";
            break;
        case Tipo::Gl:
            d = "videotestsrc name=s num-buffers=20 ! video/x-raw,width=320,height=240,framerate=30/1 ! glupload ! "
                "glcolorconvert ! video/x-raw(memory:GLMemory),format=RGBA ! " + e + " ! gldownload ! fakesink name=k";
            break;
        case Tipo::Audio:
            d = "audiotestsrc name=s num-buffers=20 ! audio/x-raw,format=F32LE,rate=48000,channels=2,layout=interleaved ! " +
                e + " ! audioconvert ! fakesink name=k";
            break;
    }
    GError* err = nullptr;
    GstElement* p = gst_parse_launch(d.c_str(), &err);
    if (err != nullptr) {
        std::string m = err->message;
        g_error_free(err);
        if (p != nullptr) gst_object_unref(p);
        return "salteado: " + m;
    }
    Cuenta c;
    GstElement* s = gst_bin_get_by_name(GST_BIN(p), "s");
    GstElement* sink = gst_bin_get_by_name(GST_BIN(p), "k");
    GstPad* ps = gst_element_get_static_pad(s, "src");
    GstPad* pk = gst_element_get_static_pad(sink, "sink");
    gst_pad_add_probe(ps, static_cast<GstPadProbeType>(GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_EVENT_UPSTREAM),
                      alSalirFuente, &c, nullptr);
    gst_pad_add_probe(pk, static_cast<GstPadProbeType>(GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM),
                      alLlegar, &c, nullptr);
    gst_element_set_state(p, GST_STATE_PLAYING);
    GstBus* bus = gst_element_get_bus(p);
    std::string error;
    GstMessage* m = gst_bus_timed_pop_filtered(bus, 15 * GST_SECOND,
                                               static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
    if (m != nullptr && GST_MESSAGE_TYPE(m) == GST_MESSAGE_ERROR) {
        GError* e2 = nullptr;
        gst_message_parse_error(m, &e2, nullptr);
        error = e2 != nullptr ? e2->message : "error";
        if (e2 != nullptr) g_error_free(e2);
    }
    if (m != nullptr) gst_message_unref(m);
    gst_element_set_state(p, GST_STATE_NULL);
    gst_object_unref(bus);
    gst_object_unref(ps);
    gst_object_unref(pk);
    gst_object_unref(s);
    gst_object_unref(sink);
    gst_object_unref(p);
    if (!c.enviado) return "salteado: no corrió (" + (error.empty() ? std::string("sin datos") : error) + ")";
    std::string falta;
    if (c.serial == 0) falta += " serializado";
    if (c.oob == 0) falta += " fuera-de-banda";
    if (c.enviadoArriba && c.arriba == 0) falta += " hacia-arriba";
    return falta.empty() ? std::string("ok") : "FALLA: no pasó el evento" + falta;
}

} // namespace

int main(int argc, char** argv) {
#ifdef BA_TEST_GST_PLUGINS
    g_setenv("GST_PLUGIN_PATH", BA_TEST_GST_PLUGINS, FALSE);
#endif
    gst_init(&argc, &argv);
    const std::vector<Caso> casos = {
        {"bacolor", Tipo::Video},          {"bacroma", Tipo::Video},
        {"barecorte", Tipo::Video},        {"balegal", Tipo::Video},
        {"baptz", Tipo::Video},            {"baptz", Tipo::Video, "ir=0.5,0.5,2,0"},
        {"bamejora", Tipo::Video},         {"barostros", Tipo::Video, "marcar=true"},
        {"bacolorgl", Tipo::Gl},           {"barecortegl", Tipo::Gl},
        {"baptzgl", Tipo::Gl},             {"baptzgl", Tipo::Gl, "ir=0.5,0.5,2,0"},
        {"barostrosgl", Tipo::Gl, "marcar=true"},
        {"baseguir", Tipo::Video, "objetivo=mayor"},  {"baseguirgl", Tipo::Gl, "objetivo=mayor"},
        {"basuperres", Tipo::Video},       {"bafsr", Tipo::Video},
        {"baonnxsr", Tipo::Video},         {"bacoremlsr", Tipo::Video},
        {"bavtsuperres", Tipo::Video},     {"baglsl", Tipo::Gl},
        {"baplugin", Tipo::Video},         {"balimitador", Tipo::Audio},
        {"baautomixer", Tipo::Audio},
    };
    int fallas = 0;
    for (const Caso& k : casos) {
        const std::string r = probar(k);
        std::printf("%-14s %-20s %s\n", k.elemento, k.props, r.c_str());
        if (r.rfind("FALLA", 0) == 0) ++fallas;
    }
    std::printf("test_eventos: %d fallas\n", fallas);
    return fallas == 0 ? 0 : 1;
}
