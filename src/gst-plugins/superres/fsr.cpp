// ============================================================================
// src/gst-plugins/superres/fsr.cpp — FSR1 (AMD FidelityFX Super Resolution) x2
//
// Proposito:
//   Elemento de GStreamer `bafsr`: duplica la resolución con FSR 1.0 de AMD
//   (MIT), que NO es una red neuronal: EASU (un escalado que sigue los bordes,
//   con 12 muestras por píxel) y RCAS (un afilado que no exagera). Anda en
//   CUALQUIER GPU con OpenGL —Mac, Windows, Linux; NVIDIA, AMD, Intel—: AMD
//   mide 0,2 a 0,5 ms a 1440p. Es la superresolución «de base» cuando no hay
//   la de Apple ni una red.
//
//   Es un bin de GStreamer:
//     glupload ! glcolorconvert ! RGBA ! glshader(EASU) ! RGBA 2x ! glshader(RCAS)
//       ! gldownload ! videoconvert
//   El tamaño de salida (el doble) se fija cuando llega el formato de entrada.
//   `nitidez`: lo que afila RCAS, en «paradas» (0 = el máximo; 0,2 de fábrica:
//   una cámara IP con H.264 tiene bloques, y afilar de más los marca).
//
//   Los shaders siguen el algoritmo de la referencia de AMD (ffx_fsr1.h,
//   v1.0.2, MIT: github.com/GPUOpen-Effects/FidelityFX-FSR), escritos en GLSL
//   simple (GStreamer los adapta al perfil de OpenGL de cada sistema). EASU
//   analiza la dirección del borde con la luma y filtra el RGB con esos pesos,
//   como la referencia.
// ============================================================================

#include <gst/gl/gl.h>
#include <gst/gst.h>

#include <cstdio>

#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#include <string>

namespace {

const char* kEasu = R"glsl(
#ifdef GL_ES
precision highp float;
#endif
varying vec2 v_texcoord;
uniform sampler2D tex;
uniform float entrada_ancho;
uniform float entrada_alto;

vec3 px(vec2 p, vec2 inv) { return texture2D(tex, (p + vec2(0.5)) * inv).rgb; }
float luma(vec3 c) { return c.g + 0.5 * (c.r + c.b); }

void setF(inout vec2 dir, inout float len, vec2 pp, float w, float lA, float lB, float lC, float lD, float lE) {
    float dc = lD - lC;
    float cb = lC - lB;
    float lenX = max(abs(dc), abs(cb));
    lenX = 1.0 / max(lenX, 1e-5);
    float dirX = lD - lB;
    dir.x += dirX * w;
    lenX = clamp(abs(dirX) * lenX, 0.0, 1.0);
    lenX *= lenX;
    len += lenX * w;
    float ec = lE - lC;
    float ca = lC - lA;
    float lenY = max(abs(ec), abs(ca));
    lenY = 1.0 / max(lenY, 1e-5);
    float dirY = lE - lA;
    dir.y += dirY * w;
    lenY = clamp(abs(dirY) * lenY, 0.0, 1.0);
    lenY *= lenY;
    len += lenY * w;
}

void tap(inout vec3 aC, inout float aW, vec2 off, vec2 dir, vec2 len, float lob, float clp, vec3 c) {
    vec2 v;
    v.x = off.x * dir.x + off.y * dir.y;
    v.y = off.x * (-dir.y) + off.y * dir.x;
    v *= len;
    float d2 = min(v.x * v.x + v.y * v.y, clp);
    float wB = 0.4 * d2 - 1.0;
    float wA = lob * d2 - 1.0;
    wB *= wB;
    wA *= wA;
    wB = 1.5625 * wB - 0.5625;
    float w = wB * wA;
    aC += c * w;
    aW += w;
}

void main() {
    vec2 inSize = vec2(entrada_ancho, entrada_alto);
    vec2 inv = 1.0 / inSize;
    vec2 pp = v_texcoord * inSize - vec2(0.5);
    vec2 fp = floor(pp);
    pp -= fp;
    vec3 b = px(fp + vec2(0.0, -1.0), inv), c = px(fp + vec2(1.0, -1.0), inv);
    vec3 e = px(fp + vec2(-1.0, 0.0), inv), f = px(fp, inv), g = px(fp + vec2(1.0, 0.0), inv), h = px(fp + vec2(2.0, 0.0), inv);
    vec3 i = px(fp + vec2(-1.0, 1.0), inv), j = px(fp + vec2(0.0, 1.0), inv), k = px(fp + vec2(1.0, 1.0), inv), l = px(fp + vec2(2.0, 1.0), inv);
    vec3 n = px(fp + vec2(0.0, 2.0), inv), o = px(fp + vec2(1.0, 2.0), inv);
    float bL = luma(b), cL = luma(c), eL = luma(e), fL = luma(f), gL = luma(g), hL = luma(h);
    float iL = luma(i), jL = luma(j), kL = luma(k), lL = luma(l), nL = luma(n), oL = luma(o);

    // Dirección y largo del borde: las 4 cuadrículas bilineales alrededor del punto.
    vec2 dir = vec2(0.0);
    float len = 0.0;
    setF(dir, len, pp, (1.0 - pp.x) * (1.0 - pp.y), bL, eL, fL, gL, jL);
    setF(dir, len, pp, pp.x * (1.0 - pp.y), cL, fL, gL, hL, kL);
    setF(dir, len, pp, (1.0 - pp.x) * pp.y, fL, iL, jL, kL, nL);
    setF(dir, len, pp, pp.x * pp.y, gL, jL, kL, lL, oL);
    vec2 dir2 = dir * dir;
    float dirR = dir2.x + dir2.y;
    bool zro = dirR < 1.0 / 32768.0;
    dirR = inversesqrt(max(dirR, 1e-12));
    dirR = zro ? 1.0 : dirR;
    dir.x = zro ? 1.0 : dir.x;
    dir *= vec2(dirR);
    len = len * 0.5;
    len *= len;
    float stretch = (dir.x * dir.x + dir.y * dir.y) / max(abs(dir.x), abs(dir.y));
    vec2 len2 = vec2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);
    float lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;
    float clp = 1.0 / lob;

    vec3 aC = vec3(0.0);
    float aW = 0.0;
    tap(aC, aW, vec2(0.0, -1.0) - pp, dir, len2, lob, clp, b);
    tap(aC, aW, vec2(1.0, -1.0) - pp, dir, len2, lob, clp, c);
    tap(aC, aW, vec2(-1.0, 1.0) - pp, dir, len2, lob, clp, i);
    tap(aC, aW, vec2(0.0, 1.0) - pp, dir, len2, lob, clp, j);
    tap(aC, aW, vec2(0.0, 0.0) - pp, dir, len2, lob, clp, f);
    tap(aC, aW, vec2(-1.0, 0.0) - pp, dir, len2, lob, clp, e);
    tap(aC, aW, vec2(1.0, 1.0) - pp, dir, len2, lob, clp, k);
    tap(aC, aW, vec2(2.0, 1.0) - pp, dir, len2, lob, clp, l);
    tap(aC, aW, vec2(2.0, 0.0) - pp, dir, len2, lob, clp, h);
    tap(aC, aW, vec2(1.0, 0.0) - pp, dir, len2, lob, clp, g);
    tap(aC, aW, vec2(1.0, 2.0) - pp, dir, len2, lob, clp, o);
    tap(aC, aW, vec2(0.0, 2.0) - pp, dir, len2, lob, clp, n);
    // Sin anillos: dentro del mínimo y el máximo de los 4 vecinos.
    vec3 mn = min(min(f, g), min(j, k));
    vec3 mx = max(max(f, g), max(j, k));
    gl_FragColor = vec4(clamp(aC / aW, mn, mx), 1.0);
}
)glsl";

const char* kRcas = R"glsl(
#ifdef GL_ES
precision highp float;
#endif
varying vec2 v_texcoord;
uniform sampler2D tex;
uniform float salida_ancho;
uniform float salida_alto;
uniform float nitidez; // paradas: 0 = el máximo

void main() {
    vec2 t = 1.0 / vec2(salida_ancho, salida_alto);
    vec3 b = texture2D(tex, v_texcoord + vec2(0.0, -t.y)).rgb;
    vec3 d = texture2D(tex, v_texcoord + vec2(-t.x, 0.0)).rgb;
    vec3 e = texture2D(tex, v_texcoord).rgb;
    vec3 f = texture2D(tex, v_texcoord + vec2(t.x, 0.0)).rgb;
    vec3 h = texture2D(tex, v_texcoord + vec2(0.0, t.y)).rgb;
    vec3 mn4 = min(min(b, d), min(f, h));
    vec3 mx4 = max(max(b, d), max(f, h));
    // Cuánto se puede afilar sin salirse de [0,1] (la «lobe» de la referencia).
    vec3 hitMin = min(mn4, e) / max(4.0 * mx4, vec3(1e-5));
    vec3 hitMax = (1.0 - max(mx4, e)) / min(4.0 * min(mn4, e) - 4.0, vec3(-1e-5));
    vec3 lobeRGB = max(-hitMin, hitMax);
    float lobe = max(-0.1875, min(max(lobeRGB.r, max(lobeRGB.g, lobeRGB.b)), 0.0)) * exp2(-nitidez);
    float rcpL = 1.0 / (4.0 * lobe + 1.0);
    gl_FragColor = vec4(clamp((lobe * (b + d + h + f) + e) * rcpL, 0.0, 1.0), 1.0);
}
)glsl";

} // namespace

G_BEGIN_DECLS
#define BA_TYPE_FSR (ba_fsr_get_type())
G_DECLARE_FINAL_TYPE(BaFsr, ba_fsr, BA, FSR, GstBin)
G_END_DECLS

struct _BaFsr {
    GstBin parent;
    GstElement* easu;
    GstElement* rcas;
    GstElement* doble; ///< capsfilter con el tamaño x2
    gdouble nitidez;
    gint ancho, alto;
};

G_DEFINE_TYPE(BaFsr, ba_fsr, GST_TYPE_BIN)

enum { PROP_0, PROP_NITIDEZ };

static GstStaticPadTemplate sinkTpl =
    GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS("video/x-raw"));
static GstStaticPadTemplate srcTpl =
    GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS("video/x-raw"));

/// El shader, armado en el hilo de OpenGL con la versión de GLSL del contexto
/// (core en macOS, compatibilidad o ES en otros): GStreamer lo adapta.
static GstGLShader* crearShader(GstElement* glshader, gpointer fuente) {
    GstGLContext* ctx = nullptr;
    g_object_get(glshader, "context", &ctx, nullptr);
    if (ctx == nullptr) return nullptr;
    GError* err = nullptr;
    GstGLSLStage* v = gst_glsl_stage_new_default_vertex(ctx);
    GstGLSLStage* f = gst_glsl_stage_new_with_string(
        ctx, GL_FRAGMENT_SHADER, GST_GLSL_VERSION_NONE,
        static_cast<GstGLSLProfile>(GST_GLSL_PROFILE_ES | GST_GLSL_PROFILE_COMPATIBILITY),
        static_cast<const char*>(fuente));
    GstGLShader* sh = gst_gl_shader_new_link_with_stages(ctx, &err, v, f, nullptr);
    if (sh == nullptr) {
        GST_ERROR_OBJECT(glshader, "FSR: el shader no compiló: %s", err ? err->message : "?");
        g_clear_error(&err);
    }
    gst_object_unref(ctx);
    return sh;
}

static void ponerUniformes(BaFsr* s) {
    if (s->ancho <= 0) return;
    GstStructure* u = gst_structure_new("u", "entrada_ancho", G_TYPE_FLOAT, static_cast<float>(s->ancho),
                                        "entrada_alto", G_TYPE_FLOAT, static_cast<float>(s->alto), nullptr);
    g_object_set(s->easu, "uniforms", u, nullptr);
    gst_structure_free(u);
    u = gst_structure_new("u", "salida_ancho", G_TYPE_FLOAT, static_cast<float>(s->ancho * 2), "salida_alto",
                          G_TYPE_FLOAT, static_cast<float>(s->alto * 2), "nitidez", G_TYPE_FLOAT,
                          static_cast<float>(s->nitidez), nullptr);
    g_object_set(s->rcas, "uniforms", u, nullptr);
    gst_structure_free(u);
}

/// Cuando llega el formato de entrada: el tamaño x2 para el EASU.
static GstPadProbeReturn alCaps(GstPad*, GstPadProbeInfo* info, gpointer data) {
    auto* s = static_cast<BaFsr*>(data);
    GstEvent* ev = GST_PAD_PROBE_INFO_EVENT(info);
    if (GST_EVENT_TYPE(ev) != GST_EVENT_CAPS) return GST_PAD_PROBE_OK;
    GstCaps* caps = nullptr;
    gst_event_parse_caps(ev, &caps);
    const GstStructure* st = gst_caps_get_structure(caps, 0);
    gint w = 0, h = 0;
    if (gst_structure_get_int(st, "width", &w) && gst_structure_get_int(st, "height", &h) &&
        (w != s->ancho || h != s->alto)) {
        s->ancho = w;
        s->alto = h;
        GstCaps* d = gst_caps_from_string("video/x-raw(memory:GLMemory),format=RGBA");
        gst_caps_set_simple(d, "width", G_TYPE_INT, w * 2, "height", G_TYPE_INT, h * 2, nullptr);
        g_object_set(s->doble, "caps", d, nullptr);
        gst_caps_unref(d);
        ponerUniformes(s);
    }
    return GST_PAD_PROBE_OK;
}

static void ponerPropiedad(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    auto* s = BA_FSR(o);
    if (id == PROP_NITIDEZ) {
        s->nitidez = g_value_get_double(v);
        ponerUniformes(s);
    } else {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static void leerPropiedad(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    if (id == PROP_NITIDEZ)
        g_value_set_double(v, BA_FSR(o)->nitidez);
    else
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
}

static void ba_fsr_class_init(BaFsrClass* k) {
    GObjectClass* go = G_OBJECT_CLASS(k);
    GstElementClass* ge = GST_ELEMENT_CLASS(k);
    go->set_property = ponerPropiedad;
    go->get_property = leerPropiedad;
    g_object_class_install_property(
        go, PROP_NITIDEZ,
        g_param_spec_double("nitidez", "Nitidez", "Cuánto afila RCAS, en paradas (0 = el máximo)", 0.0, 2.0,
                            0.2, static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    gst_element_class_set_static_metadata(
        ge, "Superresolución x2 FSR1 (AMD, OpenGL)", "Filter/Effect/Video/Scaler",
        "Duplica la resolución con FSR 1.0 (EASU + RCAS) en la GPU", "Broadcast Appliance");
    gst_element_class_add_static_pad_template(ge, &sinkTpl);
    gst_element_class_add_static_pad_template(ge, &srcTpl);
}

static void ba_fsr_init(BaFsr* s) {
    s->nitidez = 0.2;
    s->ancho = s->alto = 0;
    GstElement* sube = gst_element_factory_make("glupload", nullptr);
    GstElement* conv = gst_element_factory_make("glcolorconvert", nullptr);
    GstElement* rgba = gst_element_factory_make("capsfilter", nullptr);
    s->easu = gst_element_factory_make("glshader", nullptr);
    s->doble = gst_element_factory_make("capsfilter", nullptr);
    s->rcas = gst_element_factory_make("glshader", nullptr);
    GstElement* baja = gst_element_factory_make("gldownload", nullptr);
    GstElement* vconv = gst_element_factory_make("videoconvert", nullptr);
    if (!sube || !conv || !rgba || !s->easu || !s->doble || !s->rcas || !baja || !vconv) {
        GST_ERROR_OBJECT(s, "faltan los elementos de OpenGL de GStreamer");
        return;
    }
    GstCaps* c = gst_caps_from_string("video/x-raw(memory:GLMemory),format=RGBA");
    g_object_set(rgba, "caps", c, nullptr);
    gst_caps_unref(c);
    g_signal_connect(s->easu, "create-shader", G_CALLBACK(crearShader), const_cast<char*>(kEasu));
    g_signal_connect(s->rcas, "create-shader", G_CALLBACK(crearShader), const_cast<char*>(kRcas));
    gst_bin_add_many(GST_BIN(s), sube, conv, rgba, s->easu, s->doble, s->rcas, baja, vconv, nullptr);
    gst_element_link_many(sube, conv, rgba, s->easu, s->doble, s->rcas, baja, vconv, nullptr);
    GstPad* in = gst_element_get_static_pad(sube, "sink");
    GstPad* out = gst_element_get_static_pad(vconv, "src");
    GstPad* gin = gst_ghost_pad_new_from_template("sink", in, gst_static_pad_template_get(&sinkTpl));
    GstPad* gout = gst_ghost_pad_new_from_template("src", out, gst_static_pad_template_get(&srcTpl));
    gst_pad_add_probe(in, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, alCaps, s, nullptr);
    gst_element_add_pad(GST_ELEMENT(s), gin);
    gst_element_add_pad(GST_ELEMENT(s), gout);
    gst_object_unref(in);
    gst_object_unref(out);
}

/// Registra `bafsr` en el plugin (necesita los elementos de OpenGL de GStreamer al usarlo).
extern "C" gboolean ba_registrar_fsr(GstPlugin* plugin) {
    return gst_element_register(plugin, "bafsr", GST_RANK_NONE, BA_TYPE_FSR);
}
