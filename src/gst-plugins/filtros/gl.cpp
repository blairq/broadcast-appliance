// ============================================================================
// src/gst-plugins/filtros/gl.cpp — las tarjetas de imagen en la GPU (OpenGL)
//
// El par de GL de bacolor y barecorte (docs/MAPA-GPU-CPU.md): mismas
// propiedades y la misma cuenta, pero sobre la textura (GLMemory RGBA), así una
// entrada con tarjetas no baja a la CPU. El motor usa «<elemento>gl» cuando el
// video de la entrada va en la GPU y el par tiene todas las propiedades que se
// le ponen.
//
//     bacolorgl     brillo, contraste, saturación y gamma
//     barecortegl   recorte en fracciones (transparente o más chico) y espejo,
//                   con el tag image-orientation de la cámara
//
// Cada uno es un GstGLFilter con un shader propio (estilo ES2, comun/glsl.hpp);
// pasante si no hace nada. Las propiedades cambian andando: los uniformes se
// leen en cada cuadro; el tamaño de «recortar», al renegociar.
// ============================================================================

#include "comun.hpp"
#include "../comun/glsl.hpp"

#include <gst/video/video.h>

#include <string>

namespace {

const char* kColor = R"glsl(
#ifdef GL_ES
precision mediump float;
#endif
varying vec2 v_texcoord;
uniform sampler2D tex;
uniform float brillo;
uniform float contraste;
uniform float saturacion;
uniform float gamma;
void main() {
    vec4 c = texture2D(tex, v_texcoord);
    vec3 x = pow(max(c.rgb, vec3(0.0)), vec3(gamma));
    x = clamp((x - 0.5) * contraste + 0.5 + brillo, 0.0, 1.0);
    float l = dot(x, vec3(0.2126, 0.7152, 0.0722));
    gl_FragColor = vec4(clamp(vec3(l) + (x - vec3(l)) * saturacion, 0.0, 1.0), c.a);
}
)glsl";

// v_texcoord recorre la SALIDA. `ventana` (x0, y0, x1, y1): lo que queda, en
// fracciones de la entrada. Transparente: la salida es la entrada entera y lo
// de afuera de la ventana sale transparente. Recortar: la salida es la ventana.
// El espejo da vuelta la imagen; la ventana es de la salida (como barecorte).
const char* kRecorte = R"glsl(
#ifdef GL_ES
precision mediump float;
#endif
varying vec2 v_texcoord;
uniform sampler2D tex;
uniform vec4 ventana;
uniform float recortar;
uniform vec2 espejo;
void main() {
    vec2 p = recortar > 0.5 ? mix(ventana.xy, ventana.zw, v_texcoord) : v_texcoord;
    vec2 q = mix(p, vec2(1.0) - p, espejo);
    vec4 c = texture2D(tex, q);
    bool fuera = p.x < ventana.x || p.y < ventana.y || p.x > ventana.z || p.y > ventana.w;
    gl_FragColor = (recortar < 0.5 && fuera) ? vec4(0.0) : c;
}
)glsl";

/// Compila (una vez, en el hilo de GL) y avisa si no se puede.
bool asegurarShader(GstGLFilter* f, GstGLShader** sh, const char* frag) {
    if (*sh != nullptr) return true;
    std::string e;
    *sh = ba::glsl::compilar(GST_GL_BASE_FILTER(f)->context, frag, e);
    if (*sh == nullptr) GST_ELEMENT_ERROR(f, RESOURCE, NOT_FOUND, ("el shader no compiló"), ("%s", e.c_str()));
    return *sh != nullptr;
}

void soltarShader(GstGLShader** sh) {
    if (*sh != nullptr) gst_object_unref(*sh);
    *sh = nullptr;
}

} // namespace

// ---------------------------------------------------------------- bacolorgl

G_BEGIN_DECLS
#define BA_TYPE_COLOR_GL (ba_color_gl_get_type())
G_DECLARE_FINAL_TYPE(BaColorGl, ba_color_gl, BA, COLOR_GL, GstGLFilter)
G_END_DECLS

struct _BaColorGl {
    GstGLFilter parent;
    gdouble brillo, contraste, saturacion, gamma; ///< bajo GST_OBJECT_LOCK
    GstGLShader* shader;
};

G_DEFINE_TYPE(BaColorGl, ba_color_gl, GST_TYPE_GL_FILTER)

enum { PC_0, PC_BRILLO, PC_CONTRASTE, PC_SATURACION, PC_GAMMA };

namespace {

void colorPasante(BaColorGl* c) {
    GST_OBJECT_LOCK(c);
    const bool n = std::abs(c->brillo) < 1e-4 && std::abs(c->contraste - 1) < 1e-4 &&
                   std::abs(c->saturacion - 1) < 1e-4 && std::abs(c->gamma - 1) < 1e-4;
    GST_OBJECT_UNLOCK(c);
    gst_base_transform_set_passthrough(GST_BASE_TRANSFORM(c), n);
}

void colorPoner(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    auto* c = BA_COLOR_GL(o);
    GST_OBJECT_LOCK(c);
    switch (id) {
        case PC_BRILLO: c->brillo = g_value_get_double(v); break;
        case PC_CONTRASTE: c->contraste = g_value_get_double(v); break;
        case PC_SATURACION: c->saturacion = g_value_get_double(v); break;
        case PC_GAMMA: c->gamma = g_value_get_double(v); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps); break;
    }
    GST_OBJECT_UNLOCK(c);
    colorPasante(c);
}

void colorLeer(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    auto* c = BA_COLOR_GL(o);
    GST_OBJECT_LOCK(c);
    switch (id) {
        case PC_BRILLO: g_value_set_double(v, c->brillo); break;
        case PC_CONTRASTE: g_value_set_double(v, c->contraste); break;
        case PC_SATURACION: g_value_set_double(v, c->saturacion); break;
        case PC_GAMMA: g_value_set_double(v, c->gamma); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps); break;
    }
    GST_OBJECT_UNLOCK(c);
}

gboolean colorFiltrar(GstGLFilter* f, GstGLMemory* in, GstGLMemory* out) {
    auto* c = BA_COLOR_GL(f);
    if (!asegurarShader(f, &c->shader, kColor)) return FALSE;
    GST_OBJECT_LOCK(c);
    const auto b = static_cast<gfloat>(c->brillo), k = static_cast<gfloat>(c->contraste),
               s = static_cast<gfloat>(c->saturacion), g = static_cast<gfloat>(c->gamma);
    GST_OBJECT_UNLOCK(c);
    gst_gl_shader_use(c->shader);
    gst_gl_shader_set_uniform_1f(c->shader, "brillo", b);
    gst_gl_shader_set_uniform_1f(c->shader, "contraste", k);
    gst_gl_shader_set_uniform_1f(c->shader, "saturacion", s);
    gst_gl_shader_set_uniform_1f(c->shader, "gamma", g);
    gst_gl_filter_render_to_target_with_shader(f, in, out, c->shader);
    return TRUE;
}

void colorParar(GstGLBaseFilter* bf) {
    soltarShader(&BA_COLOR_GL(bf)->shader);
    GST_GL_BASE_FILTER_CLASS(ba_color_gl_parent_class)->gl_stop(bf);
}

} // namespace

static void ba_color_gl_init(BaColorGl* c) {
    c->brillo = 0;
    c->contraste = c->saturacion = c->gamma = 1;
    c->shader = nullptr;
    gst_base_transform_set_passthrough(GST_BASE_TRANSFORM(c), TRUE);
}

static void ba_color_gl_class_init(BaColorGlClass* k) {
    auto* go = G_OBJECT_CLASS(k);
    go->set_property = colorPoner;
    go->get_property = colorLeer;
    using ba::filtros::kVivo;
    g_object_class_install_property(go, PC_BRILLO, g_param_spec_double("brillo", "Brillo", "Se suma (-1..1)", -1, 1, 0, kVivo));
    g_object_class_install_property(go, PC_CONTRASTE,
                                    g_param_spec_double("contraste", "Contraste", "Alrededor del gris medio (0..4)", 0, 4, 1, kVivo));
    g_object_class_install_property(
        go, PC_SATURACION, g_param_spec_double("saturacion", "Saturación", "0 = blanco y negro (0..2)", 0, 2, 1, kVivo));
    g_object_class_install_property(go, PC_GAMMA, g_param_spec_double("gamma", "Gamma", "Exponente (0.2..5)", 0.2, 5, 1, kVivo));
    gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(k), "Color (GPU)", "Filter/Effect/Video",
                                          "Brillo, contraste, saturación y gamma en OpenGL", "Broadcast Appliance");
    gst_gl_filter_add_rgba_pad_templates(GST_GL_FILTER_CLASS(k));
    GST_GL_BASE_FILTER_CLASS(k)->gl_stop = colorParar;
    GST_GL_BASE_FILTER_CLASS(k)->supported_gl_api =
        static_cast<GstGLAPI>(GST_GL_API_OPENGL | GST_GL_API_OPENGL3 | GST_GL_API_GLES2);
    GST_GL_FILTER_CLASS(k)->filter_texture = colorFiltrar;
}

// ---------------------------------------------------------------- barecortegl

G_BEGIN_DECLS
#define BA_TYPE_RECORTE_GL (ba_recorte_gl_get_type())
G_DECLARE_FINAL_TYPE(BaRecorteGl, ba_recorte_gl, BA, RECORTE_GL, GstGLFilter)
G_END_DECLS

struct _BaRecorteGl {
    GstGLFilter parent;
    // Bajo GST_OBJECT_LOCK.
    gdouble izq, arr, der, aba;
    gboolean espejoH, espejoV, orientacionAuto;
    gint modo; ///< 0 transparente, 1 recortar
    GstVideoOrientationMethod tag;
    GstGLShader* shader;
};

G_DEFINE_TYPE(BaRecorteGl, ba_recorte_gl, GST_TYPE_GL_FILTER)

enum { PR_0, PR_IZQ, PR_ARR, PR_DER, PR_ABA, PR_ESPEJO_H, PR_ESPEJO_V, PR_MODO, PR_ORIENTACION_AUTO };

namespace {

struct Recorte {
    double izq, arr, der, aba;
    bool espejoH, espejoV, recortar;
};

Recorte copiaRecorte(BaRecorteGl* r) {
    GST_OBJECT_LOCK(r);
    Recorte a{std::clamp(r->izq, 0.0, 1.0), std::clamp(r->arr, 0.0, 1.0), std::clamp(r->der, 0.0, 1.0),
              std::clamp(r->aba, 0.0, 1.0), r->espejoH != FALSE, r->espejoV != FALSE, r->modo == 1};
    if (r->orientacionAuto) {
        if (r->tag == GST_VIDEO_ORIENTATION_HORIZ || r->tag == GST_VIDEO_ORIENTATION_180) a.espejoH = !a.espejoH;
        if (r->tag == GST_VIDEO_ORIENTATION_VERT || r->tag == GST_VIDEO_ORIENTATION_180) a.espejoV = !a.espejoV;
    }
    GST_OBJECT_UNLOCK(r);
    // Siempre queda algo (como barecorte: al menos un píxel de cada lado).
    if (a.izq + a.der > 0.99) a.der = std::max(0.0, 0.99 - a.izq);
    if (a.arr + a.aba > 0.99) a.aba = std::max(0.0, 0.99 - a.arr);
    return a;
}

bool recortaAlgo(const Recorte& a) { return a.izq > 0 || a.arr > 0 || a.der > 0 || a.aba > 0; }

void recortePasante(BaRecorteGl* r) {
    const Recorte a = copiaRecorte(r);
    gst_base_transform_set_passthrough(GST_BASE_TRANSFORM(r), !recortaAlgo(a) && !a.espejoH && !a.espejoV);
}

void recortePoner(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    auto* r = BA_RECORTE_GL(o);
    bool tamano = false;
    GST_OBJECT_LOCK(r);
    switch (id) {
        case PR_IZQ: r->izq = g_value_get_double(v); tamano = r->modo == 1; break;
        case PR_ARR: r->arr = g_value_get_double(v); tamano = r->modo == 1; break;
        case PR_DER: r->der = g_value_get_double(v); tamano = r->modo == 1; break;
        case PR_ABA: r->aba = g_value_get_double(v); tamano = r->modo == 1; break;
        case PR_ESPEJO_H: r->espejoH = g_value_get_boolean(v); break;
        case PR_ESPEJO_V: r->espejoV = g_value_get_boolean(v); break;
        case PR_MODO: r->modo = g_value_get_enum(v); tamano = true; break;
        case PR_ORIENTACION_AUTO: r->orientacionAuto = g_value_get_boolean(v); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps); break;
    }
    GST_OBJECT_UNLOCK(r);
    recortePasante(r);
    if (tamano) gst_base_transform_reconfigure_src(GST_BASE_TRANSFORM(r));
}

void recorteLeer(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    auto* r = BA_RECORTE_GL(o);
    GST_OBJECT_LOCK(r);
    switch (id) {
        case PR_IZQ: g_value_set_double(v, r->izq); break;
        case PR_ARR: g_value_set_double(v, r->arr); break;
        case PR_DER: g_value_set_double(v, r->der); break;
        case PR_ABA: g_value_set_double(v, r->aba); break;
        case PR_ESPEJO_H: g_value_set_boolean(v, r->espejoH); break;
        case PR_ESPEJO_V: g_value_set_boolean(v, r->espejoV); break;
        case PR_MODO: g_value_set_enum(v, r->modo); break;
        case PR_ORIENTACION_AUTO: g_value_set_boolean(v, r->orientacionAuto); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps); break;
    }
    GST_OBJECT_UNLOCK(r);
}

/// «Recortar»: la salida es la ventana (de la entrada a la salida); al revés, cualquier tamaño.
GstCaps* recorteCaps(GstGLFilter* f, GstPadDirection dir, GstCaps* caps, GstCaps*) {
    const Recorte a = copiaRecorte(BA_RECORTE_GL(f));
    GstCaps* out = gst_caps_copy(caps);
    if (!a.recortar || !recortaAlgo(a)) return out;
    for (guint k = 0; k < gst_caps_get_size(out); ++k) {
        GstStructure* st = gst_caps_get_structure(out, k);
        int w = 0, h = 0;
        if (dir == GST_PAD_SINK && gst_structure_get_int(st, "width", &w) && gst_structure_get_int(st, "height", &h)) {
            const int nw = std::max(1, static_cast<int>(std::lround(w * (1.0 - a.izq - a.der))));
            const int nh = std::max(1, static_cast<int>(std::lround(h * (1.0 - a.arr - a.aba))));
            gst_structure_set(st, "width", G_TYPE_INT, nw, "height", G_TYPE_INT, nh, nullptr);
        } else if (dir == GST_PAD_SRC) {
            gst_structure_set(st, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT, "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
                              nullptr);
        }
    }
    return out;
}

gboolean recorteFiltrar(GstGLFilter* f, GstGLMemory* in, GstGLMemory* out) {
    auto* r = BA_RECORTE_GL(f);
    if (!asegurarShader(f, &r->shader, kRecorte)) return FALSE;
    const Recorte a = copiaRecorte(r);
    gst_gl_shader_use(r->shader);
    // La textura de GL tiene el origen abajo, pero GstGL la sube con la fila 0
    // en v = 0: arriba es v chico, como en la imagen.
    gst_gl_shader_set_uniform_4f(r->shader, "ventana", static_cast<gfloat>(a.izq), static_cast<gfloat>(a.arr),
                                 static_cast<gfloat>(1.0 - a.der), static_cast<gfloat>(1.0 - a.aba));
    gst_gl_shader_set_uniform_1f(r->shader, "recortar", a.recortar ? 1.0f : 0.0f);
    gst_gl_shader_set_uniform_2f(r->shader, "espejo", a.espejoH ? 1.0f : 0.0f, a.espejoV ? 1.0f : 0.0f);
    gst_gl_filter_render_to_target_with_shader(f, in, out, r->shader);
    return TRUE;
}

gboolean recorteEvento(GstBaseTransform* bt, GstEvent* e) {
    if (GST_EVENT_TYPE(e) == GST_EVENT_TAG) {
        GstTagList* tl = nullptr;
        gst_event_parse_tag(e, &tl);
        GstVideoOrientationMethod m;
        if (tl != nullptr && gst_video_orientation_from_tag(tl, &m)) {
            auto* r = BA_RECORTE_GL(bt);
            GST_OBJECT_LOCK(r);
            r->tag = m;
            GST_OBJECT_UNLOCK(r);
            recortePasante(r);
        }
    }
    return GST_BASE_TRANSFORM_CLASS(ba_recorte_gl_parent_class)->sink_event(bt, e);
}

void recorteParar(GstGLBaseFilter* bf) {
    soltarShader(&BA_RECORTE_GL(bf)->shader);
    GST_GL_BASE_FILTER_CLASS(ba_recorte_gl_parent_class)->gl_stop(bf);
}

} // namespace

static void ba_recorte_gl_init(BaRecorteGl* r) {
    r->izq = r->arr = r->der = r->aba = 0;
    r->espejoH = r->espejoV = FALSE;
    r->orientacionAuto = TRUE;
    r->modo = 0;
    r->tag = GST_VIDEO_ORIENTATION_IDENTITY;
    r->shader = nullptr;
    gst_base_transform_set_passthrough(GST_BASE_TRANSFORM(r), TRUE);
}

static void ba_recorte_gl_class_init(BaRecorteGlClass* k) {
    auto* go = G_OBJECT_CLASS(k);
    go->set_property = recortePoner;
    go->get_property = recorteLeer;
    using ba::filtros::kVivo;
    const char* lados[] = {"izquierda", "arriba", "derecha", "abajo"};
    for (int i = 0; i < 4; ++i)
        g_object_class_install_property(go, PR_IZQ + i,
                                        g_param_spec_double(lados[i], lados[i], "Fracción que se va (0..1)", 0, 1, 0, kVivo));
    g_object_class_install_property(
        go, PR_ESPEJO_H, g_param_spec_boolean("espejo-h", "Espejo horizontal", "Izquierda <-> derecha", FALSE, kVivo));
    g_object_class_install_property(
        go, PR_ESPEJO_V, g_param_spec_boolean("espejo-v", "Espejo vertical", "Arriba <-> abajo", FALSE, kVivo));
    // El mismo enum que barecorte (lo registra él).
    GstElementFactory* fab = gst_element_factory_find("barecorte");
    GType tipoModo = 0;
    if (fab != nullptr) {
        auto* kr = static_cast<GObjectClass*>(g_type_class_ref(gst_element_factory_get_element_type(fab)));
        if (GParamSpec* ps = g_object_class_find_property(kr, "modo")) tipoModo = ps->value_type;
        g_type_class_unref(kr);
        gst_object_unref(fab);
    }
    if (tipoModo != 0)
        g_object_class_install_property(
            go, PR_MODO,
            g_param_spec_enum("modo", "Modo", "Transparente (mismo tamaño) o recortar (más chico)", tipoModo, 0, kVivo));
    g_object_class_install_property(
        go, PR_ORIENTACION_AUTO,
        g_param_spec_boolean("orientacion-auto", "Orientación automática",
                             "Seguir el tag image-orientation de la cámara (montada al revés)", TRUE, kVivo));
    gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(k), "Recorte y espejo (GPU)", "Filter/Effect/Video",
                                          "Recorta los bordes (en fracciones) y da vuelta la imagen en OpenGL",
                                          "Broadcast Appliance");
    gst_gl_filter_add_rgba_pad_templates(GST_GL_FILTER_CLASS(k));
    GST_BASE_TRANSFORM_CLASS(k)->sink_event = recorteEvento;
    GST_GL_BASE_FILTER_CLASS(k)->gl_stop = recorteParar;
    GST_GL_BASE_FILTER_CLASS(k)->supported_gl_api =
        static_cast<GstGLAPI>(GST_GL_API_OPENGL | GST_GL_API_OPENGL3 | GST_GL_API_GLES2);
    GST_GL_FILTER_CLASS(k)->filter_texture = recorteFiltrar;
    GST_GL_FILTER_CLASS(k)->transform_internal_caps = recorteCaps;
}

// ---------------------------------------------------------------- bacolord3d12
//
// Brillo, contraste y saturación en Direct3D 12 (Windows): un bin con un
// d3d12convert (que los hace al convertir). No tiene gamma: con gamma, el motor
// usa bacolor (no encuentra la propiedad en el par).

#if defined(_WIN32)
G_BEGIN_DECLS
#define BA_TYPE_COLOR_D3D12 (ba_color_d3d12_get_type())
G_DECLARE_FINAL_TYPE(BaColorD3d12, ba_color_d3d12, BA, COLOR_D3D12, GstBin)
G_END_DECLS

struct _BaColorD3d12 {
    GstBin parent;
    GstElement* conv;
    gdouble brillo, contraste, saturacion;
};

G_DEFINE_TYPE(BaColorD3d12, ba_color_d3d12, GST_TYPE_BIN)

enum { PD_0, PD_BRILLO, PD_CONTRASTE, PD_SATURACION };

namespace {
void d3dColorPoner(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    auto* c = BA_COLOR_D3D12(o);
    switch (id) {
        case PD_BRILLO: c->brillo = g_value_get_double(v); break;
        case PD_CONTRASTE: c->contraste = g_value_get_double(v); break;
        case PD_SATURACION: c->saturacion = g_value_get_double(v); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps); return;
    }
    if (c->conv != nullptr)
        g_object_set(c->conv, "brightness", c->brillo, "contrast", c->contraste, "saturation", c->saturacion, nullptr);
}
void d3dColorLeer(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    auto* c = BA_COLOR_D3D12(o);
    switch (id) {
        case PD_BRILLO: g_value_set_double(v, c->brillo); break;
        case PD_CONTRASTE: g_value_set_double(v, c->contraste); break;
        case PD_SATURACION: g_value_set_double(v, c->saturacion); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps); break;
    }
}
} // namespace

static void ba_color_d3d12_init(BaColorD3d12* c) {
    c->brillo = 0;
    c->contraste = c->saturacion = 1;
    c->conv = gst_element_factory_make("d3d12convert", nullptr);
    if (c->conv == nullptr) {
        GST_ERROR_OBJECT(c, "falta d3d12convert");
        return;
    }
    gst_bin_add(GST_BIN(c), c->conv);
    GstPad* in = gst_element_get_static_pad(c->conv, "sink");
    GstPad* out = gst_element_get_static_pad(c->conv, "src");
    GstElementClass* ek = GST_ELEMENT_GET_CLASS(c);
    gst_element_add_pad(GST_ELEMENT(c), gst_ghost_pad_new_from_template("sink", in, gst_element_class_get_pad_template(ek, "sink")));
    gst_element_add_pad(GST_ELEMENT(c), gst_ghost_pad_new_from_template("src", out, gst_element_class_get_pad_template(ek, "src")));
    gst_object_unref(in);
    gst_object_unref(out);
}

static void ba_color_d3d12_class_init(BaColorD3d12Class* k) {
    auto* go = G_OBJECT_CLASS(k);
    go->set_property = d3dColorPoner;
    go->get_property = d3dColorLeer;
    using ba::filtros::kVivo;
    g_object_class_install_property(go, PD_BRILLO, g_param_spec_double("brillo", "Brillo", "Se suma (-1..1)", -1, 1, 0, kVivo));
    g_object_class_install_property(go, PD_CONTRASTE, g_param_spec_double("contraste", "Contraste", "(0..4)", 0, 4, 1, kVivo));
    g_object_class_install_property(go, PD_SATURACION,
                                    g_param_spec_double("saturacion", "Saturación", "0 = blanco y negro (0..2)", 0, 2, 1, kVivo));
    gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(k), "Color (D3D12)", "Filter/Effect/Video",
                                          "Brillo, contraste y saturación en Direct3D 12", "Broadcast Appliance");
    GstCaps* caps = gst_caps_from_string("video/x-raw(memory:D3D12Memory)");
    gst_element_class_add_pad_template(GST_ELEMENT_CLASS(k), gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps));
    gst_element_class_add_pad_template(GST_ELEMENT_CLASS(k), gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps));
    gst_caps_unref(caps);
}
#endif

extern "C" gboolean ba_registrar_gl(GstPlugin* p) {
#if defined(_WIN32)
    if (!gst_element_register(p, "bacolord3d12", GST_RANK_NONE, BA_TYPE_COLOR_D3D12)) return FALSE;
#endif
    // barecorte va antes (barecortegl usa su enum de modo).
    return gst_element_register(p, "bacolorgl", GST_RANK_NONE, BA_TYPE_COLOR_GL) &&
           gst_element_register(p, "barecortegl", GST_RANK_NONE, BA_TYPE_RECORTE_GL);
}
