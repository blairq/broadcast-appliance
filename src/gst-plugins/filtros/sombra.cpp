// ============================================================================
// src/gst-plugins/filtros/sombra.cpp — sombra (o resplandor) y borde difuso de
// un elemento de la escena
//
// Lo que entra es la imagen de UNA capa (con su alfa: un logo, un zócalo, una
// cámara). Lo que sale es la misma imagen en un cuadro más grande (con un
// `margen` alrededor para que la sombra entre), encima de su sombra:
//
//   SOMBRA     la silueta del alfa de la imagen, difuminada (`difuminado`: el
//              radio, gaussiano), agrandada o achicada (`tamano`), corrida
//              `distancia` hacia `angulo` (grados: 0 = a la derecha, 90 = abajo,
//              45 = abajo a la derecha), pintada de `color` con su `alfa`. Con
//              distancia 0 y un color claro es un resplandor.
//   BORDE      `borde`: los últimos píxeles de la imagen se funden a
//              transparente (un borde difuso, aparte de la sombra; la sombra
//              sigue la forma ya fundida).
//
// Las medidas van en píxeles del LIENZO (como se ven en la escena), no de la
// imagen que entra: la capa se agranda o se achica en el mezclador. El que
// arma la capa le dice el tamaño de su caja en el lienzo (`caja-ancho`,
// `caja-alto`, y si entra sin deformarse, `contener`) y el margen (también en
// el lienzo); la caja de la capa en el mezclador se agranda ese margen de cada
// lado. Así la imagen queda en su lugar y del mismo tamaño, y la sombra afuera.
//
//     basombra        en la CPU (pruebas y el mezclador de la CPU)
//     basombragl      en la GPU con OpenGL (GLMemory: Mac, Linux)
//     basombrad3d12   en la GPU con Direct3D 12 (D3D12Memory: Windows), con su
//                     shader HLSL ya compilado adentro (sombra.hlsl)
//
// Los tres hacen la misma cuenta con los mismos números (empaquetar(): 8 vec4):
// la sombra en un punto es la suma, con pesos gaussianos, de una grilla de
// hasta 17 x 17 lecturas bilineales del alfa. En la CPU la grilla se hace en
// dos pasadas (de costado y de arriba abajo: es separable) y da lo mismo.
// Todas las propiedades cambian andando.
//
//   gst-launch-1.0 videotestsrc pattern=ball ! video/x-raw,width=640,height=360 !
//       glupload ! basombragl margen=40 difuminado=20 distancia=15 alfa=0.7 !
//       glimagesink
// ============================================================================

#include "comun.hpp"
#include "../comun/glsl.hpp"

#include <gst/gl/gl.h>
#include <gst/video/video.h>

#include <array>
#include <cmath>
#include <cstring>
#include <vector>

#ifdef _WIN32
#include "../comun/pasada_d3d12.hpp"
#include "sombra_d3d12.h"
#endif

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kMaxK = 8;          ///< la grilla: hasta (2·8 + 1)² lecturas
constexpr int kConstantes = 8 * 4; ///< lo que reciben los shaders (8 vec4)

/// Las propiedades (bajo GST_OBJECT_LOCK).
struct Sombra {
    int cajaAncho = 0, cajaAlto = 0; ///< la caja de la capa en el lienzo (0 = como entra)
    bool contener = false;           ///< la imagen entra en su caja sin deformarse
    int margen = 0;                  ///< alrededor, en píxeles del lienzo
    guint color = 0xFF000000;        ///< ARGB
    double alfa = 0;                 ///< 0 = sin sombra
    double difuminado = 20, distancia = 10, angulo = 45, tamano = 0;
    double borde = 0;
};

enum { P_0, P_CAJA_ANCHO, P_CAJA_ALTO, P_CONTENER, P_MARGEN, P_COLOR, P_ALFA, P_DIFUMINADO, P_DISTANCIA, P_ANGULO, P_TAMANO, P_BORDE };

void instalarPropiedades(GObjectClass* go) {
    using ba::filtros::kVivo;
    const auto num = [&](guint id, const char* n, const char* d, double mn, double mx, double def) {
        g_object_class_install_property(go, id, g_param_spec_double(n, n, d, mn, mx, def, kVivo));
    };
    const auto ent = [&](guint id, const char* n, const char* d, int mx) {
        g_object_class_install_property(go, id, g_param_spec_int(n, n, d, 0, mx, 0, kVivo));
    };
    ent(P_CAJA_ANCHO, "caja-ancho", "Ancho de la caja de la capa en el lienzo (0 = el de la imagen)", 16384);
    ent(P_CAJA_ALTO, "caja-alto", "Alto de la caja de la capa en el lienzo (0 = el de la imagen)", 16384);
    g_object_class_install_property(
        go, P_CONTENER,
        g_param_spec_boolean("contener", "contener", "La imagen entra en su caja sin deformarse (como la pone el mezclador)", FALSE, kVivo));
    ent(P_MARGEN, "margen", "Lo que se agrega alrededor para la sombra (píxeles del lienzo)", 2048);
    g_object_class_install_property(
        go, P_COLOR, g_param_spec_uint("color", "color", "Color de la sombra (ARGB)", 0, G_MAXUINT, 0xFF000000, kVivo));
    num(P_ALFA, "alfa", "Opacidad de la sombra (0 = sin sombra)", 0, 1, 0);
    num(P_DIFUMINADO, "difuminado", "Radio del difuminado de la sombra (píxeles del lienzo)", 0, 500, 20);
    num(P_DISTANCIA, "distancia", "Cuánto se corre la sombra (píxeles del lienzo)", 0, 1000, 10);
    num(P_ANGULO, "angulo", "Hacia dónde cae la sombra (grados: 0 derecha, 90 abajo)", -360, 360, 45);
    num(P_TAMANO, "tamano", "Cuánto más grande (o más chica) que la imagen es la sombra (píxeles del lienzo)", -500, 500, 0);
    num(P_BORDE, "borde", "Borde difuso: cuántos píxeles del borde de la imagen se funden (píxeles del lienzo)", 0, 1000, 0);
}

/// Pone una propiedad; true si cambia el tamaño de lo que sale (hay que renegociar).
bool ponerSombra(GstObject* o, Sombra& s, guint id, const GValue* v) {
    bool tamano = false;
    GST_OBJECT_LOCK(o);
    const auto entero = [&](int& campo) {
        tamano = campo != g_value_get_int(v);
        campo = g_value_get_int(v);
    };
    switch (id) {
        case P_CAJA_ANCHO: entero(s.cajaAncho); break;
        case P_CAJA_ALTO: entero(s.cajaAlto); break;
        case P_CONTENER:
            tamano = s.contener != static_cast<bool>(g_value_get_boolean(v));
            s.contener = g_value_get_boolean(v);
            break;
        case P_MARGEN: entero(s.margen); break;
        case P_COLOR: s.color = g_value_get_uint(v); break;
        case P_ALFA: s.alfa = g_value_get_double(v); break;
        case P_DIFUMINADO: s.difuminado = g_value_get_double(v); break;
        case P_DISTANCIA: s.distancia = g_value_get_double(v); break;
        case P_ANGULO: s.angulo = g_value_get_double(v); break;
        case P_TAMANO: s.tamano = g_value_get_double(v); break;
        case P_BORDE: s.borde = g_value_get_double(v); break;
        default: break;
    }
    GST_OBJECT_UNLOCK(o);
    return tamano;
}

void leerSombra(GstObject* o, const Sombra& s, guint id, GValue* v) {
    GST_OBJECT_LOCK(o);
    switch (id) {
        case P_CAJA_ANCHO: g_value_set_int(v, s.cajaAncho); break;
        case P_CAJA_ALTO: g_value_set_int(v, s.cajaAlto); break;
        case P_CONTENER: g_value_set_boolean(v, s.contener); break;
        case P_MARGEN: g_value_set_int(v, s.margen); break;
        case P_COLOR: g_value_set_uint(v, s.color); break;
        case P_ALFA: g_value_set_double(v, s.alfa); break;
        case P_DIFUMINADO: g_value_set_double(v, s.difuminado); break;
        case P_DISTANCIA: g_value_set_double(v, s.distancia); break;
        case P_ANGULO: g_value_set_double(v, s.angulo); break;
        case P_TAMANO: g_value_set_double(v, s.tamano); break;
        case P_BORDE: g_value_set_double(v, s.borde); break;
        default: break;
    }
    GST_OBJECT_UNLOCK(o);
}

Sombra copiaSombra(GstObject* o, const Sombra& s) {
    GST_OBJECT_LOCK(o);
    const Sombra c = s;
    GST_OBJECT_UNLOCK(o);
    return c;
}

/// Píxeles de la imagen por píxel del lienzo, de costado y de arriba abajo.
void escala(const Sombra& s, int iw, int ih, double& ex, double& ey) {
    ex = ey = 1;
    if (s.cajaAncho <= 0 || s.cajaAlto <= 0 || iw <= 0 || ih <= 0) return;
    ex = static_cast<double>(iw) / s.cajaAncho;
    ey = static_cast<double>(ih) / s.cajaAlto;
    if (s.contener) ex = ey = std::max(ex, ey); // entra entera: la escala del lado que llena
}

/// El margen en píxeles de la imagen (de costado y de arriba abajo).
void margenes(const Sombra& s, int iw, int ih, int& mx, int& my) {
    double ex, ey;
    escala(s, iw, ih, ex, ey);
    mx = static_cast<int>(std::lround(std::max(0, s.margen) * ex));
    my = static_cast<int>(std::lround(std::max(0, s.margen) * ey));
}

/// ¿Hace algo? (si no y sin margen, la CPU deja pasar el buffer tal cual).
bool haceAlgo(const Sombra& s) { return s.alfa > 0 || s.borde > 0 || s.margen > 0; }

GstCaps* capsDeTamano(GstObject* o, const Sombra& v, GstPadDirection dir, GstCaps* caps) {
    const Sombra s = copiaSombra(o, v);
    GstCaps* out = gst_caps_copy(caps);
    for (guint k = 0; k < gst_caps_get_size(out); ++k) {
        GstStructure* st = gst_caps_get_structure(out, k);
        int w = 0, h = 0;
        if (dir == GST_PAD_SINK && gst_structure_get_int(st, "width", &w) && gst_structure_get_int(st, "height", &h)) {
            int mx, my;
            margenes(s, w, h, mx, my);
            gst_structure_set(st, "width", G_TYPE_INT, w + 2 * mx, "height", G_TYPE_INT, h + 2 * my, nullptr);
        } else if (dir == GST_PAD_SINK && s.margen <= 0) {
            // Sin margen sale del mismo tamaño (aunque todavía sea un rango).
        } else {
            gst_structure_set(st, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT, "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, nullptr);
        }
    }
    return out;
}

// --- La cuenta -------------------------------------------------------------------------

/// Los números que reciben los tres (8 vec4), para una entrada iw x ih y una salida ow x oh:
///   c0   ancho y alto de la salida, ancho y alto de la entrada (píxeles)
///   c1   margen (x, y) en píxeles de la entrada, escala ex, ey (píxeles de la entrada por píxel del lienzo)
///   c2   corrimiento de la sombra (x, y, píxeles de la entrada), escala de la silueta (x, y: el `tamano`)
///   c3   paso de la grilla (x, y, píxeles de la entrada), k (la grilla va de -k a k), 1/(2σ²) (σ en pasos)
///   c4   color de la sombra (rgb 0..1), su alfa
///   c5   borde difuso (píxeles del lienzo), la suma de los pesos de una fila de la grilla
void empaquetar(const Sombra& s, int iw, int ih, int ow, int oh, float* c) {
    std::fill(c, c + kConstantes, 0.0f);
    double ex, ey;
    escala(s, iw, ih, ex, ey);
    c[0] = static_cast<float>(ow), c[1] = static_cast<float>(oh), c[2] = static_cast<float>(iw), c[3] = static_cast<float>(ih);
    c[4] = static_cast<float>((ow - iw) / 2), c[5] = static_cast<float>((oh - ih) / 2);
    c[6] = static_cast<float>(ex), c[7] = static_cast<float>(ey);
    const double a = s.angulo * kPi / 180;
    c[8] = static_cast<float>(std::cos(a) * s.distancia * ex);
    c[9] = static_cast<float>(std::sin(a) * s.distancia * ey);
    // La silueta agrandada `tamano` de cada lado: se lee más cerca del centro.
    const double hx = iw / 2.0, hy = ih / 2.0;
    c[10] = static_cast<float>(hx / std::max(1.0, hx + s.tamano * ex));
    c[11] = static_cast<float>(hy / std::max(1.0, hy + s.tamano * ey));
    // La grilla: el radio repartido en k pasos (de a 1,5 píxeles de la entrada como mucho, hasta 8).
    const double radio = std::max(0.0, s.difuminado);
    const double radioPx = radio * std::max(ex, ey);
    const int k = radioPx < 0.5 ? 0 : std::min(kMaxK, static_cast<int>(std::ceil(radioPx / 1.5)));
    double suma = 1;
    if (k > 0) {
        c[12] = static_cast<float>(radio / k * ex);
        c[13] = static_cast<float>(radio / k * ey);
        const double sigma = k / 2.0;
        c[15] = static_cast<float>(1.0 / (2 * sigma * sigma));
        suma = 0;
        for (int i = -k; i <= k; ++i) suma += std::exp(-i * i * static_cast<double>(c[15]));
    }
    c[14] = static_cast<float>(k);
    c[16] = static_cast<float>(((s.color >> 16) & 0xFF) / 255.0);
    c[17] = static_cast<float>(((s.color >> 8) & 0xFF) / 255.0);
    c[18] = static_cast<float>((s.color & 0xFF) / 255.0);
    c[19] = static_cast<float>(std::clamp(s.alfa, 0.0, 1.0) * (((s.color >> 24) & 0xFF) / 255.0));
    c[20] = static_cast<float>(std::max(0.0, s.borde));
    c[21] = static_cast<float>(suma);
}

/// Lo que funde el borde difuso en un punto de la entrada (t, en píxeles de la entrada).
inline float fundido(const float* c, float tx, float ty) {
    const float b = c[20];
    if (b <= 0) return 1;
    const float dx = std::min(tx, c[2] - tx) / c[6], dy = std::min(ty, c[3] - ty) / c[7];
    const float t = std::clamp(std::min(dx, dy) / b, 0.0f, 1.0f);
    return t * t * (3 - 2 * t); // smoothstep
}

const char* kShaderGl = R"glsl(
#ifdef GL_ES
precision highp float;
#endif
varying vec2 v_texcoord;
uniform sampler2D tex;
uniform vec4 c[8];

// Cuánto queda del punto t (píxeles de la entrada): adentro de la imagen y el borde difuso.
float dentro(vec2 t) {
    vec2 I = c[0].zw;
    vec2 d = min(t, I - t);
    float a = clamp(min(d.x, d.y) + 0.5, 0.0, 1.0);
    if (c[5].x > 0.0) {
        vec2 dc = d / c[1].zw;
        a *= smoothstep(0.0, c[5].x, min(dc.x, dc.y));
    }
    return a;
}

void main() {
    vec2 I = c[0].zw;
    vec2 q = v_texcoord * c[0].xy - c[1].xy;
    vec4 col = texture2D(tex, q / I);
    float a = col.a * dentro(q);
    float s = 0.0;
    if (c[4].a > 0.0) {
        vec2 C = I * 0.5;
        vec2 base = q - c[2].xy - C;
        float k = c[3].z;
        for (int j = -8; j <= 8; j++) {
            if (abs(float(j)) > k) continue;
            float wj = exp(-float(j * j) * c[3].w);
            for (int i = -8; i <= 8; i++) {
                if (abs(float(i)) > k) continue;
                float wi = exp(-float(i * i) * c[3].w);
                vec2 t = C + (base - vec2(float(i), float(j)) * c[3].xy) * c[2].zw;
                s += wi * wj * texture2D(tex, t / I).a * dentro(t);
            }
        }
        s = s / (c[5].y * c[5].y) * c[4].a;
    }
    float ra = a + s * (1.0 - a);
    vec3 rgb = ra > 0.0 ? (col.rgb * a + c[4].rgb * s * (1.0 - a)) / ra : vec3(0.0);
    gl_FragColor = vec4(rgb, ra);
}
)glsl";

} // namespace

// ---------------------------------------------------------------- basombra (CPU)

G_BEGIN_DECLS
#define BA_TYPE_SOMBRA (ba_sombra_get_type())
G_DECLARE_FINAL_TYPE(BaSombra, ba_sombra, BA, SOMBRA, GstVideoFilter)
G_END_DECLS

struct _BaSombra {
    GstVideoFilter parent;
    Sombra* v;
    // Del hilo del video: el alfa de la entrada, la pasada de costado y las lecturas precalculadas.
    std::vector<float>* alfa;
    std::vector<float>* fila;
};

G_DEFINE_TYPE(BaSombra, ba_sombra, GST_TYPE_VIDEO_FILTER)

namespace {

void cpuPoner(GObject* o, guint id, const GValue* val, GParamSpec*) {
    auto* s = BA_SOMBRA(o);
    if (ponerSombra(GST_OBJECT(s), *s->v, id, val)) gst_base_transform_reconfigure_src(GST_BASE_TRANSFORM(s));
}
void cpuLeer(GObject* o, guint id, GValue* val, GParamSpec*) {
    auto* s = BA_SOMBRA(o);
    leerSombra(GST_OBJECT(s), *s->v, id, val);
}

GstCaps* cpuCaps(GstBaseTransform* bt, GstPadDirection dir, GstCaps* caps, GstCaps* filtro) {
    GstCaps* out = capsDeTamano(GST_OBJECT(bt), *BA_SOMBRA(bt)->v, dir, caps);
    if (filtro != nullptr) {
        GstCaps* i = gst_caps_intersect_full(filtro, out, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref(out);
        out = i;
    }
    return out;
}

void cpuAntes(GstBaseTransform* bt, GstBuffer* b) {
    ba::filtros::sincronizarValores(bt, b);
    const Sombra s = copiaSombra(GST_OBJECT(bt), *BA_SOMBRA(bt)->v);
    gst_base_transform_set_passthrough(bt, !haceAlgo(s));
}

/// Lecturas de una pasada: para cada punto de la salida y cada paso de la grilla,
/// de qué píxel de la entrada (el de la izquierda) y cuánto del siguiente.
struct Lectura {
    int p;
    float f;
};

GstFlowReturn cpuCuadro(GstVideoFilter* f, GstVideoFrame* in, GstVideoFrame* out) {
    auto* s = BA_SOMBRA(f);
    const Sombra a = copiaSombra(GST_OBJECT(s), *s->v);
    const int ow = GST_VIDEO_FRAME_WIDTH(out), oh = GST_VIDEO_FRAME_HEIGHT(out);
    const int iw = GST_VIDEO_FRAME_WIDTH(in), ih = GST_VIDEO_FRAME_HEIGHT(in);
    std::array<float, kConstantes> c{};
    empaquetar(a, iw, ih, ow, oh, c.data());
    ba::filtros::Orden o{};
    if (!ba::filtros::ordenDe(GST_VIDEO_FRAME_FORMAT(out), o) || o.a < 0) return GST_FLOW_NOT_NEGOTIATED;
    const auto* src = static_cast<const std::uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(in, 0));
    auto* dst = static_cast<std::uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(out, 0));
    const int is = GST_VIDEO_FRAME_PLANE_STRIDE(in, 0), os = GST_VIDEO_FRAME_PLANE_STRIDE(out, 0);
    const int mx = static_cast<int>(c[4]), my = static_cast<int>(c[5]);

    // El alfa de la entrada con el borde difuso (en el centro de cada píxel).
    std::vector<float>& A = *s->alfa;
    A.resize(static_cast<size_t>(iw) * ih);
    for (int y = 0; y < ih; ++y) {
        const std::uint8_t* p = src + static_cast<ptrdiff_t>(y) * is;
        for (int x = 0; x < iw; ++x) A[static_cast<size_t>(y) * iw + x] = p[x * 4 + o.a] / 255.0f * fundido(c.data(), x + 0.5f, y + 0.5f);
    }

    // La sombra: separable. Cada lectura de la grilla en x es bilineal de costado
    // (con ceros afuera) y la de y, lineal entre filas: lo mismo que la textura.
    std::vector<float> S(static_cast<size_t>(ow) * oh, 0.0f);
    if (c[19] > 0) {
        const int k = static_cast<int>(c[14]);
        std::vector<float> w(2 * k + 1);
        for (int i = -k; i <= k; ++i) w[i + k] = std::exp(-static_cast<float>(i * i) * c[15]);
        const auto lecturas = [&](int salida, float margen, float corre, float esc, float paso, int total) {
            std::vector<Lectura> l(static_cast<size_t>(salida) * (2 * k + 1));
            const float C = total / 2.0f;
            for (int X = 0; X < salida; ++X)
                for (int i = -k; i <= k; ++i) {
                    const float t = C + ((X + 0.5f) - margen - corre - C - i * paso) * esc; // píxeles de la entrada
                    const float fx = t - 0.5f;
                    const int x0 = static_cast<int>(std::floor(fx));
                    l[static_cast<size_t>(X) * (2 * k + 1) + (i + k)] = {x0, fx - x0};
                }
            return l;
        };
        const std::vector<Lectura> lx = lecturas(ow, c[4], c[8], c[10], c[12], iw);
        const std::vector<Lectura> ly = lecturas(oh, c[5], c[9], c[11], c[13], ih);
        // De costado: para cada fila de la entrada, cada columna de la salida.
        std::vector<float>& H = *s->fila;
        H.assign(static_cast<size_t>(ow) * ih, 0.0f);
        for (int y = 0; y < ih; ++y) {
            const float* fa = &A[static_cast<size_t>(y) * iw];
            float* h = &H[static_cast<size_t>(y) * ow];
            for (int X = 0; X < ow; ++X) {
                float acc = 0;
                const Lectura* l = &lx[static_cast<size_t>(X) * (2 * k + 1)];
                for (int i = 0; i <= 2 * k; ++i) {
                    const int x0 = l[i].p;
                    const float v0 = (x0 >= 0 && x0 < iw) ? fa[x0] : 0.0f;
                    const float v1 = (x0 + 1 >= 0 && x0 + 1 < iw) ? fa[x0 + 1] : 0.0f;
                    acc += w[i] * (v0 + (v1 - v0) * l[i].f);
                }
                h[X] = acc;
            }
        }
        // De arriba abajo.
        const float norma = c[19] / (c[21] * c[21]);
        for (int Y = 0; Y < oh; ++Y) {
            const Lectura* l = &ly[static_cast<size_t>(Y) * (2 * k + 1)];
            float* sf = &S[static_cast<size_t>(Y) * ow];
            for (int j = 0; j <= 2 * k; ++j) {
                const int y0 = l[j].p;
                const float f0 = (y0 >= 0 && y0 < ih) ? w[j] * (1 - l[j].f) : 0.0f;
                const float f1 = (y0 + 1 >= 0 && y0 + 1 < ih) ? w[j] * l[j].f : 0.0f;
                if (f0 == 0 && f1 == 0) continue;
                const float* h0 = f0 != 0 ? &H[static_cast<size_t>(y0) * ow] : nullptr;
                const float* h1 = f1 != 0 ? &H[static_cast<size_t>(y0 + 1) * ow] : nullptr;
                for (int X = 0; X < ow; ++X) sf[X] += (h0 ? f0 * h0[X] : 0.0f) + (h1 ? f1 * h1[X] : 0.0f);
            }
            for (int X = 0; X < ow; ++X) sf[X] *= norma;
        }
    }

    // La imagen encima de su sombra (alfa no premultiplicado).
    const float sr = c[16] * 255, sg = c[17] * 255, sb = c[18] * 255;
    for (int Y = 0; Y < oh; ++Y) {
        std::uint8_t* d = dst + static_cast<ptrdiff_t>(Y) * os;
        const int y = Y - my;
        for (int X = 0; X < ow; ++X, d += 4) {
            const int x = X - mx;
            const float sa = S[static_cast<size_t>(Y) * ow + X];
            float ia = 0, r = 0, g = 0, b = 0;
            if (x >= 0 && x < iw && y >= 0 && y < ih) {
                const std::uint8_t* p = src + static_cast<ptrdiff_t>(y) * is + x * 4;
                ia = A[static_cast<size_t>(y) * iw + x];
                r = p[o.r], g = p[o.g], b = p[o.b];
            }
            const float ra = ia + sa * (1 - ia);
            if (ra <= 0) {
                d[o.r] = d[o.g] = d[o.b] = d[o.a] = 0;
                continue;
            }
            const float qs = sa * (1 - ia);
            d[o.r] = static_cast<std::uint8_t>(std::clamp((r * ia + sr * qs) / ra + 0.5f, 0.0f, 255.0f));
            d[o.g] = static_cast<std::uint8_t>(std::clamp((g * ia + sg * qs) / ra + 0.5f, 0.0f, 255.0f));
            d[o.b] = static_cast<std::uint8_t>(std::clamp((b * ia + sb * qs) / ra + 0.5f, 0.0f, 255.0f));
            d[o.a] = ba::filtros::a8(ra);
        }
    }
    return GST_FLOW_OK;
}

void cpuFinalizar(GObject* o) {
    auto* s = BA_SOMBRA(o);
    delete s->v;
    delete s->alfa;
    delete s->fila;
    G_OBJECT_CLASS(ba_sombra_parent_class)->finalize(o);
}

} // namespace

static void ba_sombra_init(BaSombra* s) {
    s->v = new Sombra();
    s->alfa = new std::vector<float>();
    s->fila = new std::vector<float>();
}

static void ba_sombra_class_init(BaSombraClass* k) {
    auto* go = G_OBJECT_CLASS(k);
    go->set_property = cpuPoner;
    go->get_property = cpuLeer;
    go->finalize = cpuFinalizar;
    instalarPropiedades(go);
    gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(k), "Sombra", "Filter/Effect/Video",
                                          "Sombra (o resplandor) y borde difuso de un elemento de la escena",
                                          "Broadcast Appliance");
    GstCaps* caps = gst_caps_from_string("video/x-raw, format=(string){ RGBA, BGRA, ARGB, ABGR }");
    gst_element_class_add_pad_template(GST_ELEMENT_CLASS(k), gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps));
    gst_element_class_add_pad_template(GST_ELEMENT_CLASS(k), gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps));
    gst_caps_unref(caps);
    GST_BASE_TRANSFORM_CLASS(k)->transform_caps = cpuCaps;
    GST_BASE_TRANSFORM_CLASS(k)->before_transform = cpuAntes;
    GST_VIDEO_FILTER_CLASS(k)->transform_frame = cpuCuadro;
}

// ---------------------------------------------------------------- basombragl

G_BEGIN_DECLS
#define BA_TYPE_SOMBRA_GL (ba_sombra_gl_get_type())
G_DECLARE_FINAL_TYPE(BaSombraGl, ba_sombra_gl, BA, SOMBRA_GL, GstGLFilter)
G_END_DECLS

struct _BaSombraGl {
    GstGLFilter parent;
    Sombra* v;
    GstGLShader* shader;
};

G_DEFINE_TYPE(BaSombraGl, ba_sombra_gl, GST_TYPE_GL_FILTER)

namespace {

void glPoner(GObject* o, guint id, const GValue* val, GParamSpec*) {
    auto* s = BA_SOMBRA_GL(o);
    if (ponerSombra(GST_OBJECT(s), *s->v, id, val)) gst_base_transform_reconfigure_src(GST_BASE_TRANSFORM(s));
}
void glLeer(GObject* o, guint id, GValue* val, GParamSpec*) {
    auto* s = BA_SOMBRA_GL(o);
    leerSombra(GST_OBJECT(s), *s->v, id, val);
}

GstCaps* glCaps(GstGLFilter* f, GstPadDirection dir, GstCaps* caps, GstCaps*) {
    return capsDeTamano(GST_OBJECT(f), *BA_SOMBRA_GL(f)->v, dir, caps);
}

gboolean glFiltrar(GstGLFilter* f, GstGLMemory* in, GstGLMemory* out) {
    auto* s = BA_SOMBRA_GL(f);
    if (s->shader == nullptr) {
        std::string e;
        s->shader = ba::glsl::compilar(GST_GL_BASE_FILTER(f)->context, kShaderGl, e);
        if (s->shader == nullptr) {
            GST_ELEMENT_ERROR(f, RESOURCE, NOT_FOUND, ("el shader de la sombra no compiló"), ("%s", e.c_str()));
            return FALSE;
        }
    }
    const Sombra a = copiaSombra(GST_OBJECT(s), *s->v);
    std::array<float, kConstantes> c{};
    empaquetar(a, GST_VIDEO_INFO_WIDTH(&f->in_info), GST_VIDEO_INFO_HEIGHT(&f->in_info), GST_VIDEO_INFO_WIDTH(&f->out_info),
               GST_VIDEO_INFO_HEIGHT(&f->out_info), c.data());
    gst_gl_shader_use(s->shader);
    gst_gl_shader_set_uniform_4fv(s->shader, "c", kConstantes / 4, c.data());
    gst_gl_filter_render_to_target_with_shader(f, in, out, s->shader);
    return TRUE;
}

void glParar(GstGLBaseFilter* bf) {
    auto* s = BA_SOMBRA_GL(bf);
    if (s->shader != nullptr) gst_object_unref(s->shader);
    s->shader = nullptr;
    GST_GL_BASE_FILTER_CLASS(ba_sombra_gl_parent_class)->gl_stop(bf);
}

void glFinalizar(GObject* o) {
    delete BA_SOMBRA_GL(o)->v;
    G_OBJECT_CLASS(ba_sombra_gl_parent_class)->finalize(o);
}

} // namespace

static void ba_sombra_gl_init(BaSombraGl* s) {
    s->v = new Sombra();
    s->shader = nullptr;
}

static void ba_sombra_gl_class_init(BaSombraGlClass* k) {
    auto* go = G_OBJECT_CLASS(k);
    go->set_property = glPoner;
    go->get_property = glLeer;
    go->finalize = glFinalizar;
    instalarPropiedades(go);
    gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(k), "Sombra (GPU)", "Filter/Effect/Video",
                                          "Sombra (o resplandor) y borde difuso de un elemento de la escena, en OpenGL",
                                          "Broadcast Appliance");
    gst_gl_filter_add_rgba_pad_templates(GST_GL_FILTER_CLASS(k));
    GST_GL_BASE_FILTER_CLASS(k)->gl_stop = glParar;
    GST_GL_BASE_FILTER_CLASS(k)->supported_gl_api =
        static_cast<GstGLAPI>(GST_GL_API_OPENGL | GST_GL_API_OPENGL3 | GST_GL_API_GLES2);
    GST_GL_FILTER_CLASS(k)->filter_texture = glFiltrar;
    GST_GL_FILTER_CLASS(k)->transform_internal_caps = glCaps;
}

// ---------------------------------------------------------------- basombrad3d12

#ifdef _WIN32

G_BEGIN_DECLS
#define BA_TYPE_SOMBRA_D3D12 (ba_sombra_d3d12_get_type())
G_DECLARE_FINAL_TYPE(BaSombraD3d12, ba_sombra_d3d12, BA, SOMBRA_D3D12, GstBaseTransform)
G_END_DECLS

struct _BaSombraD3d12 {
    GstBaseTransform parent;
    Sombra* v;
    ba::d3d12::Pasada* r;
};

G_DEFINE_TYPE(BaSombraD3d12, ba_sombra_d3d12, GST_TYPE_BASE_TRANSFORM)

namespace {

constexpr ba::d3d12::Bytecode kVs{kSombraVs, sizeof(kSombraVs)};
constexpr ba::d3d12::Bytecode kPs{kSombraPs, sizeof(kSombraPs)};

void d3dPoner(GObject* o, guint id, const GValue* val, GParamSpec*) {
    auto* s = BA_SOMBRA_D3D12(o);
    if (ponerSombra(GST_OBJECT(s), *s->v, id, val)) gst_base_transform_reconfigure_src(GST_BASE_TRANSFORM(s));
}
void d3dLeer(GObject* o, guint id, GValue* val, GParamSpec*) {
    auto* s = BA_SOMBRA_D3D12(o);
    leerSombra(GST_OBJECT(s), *s->v, id, val);
}
void d3dContexto(GstElement* e, GstContext* c) {
    BA_SOMBRA_D3D12(e)->r->contexto(e, c);
    GST_ELEMENT_CLASS(ba_sombra_d3d12_parent_class)->set_context(e, c);
}
gboolean d3dEmpezar(GstBaseTransform* bt) { return BA_SOMBRA_D3D12(bt)->r->empezar(GST_ELEMENT(bt)) ? TRUE : FALSE; }
gboolean d3dParar(GstBaseTransform* bt) {
    BA_SOMBRA_D3D12(bt)->r->parar();
    return TRUE;
}
gboolean d3dConsulta(GstBaseTransform* bt, GstPadDirection dir, GstQuery* q) {
    if (BA_SOMBRA_D3D12(bt)->r->consulta(GST_ELEMENT(bt), q)) return TRUE;
    return GST_BASE_TRANSFORM_CLASS(ba_sombra_d3d12_parent_class)->query(bt, dir, q);
}
GstCaps* d3dCaps(GstBaseTransform* bt, GstPadDirection dir, GstCaps* caps, GstCaps* filtro) {
    GstCaps* out = capsDeTamano(GST_OBJECT(bt), *BA_SOMBRA_D3D12(bt)->v, dir, caps);
    if (filtro != nullptr) {
        GstCaps* i = gst_caps_intersect_full(filtro, out, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref(out);
        out = i;
    }
    return out;
}
gboolean d3dPonerCaps(GstBaseTransform* bt, GstCaps* entra, GstCaps* sale) {
    return BA_SOMBRA_D3D12(bt)->r->ponerCaps(GST_OBJECT(bt), entra, sale, kVs, kPs) ? TRUE : FALSE;
}
gboolean d3dProponer(GstBaseTransform* bt, GstQuery* decidida, GstQuery* q) {
    if (!GST_BASE_TRANSFORM_CLASS(ba_sombra_d3d12_parent_class)->propose_allocation(bt, decidida, q)) return FALSE;
    return BA_SOMBRA_D3D12(bt)->r->proponer(decidida, q) ? TRUE : FALSE;
}
gboolean d3dDecidir(GstBaseTransform* bt, GstQuery* q) {
    if (!BA_SOMBRA_D3D12(bt)->r->decidir(q)) return FALSE;
    return GST_BASE_TRANSFORM_CLASS(ba_sombra_d3d12_parent_class)->decide_allocation(bt, q);
}
GstFlowReturn d3dCuadro(GstBaseTransform* bt, GstBuffer* in, GstBuffer* out) {
    auto* s = BA_SOMBRA_D3D12(bt);
    ba::filtros::sincronizarValores(bt, in);
    const Sombra a = copiaSombra(GST_OBJECT(s), *s->v);
    std::array<float, kConstantes> c{};
    empaquetar(a, GST_VIDEO_INFO_WIDTH(&s->r->entra), GST_VIDEO_INFO_HEIGHT(&s->r->entra), GST_VIDEO_INFO_WIDTH(&s->r->sale),
               GST_VIDEO_INFO_HEIGHT(&s->r->sale), c.data());
    return s->r->dibujar(GST_OBJECT(s), in, out, c.data(), c.size());
}
void d3dFinalizar(GObject* o) {
    auto* s = BA_SOMBRA_D3D12(o);
    s->r->parar();
    delete s->r;
    delete s->v;
    G_OBJECT_CLASS(ba_sombra_d3d12_parent_class)->finalize(o);
}

} // namespace

static void ba_sombra_d3d12_init(BaSombraD3d12* s) {
    s->v = new Sombra();
    s->r = new ba::d3d12::Pasada();
}

static void ba_sombra_d3d12_class_init(BaSombraD3d12Class* k) {
    auto* go = G_OBJECT_CLASS(k);
    go->set_property = d3dPoner;
    go->get_property = d3dLeer;
    go->finalize = d3dFinalizar;
    instalarPropiedades(go);
    gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(k), "Sombra (D3D12)", "Filter/Effect/Video",
                                          "Sombra (o resplandor) y borde difuso de un elemento de la escena, en Direct3D 12",
                                          "Broadcast Appliance");
    GstCaps* caps = gst_caps_from_string("video/x-raw(memory:D3D12Memory), format=(string){ RGBA, BGRA }");
    gst_element_class_add_pad_template(GST_ELEMENT_CLASS(k), gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps));
    gst_element_class_add_pad_template(GST_ELEMENT_CLASS(k), gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps));
    gst_caps_unref(caps);
    GST_ELEMENT_CLASS(k)->set_context = d3dContexto;
    auto* bt = GST_BASE_TRANSFORM_CLASS(k);
    bt->passthrough_on_same_caps = FALSE;
    bt->start = d3dEmpezar;
    bt->stop = d3dParar;
    bt->query = d3dConsulta;
    bt->transform_caps = d3dCaps;
    bt->set_caps = d3dPonerCaps;
    bt->propose_allocation = d3dProponer;
    bt->decide_allocation = d3dDecidir;
    bt->transform = d3dCuadro;
}

#endif // _WIN32

extern "C" gboolean ba_registrar_sombra(GstPlugin* p) {
    gboolean ok = gst_element_register(p, "basombra", GST_RANK_NONE, BA_TYPE_SOMBRA) &&
                  gst_element_register(p, "basombragl", GST_RANK_NONE, BA_TYPE_SOMBRA_GL);
#ifdef _WIN32
    // El motor lo elige cuando el video va en D3D12.
    ok = ok && gst_element_register(p, "basombrad3d12", GST_RANK_NONE, BA_TYPE_SOMBRA_D3D12);
#endif
    return ok;
}
