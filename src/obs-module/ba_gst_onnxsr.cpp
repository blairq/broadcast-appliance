// ============================================================================
// src/obs-module/ba_gst_onnxsr.cpp — superresolución x2 con una red ONNX
//
// Proposito:
//   Elemento de GStreamer `baonnxsr`: duplica la resolución con una red
//   neuronal en formato ONNX, corrida por ONNX Runtime (el del programa).
//   Sirve para cualquier modelo que agrande:
//     - 1 canal (luma, Y): la «red chica» y la que se entrena con un botón
//       mirando la propia cámara (assets/superres/laboratorio.py). Solo se
//       procesa el brillo; el color se agranda aparte (el ojo ve el detalle en
//       la luma): un tercio del trabajo y sin convertir a RGB.
//     - 3 canales (RGB): SPAN, Real-ESRGAN… (los convierte el laboratorio).
//   Entradas de los modelos: float 0..1, NCHW [1, C, alto, ancho], con alto y
//   ancho libres (se fijan al tamaño de la cámara al abrir la sesión). Si el
//   modelo agranda x4, el resultado se promedia a x2.
//
//   Aceleración: CoreML en macOS (GPU / Neural Engine), DirectML en Windows,
//   el procesador como respaldo (`proveedor`: auto, cpu, coreml, dml).
//   `ms-por-cuadro` dice cuánto tarda (para decidir si alcanza en vivo).
//
// Modelo de threading:
//   La sesión se arma en el hilo de streaming (al primer cuadro con el tamaño
//   ya conocido) y se usa solo ahí.
// ============================================================================

#include <gst/base/gstbasetransform.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#if defined(BA_HAVE_ROSTROS)
#include <onnxruntime_cxx_api.h>
#if defined(__APPLE__)
#include <coreml_provider_factory.h>
#endif
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

G_BEGIN_DECLS
#define BA_TYPE_ONNXSR (ba_onnxsr_get_type())
G_DECLARE_FINAL_TYPE(BaOnnxSr, ba_onnxsr, BA, ONNXSR, GstBaseTransform)
G_END_DECLS

#if defined(BA_HAVE_ROSTROS)
struct Red {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "baonnxsr"};
    std::unique_ptr<Ort::Session> sesion;
    std::string entrada, salida;
    int canales = 1;
    int factor = 2;   ///< lo que agranda el modelo (2 o 4)
    std::string proveedor;
};
#endif

struct _BaOnnxSr {
    GstBaseTransform parent;
    GstVideoInfo in, out;
    gchar* modelo;
    gchar* proveedor;
    gboolean procesa;
    gchar* error;
#if defined(BA_HAVE_ROSTROS)
    Red* red;
    GThread* preparando;   ///< la red se arma en otro hilo (CoreML compila ~3 s)
    std::atomic<Red*>* lista; ///< lo que dejó el hilo
    int pw, ph;            ///< el tamaño para el que se prepara
#endif
    guint64 cuadros;
    gdouble msTotal;
    gdouble ms; ///< promedio de los últimos cuadros (móvil)
};

G_DEFINE_TYPE(BaOnnxSr, ba_onnxsr, GST_TYPE_BASE_TRANSFORM)

enum { PROP_0, PROP_MODELO, PROP_PROVEEDOR, PROP_MS, PROP_ERROR, PROP_PROVEEDOR_USADO };

static GstStaticPadTemplate sinkTpl = GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
                                                              GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE("NV12")));
static GstStaticPadTemplate srcTpl = GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS,
                                                             GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE("NV12")));

static bool hayModelo(BaOnnxSr* s) { return s->modelo != nullptr && *s->modelo != '\0'; }

static void ponerError(BaOnnxSr* s, const std::string& e) {
    g_free(s->error);
    s->error = g_strdup(e.c_str());
    GST_WARNING_OBJECT(s, "%s", e.c_str());
}

static void soltarRed(BaOnnxSr* s) {
#if defined(BA_HAVE_ROSTROS)
    if (s->preparando != nullptr) {
        g_thread_join(s->preparando);
        s->preparando = nullptr;
    }
    delete s->lista->exchange(nullptr);
    delete s->red;
    s->red = nullptr;
#endif
}

/// Abre la sesión para este tamaño (en el hilo de preparación). false si no se pudo (queda el error).
static bool abrirRed(BaOnnxSr* s, int w, int h) {
#if defined(BA_HAVE_ROSTROS)
    try {
        auto* r = new Red();
        Ort::SessionOptions op;
        op.SetIntraOpNumThreads(4);
        op.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        // Alto y ancho libres del modelo, fijos al tamaño de la cámara: así la
        // aceleración (CoreML) compila para ese tamaño y no cae al procesador.
        for (const auto& [nombre, valor] : {std::pair{"alto", h}, {"ancho", w}, {"height", h}, {"width", w}})
            Ort::ThrowOnError(Ort::GetApi().AddFreeDimensionOverrideByName(op, nombre, valor));
        const std::string pedido = s->proveedor != nullptr ? s->proveedor : "auto";
        r->proveedor = "cpu";
#if defined(__APPLE__)
        if (pedido == "auto" || pedido == "coreml") {
            // MLProgram + todas las unidades (CPU, GPU, Neural Engine).
            if (OrtSessionOptionsAppendExecutionProvider_CoreML(op, COREML_FLAG_CREATE_MLPROGRAM) == nullptr) r->proveedor = "coreml";
        }
#elif defined(_WIN32)
        if (pedido == "auto" || pedido == "dml") {
            try {
                op.AppendExecutionProvider("DML");
                r->proveedor = "dml";
            } catch (const Ort::Exception&) {
            }
        }
#endif
#if defined(_WIN32)
        const std::wstring ruta(s->modelo, s->modelo + std::strlen(s->modelo));
        r->sesion = std::make_unique<Ort::Session>(r->env, ruta.c_str(), op);
#else
        r->sesion = std::make_unique<Ort::Session>(r->env, s->modelo, op);
#endif
        Ort::AllocatorWithDefaultOptions al;
        r->entrada = r->sesion->GetInputNameAllocated(0, al).get();
        r->salida = r->sesion->GetOutputNameAllocated(0, al).get();
        const auto forma = r->sesion->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        r->canales = forma.size() == 4 && forma[1] > 0 ? static_cast<int>(forma[1]) : 1;
        if (r->canales != 1 && r->canales != 3) throw std::runtime_error("el modelo tiene que tener 1 (luma) o 3 (RGB) canales de entrada");
        // Lo que agranda: una pasada de prueba (y de paso calienta la red).
        std::vector<float> prueba(static_cast<size_t>(r->canales) * w * h, 0.5f);
        const std::array<int64_t, 4> dims{1, r->canales, h, w};
        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value t = Ort::Value::CreateTensor<float>(mem, prueba.data(), prueba.size(), dims.data(), dims.size());
        const char* ins[] = {r->entrada.c_str()};
        const char* outs[] = {r->salida.c_str()};
        auto res = r->sesion->Run(Ort::RunOptions{nullptr}, ins, &t, 1, outs, 1);
        const auto fo = res[0].GetTensorTypeAndShapeInfo().GetShape();
        r->factor = fo.size() == 4 ? static_cast<int>(fo[3] / w) : 0;
        if (r->factor != 2 && r->factor != 4) throw std::runtime_error("el modelo agranda x" + std::to_string(r->factor) + ": se usa x2 o x4");
        s->lista->store(r);
        GST_INFO_OBJECT(s, "red %s: %d canal(es), x%d, %s", s->modelo, r->canales, r->factor, r->proveedor.c_str());
        g_free(s->error);
        s->error = nullptr;
        return true;
    } catch (const std::exception& e) {
        ponerError(s, std::string("no se pudo abrir la red: ") + e.what());
        return false;
    }
#else
    (void)w;
    (void)h;
    ponerError(s, "el programa no trae ONNX Runtime");
    return false;
#endif
}

static GstCaps* transformarCaps(GstBaseTransform* bt, GstPadDirection dir, GstCaps* caps, GstCaps* filtro) {
    auto* s = BA_ONNXSR(bt);
    GstCaps* out = gst_caps_new_empty();
    for (guint i = 0; i < gst_caps_get_size(caps); ++i) {
        GstStructure* st = gst_structure_copy(gst_caps_get_structure(caps, i));
        gst_caps_append_structure(out, gst_structure_copy(st));
        int w = 0, h = 0;
        if (hayModelo(s) && gst_structure_get_int(st, "width", &w) && gst_structure_get_int(st, "height", &h)) {
            if (dir == GST_PAD_SINK) {
                gst_structure_set(st, "width", G_TYPE_INT, w * 2, "height", G_TYPE_INT, h * 2, nullptr);
                GstCaps* d = gst_caps_new_full(st, nullptr);
                gst_caps_append(d, out);
                out = d;
                st = nullptr;
            } else if (w % 2 == 0 && h % 2 == 0) {
                gst_structure_set(st, "width", G_TYPE_INT, w / 2, "height", G_TYPE_INT, h / 2, nullptr);
                gst_caps_append_structure(out, st);
                st = nullptr;
            }
        }
        if (st != nullptr) gst_structure_free(st);
    }
    if (filtro != nullptr) {
        GstCaps* f = gst_caps_intersect_full(filtro, out, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref(out);
        out = f;
    }
    return out;
}

static gboolean tamanoSalida(GstBaseTransform*, GstPadDirection, GstCaps*, gsize, GstCaps* otras, gsize* otro) {
    GstVideoInfo vi;
    if (!gst_video_info_from_caps(&vi, otras)) return FALSE;
    *otro = GST_VIDEO_INFO_SIZE(&vi);
    return TRUE;
}

static gboolean ponerCaps(GstBaseTransform* bt, GstCaps* incaps, GstCaps* outcaps) {
    auto* s = BA_ONNXSR(bt);
    if (!gst_video_info_from_caps(&s->in, incaps) || !gst_video_info_from_caps(&s->out, outcaps)) return FALSE;
    s->procesa = GST_VIDEO_INFO_WIDTH(&s->out) == 2 * GST_VIDEO_INFO_WIDTH(&s->in);
    soltarRed(s); // se abre al primer cuadro, con este tamaño
    gst_base_transform_set_passthrough(bt, !s->procesa);
    return TRUE;
}

/// El plano de color (UV intercalado) al doble, bilineal.
static void colorX2(const guint8* uv, int paso, int cw, int ch, guint8* dst, int pasoD) {
    for (int y = 0; y < ch * 2; ++y) {
        const float sy = std::max(0.0f, (y + 0.5f) / 2.0f - 0.5f);
        const int y0 = std::min(static_cast<int>(sy), ch - 1), y1 = std::min(y0 + 1, ch - 1);
        const float fy = sy - static_cast<float>(y0);
        const guint8* r0 = uv + static_cast<size_t>(y0) * paso;
        const guint8* r1 = uv + static_cast<size_t>(y1) * paso;
        guint8* o = dst + static_cast<size_t>(y) * pasoD;
        for (int x = 0; x < cw * 2; ++x) {
            const float sx = std::max(0.0f, (x + 0.5f) / 2.0f - 0.5f);
            const int x0 = std::min(static_cast<int>(sx), cw - 1), x1 = std::min(x0 + 1, cw - 1);
            const float fx = sx - static_cast<float>(x0);
            for (int c = 0; c < 2; ++c) {
                const float a = r0[x0 * 2 + c] * (1 - fx) + r0[x1 * 2 + c] * fx;
                const float b = r1[x0 * 2 + c] * (1 - fx) + r1[x1 * 2 + c] * fx;
                o[x * 2 + c] = static_cast<guint8>(a * (1 - fy) + b * fy + 0.5f);
            }
        }
    }
}

static inline guint8 sat(float v) { return static_cast<guint8>(std::clamp(v, 0.0f, 255.0f) + 0.5f); }

static GstFlowReturn transformar(GstBaseTransform* bt, GstBuffer* inbuf, GstBuffer* outbuf) {
    auto* s = BA_ONNXSR(bt);
    if (!s->procesa) return GST_FLOW_OK;
    const int w = GST_VIDEO_INFO_WIDTH(&s->in), h = GST_VIDEO_INFO_HEIGHT(&s->in);
    GstVideoFrame fi, fo;
    if (!gst_video_frame_map(&fi, &s->in, inbuf, GST_MAP_READ)) return GST_FLOW_ERROR;
    if (!gst_video_frame_map(&fo, &s->out, outbuf, GST_MAP_WRITE)) {
        gst_video_frame_unmap(&fi);
        return GST_FLOW_ERROR;
    }
    const guint8* Y = static_cast<const guint8*>(GST_VIDEO_FRAME_PLANE_DATA(&fi, 0));
    const guint8* UV = static_cast<const guint8*>(GST_VIDEO_FRAME_PLANE_DATA(&fi, 1));
    const int pY = GST_VIDEO_FRAME_PLANE_STRIDE(&fi, 0), pUV = GST_VIDEO_FRAME_PLANE_STRIDE(&fi, 1);
    guint8* oY = static_cast<guint8*>(GST_VIDEO_FRAME_PLANE_DATA(&fo, 0));
    guint8* oUV = static_cast<guint8*>(GST_VIDEO_FRAME_PLANE_DATA(&fo, 1));
    const int poY = GST_VIDEO_FRAME_PLANE_STRIDE(&fo, 0), poUV = GST_VIDEO_FRAME_PLANE_STRIDE(&fo, 1);
    const gint64 t0 = g_get_monotonic_time();
    bool hecho = false;
#if defined(BA_HAVE_ROSTROS)
    // La red se prepara en otro hilo: mientras tanto la imagen sale agrandada
    // simple (la entrada no se congela los segundos que tarda CoreML en compilar).
    if (s->red == nullptr) {
        if (Red* r = s->lista->exchange(nullptr)) {
            s->red = r;
            if (s->preparando != nullptr) g_thread_join(s->preparando), s->preparando = nullptr;
        } else if (s->preparando == nullptr && s->error == nullptr && hayModelo(s)) {
            s->pw = w;
            s->ph = h;
            s->preparando = g_thread_new("baonnxsr-preparar", [](gpointer d) -> gpointer {
                auto* x = static_cast<BaOnnxSr*>(d);
                abrirRed(x, x->pw, x->ph);
                return nullptr;
            }, s);
        }
    }
    if (Red* r = s->red) {
        try {
            const int C = r->canales;
            std::vector<float> in(static_cast<size_t>(C) * w * h);
            if (C == 1) {
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x) in[static_cast<size_t>(y) * w + x] = (Y[y * pY + x] - 16.0f) / 219.0f;
            } else {
                // NV12 (BT.709, rango limitado) -> RGB 0..1, NCHW.
                const size_t plano = static_cast<size_t>(w) * h;
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x) {
                        const float yy = (Y[y * pY + x] - 16.0f) * (1.0f / 219.0f);
                        const float u = (UV[(y / 2) * pUV + (x / 2) * 2] - 128.0f) * (1.0f / 224.0f);
                        const float v = (UV[(y / 2) * pUV + (x / 2) * 2 + 1] - 128.0f) * (1.0f / 224.0f);
                        const size_t i = static_cast<size_t>(y) * w + x;
                        in[i] = std::clamp(yy + 1.5748f * v, 0.0f, 1.0f);
                        in[plano + i] = std::clamp(yy - 0.1873f * u - 0.4681f * v, 0.0f, 1.0f);
                        in[2 * plano + i] = std::clamp(yy + 1.8556f * u, 0.0f, 1.0f);
                    }
            }
            const std::array<int64_t, 4> dims{1, C, h, w};
            Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
            Ort::Value t = Ort::Value::CreateTensor<float>(mem, in.data(), in.size(), dims.data(), dims.size());
            const char* ins[] = {r->entrada.c_str()};
            const char* outs[] = {r->salida.c_str()};
            auto res = r->sesion->Run(Ort::RunOptions{nullptr}, ins, &t, 1, outs, 1);
            const float* o = res[0].GetTensorData<float>();
            const int F = r->factor, W = w * F, H = h * F;
            const int k = F / 2; // 1 (x2) o 2 (x4: se promedian 2x2)
            const float div = 1.0f / static_cast<float>(k * k);
            const size_t planoO = static_cast<size_t>(W) * H;
            for (int y = 0; y < h * 2; ++y)
                for (int x = 0; x < w * 2; ++x) {
                    float acc[3] = {0, 0, 0};
                    for (int dy = 0; dy < k; ++dy)
                        for (int dx = 0; dx < k; ++dx) {
                            const size_t i = static_cast<size_t>(y * k + dy) * W + (x * k + dx);
                            for (int c = 0; c < C; ++c) acc[c] += o[c * planoO + i];
                        }
                    if (C == 1) {
                        oY[y * poY + x] = sat(16.0f + 219.0f * acc[0] * div);
                    } else {
                        const float R = acc[0] * div, G = acc[1] * div, B = acc[2] * div;
                        oY[y * poY + x] = sat(16.0f + 219.0f * (0.2126f * R + 0.7152f * G + 0.0722f * B));
                        if ((x & 1) == 0 && (y & 1) == 0) {
                            oUV[(y / 2) * poUV + x] = sat(128.0f + 224.0f * (-0.1146f * R - 0.3854f * G + 0.5f * B));
                            oUV[(y / 2) * poUV + x + 1] = sat(128.0f + 224.0f * (0.5f * R - 0.4542f * G - 0.0458f * B));
                        }
                    }
                }
            if (C == 1) colorX2(UV, pUV, w / 2, h / 2, oUV, poUV);
            hecho = true;
        } catch (const std::exception& e) {
            ponerError(s, std::string("la red falló: ") + e.what());
            soltarRed(s);
        }
    }
#endif
    if (!hecho) {
        // Sin red (o falló): se agranda simple para que la imagen siga.
        for (int y = 0; y < h * 2; ++y) {
            const guint8* fila = Y + (y / 2) * pY;
            for (int x = 0; x < w * 2; ++x) oY[y * poY + x] = fila[x / 2];
        }
        colorX2(UV, pUV, w / 2, h / 2, oUV, poUV);
    }
    gst_video_frame_unmap(&fo);
    gst_video_frame_unmap(&fi);
    const double ms = static_cast<double>(g_get_monotonic_time() - t0) / 1000.0;
    if (!hecho) return GST_FLOW_OK; // preparando o sin red: no cuenta para el promedio
    ++s->cuadros;
    s->msTotal += ms;
    s->ms = s->cuadros == 1 ? ms : s->ms * 0.9 + ms * 0.1;
    return GST_FLOW_OK;
}

static void ponerPropiedad(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    auto* s = BA_ONNXSR(o);
    switch (id) {
        case PROP_MODELO:
            g_free(s->modelo);
            s->modelo = g_value_dup_string(v);
            g_free(s->error);
            s->error = nullptr;
            soltarRed(s);
            gst_base_transform_reconfigure_src(GST_BASE_TRANSFORM(s));
            break;
        case PROP_PROVEEDOR:
            g_free(s->proveedor);
            s->proveedor = g_value_dup_string(v);
            soltarRed(s);
            break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static void leerPropiedad(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    auto* s = BA_ONNXSR(o);
    switch (id) {
        case PROP_MODELO: g_value_set_string(v, s->modelo); break;
        case PROP_PROVEEDOR: g_value_set_string(v, s->proveedor); break;
        case PROP_MS: g_value_set_double(v, s->ms); break;
        case PROP_ERROR: g_value_set_string(v, s->error); break;
        case PROP_PROVEEDOR_USADO:
#if defined(BA_HAVE_ROSTROS)
            g_value_set_string(v, s->red != nullptr ? s->red->proveedor.c_str() : "");
#else
            g_value_set_string(v, "");
#endif
            break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static gboolean parar(GstBaseTransform* bt) {
    soltarRed(BA_ONNXSR(bt));
    return TRUE;
}

static void ba_onnxsr_finalize(GObject* o) {
    auto* s = BA_ONNXSR(o);
    soltarRed(s);
#if defined(BA_HAVE_ROSTROS)
    delete s->lista;
#endif
    g_free(s->modelo);
    g_free(s->proveedor);
    g_free(s->error);
    G_OBJECT_CLASS(ba_onnxsr_parent_class)->finalize(o);
}

static void ba_onnxsr_class_init(BaOnnxSrClass* k) {
    GObjectClass* go = G_OBJECT_CLASS(k);
    GstElementClass* ge = GST_ELEMENT_CLASS(k);
    GstBaseTransformClass* bt = GST_BASE_TRANSFORM_CLASS(k);
    go->set_property = ponerPropiedad;
    go->get_property = leerPropiedad;
    go->finalize = ba_onnxsr_finalize;
    const auto rw = static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    const auto ro = static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property(go, PROP_MODELO, g_param_spec_string("modelo", "Modelo", "Ruta del .onnx (vacío: pasa sin tocar)", nullptr, rw));
    g_object_class_install_property(go, PROP_PROVEEDOR,
                                    g_param_spec_string("proveedor", "Proveedor", "auto, cpu, coreml (macOS) o dml (Windows)", "auto", rw));
    g_object_class_install_property(go, PROP_MS, g_param_spec_double("ms-por-cuadro", "ms por cuadro", "Lo que tarda (promedio móvil)", 0, 100000, 0, ro));
    g_object_class_install_property(go, PROP_ERROR, g_param_spec_string("error", "Error", "Por qué no anda (si no anda)", nullptr, ro));
    g_object_class_install_property(go, PROP_PROVEEDOR_USADO,
                                    g_param_spec_string("proveedor-usado", "Proveedor usado", "Con qué corre la red", nullptr, ro));
    gst_element_class_set_static_metadata(ge, "Superresolución x2 con una red ONNX", "Filter/Effect/Video/Scaler",
                                          "Duplica la resolución con un modelo ONNX (luma o RGB, x2 o x4)", "Broadcast Appliance");
    gst_element_class_add_static_pad_template(ge, &sinkTpl);
    gst_element_class_add_static_pad_template(ge, &srcTpl);
    bt->transform_caps = transformarCaps;
    bt->transform_size = tamanoSalida;
    bt->set_caps = ponerCaps;
    bt->transform = transformar;
    bt->stop = parar;
    bt->passthrough_on_same_caps = TRUE;
}

static void ba_onnxsr_init(BaOnnxSr* s) {
    s->modelo = nullptr;
    s->proveedor = g_strdup("auto");
    s->error = nullptr;
    s->procesa = FALSE;
#if defined(BA_HAVE_ROSTROS)
    s->red = nullptr;
    s->preparando = nullptr;
    s->lista = new std::atomic<Red*>(nullptr);
    s->pw = s->ph = 0;
#endif
    s->cuadros = 0;
    s->msTotal = 0;
    s->ms = 0;
}

namespace ba::gst {
/// Registra `baonnxsr` (lo llama registrarElementosPlugin).
void registrarOnnxSr() { gst_element_register(nullptr, "baonnxsr", GST_RANK_NONE, BA_TYPE_ONNXSR); }
} // namespace ba::gst
