// ============================================================================
// src/gst-plugins/filtros/mejora.cpp — bamejora: «Mejorar imagen» (limpiar y afilar)
//
// Limpia y afila una cámara floja en tiempo real y sin demora (solo mira el
// cuadro actual y el anterior ya filtrado). Los tres pasos del efecto de antes
// (mejora-imagen.effect), en la CPU y en varios hilos:
//   1. Ruido temporal (resolución completa): cada píxel se mezcla con el del
//      cuadro anterior ya limpio, salvo donde hay movimiento. El movimiento es
//      la diferencia de brillo suavizada (carpa 3x3) entre los dos cuadros.
//   2. Color guiado por el brillo (MEDIA resolución): el color de cada bloque
//      2x2 pasa a ser el promedio del de los bloques vecinos de brillo
//      PARECIDO (3x3): se pega a los bordes y deja de chorrearse (el LED del
//      micrófono, la corbata, el 4:2:0 de la cámara).
//   3. Final (resolución completa): agranda ese color respetando los bordes de
//      brillo (los 4 bloques más cercanos, pesados por cuánto se parece su
//      brillo) y afila el brillo con AMD FidelityFX CAS (licencia MIT): fuerte
//      en lo plano, suave en lo que ya tiene borde, sin halos.
// Cada uno con su intensidad (0..100; 0 lo apaga). Todo en 0 = pasante.
//
// Propiedades: ruido (60), color (70), nitidez (50); `ms-por-cuadro` (solo
// lectura) lo que tarda.
// Formatos: RGB de 4 bytes (con o sin alfa; el alfa pasa igual).
// Concurrencia: propiedades bajo GST_OBJECT_LOCK; el cuadro trabaja con una copia.
// ============================================================================

#include "comun.hpp"
#include "platform/Hilo.hpp"

#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

G_BEGIN_DECLS
#define BA_TYPE_MEJORA (ba_mejora_get_type())
G_DECLARE_FINAL_TYPE(BaMejora, ba_mejora, BA, MEJORA, GstVideoFilter)
G_END_DECLS

namespace {
constexpr float kRuidoMax = 0.85f;

struct Estado {
    std::vector<std::uint8_t> anterior; ///< el cuadro anterior ya filtrado (mismo formato)
    std::vector<float> yAnt;            ///< su brillo
    std::vector<float> y, d, t;         ///< brillo actual, diferencia, carpa
    std::vector<std::uint8_t> limpio;   ///< tras el paso 1
    std::vector<float> medio;           ///< media resolución: (dr, dg, db, y) por bloque
    int ancho = 0, alto = 0, stride = 0;
    bool hayAnterior = false;
};
} // namespace

struct _BaMejora {
    GstVideoFilter parent;
    gint ruido, color, nitidez; ///< bajo GST_OBJECT_LOCK
    gdouble ms;
    Estado* e;
};

G_DEFINE_TYPE(BaMejora, ba_mejora, GST_TYPE_VIDEO_FILTER)

enum { PROP_0, PROP_RUIDO, PROP_COLOR, PROP_NITIDEZ, PROP_MS };

namespace {

/// fn(y0, y1) en franjas de filas, en varios hilos.
template <typename F>
void enFranjas(int h, F fn) {
    const int hilos = h >= 360 ? static_cast<int>(std::clamp(std::thread::hardware_concurrency(), 1u, 8u)) : 1;
    std::vector<std::thread> v;
    for (int k = 1; k < hilos; ++k)
        v.emplace_back([&fn](int a, int b) { ba::platform::nombrarHilo("bamejora-franja"); fn(a, b); }, h * k / hilos,
                       h * (k + 1) / hilos);
    fn(0, h / hilos);
    for (auto& t : v) t.join();
}

inline float luma(float r, float g, float b) { return 0.2126f * r + 0.7152f * g + 0.0722f * b; }

/// exp(-x) para x >= 0, por tabla (lo que más se usa).
struct TablaExp {
    static constexpr int kN = 4096;
    static constexpr float kMax = 16.0f;
    float v[kN + 1];
    TablaExp() {
        for (int i = 0; i <= kN; ++i) v[i] = std::exp(-kMax * static_cast<float>(i) / kN);
    }
    float operator()(float x) const {
        if (x >= kMax) return 0.0f;
        return v[static_cast<int>(x * (kN / kMax))];
    }
};
const TablaExp& tablaExp() {
    static const TablaExp t;
    return t;
}

void ponerPropiedad(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    auto* m = BA_MEJORA(o);
    GST_OBJECT_LOCK(m);
    switch (id) {
        case PROP_RUIDO: m->ruido = g_value_get_int(v); break;
        case PROP_COLOR: m->color = g_value_get_int(v); break;
        case PROP_NITIDEZ: m->nitidez = g_value_get_int(v); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps); break;
    }
    GST_OBJECT_UNLOCK(m);
}

void leerPropiedad(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    auto* m = BA_MEJORA(o);
    GST_OBJECT_LOCK(m);
    switch (id) {
        case PROP_RUIDO: g_value_set_int(v, m->ruido); break;
        case PROP_COLOR: g_value_set_int(v, m->color); break;
        case PROP_NITIDEZ: g_value_set_int(v, m->nitidez); break;
        case PROP_MS: g_value_set_double(v, m->ms); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps); break;
    }
    GST_OBJECT_UNLOCK(m);
}

void antes(GstBaseTransform* bt, GstBuffer* b) {
    ba::filtros::sincronizarValores(bt, b);
    auto* m = BA_MEJORA(bt);
    GST_OBJECT_LOCK(m);
    const bool nada = m->ruido <= 0 && m->color <= 0 && m->nitidez <= 0;
    if (nada) m->e->hayAnterior = false;
    GST_OBJECT_UNLOCK(m);
    ba::filtros::pasante(bt, nada);
}

GstFlowReturn transformar(GstVideoFilter* vf, GstVideoFrame* in, GstVideoFrame* out) {
    auto* m = BA_MEJORA(vf);
    GST_OBJECT_LOCK(m);
    const float ruido = kRuidoMax * static_cast<float>(std::clamp(m->ruido, 0, 100)) / 100.0f;
    const float color = static_cast<float>(std::clamp(m->color, 0, 100)) / 100.0f;
    const float nitidez = static_cast<float>(std::clamp(m->nitidez, 0, 100)) / 100.0f;
    GST_OBJECT_UNLOCK(m);
    const auto t0 = std::chrono::steady_clock::now();

    ba::filtros::Orden o{};
    if (!ba::filtros::ordenDe(GST_VIDEO_FRAME_FORMAT(in), o)) return GST_FLOW_NOT_NEGOTIATED;
    const int W = GST_VIDEO_FRAME_WIDTH(in), H = GST_VIDEO_FRAME_HEIGHT(in);
    const int ss = GST_VIDEO_FRAME_PLANE_STRIDE(in, 0), ds = GST_VIDEO_FRAME_PLANE_STRIDE(out, 0);
    const auto* src = static_cast<const std::uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(in, 0));
    auto* dst = static_cast<std::uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(out, 0));
    Estado& e = *m->e;
    const std::size_t n = static_cast<std::size_t>(W) * H;
    if (e.ancho != W || e.alto != H) {
        e.ancho = W;
        e.alto = H;
        e.stride = W * 4;
        e.anterior.assign(n * 4, 0);
        e.limpio.assign(n * 4, 0);
        e.yAnt.assign(n, 0.0f);
        e.y.assign(n, 0.0f);
        e.d.assign(n, 0.0f);
        e.t.assign(n, 0.0f);
        e.hayAnterior = false;
    }
    const TablaExp& ex = tablaExp();
    const float inv255 = 1.0f / 255.0f;

    // El brillo del cuadro (0..1).
    enFranjas(H, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const std::uint8_t* p = src + static_cast<std::size_t>(y) * ss;
            for (int x = 0; x < W; ++x, p += 4)
                e.y[static_cast<std::size_t>(y) * W + x] = luma(p[o.r], p[o.g], p[o.b]) * inv255;
        }
    });

    // 1) Ruido temporal → e.limpio (empaquetado, mismo orden de canales).
    const bool temporal = ruido > 0.0f && e.hayAnterior;
    if (temporal) {
        for (std::size_t i = 0; i < n; ++i) e.d[i] = e.y[i] - e.yAnt[i];
        // Carpa 3x3 separable ([1 2 1]/4 en cada eje), con los bordes repetidos.
        enFranjas(H, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                const float* fila = &e.d[static_cast<std::size_t>(y) * W];
                float* t = &e.t[static_cast<std::size_t>(y) * W];
                for (int x = 0; x < W; ++x) {
                    const float a = fila[std::max(0, x - 1)], b = fila[x], c = fila[std::min(W - 1, x + 1)];
                    t[x] = 0.25f * a + 0.5f * b + 0.25f * c;
                }
            }
        });
        enFranjas(H, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                const float* a = &e.t[static_cast<std::size_t>(std::max(0, y - 1)) * W];
                const float* b = &e.t[static_cast<std::size_t>(y) * W];
                const float* c = &e.t[static_cast<std::size_t>(std::min(H - 1, y + 1)) * W];
                const std::uint8_t* p = src + static_cast<std::size_t>(y) * ss;
                const std::uint8_t* q = &e.anterior[static_cast<std::size_t>(y) * e.stride];
                std::uint8_t* l = &e.limpio[static_cast<std::size_t>(y) * e.stride];
                for (int x = 0; x < W; ++x, p += 4, q += 4, l += 4) {
                    const float dif = std::fabs(0.25f * a[x] + 0.5f * b[x] + 0.25f * c[x]) / 0.04f;
                    const float k = ruido * ex(dif * dif);
                    for (int ch = 0; ch < 4; ++ch)
                        l[ch] = static_cast<std::uint8_t>(static_cast<float>(p[ch]) + (static_cast<float>(q[ch]) - p[ch]) * k + 0.5f);
                }
            }
        });
    } else {
        for (int y = 0; y < H; ++y) std::memcpy(&e.limpio[static_cast<std::size_t>(y) * e.stride], src + static_cast<std::size_t>(y) * ss, e.stride);
    }
    // El brillo de lo limpio (para los pasos 2 y 3).
    if (temporal)
        enFranjas(H, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                const std::uint8_t* p = &e.limpio[static_cast<std::size_t>(y) * e.stride];
                for (int x = 0; x < W; ++x, p += 4)
                    e.y[static_cast<std::size_t>(y) * W + x] = luma(p[o.r], p[o.g], p[o.b]) * inv255;
            }
        });

    // 2) Color guiado, a media resolución: (dr, dg, db, y) por bloque 2x2.
    const int mw = (W + 1) / 2, mh = (H + 1) / 2;
    std::vector<float> bloque(static_cast<std::size_t>(mw) * mh * 4);
    if (color > 0.0f) {
        enFranjas(mh, [&](int y0, int y1) {
            for (int by = y0; by < y1; ++by)
                for (int bx = 0; bx < mw; ++bx) {
                    float r = 0, g = 0, b = 0;
                    int c = 0;
                    for (int j = 0; j < 2; ++j)
                        for (int i = 0; i < 2; ++i) {
                            const int x = std::min(W - 1, bx * 2 + i), y = std::min(H - 1, by * 2 + j);
                            const std::uint8_t* p = &e.limpio[static_cast<std::size_t>(y) * e.stride + static_cast<std::size_t>(x) * 4];
                            r += p[o.r];
                            g += p[o.g];
                            b += p[o.b];
                            ++c;
                        }
                    float* f = &bloque[(static_cast<std::size_t>(by) * mw + bx) * 4];
                    f[0] = r * inv255 / c;
                    f[1] = g * inv255 / c;
                    f[2] = b * inv255 / c;
                    f[3] = luma(f[0], f[1], f[2]);
                }
        });
        e.medio.assign(bloque.size(), 0.0f);
        static const float kEsp[3] = {std::exp(-0.0f), std::exp(-0.5f), std::exp(-1.0f)}; // (i²+j²)·0,5
        enFranjas(mh, [&](int y0, int y1) {
            for (int by = y0; by < y1; ++by)
                for (int bx = 0; bx < mw; ++bx) {
                    const float yc = bloque[(static_cast<std::size_t>(by) * mw + bx) * 4 + 3];
                    float ar = 0, ag = 0, ab = 0, suma = 0;
                    for (int j = -1; j <= 1; ++j)
                        for (int i = -1; i <= 1; ++i) {
                            const int x = std::clamp(bx + i, 0, mw - 1), y = std::clamp(by + j, 0, mh - 1);
                            const float* v = &bloque[(static_cast<std::size_t>(y) * mw + x) * 4];
                            const float dy = (v[3] - yc) / 0.06f;
                            const float peso = ex(dy * dy) * kEsp[i * i + j * j];
                            ar += (v[0] - v[3]) * peso;
                            ag += (v[1] - v[3]) * peso;
                            ab += (v[2] - v[3]) * peso;
                            suma += peso;
                        }
                    float* f = &e.medio[(static_cast<std::size_t>(by) * mw + bx) * 4];
                    f[0] = ar / suma;
                    f[1] = ag / suma;
                    f[2] = ab / suma;
                    f[3] = yc;
                }
        });
    }

    // 3) Final: nitidez (CAS) sobre el brillo + el color guiado agrandado.
    const float pesoCas = nitidez > 0.0001f ? -1.0f / (8.0f - 3.0f * nitidez) : 0.0f;
    enFranjas(H, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const std::uint8_t* p = &e.limpio[static_cast<std::size_t>(y) * e.stride];
            std::uint8_t* q = dst + static_cast<std::size_t>(y) * ds;
            const float* fy = &e.y[static_cast<std::size_t>(y) * W];
            const float* fa = &e.y[static_cast<std::size_t>(std::max(0, y - 1)) * W];
            const float* fb = &e.y[static_cast<std::size_t>(std::min(H - 1, y + 1)) * W];
            for (int x = 0; x < W; ++x, p += 4, q += 4) {
                const float yc = fy[x];
                float yNueva = yc;
                if (pesoCas != 0.0f) {
                    const float yArr = fa[x], yAba = fb[x], yIzq = fy[std::max(0, x - 1)], yDer = fy[std::min(W - 1, x + 1)];
                    const float mn = std::min(std::min(std::min(yArr, yIzq), std::min(yDer, yAba)), yc);
                    const float mx = std::max(std::max(std::max(yArr, yIzq), std::max(yDer, yAba)), yc);
                    const float amp = std::sqrt(std::clamp(std::min(mn, 1.0f - mx) / std::max(mx, 0.00001f), 0.0f, 1.0f));
                    const float w = amp * pesoCas;
                    yNueva = (yc + w * (yArr + yIzq + yDer + yAba)) / (1.0f + 4.0f * w);
                }
                const float pr = p[o.r] * inv255, pg = p[o.g] * inv255, pb = p[o.b] * inv255;
                float cr = pr - yc, cg = pg - yc, cb = pb - yc;
                if (color > 0.0f) {
                    // El centro del píxel cae a 1/4 de un bloque: pesos bilineales 0,75 / 0,25.
                    const int bx = (x - 1) >> 1, by = (y - 1) >> 1; // floor((x+0,5)/2 - 0,5)
                    const float frx = (x & 1) != 0 ? 0.25f : 0.75f, fry = (y & 1) != 0 ? 0.25f : 0.75f;
                    const int x0 = std::clamp(bx, 0, mw - 1), x1 = std::clamp(bx + 1, 0, mw - 1);
                    const int y0b = std::clamp(by, 0, mh - 1), y1b = std::clamp(by + 1, 0, mh - 1);
                    const float* f00 = &e.medio[(static_cast<std::size_t>(y0b) * mw + x0) * 4];
                    const float* f10 = &e.medio[(static_cast<std::size_t>(y0b) * mw + x1) * 4];
                    const float* f01 = &e.medio[(static_cast<std::size_t>(y1b) * mw + x0) * 4];
                    const float* f11 = &e.medio[(static_cast<std::size_t>(y1b) * mw + x1) * 4];
                    const auto peso = [&](const float* f, float bil) {
                        const float d = (f[3] - yc) * (1.0f / 0.06f);
                        return bil * ex(d * d) + 0.00001f;
                    };
                    const float w00 = peso(f00, (1.0f - frx) * (1.0f - fry)), w10 = peso(f10, frx * (1.0f - fry));
                    const float w01 = peso(f01, (1.0f - frx) * fry), w11 = peso(f11, frx * fry);
                    const float sw = 1.0f / (w00 + w10 + w01 + w11);
                    const float gr = (f00[0] * w00 + f10[0] * w10 + f01[0] * w01 + f11[0] * w11) * sw;
                    const float gg = (f00[1] * w00 + f10[1] * w10 + f01[1] * w01 + f11[1] * w11) * sw;
                    const float gb = (f00[2] * w00 + f10[2] * w10 + f01[2] * w01 + f11[2] * w11) * sw;
                    cr += (gr - cr) * color;
                    cg += (gg - cg) * color;
                    cb += (gb - cb) * color;
                }
                q[o.r] = ba::filtros::a8(yNueva + cr);
                q[o.g] = ba::filtros::a8(yNueva + cg);
                q[o.b] = ba::filtros::a8(yNueva + cb);
                if (o.a >= 0) q[o.a] = p[o.a];
            }
        }
    });

    // Para el próximo cuadro: lo filtrado y su brillo.
    if (ruido > 0.0f) {
        for (int y = 0; y < H; ++y) std::memcpy(&e.anterior[static_cast<std::size_t>(y) * e.stride], dst + static_cast<std::size_t>(y) * ds, e.stride);
        enFranjas(H, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                const std::uint8_t* p = &e.anterior[static_cast<std::size_t>(y) * e.stride];
                for (int x = 0; x < W; ++x, p += 4)
                    e.yAnt[static_cast<std::size_t>(y) * W + x] = luma(p[o.r], p[o.g], p[o.b]) * inv255;
            }
        });
        e.hayAnterior = true;
    } else {
        e.hayAnterior = false;
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    GST_OBJECT_LOCK(m);
    m->ms = m->ms <= 0 ? ms : m->ms * 0.9 + ms * 0.1;
    GST_OBJECT_UNLOCK(m);
    return GST_FLOW_OK;
}

gboolean alEvento(GstBaseTransform* bt, GstEvent* ev) {
    // Un salto (flush, segmento nuevo): el cuadro anterior ya no vale.
    if (GST_EVENT_TYPE(ev) == GST_EVENT_FLUSH_STOP || GST_EVENT_TYPE(ev) == GST_EVENT_SEGMENT) BA_MEJORA(bt)->e->hayAnterior = false;
    return GST_BASE_TRANSFORM_CLASS(ba_mejora_parent_class)->sink_event(bt, ev);
}

void finalizar(GObject* o) {
    delete BA_MEJORA(o)->e;
    G_OBJECT_CLASS(ba_mejora_parent_class)->finalize(o);
}

} // namespace

static void ba_mejora_init(BaMejora* m) {
    m->ruido = 60;
    m->color = 70;
    m->nitidez = 50;
    m->ms = 0;
    m->e = new Estado();
    gst_base_transform_set_qos_enabled(GST_BASE_TRANSFORM(m), TRUE);
}

static void ba_mejora_class_init(BaMejoraClass* k) {
    auto* go = G_OBJECT_CLASS(k);
    go->set_property = ponerPropiedad;
    go->get_property = leerPropiedad;
    go->finalize = finalizar;
    g_object_class_install_property(go, PROP_RUIDO,
                                    g_param_spec_int("ruido", "Ruido", "Ruido temporal (0 = apagado)", 0, 100, 60, ba::filtros::kVivo));
    g_object_class_install_property(go, PROP_COLOR, g_param_spec_int("color", "Color", "Color pegado al brillo (0 = apagado)", 0,
                                                                     100, 70, ba::filtros::kVivo));
    g_object_class_install_property(go, PROP_NITIDEZ,
                                    g_param_spec_int("nitidez", "Nitidez", "Nitidez CAS (0 = apagada)", 0, 100, 50, ba::filtros::kVivo));
    g_object_class_install_property(go, PROP_MS,
                                    g_param_spec_double("ms-por-cuadro", "ms por cuadro", "Lo que tarda cada cuadro", 0, 1000, 0,
                                                        static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
    auto* ek = GST_ELEMENT_CLASS(k);
    gst_element_class_set_static_metadata(ek, "Mejorar imagen", "Filter/Effect/Video",
                                          "Ruido temporal, color guiado por el brillo y nitidez (CAS)", "Broadcast Appliance");
    GstCaps* caps = gst_caps_from_string("video/x-raw,format={BGRA,RGBA,ARGB,ABGR,BGRx,RGBx,xRGB,xBGR}");
    gst_element_class_add_pad_template(ek, gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps));
    gst_element_class_add_pad_template(ek, gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps));
    gst_caps_unref(caps);
    auto* bk = GST_BASE_TRANSFORM_CLASS(k);
    bk->before_transform = antes;
    bk->sink_event = alEvento;
    GST_VIDEO_FILTER_CLASS(k)->transform_frame = transformar;
}

extern "C" gboolean ba_registrar_mejora(GstPlugin* p) {
    return gst_element_register(p, "bamejora", GST_RANK_NONE, BA_TYPE_MEJORA);
}
