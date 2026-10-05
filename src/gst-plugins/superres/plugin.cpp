// ============================================================================
// src/gst-plugins/superres/plugin.cpp — el plugin de GStreamer «basuperres»
//
// Proposito:
//   Un plugin de GStreamer de verdad (libgstbasuperres): lo carga cualquier
//   GStreamer que lo tenga en su ruta de plugins —el del programa, y también
//   `gst-launch-1.0` con el entorno del programa—. Trae:
//
//     basuperres    EL elemento: duplica la resolución con el motor que se
//                   pida. Es lo que usa la tarjeta «Superresolución» del grafo.
//                     motor=auto|apple|fsr|onnx|coreml (auto: la red si hay `modelo`;
//                                                  si no, Apple en macOS 26; si
//                                                  no, FSR)
//                     modelo=ruta.onnx|.mlmodelc   (motores onnx y coreml)
//                     escala-red=2|4               (coreml: 4 = la red recibe la mitad)
//                     nitidez=0.2                  (FSR: cuánto afila, en paradas)
//                     proveedor=auto|cpu|coreml|dml (onnx: con qué acelerar)
//                   y para leer: motor-usado, ms-por-cuadro, error.
//     bavtsuperres  Apple (VideoToolbox, macOS 26).   vtsuperres_mac.mm
//     bafsr         FSR 1 de AMD (OpenGL).            fsr.cpp
//     baonnxsr      una red ONNX (ONNX Runtime).      onnxsr.cpp
//     bacoremlsr    una red CoreML (Neural Engine).   coremlsr_mac.mm
//
//   Ejemplo:  gst-launch-1.0 videotestsrc ! video/x-raw,width=640,height=360 !
//             basuperres motor=fsr ! autovideosink
//
//   `basuperres` es un bin: videoconvert ! NV12 ! <motor> ! videoconvert.
//   El motor se elige al armarlo (cambiarlo con el elemento andando no lo
//   rearma: la tarjeta del grafo rearma su pipeline cuando cambia el motor).
// ============================================================================

#include <gst/gst.h>

#include <algorithm>
#include <string>

extern "C" {
gboolean ba_registrar_fsr(GstPlugin* plugin);
gboolean ba_registrar_onnxsr(GstPlugin* plugin);
gboolean ba_registrar_glsl(GstPlugin* plugin);
#if defined(__APPLE__)
gboolean ba_registrar_vtsuperres(GstPlugin* plugin);
gboolean ba_vtsuperres_soportado();
gboolean ba_registrar_coremlsr(GstPlugin* plugin);
#endif
}

/// El motor, como enumeración de GObject (gst-inspect la muestra con sus valores).
static GType tipoMotor() {
    static GType t = 0;
    if (g_once_init_enter(&t)) {
        static const GEnumValue v[] = {
            {0, "Elegir solo (red si hay modelo; Apple en macOS 26; si no, FSR)", "auto"},
            {1, "Apple VideoToolbox (macOS 26, el tamaño máximo lo dice el sistema)", "apple"},
            {2, "FSR 1 de AMD (OpenGL, cualquier GPU)", "fsr"},
            {3, "Una red ONNX (propiedad modelo)", "onnx"},
            {4, "Una red CoreML (.mlmodelc, Neural Engine/GPU de la Mac)", "coreml"},
            {5, "Un shader de mpv (.glsl: FSRCNNX, Anime4K; OpenGL, cualquier GPU)", "glsl"},
            {0, nullptr, nullptr}};
        g_once_init_leave(&t, g_enum_register_static("BaSuperResMotor", v));
    }
    return t;
}
static GType tipoProveedor() {
    static GType t = 0;
    if (g_once_init_enter(&t)) {
        static const GEnumValue v[] = {{0, "El mejor que haya", "auto"},
                                       {1, "Solo el procesador", "cpu"},
                                       {2, "CoreML (macOS: GPU y Neural Engine)", "coreml"},
                                       {3, "DirectML (Windows: AMD, NVIDIA, Intel)", "dml"},
                                       {4, "TensorRT (NVIDIA: compila la red para la placa)", "tensorrt"},
                                       {5, "CUDA (NVIDIA)", "cuda"},
                                       {6, "MIGraphX (AMD en Linux: compila la red)", "migraphx"},
                                       {7, "ROCm (AMD en Linux)", "rocm"},
                                       {8, "OpenVINO (Intel)", "openvino"},
                                       {0, nullptr, nullptr}};
        g_once_init_leave(&t, g_enum_register_static("BaSuperResProveedor", v));
    }
    return t;
}
static const char* kMotores[] = {"auto", "apple", "fsr", "onnx", "coreml", "glsl"};
constexpr int kNumMotores = 6;
static const char* kProveedores[] = {"auto", "cpu", "coreml", "dml", "tensorrt", "cuda", "migraphx", "rocm", "openvino"};
constexpr int kNumProveedores = 9;

G_BEGIN_DECLS
#define BA_TYPE_SUPERRES (ba_superres_get_type())
G_DECLARE_FINAL_TYPE(BaSuperRes, ba_superres, BA, SUPERRES, GstBin)
G_END_DECLS

struct _BaSuperRes {
    GstBin parent;
    GstElement* convEntrada;
    GstElement* nv12;
    GstElement* motor;
    GstElement* convSalida;
    gint escalaRed;    ///< lo que agranda la red (2; 4 = entra a la mitad y sale x2)
    gchar* pedido; ///< motor pedido
    gchar* usado;  ///< motor que quedó
    gchar* modelo;
    gchar* variantes; ///< la red fija para cada tamaño (motores onnx y coreml)
    gchar* proveedor;
    gint unidades; ///< coreml: 0 auto, 1 todas, 2 ane, 3 gpu, 4 cpu
    gdouble nitidez;
    gboolean activo;
    gchar* error;
};

G_DEFINE_TYPE(BaSuperRes, ba_superres, GST_TYPE_BIN)

GST_DEBUG_CATEGORY_STATIC(ba_superres_debug);
#define GST_CAT_DEFAULT ba_superres_debug

enum {
    PROP_0,
    PROP_MOTOR,
    PROP_MODELO,
    PROP_VARIANTES,
    PROP_NITIDEZ,
    PROP_PROVEEDOR,
    PROP_ACTIVO,
    PROP_ESCALA_RED,
    PROP_UNIDADES,
    PROP_UNIDADES_USADAS,
    PROP_ACELERADOR,
    PROP_MEZCLA,
    PROP_MOTOR_USADO,
    PROP_MS,
    PROP_ERROR
};

static GstStaticPadTemplate sinkTpl =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS("video/x-raw"));
static GstStaticPadTemplate srcTpl =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS("video/x-raw"));

static bool hay(const char* fabrica) {
    GstElementFactory* f = gst_element_factory_find(fabrica);
    if (f == nullptr) return false;
    gst_object_unref(f);
    return true;
}

/// El motor que corresponde a lo pedido (y a lo que hay en este sistema).
static std::string elegir(BaSuperRes* s) {
    const std::string p = s->pedido != nullptr ? s->pedido : "auto";
    const bool conModelo = s->modelo != nullptr && *s->modelo != '\0';
    if (p == "apple" || p == "fsr" || p == "onnx" || p == "coreml" || p == "glsl") return p;
    if (conModelo && hay("baonnxsr")) return "onnx";
#if defined(__APPLE__)
    if (ba_vtsuperres_soportado()) return "apple";
#endif
    return "fsr";
}

/// Arma (o rearma) el motor dentro del bin.
static void armar(BaSuperRes* s) {
    if (s->motor != nullptr) {
        gst_element_set_state(s->motor, GST_STATE_NULL);
        gst_bin_remove(GST_BIN(s), s->motor);
        s->motor = nullptr;
    }
    gst_element_unlink(s->nv12, s->convSalida);
    g_free(s->error);
    s->error = nullptr;
    const std::string m = s->activo ? elegir(s) : "ninguno";
    const char* fabrica = m == "apple"    ? "bavtsuperres"
                          : m == "onnx"   ? "baonnxsr"
                          : m == "fsr"    ? "bafsr"
                          : m == "coreml" ? "bacoremlsr"
                          : m == "glsl"   ? "baglsl"
                                          : "identity";
    // El formato que pide el motor: CoreML toma NV12 o BGRA tal cual (si viene
    // de vtdec, con su IOSurface: sin copias) y convierte en la GPU; GLSL y FSR
    // corren en OpenGL (convierten ellos, cualquier formato); Apple y las redes
    // ONNX de NV12, NV12 de RANGO LIMITADO con su matriz (601, 709 o 2020: la
    // red va y vuelve con la misma). Un rango completo (cámara JPEG) se lleva
    // a limitado acá, en vez de recortarse adentro.
    GstCaps* c = gst_caps_from_string(
        m == "coreml"                ? "video/x-raw,format={NV12,BGRA}"
        : m == "glsl" || m == "fsr" ? "video/x-raw"
                                     : "video/x-raw,format=NV12,colorimetry={bt709,bt601,bt2020,smpte240m}");
    g_object_set(s->nv12, "caps", c, nullptr);
    gst_caps_unref(c);
    GstElement* e = nullptr;
    if (m == "glsl" && s->activo && hay("baglsl")) {
        // El shader corre en la GPU: se sube, se convierte a RGBA, el shader, y se baja.
        GError* err = nullptr;
        e = gst_parse_bin_from_description(
            "glupload ! glcolorconvert ! baglsl name=shader ! glcolorconvert ! gldownload", TRUE, &err);
        if (err != nullptr) {
            s->error = g_strdup(err->message);
            g_error_free(err);
            if (e != nullptr) gst_object_unref(e);
            e = nullptr;
        } else {
            gst_object_set_name(GST_OBJECT(e), "motor");
            if (GstElement* sh = gst_bin_get_by_name(GST_BIN(e), "shader")) {
                g_object_set(sh, "shader", s->modelo != nullptr ? s->modelo : "", nullptr);
                gst_object_unref(sh);
            }
        }
    } else {
        e = gst_element_factory_make(fabrica, "motor");
    }
    if (!s->activo) {
        // Apagado: pasa sin tocar (y sin agrandar).
    } else if (m == "glsl") {
        if (e == nullptr) e = gst_element_factory_make("identity", "motor");
    } else if (e == nullptr) {
        s->error =
            g_strdup((std::string("no hay el motor «") + m + "» (" + fabrica + ") en este sistema").c_str());
        e = gst_element_factory_make("identity", "motor"); // la imagen sigue, sin agrandar
    } else if (m == "onnx") {
        g_object_set(e, "modelo", s->modelo != nullptr ? s->modelo : "", "variantes",
                     s->variantes != nullptr ? s->variantes : "", "proveedor",
                     s->proveedor != nullptr ? s->proveedor : "auto", nullptr);
    } else if (m == "coreml") {
        g_object_set(e, "modelo", s->modelo != nullptr ? s->modelo : "", "variantes",
                     s->variantes != nullptr ? s->variantes : "", "escala", s->escalaRed, nullptr);
        gst_util_set_object_arg(G_OBJECT(e), "unidades", std::to_string(s->unidades).c_str());
    } else if (m == "fsr") {
        g_object_set(e, "nitidez", s->nitidez, nullptr);
    }
    g_free(s->usado);
    s->usado = g_strdup(s->error != nullptr ? "ninguno" : m.c_str());
    // La demora del motor la declara él (consulta LATENCY): el bin la suma sola.
    gst_element_post_message(GST_ELEMENT(s), gst_message_new_latency(GST_OBJECT(s)));
    s->motor = e;
    gst_bin_add(GST_BIN(s), e);
    // Una red x4 recibe la imagen a la mitad: lo hace el motor (bacoremlsr
    // `escala`), que sale x2 de lo que entra.
    gst_element_link_many(s->nv12, e, s->convSalida, nullptr);
    gst_element_sync_state_with_parent(e);
}

static void ponerPropiedad(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    auto* s = BA_SUPERRES(o);
    bool rearmar = false;
    switch (id) {
        case PROP_MOTOR:
            g_free(s->pedido);
            s->pedido = g_strdup(kMotores[std::clamp(g_value_get_enum(v), 0, kNumMotores - 1)]);
            rearmar = true;
            break;
        case PROP_ACTIVO:
            s->activo = g_value_get_boolean(v);
            if (s->motor != nullptr &&
                g_object_class_find_property(G_OBJECT_GET_CLASS(s->motor), "activo") != nullptr)
                g_object_set(s->motor, "activo", s->activo,
                             nullptr); // los que saben pasar sin tocar, en vivo
            else
                rearmar = true;
            break;
        case PROP_MODELO:
            g_free(s->modelo);
            s->modelo = g_value_dup_string(v);
            if (s->motor != nullptr && s->usado != nullptr && std::string(s->usado) == "onnx")
                g_object_set(s->motor, "modelo", s->modelo != nullptr ? s->modelo : "", nullptr);
            else
                rearmar = true; // en auto, un modelo cambia el motor
            break;
        case PROP_VARIANTES:
            g_free(s->variantes);
            s->variantes = g_value_dup_string(v);
            if (s->motor != nullptr && g_object_class_find_property(G_OBJECT_GET_CLASS(s->motor), "variantes") != nullptr)
                g_object_set(s->motor, "variantes", s->variantes != nullptr ? s->variantes : "", nullptr);
            break;
        case PROP_NITIDEZ:
            s->nitidez = g_value_get_double(v);
            if (s->motor != nullptr &&
                g_object_class_find_property(G_OBJECT_GET_CLASS(s->motor), "nitidez") != nullptr)
                g_object_set(s->motor, "nitidez", s->nitidez, nullptr);
            break;
        case PROP_ESCALA_RED:
            s->escalaRed = g_value_get_int(v);
            rearmar = true;
            break;
        case PROP_UNIDADES:
            s->unidades = std::clamp(g_value_get_int(v), 0, 4);
            rearmar = true;
            break;
        case PROP_PROVEEDOR:
            g_free(s->proveedor);
            s->proveedor = g_strdup(kProveedores[std::clamp(g_value_get_enum(v), 0, kNumProveedores - 1)]);
            if (s->motor != nullptr &&
                g_object_class_find_property(G_OBJECT_GET_CLASS(s->motor), "proveedor") != nullptr)
                g_object_set(s->motor, "proveedor", s->proveedor, nullptr);
            break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps); return;
    }
    // El motor se cambia solo con el elemento parado (antes de PLAYING).
    if (rearmar && GST_STATE(s) <= GST_STATE_READY) armar(s);
}

static void leerPropiedad(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    auto* s = BA_SUPERRES(o);
    switch (id) {
        case PROP_MOTOR: {
            int i = 0;
            for (int k = 0; k < kNumMotores; ++k)
                if (std::string(s->pedido) == kMotores[k]) i = k;
            g_value_set_enum(v, i);
            break;
        }
        case PROP_ACTIVO: g_value_set_boolean(v, s->activo); break;
        case PROP_ESCALA_RED: g_value_set_int(v, s->escalaRed); break;
        case PROP_UNIDADES: g_value_set_int(v, s->unidades); break;
        case PROP_ACELERADOR: {
            // Qué acelerador usa (para repartir: cada uno corre de a una red por vez).
            std::string a = "ninguno";
            const std::string u = s->usado != nullptr ? s->usado : "";
            const auto leer = [&](const char* prop) {
                gchar* x = nullptr;
                if (s->motor != nullptr && g_object_class_find_property(G_OBJECT_GET_CLASS(s->motor), prop) != nullptr)
                    g_object_get(s->motor, prop, &x, nullptr);
                std::string r = x != nullptr ? x : "";
                g_free(x);
                return r;
            };
            if (u == "coreml") {
                const std::string un = leer("unidades-usadas");
                a = un == "gpu" ? "gpu" : un == "cpu" ? "cpu" : "neural-engine";
            } else if (u == "onnx") {
                const std::string pr = leer("proveedor-usado");
                a = pr.empty() ? "cpu" : pr == "coreml" ? "neural-engine" : pr == "tensorrt" ? "cuda" : pr;
            } else if (u == "apple") {
                a = "videotoolbox";
            } else if (u == "fsr" || u == "glsl") {
                a = "gpu";
            }
            g_value_set_string(v, a.c_str());
            break;
        }
        case PROP_MEZCLA: {
            gdouble m = 1.0;
            if (s->motor != nullptr && g_object_class_find_property(G_OBJECT_GET_CLASS(s->motor), "mezcla") != nullptr)
                g_object_get(s->motor, "mezcla", &m, nullptr);
            g_value_set_double(v, m);
            break;
        }
        case PROP_UNIDADES_USADAS: {
            gchar* u = nullptr;
            if (s->motor != nullptr &&
                g_object_class_find_property(G_OBJECT_GET_CLASS(s->motor), "unidades-usadas") != nullptr)
                g_object_get(s->motor, "unidades-usadas", &u, nullptr);
            g_value_take_string(v, u);
            break;
        }
        case PROP_MODELO: g_value_set_string(v, s->modelo); break;
        case PROP_VARIANTES: g_value_set_string(v, s->variantes); break;
        case PROP_NITIDEZ: g_value_set_double(v, s->nitidez); break;
        case PROP_PROVEEDOR: {
            int i = 0;
            for (int k = 0; k < kNumProveedores; ++k)
                if (std::string(s->proveedor) == kProveedores[k]) i = k;
            g_value_set_enum(v, i);
            break;
        }
        case PROP_MOTOR_USADO: g_value_set_string(v, s->usado); break;
        case PROP_MS: {
            gdouble ms = 0;
            if (s->motor != nullptr &&
                g_object_class_find_property(G_OBJECT_GET_CLASS(s->motor), "ms-por-cuadro") != nullptr)
                g_object_get(s->motor, "ms-por-cuadro", &ms, nullptr);
            g_value_set_double(v, ms);
            break;
        }
        case PROP_ERROR: {
            gchar* e = nullptr;
            if (s->error == nullptr && s->motor != nullptr &&
                g_object_class_find_property(G_OBJECT_GET_CLASS(s->motor), "error") != nullptr)
                g_object_get(s->motor, "error", &e, nullptr);
            g_value_take_string(v, s->error != nullptr ? g_strdup(s->error) : e);
            break;
        }
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static void ba_superres_finalize(GObject* o) {
    auto* s = BA_SUPERRES(o);
    g_free(s->pedido);
    g_free(s->usado);
    g_free(s->modelo);
    g_free(s->variantes);
    g_free(s->proveedor);
    g_free(s->error);
    G_OBJECT_CLASS(ba_superres_parent_class)->finalize(o);
}

static void ba_superres_class_init(BaSuperResClass* k) {
    GST_DEBUG_CATEGORY_INIT(ba_superres_debug, "basuperres", 0, "Superresolución");
    GObjectClass* go = G_OBJECT_CLASS(k);
    GstElementClass* ge = GST_ELEMENT_CLASS(k);
    go->set_property = ponerPropiedad;
    go->get_property = leerPropiedad;
    go->finalize = ba_superres_finalize;
    // Lo que se puede cambiar andando (MUTABLE_PLAYING) y lo que rearma el motor
    // (MUTABLE_READY: con la cadena parada; la tarjeta del grafo rearma sola).
    const auto vivo =
        static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING);
    const auto listo =
        static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY);
    const auto ro = static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property(
        go, PROP_MOTOR, g_param_spec_enum("motor", "Motor", "Con qué se agranda", tipoMotor(), 0, listo));
    g_object_class_install_property(
        go, PROP_MODELO, g_param_spec_string("modelo", "Modelo", "Ruta del .onnx (motor onnx)", "", vivo));
    g_object_class_install_property(
        go, PROP_VARIANTES,
        g_param_spec_string("variantes", "Variantes",
                            "La red compilada para cada tamaño de entrada («1024x576=/ruta;…», motores onnx y "
                            "coreml): se usa la del tamaño que llega; si no hay, `modelo`",
                            "", vivo));
    g_object_class_install_property(go, PROP_NITIDEZ,
                                    g_param_spec_double("nitidez", "Nitidez",
                                                        "FSR: cuánto afila, en paradas (0 = el máximo)", 0.0,
                                                        2.0, 0.2, vivo));
    g_object_class_install_property(go, PROP_PROVEEDOR,
                                    g_param_spec_enum("proveedor", "Proveedor",
                                                      "Motor onnx: con qué acelerar la red", tipoProveedor(),
                                                      0, listo));
    g_object_class_install_property(go, PROP_ACTIVO,
                                    g_param_spec_boolean("activo", "Activo",
                                                         "Agranda (FALSE: pasa sin tocar, del mismo tamaño)",
                                                         TRUE, vivo));
    g_object_class_install_property(go, PROP_ESCALA_RED,
                                    g_param_spec_int("escala-red", "Escala de la red",
                                                     "Lo que agranda la red del modelo (2; 4 = entra a la mitad y sale x2)",
                                                     2, 4, 2, listo));
    g_object_class_install_property(
        go, PROP_UNIDADES,
        g_param_spec_int("unidades", "Unidades",
                         "coreml: dónde corre la red (0 auto = Neural Engine, 1 todas, 2 ane, 3 gpu, 4 cpu)", 0, 4,
                         0, listo));
    g_object_class_install_property(
        go, PROP_ACELERADOR,
        g_param_spec_string("acelerador", "Acelerador",
                            "Dónde corre la red: neural-engine, gpu, cpu, cuda, dml, migraphx, rocm, openvino, "
                            "videotoolbox (para repartir el trabajo entre cámaras)",
                            nullptr, ro));
    g_object_class_install_property(
        go, PROP_MEZCLA,
        g_param_spec_double("mezcla", "Mezcla", "Cuánto de la red sale ahora (0 agrandado común, 1 la red)", 0, 1, 1, ro));
    g_object_class_install_property(
        go, PROP_UNIDADES_USADAS,
        g_param_spec_string("unidades-usadas", "Unidades usadas", "coreml: dónde quedó la red", nullptr, ro));
    g_object_class_install_property(
        go, PROP_MOTOR_USADO, g_param_spec_string("motor-usado", "Motor usado", "El que quedó", nullptr, ro));
    g_object_class_install_property(
        go, PROP_MS,
        g_param_spec_double("ms-por-cuadro", "ms por cuadro", "Lo que tarda (onnx/apple)", 0, 100000, 0, ro));
    g_object_class_install_property(
        go, PROP_ERROR, g_param_spec_string("error", "Error", "Por qué no anda (si no anda)", nullptr, ro));
    gst_element_class_set_static_metadata(
        ge, "Superresolución x2", "Filter/Effect/Video/Scaler",
        "Duplica la resolución: Apple (VideoToolbox), FSR 1, una red ONNX (cualquier placa), CoreML o un shader de mpv",
        "Broadcast Appliance");
    gst_element_class_add_static_pad_template(ge, &sinkTpl);
    gst_element_class_add_static_pad_template(ge, &srcTpl);
    gst_type_mark_as_plugin_api(tipoMotor(), static_cast<GstPluginAPIFlags>(0));
    gst_type_mark_as_plugin_api(tipoProveedor(), static_cast<GstPluginAPIFlags>(0));
}

static void ba_superres_init(BaSuperRes* s) {
    s->pedido = g_strdup("auto");
    s->usado = nullptr;
    s->modelo = g_strdup("");
    s->variantes = g_strdup("");
    s->proveedor = g_strdup("auto");
    s->nitidez = 0.2;
    s->activo = TRUE;
    s->error = nullptr;
    s->motor = nullptr;
    s->escalaRed = 2;
    s->unidades = 0;
    s->convEntrada = gst_element_factory_make("videoconvert", "entrada");
    s->nv12 = gst_element_factory_make("capsfilter", "nv12");
    s->convSalida = gst_element_factory_make("videoconvert", "salida");
    GstCaps* c = gst_caps_from_string("video/x-raw,format=NV12");
    g_object_set(s->nv12, "caps", c, nullptr);
    gst_caps_unref(c);
    gst_bin_add_many(GST_BIN(s), s->convEntrada, s->nv12, s->convSalida, nullptr);
    gst_element_link(s->convEntrada, s->nv12);
    GstPad* in = gst_element_get_static_pad(s->convEntrada, "sink");
    GstPad* out = gst_element_get_static_pad(s->convSalida, "src");
    gst_element_add_pad(GST_ELEMENT(s),
                        gst_ghost_pad_new_from_template("sink", in, gst_static_pad_template_get(&sinkTpl)));
    gst_element_add_pad(GST_ELEMENT(s),
                        gst_ghost_pad_new_from_template("src", out, gst_static_pad_template_get(&srcTpl)));
    gst_object_unref(in);
    gst_object_unref(out);
    armar(s);
}

static gboolean plugin_init(GstPlugin* plugin) {
    gboolean ok = ba_registrar_fsr(plugin) && ba_registrar_onnxsr(plugin) && ba_registrar_glsl(plugin);
#if defined(__APPLE__)
    ok = ok && ba_registrar_vtsuperres(plugin) && ba_registrar_coremlsr(plugin);
#endif
    return ok && gst_element_register(plugin, "basuperres", GST_RANK_NONE, BA_TYPE_SUPERRES);
}

#define PACKAGE "basuperres"
GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, basuperres,
                  "Superresolución x2: Apple (VideoToolbox), FSR 1 (AMD, OpenGL) y redes ONNX", plugin_init,
                  "1.0", "GPL", "Broadcast Appliance", "Broadcast Appliance")
