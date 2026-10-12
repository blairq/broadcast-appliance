// ============================================================================
// src/gst-plugins/filtros/vista360.cpp — unir y enderezar cámaras: la 360 de
// ojos de pez, una fila de cámaras comunes (la panorámica) o una equirectangular,
// vistas como una sola imagen (las tarjetas «Vista 360» y «Panorámica» del grafo)
//
// LO QUE ENTRA (`modo`):
//   equirect   una imagen equirectangular (360° de costado, 180° de arriba abajo)
//   ojo        un ojo de pez (todo el cuadro)
//   dos-ojos   dos ojos de pez lado a lado (mitad izquierda = lente 1, derecha =
//              lente 2, que mira para atrás)
//   lentes     `lentes` cámaras lado a lado, cada una en su franja igual del
//              cuadro (así las junta la tarjeta Panorámica, un stream por cámara),
//              cada una con su tipo de lente
//
// CADA LENTE (lenteN-*, hasta 8) — su deformación y cómo encaja:
//   tipo       ojo (ojo de pez equidistante) o normal (una cámara común)
//   x, y       el centro óptico (fracciones de su franja: 0.5 = el medio)
//   radio      ojo de pez: el radio del círculo (fracción del alto)
//   campo      lo que cubre (grados): el círculo en un ojo de pez, el ancho en una normal
//   yaw, pitch, roll   cómo está montada (la matriz de rotación): hacia dónde
//              mira de costado, de arriba abajo y girada sobre su eje
//   k1, k2     la distorsión: ojo de pez, r ∝ θ·(1 + k1·θ² + k2·θ⁴) normalizado al
//              borde del círculo; normal, Brown (r·(1 + k1·r² + k2·r⁴))
// Donde se ven dos, se mezclan en una franja de `mezcla` grados (sin costura).
//
// LO QUE SALE (`ver`):
//   vista      lo que vería una cámara común que mira hacia `yaw`/`pitch` con un
//              campo vertical `fov` (un PTZ virtual)
//   tira       la panorámica cilíndrica: `tira-campo` grados de costado (centrada
//              en `yaw`), con los píxeles cuadrados; de fábrica (ancho y alto 0), el
//              tamaño de lo que entra = la suma de las resoluciones de las cámaras
//   panorama   la esfera entera, equirectangular
//   entrada    lo que entra tal cual, con los círculos de los ojos de pez y los
//              límites de las franjas (para encajar centro y radio)
// Tamaño `ancho` x `alto` (0 = como entra).
//
// LA GUÍA (guia-*): un dibujo encima para calibrar a mano: rectángulo, círculo,
// grilla, cruz (en píxeles de la salida) o esfera (paralelos y meridianos cada
// 15°: con todo a nivel, el horizonte queda en el ecuador).
//
//     ba360         en la CPU (una tabla de dónde leer cada píxel, que se rehace
//                   solo cuando cambia algo; para pruebas y cuadros chicos)
//     ba360gl       en la GPU con OpenGL (GLMemory: Mac, Linux)
//     ba360d3d12    en la GPU con Direct3D 12 (D3D12Memory: Windows), con su
//                   shader HLSL ya compilado adentro (vista360.hlsl)
//
// Los tres hacen la misma cuenta con los mismos números (empaquetar(): 48
// vec4); el motor elige «ba360gl» o «ba360d3d12» cuando el video va en la GPU
// (Piezas: el par «<elemento>gl» / «<elemento>d3d12»), así la imagen no baja a
// la memoria de la máquina. Todas las propiedades cambian andando.
//
//   gst-launch-1.0 filesrc location=360.jpg ! decodebin ! videoconvert ! imagefreeze !
//       glupload ! ba360gl yaw=90 fov=70 ancho=1280 alto=720 guia=esfera ! glimagesink
// ============================================================================

#include "comun.hpp"
#include "../comun/glsl.hpp"

#include <gst/gl/gl.h>
#include <gst/video/video.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <gst/d3d12/gstd3d12.h>
#include <wrl/client.h>

#include "vista360_d3d12.h"
#endif

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kMaxLentes = 8;
constexpr int kCabeza = 8;                               ///< vec4 generales
constexpr int kPorLenteV4 = 5;                           ///< vec4 por lente
constexpr int kVec4 = kCabeza + kPorLenteV4 * kMaxLentes; ///< 48
constexpr int kConstantes = kVec4 * 4;                   ///< 192 (lo que reciben los shaders)

enum Modo { kEquirect = 0, kOjo = 1, kDosOjos = 2, kLentes = 3 };
enum Ver { kVista = 0, kPanorama = 1, kEntrada = 2, kTira = 3 };
enum Guia { kNinguna = 0, kRectangulo, kCirculo, kGrilla, kCruz, kEsfera };
enum TipoLente { kLenteOjo = 0, kLenteNormal = 1 };

struct Lente {
    int tipo = kLenteOjo;
    double x = 0.5, y = 0.5, radio = 0.5, campo = 190, yaw = 0, pitch = 0, roll = 0, k1 = 0, k2 = 0;
};

/// Las propiedades (bajo GST_OBJECT_LOCK).
struct Vista {
    double yaw = 0, pitch = 0, fov = 90, tiraCampo = 180;
    int ancho = 0, alto = 0;
    int modo = kEquirect, ver = kVista, lentes = 2;
    double mezcla = 5;
    Lente l[kMaxLentes];
    int guia = kNinguna;
    double gx = 0.5, gy = 0.5, gAncho = 0.5, gAlto = 0.5, gGrosor = 3;
    guint gColor = 0xFF00FF00; ///< ARGB
};

// --- Las propiedades -------------------------------------------------------------------

enum {
    P_0,
    P_YAW, P_PITCH, P_FOV, P_ANCHO, P_ALTO, P_MODO, P_VER, P_MEZCLA, P_LENTES, P_TIRA,
    P_GUIA, P_GX, P_GY, P_GANCHO, P_GALTO, P_GGROSOR, P_GCOLOR,
    P_LENTE // + kPorLente por lente: tipo y los números
};
constexpr int kNumLente = 9;                // los números de una lente
constexpr int kPorLente = kNumLente + 1;    // + el tipo
const char* const kLenteProp[kNumLente] = {"x", "y", "radio", "campo", "yaw", "pitch", "roll", "k1", "k2"};
const char* const kLenteDesc[kNumLente] = {
    "Centro óptico, de costado (fracción de su franja)", "Centro óptico, de arriba abajo (fracción del alto)",
    "Ojo de pez: radio del círculo (fracción del alto)", "Lo que cubre (grados): el círculo, o el ancho si es normal",
    "Montaje: hacia dónde mira, de costado (grados)", "Montaje: inclinación (grados)", "Montaje: rotación sobre su eje (grados)",
    "Distorsión k1", "Distorsión k2"};
const double kLenteMin[kNumLente] = {-0.5, -0.5, 0.05, 10, -180, -90, -180, -1, -1};
const double kLenteMax[kNumLente] = {1.5, 1.5, 1.5, 270, 180, 90, 180, 1, 1};
const double kLenteDef[kNumLente] = {0.5, 0.5, 0.5, 190, 0, 0, 0, 0, 0};

double* campoLente(Lente& l, int i) {
    double* c[kNumLente] = {&l.x, &l.y, &l.radio, &l.campo, &l.yaw, &l.pitch, &l.roll, &l.k1, &l.k2};
    return c[i];
}

GType registrarEnum(GType* t, const char* nombre, const GEnumValue* v) {
    if (g_once_init_enter(t)) g_once_init_leave(t, g_enum_register_static(nombre, v));
    return *t;
}
GType tipoModo() {
    static GType t = 0;
    static const GEnumValue v[] = {{kEquirect, "Equirectangular", "equirect"},
                                   {kOjo, "Un ojo de pez", "ojo"},
                                   {kDosOjos, "Dos ojos de pez lado a lado", "dos-ojos"},
                                   {kLentes, "Varias cámaras lado a lado", "lentes"},
                                   {0, nullptr, nullptr}};
    return registrarEnum(&t, "BaVista360Modo", v);
}
GType tipoVer() {
    static GType t = 0;
    static const GEnumValue v[] = {{kVista, "La vista (una cámara común)", "vista"},
                                   {kPanorama, "La esfera entera (equirectangular)", "panorama"},
                                   {kEntrada, "La entrada, con los círculos y las franjas", "entrada"},
                                   {kTira, "La panorámica (cilíndrica)", "tira"},
                                   {0, nullptr, nullptr}};
    return registrarEnum(&t, "BaVista360Ver", v);
}
GType tipoGuia() {
    static GType t = 0;
    static const GEnumValue v[] = {{kNinguna, "Ninguna", "ninguna"},
                                   {kRectangulo, "Rectángulo", "rectangulo"},
                                   {kCirculo, "Círculo", "circulo"},
                                   {kGrilla, "Grilla", "grilla"},
                                   {kCruz, "Cruz", "cruz"},
                                   {kEsfera, "Esfera (paralelos y meridianos cada 15°)", "esfera"},
                                   {0, nullptr, nullptr}};
    return registrarEnum(&t, "BaVista360Guia", v);
}
GType tipoLente() {
    static GType t = 0;
    static const GEnumValue v[] = {{kLenteOjo, "Ojo de pez", "ojo"}, {kLenteNormal, "Común (rectilínea)", "normal"}, {0, nullptr, nullptr}};
    return registrarEnum(&t, "BaVista360Lente", v);
}

void instalarPropiedades(GObjectClass* go) {
    using ba::filtros::kVivo;
    const auto num = [&](guint id, const char* n, const char* d, double mn, double mx, double def) {
        g_object_class_install_property(go, id, g_param_spec_double(n, n, d, mn, mx, def, kVivo));
    };
    num(P_YAW, "yaw", "Hacia dónde mira, de costado (grados): el pan", -180, 180, 0);
    num(P_PITCH, "pitch", "Hacia arriba o abajo (grados): el tilt", -90, 90, 0);
    num(P_FOV, "fov", "La vista: cuánto abre, vertical (grados): el zoom", 10, 170, 90);
    num(P_TIRA, "tira-campo", "La panorámica: cuántos grados de costado", 10, 360, 180);
    g_object_class_install_property(
        go, P_ANCHO, g_param_spec_int("ancho", "ancho", "Ancho del cuadro que sale (0 = como entra)", 0, 16384, 0, kVivo));
    g_object_class_install_property(
        go, P_ALTO, g_param_spec_int("alto", "alto", "Alto del cuadro que sale (0 = como entra)", 0, 16384, 0, kVivo));
    g_object_class_install_property(go, P_MODO, g_param_spec_enum("modo", "modo", "Lo que entra", tipoModo(), kEquirect, kVivo));
    g_object_class_install_property(go, P_VER, g_param_spec_enum("ver", "ver", "Lo que sale", tipoVer(), kVista, kVivo));
    g_object_class_install_property(
        go, P_LENTES, g_param_spec_int("lentes", "lentes", "Con modo lentes: cuántas cámaras lado a lado", 1, kMaxLentes, 2, kVivo));
    num(P_MEZCLA, "mezcla", "Franja donde se mezclan dos lentes (grados)", 0, 40, 5);
    g_object_class_install_property(go, P_GUIA, g_param_spec_enum("guia", "guia", "El dibujo para calibrar", tipoGuia(), kNinguna, kVivo));
    num(P_GX, "guia-x", "Centro de la guía, de costado (fracción de la salida)", -1, 2, 0.5);
    num(P_GY, "guia-y", "Centro de la guía, de arriba abajo (fracción de la salida)", -1, 2, 0.5);
    num(P_GANCHO, "guia-ancho", "Ancho de la guía (fracción; en la grilla, el paso)", 0, 2, 0.5);
    num(P_GALTO, "guia-alto", "Alto de la guía (fracción; en la grilla, el paso)", 0, 2, 0.5);
    num(P_GGROSOR, "guia-grosor", "Grosor de la línea (píxeles)", 0.5, 40, 3);
    g_object_class_install_property(
        go, P_GCOLOR, g_param_spec_uint("guia-color", "guia-color", "Color de la guía (ARGB)", 0, G_MAXUINT, 0xFF00FF00, kVivo));
    for (int l = 0; l < kMaxLentes; ++l) {
        const std::string b = "lente" + std::to_string(l + 1) + "-";
        const guint base = P_LENTE + static_cast<guint>(l * kPorLente);
        g_object_class_install_property(
            go, base, g_param_spec_enum(g_intern_string((b + "tipo").c_str()), g_intern_string((b + "tipo").c_str()),
                                        "Ojo de pez o común", tipoLente(), kLenteOjo, kVivo));
        for (int i = 0; i < kNumLente; ++i) {
            const std::string n = b + kLenteProp[i];
            g_object_class_install_property(
                go, base + 1 + static_cast<guint>(i),
                g_param_spec_double(g_intern_string(n.c_str()), g_intern_string(n.c_str()), kLenteDesc[i], kLenteMin[i],
                                    kLenteMax[i], kLenteDef[i], kVivo));
        }
    }
}

/// Pone una propiedad; true si cambió el tamaño (hay que renegociar).
bool ponerVista(GstObject* o, Vista& v, guint id, const GValue* val) {
    bool tamano = false;
    GST_OBJECT_LOCK(o);
    switch (id) {
        case P_YAW: v.yaw = g_value_get_double(val); break;
        case P_PITCH: v.pitch = g_value_get_double(val); break;
        case P_FOV: v.fov = g_value_get_double(val); break;
        case P_TIRA: v.tiraCampo = g_value_get_double(val); break;
        case P_ANCHO: tamano = v.ancho != g_value_get_int(val); v.ancho = g_value_get_int(val); break;
        case P_ALTO: tamano = v.alto != g_value_get_int(val); v.alto = g_value_get_int(val); break;
        case P_MODO: v.modo = g_value_get_enum(val); break;
        case P_VER: v.ver = g_value_get_enum(val); break;
        case P_LENTES: v.lentes = g_value_get_int(val); break;
        case P_MEZCLA: v.mezcla = g_value_get_double(val); break;
        case P_GUIA: v.guia = g_value_get_enum(val); break;
        case P_GX: v.gx = g_value_get_double(val); break;
        case P_GY: v.gy = g_value_get_double(val); break;
        case P_GANCHO: v.gAncho = g_value_get_double(val); break;
        case P_GALTO: v.gAlto = g_value_get_double(val); break;
        case P_GGROSOR: v.gGrosor = g_value_get_double(val); break;
        case P_GCOLOR: v.gColor = g_value_get_uint(val); break;
        default:
            if (id >= P_LENTE && id < P_LENTE + kMaxLentes * kPorLente) {
                Lente& l = v.l[(id - P_LENTE) / kPorLente];
                const int i = static_cast<int>((id - P_LENTE) % kPorLente);
                if (i == 0)
                    l.tipo = g_value_get_enum(val);
                else
                    *campoLente(l, i - 1) = g_value_get_double(val);
            }
            break;
    }
    GST_OBJECT_UNLOCK(o);
    return tamano;
}

void leerVista(GstObject* o, Vista& v, guint id, GValue* val) {
    GST_OBJECT_LOCK(o);
    switch (id) {
        case P_YAW: g_value_set_double(val, v.yaw); break;
        case P_PITCH: g_value_set_double(val, v.pitch); break;
        case P_FOV: g_value_set_double(val, v.fov); break;
        case P_TIRA: g_value_set_double(val, v.tiraCampo); break;
        case P_ANCHO: g_value_set_int(val, v.ancho); break;
        case P_ALTO: g_value_set_int(val, v.alto); break;
        case P_MODO: g_value_set_enum(val, v.modo); break;
        case P_VER: g_value_set_enum(val, v.ver); break;
        case P_LENTES: g_value_set_int(val, v.lentes); break;
        case P_MEZCLA: g_value_set_double(val, v.mezcla); break;
        case P_GUIA: g_value_set_enum(val, v.guia); break;
        case P_GX: g_value_set_double(val, v.gx); break;
        case P_GY: g_value_set_double(val, v.gy); break;
        case P_GANCHO: g_value_set_double(val, v.gAncho); break;
        case P_GALTO: g_value_set_double(val, v.gAlto); break;
        case P_GGROSOR: g_value_set_double(val, v.gGrosor); break;
        case P_GCOLOR: g_value_set_uint(val, v.gColor); break;
        default:
            if (id >= P_LENTE && id < P_LENTE + kMaxLentes * kPorLente) {
                Lente& l = v.l[(id - P_LENTE) / kPorLente];
                const int i = static_cast<int>((id - P_LENTE) % kPorLente);
                if (i == 0)
                    g_value_set_enum(val, l.tipo);
                else
                    g_value_set_double(val, *campoLente(l, i - 1));
            }
            break;
    }
    GST_OBJECT_UNLOCK(o);
}

Vista copiaVista(GstObject* o, const Vista& v) {
    GST_OBJECT_LOCK(o);
    const Vista c = v;
    GST_OBJECT_UNLOCK(o);
    return c;
}

/// El tamaño: con ancho y alto, la salida es de ese tamaño (y la entrada, cualquiera);
/// sin ellos, el mismo de un lado y del otro.
GstCaps* capsDeTamano(GstObject* o, const Vista& v, GstPadDirection dir, GstCaps* caps) {
    const Vista a = copiaVista(o, v);
    GstCaps* out = gst_caps_copy(caps);
    if (a.ancho <= 0 || a.alto <= 0) return out;
    for (guint k = 0; k < gst_caps_get_size(out); ++k) {
        GstStructure* st = gst_caps_get_structure(out, k);
        if (dir == GST_PAD_SINK)
            gst_structure_set(st, "width", G_TYPE_INT, a.ancho, "height", G_TYPE_INT, a.alto, nullptr);
        else
            gst_structure_set(st, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT, "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
                              nullptr);
        gst_structure_remove_field(st, "pixel-aspect-ratio");
    }
    return out;
}

// --- La cuenta -------------------------------------------------------------------------

using Mat = std::array<double, 9>; // por filas
Mat mul(const Mat& a, const Mat& b) {
    Mat r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r[i * 3 + j] = a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j];
    return r;
}
Mat transpuesta(const Mat& a) { return {a[0], a[3], a[6], a[1], a[4], a[7], a[2], a[5], a[8]}; }
double rad(double g) { return g * kPi / 180; }
// Giro de costado (eje y), inclinación (eje x: + mira arriba) y rotación (eje z).
Mat ry(double g) { const double c = std::cos(rad(g)), s = std::sin(rad(g)); return {c, 0, s, 0, 1, 0, -s, 0, c}; }
Mat rx(double g) { const double c = std::cos(rad(g)), s = std::sin(rad(g)); return {1, 0, 0, 0, c, s, 0, -s, c}; }
Mat rz(double g) { const double c = std::cos(rad(g)), s = std::sin(rad(g)); return {c, -s, 0, s, c, 0, 0, 0, 1}; }

/// Los números que reciben los tres (48 vec4), para una entrada iw x ih y una salida ow x oh:
///   c0..c2   la vista (filas de su matriz) | .w: proporción de la salida, tan(fov/2), 0 equirect / 1 lentes
///   c3       ver (0 vista, 1 panorama, 2 entrada, 3 tira), proporción de la entrada, mezcla (rad), lentes
///   c4       guía: tipo, x, y, ancho      c5: alto, grosor (px), ancho y alto de la salida
///   c6       color de la guía (rgba 0..1)
///   c7       la tira: la mitad de su campo (rad)
///   c8+5i    la lente i: filas de su matriz (del mundo a la lente) | .w: x, y, radio;
///            después (medio campo en rad, k1, k2, 1/normalización del ojo de pez) y
///            (tipo: 0 ojo / 1 normal, foco de la normal en anchos de franja, -, -)
void empaquetar(const Vista& a, int iw, int ih, int ow, int oh, float* c) {
    std::memset(c, 0, sizeof(float) * kConstantes);
    const Mat v = mul(ry(a.yaw), rx(a.pitch));
    const int n = a.modo == kOjo ? 1 : a.modo == kDosOjos ? 2 : std::clamp(a.lentes, 1, kMaxLentes);
    for (int f = 0; f < 3; ++f)
        for (int k = 0; k < 3; ++k) c[f * 4 + k] = static_cast<float>(v[f * 3 + k]);
    c[3] = static_cast<float>(static_cast<double>(ow) / std::max(1, oh));
    c[7] = static_cast<float>(std::tan(rad(a.fov) / 2));
    c[11] = a.modo == kEquirect ? 0.0f : 1.0f;
    c[12] = static_cast<float>(a.ver);
    c[13] = static_cast<float>(static_cast<double>(iw) / std::max(1, ih));
    c[14] = static_cast<float>(rad(a.mezcla));
    c[15] = static_cast<float>(n);
    c[16] = static_cast<float>(a.guia);
    c[17] = static_cast<float>(a.gx);
    c[18] = static_cast<float>(a.gy);
    c[19] = static_cast<float>(a.gAncho);
    c[20] = static_cast<float>(a.gAlto);
    c[21] = static_cast<float>(a.gGrosor);
    c[22] = static_cast<float>(ow);
    c[23] = static_cast<float>(oh);
    c[24] = static_cast<float>(((a.gColor >> 16) & 0xFF) / 255.0);
    c[25] = static_cast<float>(((a.gColor >> 8) & 0xFF) / 255.0);
    c[26] = static_cast<float>((a.gColor & 0xFF) / 255.0);
    c[27] = static_cast<float>(((a.gColor >> 24) & 0xFF) / 255.0);
    c[28] = static_cast<float>(rad(a.tiraCampo) / 2);
    for (int l = 0; l < n; ++l) {
        const Lente& L = a.l[l];
        const int tipo = a.modo == kOjo || a.modo == kDosOjos ? kLenteOjo : L.tipo;
        // En dos-ojos la lente 2 mira para atrás. De la lente al mundo; la cuenta va del mundo a la lente.
        const double base = a.modo == kDosOjos && l == 1 ? 180.0 : 0.0;
        const Mat m = transpuesta(mul(mul(ry(base + L.yaw), rx(L.pitch)), rz(L.roll)));
        float* b = c + (kCabeza + l * kPorLenteV4) * 4;
        const double w[3] = {L.x, L.y, L.radio};
        for (int f = 0; f < 3; ++f) {
            for (int k = 0; k < 3; ++k) b[f * 4 + k] = static_cast<float>(m[f * 3 + k]);
            b[f * 4 + 3] = static_cast<float>(w[f]);
        }
        const double tm = rad(std::clamp(L.campo, 1.0, tipo == kLenteNormal ? 175.0 : 359.0)) / 2;
        b[12] = static_cast<float>(tm);
        b[13] = static_cast<float>(L.k1);
        b[14] = static_cast<float>(L.k2);
        b[15] = static_cast<float>(1.0 / std::max(1e-3, 1 + L.k1 * tm * tm + L.k2 * tm * tm * tm * tm));
        b[16] = static_cast<float>(tipo);
        b[17] = static_cast<float>(0.5 / std::tan(tm)); // x en la franja = cx + esto · x/z
    }
}

// La misma cuenta en C++ (ba360 y las pruebas): para un punto de la salida, de dónde
// leer (hasta tres lugares con su peso) y cuánto tapa la guía.
constexpr int kMuestras = 3;
struct Punto {
    float u[kMuestras], v[kMuestras], w[kMuestras];
    int n;      ///< 0 = negro
    float guia; ///< 0..1
};

struct V3 { double x, y, z; };
V3 rot(const float* f0, const float* f1, const float* f2, V3 d) {
    return {f0[0] * d.x + f0[1] * d.y + f0[2] * d.z, f1[0] * d.x + f1[1] * d.y + f1[2] * d.z,
            f2[0] * d.x + f2[1] * d.y + f2[2] * d.z};
}
double modulo(double x, double y) { return x - y * std::floor(x / y); }
double linea(double d, double g) { return std::clamp(g * 0.5 + 0.5 - std::abs(d), 0.0, 1.0); }

/// x, y, peso: de dónde leer en una lente (b: sus 20 números; i: cuál es).
void lente(const float* c, const float* b, double i, V3 dir, double& u, double& v, double& w) {
    const V3 d = rot(b, b + 4, b + 8, dir);
    const double n = c[15], ha = c[13] / n; // la proporción de una franja
    double xh, yh;
    w = 0;
    if (b[16] > 0.5) { // normal (rectilínea, Brown)
        if (d.z < 1e-4) { u = v = 0; return; }
        const double xu = d.x / d.z, yu = d.y / d.z, r2 = xu * xu + yu * yu;
        const double k = 1 + b[13] * r2 + b[14] * r2 * r2;
        // Donde la distorsión deja de crecer, se pliega: lo de afuera caería adentro.
        if (1 + 3 * b[13] * r2 + 5 * b[14] * r2 * r2 <= 0) { u = v = 0; return; }
        xh = b[3] + b[17] * xu * k;
        yh = b[7] - b[17] * ha * yu * k;
        // Más fuerte lejos del borde de su imagen (en altos), en una franja de `mezcla`.
        const double borde = std::min({xh * ha, (1 - xh) * ha, yh, 1 - yh});
        w = std::clamp(borde / std::max(c[14] * b[17] * ha, 1e-4), 0.0, 1.0);
    } else { // ojo de pez equidistante
        const double th = std::acos(std::clamp(d.z, -1.0, 1.0)), t2 = th * th;
        if (1 + 3 * b[13] * t2 + 5 * b[14] * t2 * t2 <= 0) { u = v = 0; return; }
        const double r = th / b[12] * (1 + b[13] * t2 + b[14] * t2 * t2) * b[15];
        const double rho = std::sqrt(d.x * d.x + d.y * d.y);
        const double cf = rho > 1e-9 ? d.x / rho : 1, sf = rho > 1e-9 ? d.y / rho : 0;
        xh = b[3] + r * b[11] * cf / ha;
        yh = b[7] - r * b[11] * sf;
        w = std::clamp((b[12] - th) / std::max(static_cast<double>(c[14]), 1e-4), 0.0, 1.0);
    }
    if (xh < 0 || xh > 1 || yh < 0 || yh > 1) w = 0;
    u = (i + xh) / n;
    v = yh;
}

Punto evaluar(const float* c, double uu, double vv) {
    Punto p{};
    const double ver = c[12];
    V3 dir{0, 0, 1};
    const bool esfera = ver < 1.5 || ver > 2.5;
    if (ver < 0.5) {
        const double px = uu * 2 - 1, py = 1 - vv * 2;
        V3 d{px * c[7] * c[3], py * c[7], 1};
        const double l = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        dir = rot(c, c + 4, c + 8, {d.x / l, d.y / l, d.z / l});
    } else if (ver < 1.5) {
        const double lon = (uu - 0.5) * 2 * kPi, lat = (0.5 - vv) * kPi;
        dir = {std::cos(lat) * std::sin(lon), std::sin(lat), std::cos(lat) * std::cos(lon)};
    } else if (ver > 2.5) {
        // La tira: cilíndrica (de costado, parejo; de arriba abajo, tan), píxeles cuadrados.
        const double lon = (uu * 2 - 1) * c[28], lat = std::atan((0.5 - vv) * 2 * c[28] / c[3]);
        dir = rot(c, c + 4, c + 8, {std::cos(lat) * std::sin(lon), std::sin(lat), std::cos(lat) * std::cos(lon)});
    }
    if (!esfera) {
        p.n = 1, p.u[0] = static_cast<float>(uu), p.v[0] = static_cast<float>(vv), p.w[0] = 1;
    } else if (c[11] < 0.5) {
        const double lon = std::atan2(dir.x, dir.z), lat = std::asin(std::clamp(dir.y, -1.0, 1.0));
        p.n = 1, p.u[0] = static_cast<float>(lon / (2 * kPi) + 0.5), p.v[0] = static_cast<float>(0.5 - lat / kPi), p.w[0] = 1;
    } else {
        for (int l = 0; l < static_cast<int>(c[15]) && p.n < kMuestras; ++l) {
            double u, v, w;
            lente(c, c + (kCabeza + l * kPorLenteV4) * 4, l, dir, u, v, w);
            if (w <= 0) continue;
            p.u[p.n] = static_cast<float>(u), p.v[p.n] = static_cast<float>(v), p.w[p.n] = static_cast<float>(w);
            ++p.n;
        }
    }
    // La guía, en píxeles de la salida.
    const double t = c[16], g = c[21], W = c[22], H = c[23];
    const double x = uu * W, y = vv * H, cx = c[17] * W, cy = c[18] * H, hx = c[19] * W / 2, hy = c[20] * H / 2;
    double a = 0;
    if (t > 0.5 && t < 1.5) {
        const double qx = std::abs(x - cx) - hx, qy = std::abs(y - cy) - hy;
        const double sd = std::hypot(std::max(qx, 0.0), std::max(qy, 0.0)) + std::min(std::max(qx, qy), 0.0);
        a = linea(sd, g);
    } else if (t > 1.5 && t < 2.5) {
        const double ex = std::max(hx, 1.0), ey = std::max(hy, 1.0);
        a = linea((std::hypot((x - cx) / ex, (y - cy) / ey) - 1) * std::min(ex, ey), g);
    } else if (t > 2.5 && t < 3.5) {
        const double px = std::max(hx * 2, 4.0), py = std::max(hy * 2, 4.0);
        a = std::max(linea(modulo(x - cx + px / 2, px) - px / 2, g), linea(modulo(y - cy + py / 2, py) - py / 2, g));
    } else if (t > 3.5 && t < 4.5) {
        a = std::max(linea(x - cx, g), linea(y - cy, g));
    } else if (t > 4.5 && esfera) {
        const double porPx = ver < 0.5 ? 2 * std::atan(c[7]) / H : ver > 2.5 ? 2 * c[28] / W : kPi / H;
        const double lon = std::atan2(dir.x, dir.z), lat = std::asin(std::clamp(dir.y, -1.0, 1.0)), paso = rad(15);
        a = std::max(linea((modulo(lat + paso / 2, paso) - paso / 2) / porPx, g),
                     linea((modulo(lon + paso / 2, paso) - paso / 2) * std::cos(lat) / porPx, g));
    }
    // Viendo la entrada: los círculos de los ojos de pez y los límites de las franjas.
    if (!esfera && c[11] > 0.5) {
        const int n = static_cast<int>(c[15]);
        for (int l = 0; l < n; ++l) {
            const float* b = c + (kCabeza + l * kPorLenteV4) * 4;
            if (b[16] < 0.5) {
                const double qx = (uu - (l + b[3]) / n) * c[13] * H, qy = (vv - b[7]) * H;
                a = std::max(a, linea(std::hypot(qx, qy) - b[11] * H, g));
            }
            if (l > 0) a = std::max(a, linea((uu - static_cast<double>(l) / n) * W, g));
        }
    }
    p.guia = static_cast<float>(a * c[27]);
    return p;
}

const char* kShaderGl = R"glsl(
#ifdef GL_ES
precision highp float;
#endif
varying vec2 v_texcoord;
uniform sampler2D tex;
uniform vec4 c[48];
const float PI = 3.14159265;

vec3 rot(vec4 a, vec4 b, vec4 d, vec3 v) { return vec3(dot(a.xyz, v), dot(b.xyz, v), dot(d.xyz, v)); }
float modulo(float x, float y) { return x - y * floor(x / y); }
float linea(float d, float g) { return clamp(g * 0.5 + 0.5 - abs(d), 0.0, 1.0); }

// De dónde leer en la lente i (x, y) y su peso (z).
vec3 lente(vec3 dir, int i) {
    vec4 r0 = c[8 + 5 * i];
    vec4 r1 = c[9 + 5 * i];
    vec4 r2 = c[10 + 5 * i];
    vec4 p = c[11 + 5 * i];
    vec4 e = c[12 + 5 * i];
    vec3 d = rot(r0, r1, r2, dir);
    float n = c[3].w;
    float ha = c[3].y / n;
    float xh;
    float yh;
    float w;
    if (e.x > 0.5) {
        if (d.z < 1e-4) return vec3(0.0);
        vec2 u = d.xy / d.z;
        float r2_ = dot(u, u);
        float k = 1.0 + p.y * r2_ + p.z * r2_ * r2_;
        if (1.0 + 3.0 * p.y * r2_ + 5.0 * p.z * r2_ * r2_ <= 0.0) return vec3(0.0);
        xh = r0.w + e.y * u.x * k;
        yh = r1.w - e.y * ha * u.y * k;
        float borde = min(min(xh * ha, (1.0 - xh) * ha), min(yh, 1.0 - yh));
        w = clamp(borde / max(c[3].z * e.y * ha, 1e-4), 0.0, 1.0);
    } else {
        float th = acos(clamp(d.z, -1.0, 1.0));
        float t2 = th * th;
        if (1.0 + 3.0 * p.y * t2 + 5.0 * p.z * t2 * t2 <= 0.0) return vec3(0.0);
        float r = th / p.x * (1.0 + p.y * t2 + p.z * t2 * t2) * p.w;
        float rho = length(d.xy);
        vec2 f = rho > 1e-6 ? d.xy / rho : vec2(1.0, 0.0);
        xh = r0.w + r * r2.w * f.x / ha;
        yh = r1.w - r * r2.w * f.y;
        w = clamp((p.x - th) / max(c[3].z, 1e-4), 0.0, 1.0);
    }
    if (xh < 0.0 || xh > 1.0 || yh < 0.0 || yh > 1.0) w = 0.0;
    return vec3((float(i) + xh) / n, yh, w);
}

void main() {
    vec2 uv = v_texcoord;
    float ver = c[3].x;
    vec3 dir = vec3(0.0, 0.0, 1.0);
    bool esfera = ver < 1.5 || ver > 2.5;
    if (ver < 0.5) {
        vec2 p = uv * 2.0 - 1.0;
        p.y = -p.y;
        dir = rot(c[0], c[1], c[2], normalize(vec3(p.x * c[1].w * c[0].w, p.y * c[1].w, 1.0)));
    } else if (ver < 1.5) {
        float lo = (uv.x - 0.5) * 2.0 * PI;
        float la = (0.5 - uv.y) * PI;
        dir = vec3(cos(la) * sin(lo), sin(la), cos(la) * cos(lo));
    } else if (ver > 2.5) {
        float lo = (uv.x * 2.0 - 1.0) * c[7].x;
        float la = atan((0.5 - uv.y) * 2.0 * c[7].x / c[0].w);
        dir = rot(c[0], c[1], c[2], vec3(cos(la) * sin(lo), sin(la), cos(la) * cos(lo)));
    }
    vec4 col;
    if (!esfera) {
        col = texture2D(tex, uv);
    } else if (c[2].w < 0.5) {
        col = texture2D(tex, vec2(atan(dir.x, dir.z) / (2.0 * PI) + 0.5, 0.5 - asin(clamp(dir.y, -1.0, 1.0)) / PI));
    } else {
        vec4 acc = vec4(0.0);
        float s = 0.0;
        for (int i = 0; i < 8; i++) {
            if (float(i) >= c[3].w) break;
            vec3 m = lente(dir, i);
            if (m.z > 0.0) {
                acc += texture2D(tex, m.xy) * m.z;
                s += m.z;
            }
        }
        col = s > 0.0 ? acc / s : vec4(0.0, 0.0, 0.0, 1.0);
    }
    float t = c[4].x;
    float g = c[5].y;
    vec2 S = c[5].zw;
    vec2 q = uv * S;
    vec2 ce = c[4].yz * S;
    vec2 h = vec2(c[4].w, c[5].x) * S * 0.5;
    float al = 0.0;
    if (t > 0.5 && t < 1.5) {
        vec2 k = abs(q - ce) - h;
        al = linea(length(max(k, 0.0)) + min(max(k.x, k.y), 0.0), g);
    } else if (t > 1.5 && t < 2.5) {
        vec2 e = max(h, vec2(1.0));
        al = linea((length((q - ce) / e) - 1.0) * min(e.x, e.y), g);
    } else if (t > 2.5 && t < 3.5) {
        vec2 pa = max(h * 2.0, vec2(4.0));
        al = max(linea(modulo(q.x - ce.x + pa.x * 0.5, pa.x) - pa.x * 0.5, g),
                 linea(modulo(q.y - ce.y + pa.y * 0.5, pa.y) - pa.y * 0.5, g));
    } else if (t > 3.5 && t < 4.5) {
        al = max(linea(q.x - ce.x, g), linea(q.y - ce.y, g));
    } else if (t > 4.5 && esfera) {
        float porPx = ver < 0.5 ? 2.0 * atan(c[1].w) / S.y : (ver > 2.5 ? 2.0 * c[7].x / S.x : PI / S.y);
        float lo = atan(dir.x, dir.z);
        float la = asin(clamp(dir.y, -1.0, 1.0));
        float pa = radians(15.0);
        al = max(linea((modulo(la + pa * 0.5, pa) - pa * 0.5) / porPx, g),
                 linea((modulo(lo + pa * 0.5, pa) - pa * 0.5) * cos(la) / porPx, g));
    }
    if (!esfera && c[2].w > 0.5) {
        float n = c[3].w;
        for (int i = 0; i < 8; i++) {
            if (float(i) >= n) break;
            vec4 r0 = c[8 + 5 * i];
            vec4 r1 = c[9 + 5 * i];
            vec4 r2 = c[10 + 5 * i];
            if (c[12 + 5 * i].x < 0.5) {
                vec2 k = vec2((uv.x - (float(i) + r0.w) / n) * c[3].y, uv.y - r1.w) * S.y;
                al = max(al, linea(length(k) - r2.w * S.y, g));
            }
            if (i > 0) al = max(al, linea((uv.x - float(i) / n) * S.x, g));
        }
    }
    gl_FragColor = mix(col, vec4(c[6].rgb, 1.0), al * c[6].a);
}
)glsl";

} // namespace

// ---------------------------------------------------------------- ba360 (CPU)

G_BEGIN_DECLS
#define BA_TYPE_VISTA360 (ba_vista360_get_type())
G_DECLARE_FINAL_TYPE(BaVista360, ba_vista360, BA, VISTA360, GstVideoFilter)
G_END_DECLS

struct _BaVista360 {
    GstVideoFilter parent;
    Vista* v;
    // Del hilo del video: la tabla (un Punto por píxel de la salida) y con qué números se hizo.
    std::vector<Punto>* tabla;
    std::array<float, kConstantes>* hecha;
};

G_DEFINE_TYPE(BaVista360, ba_vista360, GST_TYPE_VIDEO_FILTER)

namespace {

void cpuPoner(GObject* o, guint id, const GValue* val, GParamSpec*) {
    auto* s = BA_VISTA360(o);
    if (ponerVista(GST_OBJECT(s), *s->v, id, val)) gst_base_transform_reconfigure_src(GST_BASE_TRANSFORM(s));
}
void cpuLeer(GObject* o, guint id, GValue* val, GParamSpec*) {
    auto* s = BA_VISTA360(o);
    leerVista(GST_OBJECT(s), *s->v, id, val);
}

GstCaps* cpuCaps(GstBaseTransform* bt, GstPadDirection dir, GstCaps* caps, GstCaps* filtro) {
    GstCaps* out = capsDeTamano(GST_OBJECT(bt), *BA_VISTA360(bt)->v, dir, caps);
    if (filtro != nullptr) {
        GstCaps* i = gst_caps_intersect_full(filtro, out, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref(out);
        out = i;
    }
    return out;
}

GstFlowReturn cpuCuadro(GstVideoFilter* f, GstVideoFrame* in, GstVideoFrame* out) {
    auto* s = BA_VISTA360(f);
    ba::filtros::sincronizarValores(GST_BASE_TRANSFORM(f), in->buffer);
    const Vista a = copiaVista(GST_OBJECT(s), *s->v);
    const int ow = GST_VIDEO_FRAME_WIDTH(out), oh = GST_VIDEO_FRAME_HEIGHT(out);
    const int iw = GST_VIDEO_FRAME_WIDTH(in), ih = GST_VIDEO_FRAME_HEIGHT(in);
    std::array<float, kConstantes> c{};
    empaquetar(a, iw, ih, ow, oh, c.data());
    if (c != *s->hecha || s->tabla->size() != static_cast<size_t>(ow) * oh) {
        s->tabla->resize(static_cast<size_t>(ow) * oh);
        Punto* t = s->tabla->data();
        for (int y = 0; y < oh; ++y)
            for (int x = 0; x < ow; ++x) *t++ = evaluar(c.data(), (x + 0.5) / ow, (y + 0.5) / oh);
        *s->hecha = c;
    }
    // Bilineal (los bordes se estiran); el color de la guía, en el orden de los canales.
    ba::filtros::Orden o{};
    const bool conOrden = ba::filtros::ordenDe(GST_VIDEO_FRAME_FORMAT(out), o);
    std::uint8_t guia[4] = {0, 0, 0, 255};
    if (conOrden) {
        guia[o.r] = static_cast<std::uint8_t>(c[24] * 255), guia[o.g] = static_cast<std::uint8_t>(c[25] * 255),
        guia[o.b] = static_cast<std::uint8_t>(c[26] * 255);
        if (o.a >= 0) guia[o.a] = 255;
    }
    const auto* src = static_cast<const std::uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(in, 0));
    auto* dst = static_cast<std::uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(out, 0));
    const int is = GST_VIDEO_FRAME_PLANE_STRIDE(in, 0), os = GST_VIDEO_FRAME_PLANE_STRIDE(out, 0);
    const auto leer = [&](float u, float v, float* acc, float w) {
        const float fx = std::clamp(u * iw - 0.5f, 0.0f, static_cast<float>(iw - 1));
        const float fy = std::clamp(v * ih - 0.5f, 0.0f, static_cast<float>(ih - 1));
        const int x0 = static_cast<int>(fx), y0 = static_cast<int>(fy);
        const int x1 = std::min(x0 + 1, iw - 1), y1 = std::min(y0 + 1, ih - 1);
        const float ax = fx - x0, ay = fy - y0;
        const std::uint8_t* p00 = src + static_cast<ptrdiff_t>(y0) * is + x0 * 4;
        const std::uint8_t* p01 = src + static_cast<ptrdiff_t>(y0) * is + x1 * 4;
        const std::uint8_t* p10 = src + static_cast<ptrdiff_t>(y1) * is + x0 * 4;
        const std::uint8_t* p11 = src + static_cast<ptrdiff_t>(y1) * is + x1 * 4;
        for (int k = 0; k < 4; ++k) {
            const float ar = p00[k] + (p01[k] - p00[k]) * ax, ab = p10[k] + (p11[k] - p10[k]) * ax;
            acc[k] += (ar + (ab - ar) * ay) * w;
        }
    };
    const Punto* t = s->tabla->data();
    for (int y = 0; y < oh; ++y) {
        std::uint8_t* d = dst + static_cast<ptrdiff_t>(y) * os;
        for (int x = 0; x < ow; ++x, ++t, d += 4) {
            float acc[4] = {0, 0, 0, 0}, suma = 0;
            for (int k = 0; k < t->n; ++k) {
                leer(t->u[k], t->v[k], acc, t->w[k]);
                suma += t->w[k];
            }
            if (suma > 0)
                for (float& z : acc) z /= suma;
            else if (conOrden && o.a >= 0)
                acc[o.a] = 255; // negro opaco donde no se ve nada
            for (int k = 0; k < 4; ++k)
                d[k] = static_cast<std::uint8_t>(acc[k] + (guia[k] - acc[k]) * t->guia + 0.5f);
        }
    }
    return GST_FLOW_OK;
}

void cpuFinalizar(GObject* o) {
    auto* s = BA_VISTA360(o);
    delete s->v;
    delete s->tabla;
    delete s->hecha;
    G_OBJECT_CLASS(ba_vista360_parent_class)->finalize(o);
}

} // namespace

static void ba_vista360_init(BaVista360* s) {
    s->v = new Vista();
    s->tabla = new std::vector<Punto>();
    s->hecha = new std::array<float, kConstantes>();
}

static void ba_vista360_class_init(BaVista360Class* k) {
    auto* go = G_OBJECT_CLASS(k);
    go->set_property = cpuPoner;
    go->get_property = cpuLeer;
    go->finalize = cpuFinalizar;
    instalarPropiedades(go);
    gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(k), "Vista 360", "Filter/Effect/Video",
                                          "Une y endereza cámaras (360 de ojos de pez, una fila de cámaras o una equirectangular)",
                                          "Broadcast Appliance");
    GstCaps* caps = gst_caps_from_string("video/x-raw, format=(string){ RGBA, BGRA, ARGB, ABGR, RGBx, BGRx, xRGB, xBGR }");
    gst_element_class_add_pad_template(GST_ELEMENT_CLASS(k), gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps));
    gst_element_class_add_pad_template(GST_ELEMENT_CLASS(k), gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps));
    gst_caps_unref(caps);
    GST_BASE_TRANSFORM_CLASS(k)->transform_caps = cpuCaps;
    GST_VIDEO_FILTER_CLASS(k)->transform_frame = cpuCuadro;
}

// ---------------------------------------------------------------- ba360gl

G_BEGIN_DECLS
#define BA_TYPE_VISTA360_GL (ba_vista360_gl_get_type())
G_DECLARE_FINAL_TYPE(BaVista360Gl, ba_vista360_gl, BA, VISTA360_GL, GstGLFilter)
G_END_DECLS

struct _BaVista360Gl {
    GstGLFilter parent;
    Vista* v;
    GstGLShader* shader;
};

G_DEFINE_TYPE(BaVista360Gl, ba_vista360_gl, GST_TYPE_GL_FILTER)

namespace {

void glPoner(GObject* o, guint id, const GValue* val, GParamSpec*) {
    auto* s = BA_VISTA360_GL(o);
    if (ponerVista(GST_OBJECT(s), *s->v, id, val)) gst_base_transform_reconfigure_src(GST_BASE_TRANSFORM(s));
}
void glLeer(GObject* o, guint id, GValue* val, GParamSpec*) {
    auto* s = BA_VISTA360_GL(o);
    leerVista(GST_OBJECT(s), *s->v, id, val);
}

GstCaps* glCaps(GstGLFilter* f, GstPadDirection dir, GstCaps* caps, GstCaps*) {
    return capsDeTamano(GST_OBJECT(f), *BA_VISTA360_GL(f)->v, dir, caps);
}

gboolean glFiltrar(GstGLFilter* f, GstGLMemory* in, GstGLMemory* out) {
    auto* s = BA_VISTA360_GL(f);
    if (s->shader == nullptr) {
        std::string e;
        s->shader = ba::glsl::compilar(GST_GL_BASE_FILTER(f)->context, kShaderGl, e);
        if (s->shader == nullptr) {
            GST_ELEMENT_ERROR(f, RESOURCE, NOT_FOUND, ("el shader de la Vista 360 no compiló"), ("%s", e.c_str()));
            return FALSE;
        }
    }
    const Vista a = copiaVista(GST_OBJECT(s), *s->v);
    std::array<float, kConstantes> c{};
    empaquetar(a, GST_VIDEO_INFO_WIDTH(&f->in_info), GST_VIDEO_INFO_HEIGHT(&f->in_info), GST_VIDEO_INFO_WIDTH(&f->out_info),
               GST_VIDEO_INFO_HEIGHT(&f->out_info), c.data());
    gst_gl_shader_use(s->shader);
    gst_gl_shader_set_uniform_4fv(s->shader, "c", kConstantes / 4, c.data());
    gst_gl_filter_render_to_target_with_shader(f, in, out, s->shader);
    return TRUE;
}

void glParar(GstGLBaseFilter* bf) {
    auto* s = BA_VISTA360_GL(bf);
    if (s->shader != nullptr) gst_object_unref(s->shader);
    s->shader = nullptr;
    GST_GL_BASE_FILTER_CLASS(ba_vista360_gl_parent_class)->gl_stop(bf);
}

void glFinalizar(GObject* o) {
    delete BA_VISTA360_GL(o)->v;
    G_OBJECT_CLASS(ba_vista360_gl_parent_class)->finalize(o);
}

} // namespace

static void ba_vista360_gl_init(BaVista360Gl* s) {
    s->v = new Vista();
    s->shader = nullptr;
}

static void ba_vista360_gl_class_init(BaVista360GlClass* k) {
    auto* go = G_OBJECT_CLASS(k);
    go->set_property = glPoner;
    go->get_property = glLeer;
    go->finalize = glFinalizar;
    instalarPropiedades(go);
    gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(k), "Vista 360 (GPU)", "Filter/Effect/Video",
                                          "Une y endereza cámaras (360 de ojos de pez, una fila de cámaras o una equirectangular), en OpenGL",
                                          "Broadcast Appliance");
    gst_gl_filter_add_rgba_pad_templates(GST_GL_FILTER_CLASS(k));
    GST_GL_BASE_FILTER_CLASS(k)->gl_stop = glParar;
    GST_GL_BASE_FILTER_CLASS(k)->supported_gl_api =
        static_cast<GstGLAPI>(GST_GL_API_OPENGL | GST_GL_API_OPENGL3 | GST_GL_API_GLES2);
    GST_GL_FILTER_CLASS(k)->filter_texture = glFiltrar;
    GST_GL_FILTER_CLASS(k)->transform_internal_caps = glCaps;
}

// ---------------------------------------------------------------- ba360d3d12

#ifdef _WIN32

using Microsoft::WRL::ComPtr;

G_BEGIN_DECLS
#define BA_TYPE_VISTA360_D3D12 (ba_vista360_d3d12_get_type())
G_DECLARE_FINAL_TYPE(BaVista360D3d12, ba_vista360_d3d12, BA, VISTA360_D3D12, GstBaseTransform)
G_END_DECLS

namespace {
/// Lo de D3D12 (del hilo del video, salvo el dispositivo, que también toca set_context).
struct RecursosD3d12 {
    GstD3D12Device* device = nullptr;
    GstVideoInfo entra{}, sale{};
    DXGI_FORMAT formato = DXGI_FORMAT_UNKNOWN; ///< el de la tubería armada
    ComPtr<ID3D12RootSignature> firma;
    ComPtr<ID3D12PipelineState> tuberia;
    ComPtr<ID3D12GraphicsCommandList> lista;
    GstD3D12CmdAllocPool* asignadores = nullptr;
    GstD3D12DescHeapPool* descriptores = nullptr;
    GstD3D12FenceDataPool* pendientes = nullptr;
    guint64 valla = 0; ///< la del último cuadro mandado
    // Los números de cada cuadro (192 floats): un anillo de constant buffers en memoria
    // de subida, mapeado siempre; cada lugar recuerda la valla del cuadro que lo usó.
    static constexpr int kLugares = 64, kLugarBytes = 1024;
    ComPtr<ID3D12Resource> numeros;
    std::uint8_t* numerosMapa = nullptr;
    guint64 vallaLugar[kLugares] = {};
    int lugar = 0;

    void soltar() {
        if (device != nullptr && valla > 0) gst_d3d12_device_fence_wait(device, D3D12_COMMAND_LIST_TYPE_DIRECT, valla);
        valla = 0;
        if (numeros && numerosMapa != nullptr) numeros->Unmap(0, nullptr);
        numerosMapa = nullptr;
        numeros.Reset();
        std::fill(std::begin(vallaLugar), std::end(vallaLugar), 0);
        lugar = 0;
        lista.Reset();
        tuberia.Reset();
        firma.Reset();
        formato = DXGI_FORMAT_UNKNOWN;
        gst_clear_object(&asignadores);
        gst_clear_object(&descriptores);
        gst_clear_object(&pendientes);
    }
};
} // namespace

struct _BaVista360D3d12 {
    GstBaseTransform parent;
    Vista* v;
    RecursosD3d12* r;
};

G_DEFINE_TYPE(BaVista360D3d12, ba_vista360_d3d12, GST_TYPE_BASE_TRANSFORM)

namespace {

void d3dPoner(GObject* o, guint id, const GValue* val, GParamSpec*) {
    auto* s = BA_VISTA360_D3D12(o);
    if (ponerVista(GST_OBJECT(s), *s->v, id, val)) gst_base_transform_reconfigure_src(GST_BASE_TRANSFORM(s));
}
void d3dLeer(GObject* o, guint id, GValue* val, GParamSpec*) {
    auto* s = BA_VISTA360_D3D12(o);
    leerVista(GST_OBJECT(s), *s->v, id, val);
}

void d3dContexto(GstElement* e, GstContext* c) {
    auto* s = BA_VISTA360_D3D12(e);
    gst_d3d12_handle_set_context(e, c, -1, &s->r->device);
    GST_ELEMENT_CLASS(ba_vista360_d3d12_parent_class)->set_context(e, c);
}

gboolean d3dEmpezar(GstBaseTransform* bt) {
    auto* s = BA_VISTA360_D3D12(bt);
    if (!gst_d3d12_ensure_element_data(GST_ELEMENT(bt), -1, &s->r->device)) {
        GST_ELEMENT_ERROR(bt, RESOURCE, NOT_FOUND, ("no hay un dispositivo de Direct3D 12"), (nullptr));
        return FALSE;
    }
    ID3D12Device* dev = gst_d3d12_device_get_device_handle(s->r->device);
    s->r->asignadores = gst_d3d12_cmd_alloc_pool_new(dev, D3D12_COMMAND_LIST_TYPE_DIRECT);
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = 1;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    s->r->descriptores = gst_d3d12_desc_heap_pool_new(dev, &hd);
    s->r->pendientes = gst_d3d12_fence_data_pool_new();
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = RecursosD3d12::kLugares * RecursosD3d12::kLugarBytes;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    HRESULT hr = dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                              IID_PPV_ARGS(&s->r->numeros));
    const D3D12_RANGE nada{0, 0};
    if (SUCCEEDED(hr)) hr = s->r->numeros->Map(0, &nada, reinterpret_cast<void**>(&s->r->numerosMapa));
    if (FAILED(hr)) {
        GST_ELEMENT_ERROR(bt, RESOURCE, FAILED, ("no se pudo crear el buffer de los números"), ("0x%x", static_cast<unsigned>(hr)));
        return FALSE;
    }
    return TRUE;
}

gboolean d3dParar(GstBaseTransform* bt) {
    auto* s = BA_VISTA360_D3D12(bt);
    s->r->soltar();
    gst_clear_object(&s->r->device);
    return TRUE;
}

gboolean d3dConsulta(GstBaseTransform* bt, GstPadDirection dir, GstQuery* q) {
    if (GST_QUERY_TYPE(q) == GST_QUERY_CONTEXT &&
        gst_d3d12_handle_context_query(GST_ELEMENT(bt), q, BA_VISTA360_D3D12(bt)->r->device))
        return TRUE;
    return GST_BASE_TRANSFORM_CLASS(ba_vista360_d3d12_parent_class)->query(bt, dir, q);
}

GstCaps* d3dCaps(GstBaseTransform* bt, GstPadDirection dir, GstCaps* caps, GstCaps* filtro) {
    GstCaps* out = capsDeTamano(GST_OBJECT(bt), *BA_VISTA360_D3D12(bt)->v, dir, caps);
    if (filtro != nullptr) {
        GstCaps* i = gst_caps_intersect_full(filtro, out, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref(out);
        out = i;
    }
    return out;
}

DXGI_FORMAT formatoDe(GstVideoFormat f) {
    return f == GST_VIDEO_FORMAT_BGRA ? DXGI_FORMAT_B8G8R8A8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
}

/// La tubería para el formato de la salida (la firma raíz viene en el mismo bytecode).
bool armarTuberia(BaVista360D3d12* s, DXGI_FORMAT fmt) {
    RecursosD3d12& r = *s->r;
    if (r.tuberia && r.formato == fmt) return true;
    ID3D12Device* dev = gst_d3d12_device_get_device_handle(r.device);
    HRESULT hr = dev->CreateRootSignature(0, kVista360Ps, sizeof(kVista360Ps), IID_PPV_ARGS(&r.firma));
    if (FAILED(hr)) {
        GST_ERROR_OBJECT(s, "CreateRootSignature: 0x%x", static_cast<unsigned>(hr));
        return false;
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
    d.pRootSignature = r.firma.Get();
    d.VS = {kVista360Vs, sizeof(kVista360Vs)};
    d.PS = {kVista360Ps, sizeof(kVista360Ps)};
    d.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    d.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    d.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    d.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    d.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    d.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    d.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    d.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
    d.SampleMask = UINT_MAX;
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    d.RasterizerState.DepthClipEnable = TRUE;
    d.DepthStencilState.DepthEnable = FALSE;
    d.DepthStencilState.StencilEnable = FALSE;
    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets = 1;
    d.RTVFormats[0] = fmt;
    d.SampleDesc.Count = 1;
    hr = dev->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&r.tuberia));
    if (FAILED(hr)) {
        GST_ERROR_OBJECT(s, "CreateGraphicsPipelineState: 0x%x", static_cast<unsigned>(hr));
        return false;
    }
    r.formato = fmt;
    return true;
}

gboolean d3dPonerCaps(GstBaseTransform* bt, GstCaps* entra, GstCaps* sale) {
    auto* s = BA_VISTA360_D3D12(bt);
    if (!gst_video_info_from_caps(&s->r->entra, entra) || !gst_video_info_from_caps(&s->r->sale, sale)) return FALSE;
    return armarTuberia(s, formatoDe(GST_VIDEO_INFO_FORMAT(&s->r->sale))) ? TRUE : FALSE;
}

/// Un pool de D3D12 de este dispositivo con esos flags (y el tamaño que queda).
GstBufferPool* poolD3d12(GstD3D12Device* dev, GstCaps* caps, const GstVideoInfo& info, D3D12_RESOURCE_FLAGS flags,
                         GstBufferPool* existente, guint& tamano, guint minimo, guint maximo) {
    GstBufferPool* pool = existente;
    if (pool != nullptr && (!GST_IS_D3D12_BUFFER_POOL(pool) ||
                            !gst_d3d12_device_is_equal(GST_D3D12_BUFFER_POOL_CAST(pool)->device, dev)))
        gst_clear_object(&pool);
    if (pool == nullptr) pool = gst_d3d12_buffer_pool_new(dev);
    GstStructure* cfg = gst_buffer_pool_get_config(pool);
    gst_buffer_pool_config_add_option(cfg, GST_BUFFER_POOL_OPTION_VIDEO_META);
    GstD3D12AllocationParams* p = gst_buffer_pool_config_get_d3d12_allocation_params(cfg);
    if (p == nullptr) {
        p = gst_d3d12_allocation_params_new(dev, &info, GST_D3D12_ALLOCATION_FLAG_DEFAULT, flags, D3D12_HEAP_FLAG_NONE);
    } else {
        gst_d3d12_allocation_params_set_resource_flags(p, flags);
        gst_d3d12_allocation_params_unset_resource_flags(p, D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
    }
    gst_buffer_pool_config_set_d3d12_allocation_params(cfg, p);
    gst_d3d12_allocation_params_free(p);
    gst_buffer_pool_config_set_params(cfg, caps, std::max<guint>(tamano, static_cast<guint>(info.size)), minimo, maximo);
    if (!gst_buffer_pool_set_config(pool, cfg)) {
        gst_object_unref(pool);
        return nullptr;
    }
    cfg = gst_buffer_pool_get_config(pool);
    gst_buffer_pool_config_get_params(cfg, nullptr, &tamano, nullptr, nullptr);
    gst_structure_free(cfg);
    return pool;
}

gboolean d3dProponer(GstBaseTransform* bt, GstQuery* decidida, GstQuery* q) {
    auto* s = BA_VISTA360_D3D12(bt);
    if (!GST_BASE_TRANSFORM_CLASS(ba_vista360_d3d12_parent_class)->propose_allocation(bt, decidida, q)) return FALSE;
    if (decidida == nullptr) return TRUE;
    GstCaps* caps = nullptr;
    gst_query_parse_allocation(q, &caps, nullptr);
    GstVideoInfo info;
    if (caps == nullptr || !gst_video_info_from_caps(&info, caps)) return FALSE;
    if (gst_query_get_n_allocation_pools(q) == 0) {
        guint tamano = 0;
        GstBufferPool* pool = poolD3d12(s->r->device, caps, info, D3D12_RESOURCE_FLAG_NONE, nullptr, tamano, 0, 0);
        if (pool == nullptr) return FALSE;
        gst_query_add_allocation_pool(q, pool, tamano, 0, 0);
        gst_object_unref(pool);
    }
    gst_query_add_allocation_meta(q, GST_VIDEO_META_API_TYPE, nullptr);
    return TRUE;
}

gboolean d3dDecidir(GstBaseTransform* bt, GstQuery* q) {
    auto* s = BA_VISTA360_D3D12(bt);
    GstCaps* caps = nullptr;
    gst_query_parse_allocation(q, &caps, nullptr);
    GstVideoInfo info;
    if (caps == nullptr || !gst_video_info_from_caps(&info, caps)) return FALSE;
    GstBufferPool* pool = nullptr;
    guint tamano = 0, minimo = 0, maximo = 0;
    const bool hay = gst_query_get_n_allocation_pools(q) > 0;
    if (hay) gst_query_parse_nth_allocation_pool(q, 0, &pool, &tamano, &minimo, &maximo);
    // La salida es el destino del dibujo (render target); simultánea, así no hacen
    // falta barreras con los que la leen después.
    pool = poolD3d12(s->r->device, caps, info,
                     static_cast<D3D12_RESOURCE_FLAGS>(D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
                                                       D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS),
                     pool, tamano, minimo, maximo);
    if (pool == nullptr) return FALSE;
    if (hay)
        gst_query_set_nth_allocation_pool(q, 0, pool, tamano, minimo, maximo);
    else
        gst_query_add_allocation_pool(q, pool, tamano, minimo, maximo);
    gst_object_unref(pool);
    return GST_BASE_TRANSFORM_CLASS(ba_vista360_d3d12_parent_class)->decide_allocation(bt, q);
}

GstFlowReturn d3dCuadro(GstBaseTransform* bt, GstBuffer* in, GstBuffer* out) {
    auto* s = BA_VISTA360_D3D12(bt);
    RecursosD3d12& r = *s->r;
    ba::filtros::sincronizarValores(bt, in);
    const Vista a = copiaVista(GST_OBJECT(s), *s->v);

    GstD3D12Frame fe, fs;
    if (!gst_d3d12_frame_map(&fe, &r.entra, in, GST_MAP_READ_D3D12, GST_D3D12_FRAME_MAP_FLAG_SRV)) {
        GST_ERROR_OBJECT(s, "no se pudo mapear la entrada (¿no es D3D12Memory de este dispositivo?)");
        return GST_FLOW_ERROR;
    }
    if (!gst_d3d12_frame_map(&fs, &r.sale, out, GST_MAP_WRITE_D3D12, GST_D3D12_FRAME_MAP_FLAG_RTV)) {
        gst_d3d12_frame_unmap(&fe);
        GST_ERROR_OBJECT(s, "no se pudo mapear la salida");
        return GST_FLOW_ERROR;
    }
    auto fallar = [&](const char* que, HRESULT hr) {
        GST_ERROR_OBJECT(s, "%s: 0x%x", que, static_cast<unsigned>(hr));
        gst_d3d12_frame_unmap(&fs);
        gst_d3d12_frame_unmap(&fe);
        return GST_FLOW_ERROR;
    };

    ID3D12Device* dev = gst_d3d12_device_get_device_handle(r.device);
    GstD3D12CmdAlloc* ca = nullptr;
    if (!gst_d3d12_cmd_alloc_pool_acquire(r.asignadores, &ca)) return fallar("sin asignador de comandos", E_FAIL);
    ID3D12CommandAllocator* asig = gst_d3d12_cmd_alloc_get_handle(ca);
    HRESULT hr = asig->Reset();
    if (SUCCEEDED(hr))
        hr = r.lista ? r.lista->Reset(asig, r.tuberia.Get())
                     : dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, asig, r.tuberia.Get(), IID_PPV_ARGS(&r.lista));
    if (FAILED(hr)) {
        gst_d3d12_cmd_alloc_unref(ca);
        return fallar("lista de comandos", hr);
    }
    GstD3D12DescHeap* heap = nullptr;
    if (!gst_d3d12_desc_heap_pool_acquire(r.descriptores, &heap)) {
        r.lista->Close();
        gst_d3d12_cmd_alloc_unref(ca);
        return fallar("sin descriptores", E_FAIL);
    }
    ID3D12DescriptorHeap* h = gst_d3d12_desc_heap_get_handle(heap);
    dev->CopyDescriptorsSimple(1, h->GetCPUDescriptorHandleForHeapStart(), fe.srv_desc_handle[0],
                               D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    const int ow = GST_VIDEO_INFO_WIDTH(&r.sale), oh = GST_VIDEO_INFO_HEIGHT(&r.sale);
    std::array<float, kConstantes> c{};
    empaquetar(a, GST_VIDEO_INFO_WIDTH(&r.entra), GST_VIDEO_INFO_HEIGHT(&r.entra), ow, oh, c.data());
    ID3D12GraphicsCommandList* cl = r.lista.Get();
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = fs.data[0];
    b.Transition.Subresource = fs.subresource_index[0];
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    cl->ResourceBarrier(1, &b);
    cl->SetGraphicsRootSignature(r.firma.Get());
    cl->SetDescriptorHeaps(1, &h);
    // Un lugar del anillo que la GPU ya no esté leyendo.
    const int lugar = r.lugar;
    r.lugar = (r.lugar + 1) % RecursosD3d12::kLugares;
    if (r.vallaLugar[lugar] > gst_d3d12_device_get_completed_value(r.device, D3D12_COMMAND_LIST_TYPE_DIRECT))
        gst_d3d12_device_fence_wait(r.device, D3D12_COMMAND_LIST_TYPE_DIRECT, r.vallaLugar[lugar]);
    std::memcpy(r.numerosMapa + lugar * RecursosD3d12::kLugarBytes, c.data(), sizeof(float) * kConstantes);
    cl->SetGraphicsRootConstantBufferView(0, r.numeros->GetGPUVirtualAddress() +
                                                 static_cast<UINT64>(lugar) * RecursosD3d12::kLugarBytes);
    cl->SetGraphicsRootDescriptorTable(1, h->GetGPUDescriptorHandleForHeapStart());
    cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const D3D12_VIEWPORT vp{0, 0, static_cast<float>(ow), static_cast<float>(oh), 0, 1};
    const D3D12_RECT recorte{0, 0, ow, oh};
    cl->RSSetViewports(1, &vp);
    cl->RSSetScissorRects(1, &recorte);
    cl->OMSetRenderTargets(1, &fs.rtv_desc_handle[0], FALSE, nullptr);
    cl->DrawInstanced(3, 1, 0, 0);
    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    cl->ResourceBarrier(1, &b);
    hr = cl->Close();
    if (FAILED(hr)) {
        gst_d3d12_desc_heap_unref(heap);
        gst_d3d12_cmd_alloc_unref(ca);
        return fallar("Close", hr);
    }

    // Que la GPU espere lo que estaba escribiendo la entrada (sin frenar la CPU).
    GstD3D12CmdQueue* cola = gst_d3d12_device_get_cmd_queue(r.device, D3D12_COMMAND_LIST_TYPE_DIRECT);
    gst_d3d12_frame_fence_gpu_wait(&fe, cola);
    ID3D12CommandList* listas[] = {cl};
    hr = gst_d3d12_device_execute_command_lists(r.device, D3D12_COMMAND_LIST_TYPE_DIRECT, 1, listas, &r.valla);
    if (FAILED(hr)) {
        gst_d3d12_desc_heap_unref(heap);
        gst_d3d12_cmd_alloc_unref(ca);
        return fallar("ExecuteCommandLists", hr);
    }
    r.vallaLugar[lugar] = r.valla;
    // Lo que usa la GPU vive hasta que termina: el asignador, los descriptores y la entrada.
    GstD3D12FenceData* pend = nullptr;
    gst_d3d12_fence_data_pool_acquire(r.pendientes, &pend);
    gst_d3d12_fence_data_push(pend, ca, reinterpret_cast<GDestroyNotify>(gst_d3d12_cmd_alloc_unref));
    gst_d3d12_fence_data_push(pend, heap, reinterpret_cast<GDestroyNotify>(gst_d3d12_desc_heap_unref));
    gst_d3d12_fence_data_push(pend, gst_buffer_ref(in), reinterpret_cast<GDestroyNotify>(gst_buffer_unref));
    gst_d3d12_device_set_fence_notify(r.device, D3D12_COMMAND_LIST_TYPE_DIRECT, r.valla, pend,
                                      reinterpret_cast<GDestroyNotify>(gst_d3d12_fence_data_unref));
    // El que sigue espera esta valla antes de leer la salida.
    gst_d3d12_buffer_set_fence(out, gst_d3d12_device_get_fence_handle(r.device, D3D12_COMMAND_LIST_TYPE_DIRECT), r.valla,
                               FALSE);
    gst_d3d12_frame_unmap(&fs);
    gst_d3d12_frame_unmap(&fe);
    return GST_FLOW_OK;
}

void d3dFinalizar(GObject* o) {
    auto* s = BA_VISTA360_D3D12(o);
    s->r->soltar();
    gst_clear_object(&s->r->device);
    delete s->r;
    delete s->v;
    G_OBJECT_CLASS(ba_vista360_d3d12_parent_class)->finalize(o);
}

} // namespace

static void ba_vista360_d3d12_init(BaVista360D3d12* s) {
    s->v = new Vista();
    s->r = new RecursosD3d12();
}

static void ba_vista360_d3d12_class_init(BaVista360D3d12Class* k) {
    auto* go = G_OBJECT_CLASS(k);
    go->set_property = d3dPoner;
    go->get_property = d3dLeer;
    go->finalize = d3dFinalizar;
    instalarPropiedades(go);
    gst_element_class_set_static_metadata(GST_ELEMENT_CLASS(k), "Vista 360 (D3D12)", "Filter/Effect/Video",
                                          "Une y endereza cámaras (360 de ojos de pez, una fila de cámaras o una equirectangular), en Direct3D 12",
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

extern "C" gboolean ba_registrar_vista360(GstPlugin* p) {
    gboolean ok = gst_element_register(p, "ba360", GST_RANK_NONE, BA_TYPE_VISTA360) &&
                  gst_element_register(p, "ba360gl", GST_RANK_NONE, BA_TYPE_VISTA360_GL);
#ifdef _WIN32
    // El motor lo elige cuando el video de la entrada va en D3D12.
    ok = ok && gst_element_register(p, "ba360d3d12", GST_RANK_NONE, BA_TYPE_VISTA360_D3D12);
#endif
    return ok;
}
