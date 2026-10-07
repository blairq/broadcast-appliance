// ============================================================================
// src/gst-plugins/filtros/salida.cpp — basalida: publicar un punto de la cadena
//
// La tarjeta Salida en el MEDIO de la cadena de una entrada (después del croma
// y antes del PTZ, por ejemplo): la imagen de ese punto se publica con nombre,
// sin cortar la cadena. Es un bin de una entrada y una salida, así que va en
// la descripción lineal de la entrada como cualquier filtro:
//
//     sink ─ tee ─┬─────────────────────────────── src   (sigue la cadena)
//                 └─ queue (descarta lo viejo) ─ intersink producer-name=<productor>
//
// Del otro lado, la escena interna de la Salida lee ese productor (intersrc),
// igual que lee la salida de una entrada. No toca la imagen ni la memoria
// (sirve en CPU y en GPU: se registra también como basalidagl y basalidad3d12
// para que la cadena de la GPU no la baje), y deja pasar todos los eventos.
//
//   gst-launch-1.0 videotestsrc ! basalida productor=prueba ! fakesink
// ============================================================================
#include <gst/gst.h>

#include <string>

namespace {

struct BaSalida {
    GstBin parent;
    GstElement* inter = nullptr;
    std::string* productor = nullptr;
};
struct BaSalidaClass {
    GstBinClass parent_class;
};

enum { PROP_0, PROP_PRODUCTOR };

GType ba_salida_get_type();
#define BA_SALIDA(o) (reinterpret_cast<BaSalida*>(o))

G_DEFINE_TYPE(BaSalida, ba_salida, GST_TYPE_BIN)

GstStaticPadTemplate kSink = GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);
GstStaticPadTemplate kSrc = GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

void ba_salida_init(BaSalida* s) {
    s->productor = new std::string();
    GstElement* tee = gst_element_factory_make("tee", "t");
    GstElement* cola = gst_element_factory_make("queue", "cola");
    s->inter = gst_element_factory_make("intersink", "inter");
    if (tee == nullptr || cola == nullptr || s->inter == nullptr) {
        // Sin intersink (el plugin «inter» no está): pasa sin publicar.
        GstElement* id = gst_element_factory_make("identity", "pasa");
        gst_bin_add(GST_BIN(s), id);
        GstPad* sp = gst_element_get_static_pad(id, "sink");
        GstPad* op = gst_element_get_static_pad(id, "src");
        gst_element_add_pad(GST_ELEMENT(s), gst_ghost_pad_new_from_template("sink", sp, gst_static_pad_template_get(&kSink)));
        gst_element_add_pad(GST_ELEMENT(s), gst_ghost_pad_new_from_template("src", op, gst_static_pad_template_get(&kSrc)));
        gst_object_unref(sp);
        gst_object_unref(op);
        for (GstElement* e : {tee, cola, s->inter})
            if (e != nullptr) gst_object_unref(e);
        s->inter = nullptr;
        return;
    }
    // Lo publicado nunca frena la cadena: si nadie lo lee rápido, se pierde lo viejo.
    g_object_set(cola, "leaky", 2, "max-size-buffers", 1u, "max-size-time", static_cast<guint64>(0), "max-size-bytes", 0u, nullptr);
    g_object_set(s->inter, "sync", FALSE, nullptr);
    g_object_set(tee, "allow-not-linked", TRUE, nullptr);
    gst_bin_add_many(GST_BIN(s), tee, cola, s->inter, nullptr);
    gst_element_link_many(cola, s->inter, nullptr);
    GstPad* rama = gst_element_request_pad_simple(tee, "src_%u");
    GstPad* colaSink = gst_element_get_static_pad(cola, "sink");
    gst_pad_link(rama, colaSink);
    gst_object_unref(colaSink);
    gst_object_unref(rama);
    GstPad* sigue = gst_element_request_pad_simple(tee, "src_%u");
    GstPad* entra = gst_element_get_static_pad(tee, "sink");
    gst_element_add_pad(GST_ELEMENT(s), gst_ghost_pad_new_from_template("sink", entra, gst_static_pad_template_get(&kSink)));
    gst_element_add_pad(GST_ELEMENT(s), gst_ghost_pad_new_from_template("src", sigue, gst_static_pad_template_get(&kSrc)));
    gst_object_unref(entra);
    gst_object_unref(sigue);
}

void ba_salida_finalize(GObject* o) {
    delete BA_SALIDA(o)->productor;
    G_OBJECT_CLASS(ba_salida_parent_class)->finalize(o);
}

void ba_salida_set_property(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    BaSalida* s = BA_SALIDA(o);
    if (id != PROP_PRODUCTOR) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
        return;
    }
    const gchar* p = g_value_get_string(v);
    *s->productor = p != nullptr ? p : "";
    if (s->inter != nullptr) g_object_set(s->inter, "producer-name", s->productor->c_str(), nullptr);
}

void ba_salida_get_property(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    if (id != PROP_PRODUCTOR) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
        return;
    }
    g_value_set_string(v, BA_SALIDA(o)->productor->c_str());
}

void ba_salida_class_init(BaSalidaClass* k) {
    GObjectClass* g = G_OBJECT_CLASS(k);
    g->set_property = ba_salida_set_property;
    g->get_property = ba_salida_get_property;
    g->finalize = ba_salida_finalize;
    g_object_class_install_property(
        g, PROP_PRODUCTOR,
        g_param_spec_string("productor", "Productor",
                            "El nombre con que se publica este punto de la cadena (lo lee la escena de la Salida)", "",
                            static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    GstElementClass* e = GST_ELEMENT_CLASS(k);
    gst_element_class_add_static_pad_template(e, &kSink);
    gst_element_class_add_static_pad_template(e, &kSrc);
    gst_element_class_set_static_metadata(e, "Salida en un punto de la cadena", "Filter/Video",
                                          "Publica la imagen de este punto de la cadena con nombre, sin cortarla",
                                          "Broadcast Appliance");
}

} // namespace

extern "C" gboolean ba_registrar_salida(GstPlugin* p) {
    bool ok = gst_element_register(p, "basalida", GST_RANK_NONE, ba_salida_get_type()) &&
              gst_element_register(p, "basalidagl", GST_RANK_NONE, ba_salida_get_type());
#if defined(_WIN32)
    ok = ok && gst_element_register(p, "basalidad3d12", GST_RANK_NONE, ba_salida_get_type());
#endif
    return ok ? TRUE : FALSE;
}
