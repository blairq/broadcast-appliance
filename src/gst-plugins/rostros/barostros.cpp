// ============================================================================
// src/gst-plugins/rostros/barostros.cpp — barostros: caras en la cadena de una entrada
//
// La tarjeta «Rostros» del grafo de video como elemento de verdad. La imagen
// PASA SIN DEMORA y sin copia; aparte, unas pocas veces por segundo, una copia
// chica se analiza en su propio hilo:
//
//   sink ─▶ tee ─┬─▶ [gloverlaycompositor] ─▶ sale ─▶ src      (todos los cuadros)
//                └─▶ queue(descarta) ─▶ videorate(fps) ─▶ [bajar de la GPU]
//                      ─▶ videoconvertscale(BGRA, chico) ─▶ appsink   (análisis)
//
// Si el análisis se atrasa, la cola tira lo viejo: el video nunca lo espera.
// Cada cara lleva un NÚMERO (id) que se conserva de un análisis al siguiente
// (la más cercana a donde estaba): sirve para seguirla y para ponerle nombre.
//
// LO QUE PUBLICA, para el resto del pipeline y para la app:
//   - En CADA cuadro, una GstVideoRegionOfInterestMeta por cara (tipo «face»,
//     `id` = el número de la cara, en píxeles del cuadro) con un parámetro
//     «ba-rostro» {id, confianza, nombre, cargo, edad-ms}: la meta estándar de
//     GStreamer para regiones, la que entienden los elementos que miran ROIs.
//   - Por cada análisis, un evento «ba/rostros» río abajo
//     (GST_EVENT_CUSTOM_DOWNSTREAM): `cantidad`, `pts` (el del cuadro
//     analizado), `ms`, `detector`, `caras` («x,y,ancho,alto;…» en fracciones:
//     lo lee baptz para seguir a una) y `json` (todo: id, caja, confianza, los
//     cinco puntos, nombre y cargo si se conocen).
//   - Por cada análisis, un mensaje de elemento «ba-rostros» al bus: el mismo
//     `json` más el vector de cada cara (con `reconocer`) y `cuadro` (el cuadro
//     analizado, un GstSample, para la foto de una ficha). La app lo lee como
//     los vúmetros: sin vista aparte ni copias.
//
// EL OVERLAY (`marcar`): `estilo` recuadro | esquinas | relleno | tapar |
// pixelar (los dos últimos, para no mostrar una cara), `color`, `grosor` y un
// TEXTO con plantilla (`texto`: {nombre} {cargo} {id} {confianza} {n}) arriba
// o abajo de la cara. El nombre y el cargo los pone la app cuando reconoce a
// alguien (canal de control «ba/control», campo `rostros_etiquetas`: JSON
// [{id, nombre, cargo}], o la propiedad `etiquetas`). En GL lo dibuja
// gloverlaycompositor; en memoria de sistema se mezcla en el cuadro; en D3D12
// todavía no hay quién lo dibuje (sigue publicando todo lo demás).
//
// DETECTORES: en la Mac, el de Apple (Vision, en el Neural Engine) si no hace
// falta reconocer; si no, YuNet (ONNX Runtime). Reconocer es SFace sobre las
// caras de YuNet (src/core/rostros, el mismo que usa la libreta de Personas).
//
// Tres fábricas, una por memoria (el motor elige la del video de la entrada):
//   barostros (sistema), barostrosgl (GLMemory), barostrosd3d12 (D3D12Memory).
// ============================================================================
#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#if defined(BA_ROSTROS_TEXTO)
#include <pango/pangocairo.h>
#endif

#include "rostros/Rostros.hpp"
#include "vision.hpp"

namespace {

enum class Memoria { Sistema, Gl, D3d12 };

std::string gCarpetaModelos; ///< al lado del plugin: ../data/rostros (la pone ba_registrar_rostros)

/// Una caja en fracciones del cuadro.
struct CajaN {
    double x = 0, y = 0, ancho = 0, alto = 0;
    double cx() const { return x + ancho / 2; }
    double cy() const { return y + alto / 2; }
};

/// Una cara que se sigue de un análisis al siguiente.
struct Pista {
    int id = 0;
    CajaN caja;
    double confianza = 0;
    bool hayPuntos = false;
    float puntos[10] = {}; ///< en fracciones del cuadro
    std::vector<float> vector;
};

struct Etiqueta {
    std::string nombre, cargo;
};

struct Estado {
    std::mutex mu;
    // Ajustes (bajo mu).
    bool activo = true;
    double fps = 5.0;
    bool reconocer = false;
    double confianza = 0.7;
    std::string detector = "auto"; ///< auto | vision | yunet
    std::string modelos;
    int anchoAnalisis = 640;
    bool marcar = false;
    std::string estilo = "recuadro"; ///< recuadro | esquinas | relleno | tapar | pixelar
    guint color = 0xFFFFC83C;        ///< ARGB
    int grosor = 4;                  ///< en píxeles del video
    std::string texto;               ///< plantilla: {nombre} {cargo} {id} {confianza} {n}
    std::string textoPosicion = "arriba";
    double textoTamano = 0.035; ///< fracción del alto del video
    std::map<int, Etiqueta> etiquetas;
    // Lo último que se vio (bajo mu).
    std::vector<Pista> pistas;
    int proximoId = 1;
    GstClockTime ptsAnalisis = GST_CLOCK_TIME_NONE;
    std::int64_t vistoUs = 0;          ///< cuándo se analizó (reloj monotónico)
    std::vector<std::uint8_t> analisis; ///< el último cuadro analizado (BGRA), para pixelar
    int anchoA = 0, altoA = 0;
    bool nuevas = false; ///< hay que mandar el evento río abajo
    GstVideoOverlayComposition* comp = nullptr;
    int anchoVideo = 0, altoVideo = 0;
    GstVideoInfo infoVideo{};
    bool hayInfo = false;
    std::string ultimoJson = "{\"caras\":[]}";
    std::string error;
    double msAnalisis = 0;
    std::string detectorUsado;
    // Del hilo del análisis.
    std::unique_ptr<ba::rostros::Motor> motor;
    bool motorProbado = false;
};

} // namespace

G_BEGIN_DECLS
#define BA_TYPE_ROSTROS (ba_rostros_get_type())
G_DECLARE_DERIVABLE_TYPE(BaRostros, ba_rostros, BA, ROSTROS, GstBin)
struct _BaRostrosClass {
    GstBinClass parent;
    Memoria memoria;
};
G_END_DECLS

struct BaRostrosPrivate {
    Estado* st = nullptr;
    GstElement* sale = nullptr;
    GstElement* tasa = nullptr; ///< videorate de la rama de análisis
    GstElement* tam = nullptr;  ///< capsfilter de la rama de análisis
};

G_DEFINE_TYPE_WITH_PRIVATE(BaRostros, ba_rostros, GST_TYPE_BIN)

enum {
    PROP_0,
    PROP_ACTIVO,
    PROP_FPS,
    PROP_RECONOCER,
    PROP_CONFIANZA,
    PROP_DETECTOR,
    PROP_MODELOS,
    PROP_ANCHO,
    PROP_MARCAR,
    PROP_ESTILO,
    PROP_COLOR,
    PROP_GROSOR,
    PROP_TEXTO,
    PROP_TEXTO_POSICION,
    PROP_TEXTO_TAMANO,
    PROP_ETIQUETAS,
    PROP_CARAS,
    PROP_ERROR,
    PROP_MS
};

namespace {

BaRostrosPrivate* priv(gpointer o) {
    return static_cast<BaRostrosPrivate*>(ba_rostros_get_instance_private(BA_ROSTROS(o)));
}

std::string decimal(double v) {
    gchar b[G_ASCII_DTOSTR_BUF_SIZE];
    return g_ascii_formatd(b, sizeof b, "%.4f", v);
}

std::int64_t ahoraUs() { return g_get_monotonic_time(); }

// ------------------------------------------------------------------ el overlay

/// Una imagen BGRA (sin premultiplicar) como rectángulo de overlay en (x, y, w, h) del video.
GstVideoOverlayRectangle* rectangulo(const std::vector<std::uint8_t>& px, int iw, int ih, int x, int y, int w, int h,
                                     bool premultiplicado = false) {
    GstBuffer* b = gst_buffer_new_allocate(nullptr, static_cast<gsize>(iw) * ih * 4, nullptr);
    gst_buffer_fill(b, 0, px.data(), px.size());
    gst_buffer_add_video_meta(b, GST_VIDEO_FRAME_FLAG_NONE, GST_VIDEO_OVERLAY_COMPOSITION_FORMAT_RGB, iw, ih);
    GstVideoOverlayRectangle* r = gst_video_overlay_rectangle_new_raw(
        b, x, y, static_cast<guint>(w), static_cast<guint>(h),
        premultiplicado ? GST_VIDEO_OVERLAY_FORMAT_FLAG_PREMULTIPLIED_ALPHA : GST_VIDEO_OVERLAY_FORMAT_FLAG_NONE);
    gst_buffer_unref(b);
    return r;
}

void pintar(std::vector<std::uint8_t>& px, int iw, int i, int j, guint argb, std::uint8_t alfa) {
    std::uint8_t* p = px.data() + (static_cast<size_t>(j) * iw + i) * 4;
    p[0] = argb & 0xFF;
    p[1] = (argb >> 8) & 0xFF;
    p[2] = (argb >> 16) & 0xFF;
    p[3] = alfa;
}

/// La marca de una cara según el estilo (bajo mu: lee el cuadro analizado para pixelar).
GstVideoOverlayRectangle* marca(const Estado* st, const CajaN& c) {
    const int W = st->anchoVideo, H = st->altoVideo;
    const int x = std::clamp(static_cast<int>(std::lround(c.x * W)), 0, W - 1);
    const int y = std::clamp(static_cast<int>(std::lround(c.y * H)), 0, H - 1);
    const int w = std::clamp(static_cast<int>(std::lround(c.ancho * W)), 4, W - x);
    const int h = std::clamp(static_cast<int>(std::lround(c.alto * H)), 4, H - y);
    const std::uint8_t alfa = static_cast<std::uint8_t>((st->color >> 24) & 0xFF);
    if (st->estilo == "tapar") {
        std::vector<std::uint8_t> px(4 * 4 * 4);
        for (int j = 0; j < 4; ++j)
            for (int i = 0; i < 4; ++i) pintar(px, 4, i, j, st->color, 255);
        return rectangulo(px, 4, 4, x, y, w, h);
    }
    if (st->estilo == "pixelar" && !st->analisis.empty()) {
        // Bloques de ~1/10 de la cara, con el color medio del cuadro analizado.
        const int cols = 10, filas = std::clamp(static_cast<int>(std::lround(10.0 * h / w)), 4, 20);
        std::vector<std::uint8_t> px(static_cast<size_t>(cols) * filas * 4);
        for (int j = 0; j < filas; ++j)
            for (int i = 0; i < cols; ++i) {
                const int ax0 = static_cast<int>((c.x + c.ancho * i / cols) * st->anchoA);
                const int ax1 = std::max(ax0 + 1, static_cast<int>((c.x + c.ancho * (i + 1) / cols) * st->anchoA));
                const int ay0 = static_cast<int>((c.y + c.alto * j / filas) * st->altoA);
                const int ay1 = std::max(ay0 + 1, static_cast<int>((c.y + c.alto * (j + 1) / filas) * st->altoA));
                long s[3] = {0, 0, 0};
                long n = 0;
                for (int yy = std::max(0, ay0); yy < std::min(st->altoA, ay1); ++yy)
                    for (int xx = std::max(0, ax0); xx < std::min(st->anchoA, ax1); ++xx) {
                        const std::uint8_t* q = st->analisis.data() + (static_cast<size_t>(yy) * st->anchoA + xx) * 4;
                        s[0] += q[0];
                        s[1] += q[1];
                        s[2] += q[2];
                        ++n;
                    }
                std::uint8_t* p = px.data() + (static_cast<size_t>(j) * cols + i) * 4;
                for (int k = 0; k < 3; ++k) p[k] = static_cast<std::uint8_t>(n > 0 ? s[k] / n : 0);
                p[3] = 255;
            }
        return rectangulo(px, cols, filas, x, y, w, h);
    }
    // Recuadro, esquinas o relleno: se dibuja a escala y se estira al tamaño de la cara.
    const int iw = std::clamp(w / 2, 8, 512), ih = std::clamp(h / 2, 8, 512);
    const double s = static_cast<double>(iw) / w;
    const int borde = std::max(1, static_cast<int>(std::lround(st->grosor * s)));
    std::vector<std::uint8_t> px(static_cast<size_t>(iw) * ih * 4, 0);
    const int largoX = iw / 4, largoY = ih / 4;
    for (int j = 0; j < ih; ++j)
        for (int i = 0; i < iw; ++i) {
            const bool enBorde = i < borde || i >= iw - borde || j < borde || j >= ih - borde;
            if (st->estilo == "esquinas") {
                const bool esquina = (i < largoX || i >= iw - largoX) && (j < largoY || j >= ih - largoY);
                if (enBorde && esquina) pintar(px, iw, i, j, st->color, alfa);
            } else if (enBorde) {
                pintar(px, iw, i, j, st->color, alfa);
            } else if (st->estilo == "relleno") {
                pintar(px, iw, i, j, st->color, static_cast<std::uint8_t>(alfa / 4));
            }
        }
    return rectangulo(px, iw, ih, x, y, w, h);
}

/// La plantilla del texto con lo que se sabe de la cara.
std::string textoDe(const Estado* st, const Pista& p) {
    if (st->texto.empty()) return {};
    const auto e = st->etiquetas.find(p.id);
    const std::string nombre = e != st->etiquetas.end() ? e->second.nombre : std::string();
    const std::string cargo = e != st->etiquetas.end() ? e->second.cargo : std::string();
    std::string t = st->texto;
    const auto poner = [&t](const std::string& clave, const std::string& valor) {
        for (std::size_t i = t.find(clave); i != std::string::npos; i = t.find(clave, i + valor.size()))
            t.replace(i, clave.size(), valor);
    };
    poner("{nombre}", nombre);
    poner("{cargo}", cargo);
    poner("{id}", std::to_string(p.id));
    poner("{confianza}", std::to_string(static_cast<int>(std::lround(p.confianza * 100))) + " %");
    poner("{n}", std::to_string(st->pistas.size()));
    // Lo que quedó vacío no deja separadores colgando («Ana — »).
    while (!t.empty() && (t.back() == ' ' || t.back() == '-' || t.back() == ',' || t.back() == '|'))
        t.pop_back();
    if (t.size() >= 4 && t.compare(t.size() - 4, 4, " \xE2\x80\x94") == 0) t.resize(t.size() - 4);
    std::size_t i = 0;
    while (i < t.size() && (t[i] == ' ' || t[i] == '-' || t[i] == ',' || t[i] == '|')) ++i;
    return t.substr(i);
}

#if defined(BA_ROSTROS_TEXTO)
/// El texto como rectángulo de overlay, pegado a la caja (arriba o abajo).
GstVideoOverlayRectangle* cartel(const Estado* st, const std::string& texto, int bx, int by, int bh) {
    const int W = st->anchoVideo, H = st->altoVideo;
    const int px = std::max(10, static_cast<int>(std::lround(st->textoTamano * H)));
    cairo_surface_t* medir = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t* cm = cairo_create(medir);
    PangoLayout* lay = pango_cairo_create_layout(cm);
    PangoFontDescription* fd = pango_font_description_from_string("Sans Bold");
    pango_font_description_set_absolute_size(fd, px * PANGO_SCALE);
    pango_layout_set_font_description(lay, fd);
    pango_layout_set_text(lay, texto.c_str(), -1);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(lay, &tw, &th);
    const int pad = std::max(2, px / 4);
    const int w = std::min(W, tw + 2 * pad), h = std::min(H, th + 2 * pad);
    cairo_surface_t* sup = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t* cr = cairo_create(sup);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.62);
    cairo_paint(cr);
    const guint c = st->color;
    cairo_set_source_rgba(cr, ((c >> 16) & 0xFF) / 255.0, ((c >> 8) & 0xFF) / 255.0, (c & 0xFF) / 255.0, 1.0);
    cairo_move_to(cr, pad, pad);
    pango_cairo_update_layout(cr, lay);
    pango_cairo_show_layout(cr, lay);
    cairo_surface_flush(sup);
    std::vector<std::uint8_t> datos(static_cast<size_t>(w) * h * 4);
    const int stride = cairo_image_surface_get_stride(sup);
    const unsigned char* src = cairo_image_surface_get_data(sup);
    for (int j = 0; j < h; ++j) std::memcpy(datos.data() + static_cast<size_t>(j) * w * 4, src + static_cast<size_t>(j) * stride, static_cast<size_t>(w) * 4);
    cairo_destroy(cr);
    cairo_surface_destroy(sup);
    pango_font_description_free(fd);
    g_object_unref(lay);
    cairo_destroy(cm);
    cairo_surface_destroy(medir);
    const int x = std::clamp(bx, 0, std::max(0, W - w));
    int y = st->textoPosicion == "abajo" ? by + bh + 2 : by - h - 2;
    if (y < 0) y = by + bh + 2; // arriba no entra: abajo
    y = std::clamp(y, 0, std::max(0, H - h));
    // cairo: BGRA premultiplicado en memoria (little endian), lo mismo que pide el overlay.
    return rectangulo(datos, w, h, x, y, w, h, true);
}
#endif

/// Rehace las marcas con lo de ahora (bajo mu).
void rehacerComposicion(Estado* st) {
    if (st->comp != nullptr) {
        gst_video_overlay_composition_unref(st->comp);
        st->comp = nullptr;
    }
    if (!st->marcar || st->pistas.empty() || st->anchoVideo <= 0 || st->altoVideo <= 0) return;
    const auto sumar = [st](GstVideoOverlayRectangle* r) {
        if (r == nullptr) return;
        if (st->comp == nullptr)
            st->comp = gst_video_overlay_composition_new(r);
        else
            gst_video_overlay_composition_add_rectangle(st->comp, r);
        gst_video_overlay_rectangle_unref(r);
    };
    for (const Pista& p : st->pistas) {
        sumar(marca(st, p.caja));
#if defined(BA_ROSTROS_TEXTO)
        const std::string t = textoDe(st, p);
        if (!t.empty())
            sumar(cartel(st, t, static_cast<int>(p.caja.x * st->anchoVideo), static_cast<int>(p.caja.y * st->altoVideo),
                         static_cast<int>(p.caja.alto * st->altoVideo)));
#endif
    }
}

// ------------------------------------------------------------------ el análisis

/// Cada cara nueva toma el número de la pista más cercana (si está cerca): así
/// una cara conserva su id mientras se mueve y la app le puede poner nombre.
void asignarIds(Estado* st, std::vector<Pista>& nuevas) {
    std::vector<bool> usada(st->pistas.size(), false);
    for (Pista& n : nuevas) {
        int mejor = -1;
        double dmin = 1e9;
        for (std::size_t i = 0; i < st->pistas.size(); ++i) {
            if (usada[i]) continue;
            const CajaN& v = st->pistas[i].caja;
            const double d = std::hypot(n.caja.cx() - v.cx(), n.caja.cy() - v.cy());
            const double tope = 0.6 * std::max({n.caja.ancho, n.caja.alto, v.ancho, v.alto});
            if (d < tope && d < dmin) {
                dmin = d;
                mejor = static_cast<int>(i);
            }
        }
        if (mejor >= 0) {
            usada[static_cast<size_t>(mejor)] = true;
            n.id = st->pistas[static_cast<size_t>(mejor)].id;
        } else {
            n.id = st->proximoId++;
        }
    }
    // Las etiquetas de las caras que ya no están se olvidan.
    for (auto it = st->etiquetas.begin(); it != st->etiquetas.end();) {
        bool sigue = false;
        for (const Pista& n : nuevas) sigue = sigue || n.id == it->first;
        it = sigue ? std::next(it) : st->etiquetas.erase(it);
    }
}

/// Todo lo de las caras como JSON (bajo mu). Con `vectores`, también el de cada una.
nlohmann::json comoJson(const Estado* st, bool vectores) {
    nlohmann::json caras = nlohmann::json::array();
    for (const Pista& p : st->pistas) {
        nlohmann::json j{{"id", p.id},
                         {"x", p.caja.x},
                         {"y", p.caja.y},
                         {"ancho", p.caja.ancho},
                         {"alto", p.caja.alto},
                         {"confianza", std::round(p.confianza * 1000) / 1000}};
        if (p.hayPuntos) j["puntos"] = std::vector<float>(p.puntos, p.puntos + 10);
        if (const auto e = st->etiquetas.find(p.id); e != st->etiquetas.end()) {
            j["nombre"] = e->second.nombre;
            if (!e->second.cargo.empty()) j["cargo"] = e->second.cargo;
        }
        if (vectores && !p.vector.empty()) j["vector"] = p.vector;
        caras.push_back(std::move(j));
    }
    nlohmann::json out{{"caras", caras},
                       {"cantidad", st->pistas.size()},
                       {"detector", st->detectorUsado},
                       {"ms", std::round(st->msAnalisis * 10) / 10},
                       {"video", {{"ancho", st->anchoVideo}, {"alto", st->altoVideo}}},
                       {"analisis", {{"ancho", st->anchoA}, {"alto", st->altoA}}}};
    if (GST_CLOCK_TIME_IS_VALID(st->ptsAnalisis)) out["pts"] = st->ptsAnalisis;
    if (!st->error.empty()) out["error"] = st->error;
    return out;
}

/// EL ANÁLISIS: un cuadro chico de la rama, en el hilo de su cola.
GstFlowReturn alMuestra(GstAppSink* sink, gpointer d) {
    auto* self = static_cast<GstElement*>(d);
    Estado* st = priv(self)->st;
    GstSample* s = gst_app_sink_pull_sample(sink);
    if (s == nullptr) return GST_FLOW_OK;
    bool activo, reconocer;
    double conf;
    std::string detector, modelos;
    {
        std::lock_guard<std::mutex> l(st->mu);
        activo = st->activo;
        reconocer = st->reconocer;
        conf = st->confianza;
        detector = st->detector;
        modelos = st->modelos.empty() ? gCarpetaModelos : st->modelos;
    }
    GstVideoInfo vi;
    GstBuffer* b = gst_sample_get_buffer(s);
    GstVideoFrame f;
    if (!activo || b == nullptr || !gst_video_info_from_caps(&vi, gst_sample_get_caps(s)) ||
        !gst_video_frame_map(&f, &vi, b, GST_MAP_READ)) {
        gst_sample_unref(s);
        return GST_FLOW_OK;
    }
    const GstClockTime pts = GST_BUFFER_PTS(b);
    const int W = GST_VIDEO_FRAME_WIDTH(&f), H = GST_VIDEO_FRAME_HEIGHT(&f);
    const int stride = GST_VIDEO_FRAME_PLANE_STRIDE(&f, 0);
    // Contiguo (lo que piden los modelos).
    std::vector<std::uint8_t> bgra(static_cast<size_t>(W) * H * 4);
    for (int y = 0; y < H; ++y)
        std::memcpy(bgra.data() + static_cast<size_t>(y) * W * 4,
                    static_cast<const std::uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(&f, 0)) + static_cast<size_t>(y) * stride,
                    static_cast<size_t>(W) * 4);
    gst_video_frame_unmap(&f);

    const auto t0 = std::chrono::steady_clock::now();
    std::vector<Pista> nuevas;
    std::string error;
    const bool conVision = !reconocer && detector != "yunet" && ba::rostros::hayVision();
    if (conVision) {
        std::vector<ba::rostros::CajaVision> v;
        if (ba::rostros::detectarConVision(bgra.data(), W, H, v)) {
            for (const auto& c : v) {
                if (c.confianza < conf) continue;
                Pista p;
                p.caja = {c.x, c.y, c.ancho, c.alto};
                p.confianza = c.confianza;
                nuevas.push_back(std::move(p));
            }
        } else {
            error = "Vision no pudo analizar el cuadro";
        }
    } else {
        if (!st->motorProbado) {
            st->motorProbado = true;
            st->motor = std::make_unique<ba::rostros::Motor>();
            if (!st->motor->abrir(modelos)) GST_WARNING_OBJECT(self, "rostros: %s", st->motor->error().c_str());
        }
        if (st->motor && st->motor->abierto()) {
            for (const ba::rostros::Cara& c : st->motor->detectar(bgra.data(), W, H, static_cast<float>(conf))) {
                Pista p;
                p.caja = {static_cast<double>(c.x) / W, static_cast<double>(c.y) / H, static_cast<double>(c.ancho) / W,
                          static_cast<double>(c.alto) / H};
                p.confianza = c.confianza;
                p.hayPuntos = true;
                for (int k = 0; k < 5; ++k) {
                    p.puntos[k * 2] = c.puntos[k * 2] / W;
                    p.puntos[k * 2 + 1] = c.puntos[k * 2 + 1] / H;
                }
                if (reconocer) p.vector = st->motor->describir(bgra.data(), W, H, c);
                nuevas.push_back(std::move(p));
            }
            error = st->motor->error();
        } else {
            error = st->motor ? st->motor->error() : "no se pudieron cargar los modelos de rostros";
        }
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::string js;
    {
        std::lock_guard<std::mutex> l(st->mu);
        asignarIds(st, nuevas);
        st->pistas = std::move(nuevas);
        st->ptsAnalisis = pts;
        st->vistoUs = ahoraUs();
        st->analisis = std::move(bgra);
        st->anchoA = W;
        st->altoA = H;
        st->msAnalisis = ms;
        st->detectorUsado = conVision ? "vision" : "yunet";
        st->error = error;
        st->nuevas = true;
        st->ultimoJson = comoJson(st, false).dump();
        js = comoJson(st, true).dump();
        rehacerComposicion(st);
    }
    GstStructure* e = gst_structure_new("ba-rostros", "json", G_TYPE_STRING, js.c_str(), "cuadro", GST_TYPE_SAMPLE, s, nullptr);
    gst_element_post_message(self, gst_message_new_element(GST_OBJECT(self), e));
    gst_sample_unref(s);
    return GST_FLOW_OK;
}

/// [{id, nombre, cargo}] -> las etiquetas (bajo mu).
void leerEtiquetas(Estado* st, const char* texto) {
    st->etiquetas.clear();
    const nlohmann::json j = nlohmann::json::parse(texto != nullptr ? texto : "", nullptr, false);
    if (j.is_array())
        for (const auto& e : j)
            if (e.is_object() && e.contains("id"))
                st->etiquetas[e.value("id", 0)] = {e.value("nombre", std::string()), e.value("cargo", std::string())};
    rehacerComposicion(st);
}

// ------------------------------------------------------------------ el camino principal

/// El tamaño de la rama del análisis, ENTERO (ancho y alto): dejándole el alto
/// a videoconvertscale, con algunas cámaras la cuenta de la proporción desbordaba
/// («integer overflow») y la entrada no negociaba.
void fijarTamAnalisis(GstElement* self, const GstVideoInfo& vi, int ancho) {
    const int W = GST_VIDEO_INFO_WIDTH(&vi), H = GST_VIDEO_INFO_HEIGHT(&vi);
    if (W <= 0 || H <= 0) return;
    int parN = GST_VIDEO_INFO_PAR_N(&vi), parD = GST_VIDEO_INFO_PAR_D(&vi);
    if (parN <= 0 || parD <= 0) parN = parD = 1;
    const int a = std::min(ancho, W);
    const int h = std::max(2, static_cast<int>(std::lround(static_cast<double>(a) * H * parD / (static_cast<double>(W) * parN))) & ~1);
    // Sin pixel-aspect-ratio: con la PAR rara de algunas cámaras, videoconvertscale
    // no podía calcular los bordes. Escala directo a este tamaño (add-borders=false).
    gchar* t = g_strdup_printf("video/x-raw,format=BGRA,width=%d,height=%d", a & ~1, h);
    GstCaps* c = gst_caps_from_string(t);
    g_object_set(priv(self)->tam, "caps", c, nullptr);
    gst_caps_unref(c);
    g_free(t);
}

/// La entrada al tee: el tamaño del video, el canal de control (nombres) y, en
/// GL, las marcas como meta (las dibuja gloverlaycompositor, antes de `sale`).
GstPadProbeReturn alEntrar(GstPad*, GstPadProbeInfo* info, gpointer d) {
    auto* self = static_cast<GstElement*>(d);
    Estado* st = priv(self)->st;
    if ((info->type & (GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM)) != 0) {
        GstEvent* e = GST_PAD_PROBE_INFO_EVENT(info);
        if (GST_EVENT_TYPE(e) == GST_EVENT_CAPS) {
            GstCaps* c = nullptr;
            gst_event_parse_caps(e, &c);
            GstVideoInfo vi;
            if (c != nullptr && gst_video_info_from_caps(&vi, c)) {
                int anchoA = 640;
                {
                    std::lock_guard<std::mutex> l(st->mu);
                    st->infoVideo = vi;
                    st->hayInfo = true;
                    st->anchoVideo = GST_VIDEO_INFO_WIDTH(&vi);
                    st->altoVideo = GST_VIDEO_INFO_HEIGHT(&vi);
                    anchoA = st->anchoAnalisis;
                    rehacerComposicion(st);
                }
                fijarTamAnalisis(self, vi, anchoA);
            }
        } else if (GST_EVENT_TYPE(e) == GST_EVENT_CUSTOM_DOWNSTREAM_OOB || GST_EVENT_TYPE(e) == GST_EVENT_CUSTOM_DOWNSTREAM) {
            const GstStructure* s = gst_event_get_structure(e);
            if (s != nullptr && gst_structure_has_name(s, "ba/control")) {
                if (const gchar* t = gst_structure_get_string(s, "rostros_etiquetas")) {
                    std::lock_guard<std::mutex> l(st->mu);
                    leerEtiquetas(st, t);
                }
            }
        }
        return GST_PAD_PROBE_OK;
    }
    if (BA_ROSTROS_GET_CLASS(self)->memoria != Memoria::Gl) return GST_PAD_PROBE_OK;
    GstVideoOverlayComposition* comp = nullptr;
    {
        std::lock_guard<std::mutex> l(st->mu);
        if (st->marcar && st->comp != nullptr) comp = gst_video_overlay_composition_ref(st->comp);
    }
    if (comp == nullptr) return GST_PAD_PROBE_OK;
    GstBuffer* b = gst_buffer_make_writable(GST_PAD_PROBE_INFO_BUFFER(info)); // comparte la memoria
    GST_PAD_PROBE_INFO_DATA(info) = b;
    gst_buffer_add_video_overlay_composition_meta(b, comp);
    gst_video_overlay_composition_unref(comp);
    return GST_PAD_PROBE_OK;
}

/// La salida del camino principal: el evento con lo nuevo, las metas de región
/// de cada cara y, en memoria de sistema, las marcas mezcladas en el cuadro.
GstPadProbeReturn alSalir(GstPad*, GstPadProbeInfo* info, gpointer d) {
    auto* self = static_cast<GstElement*>(d);
    Estado* st = priv(self)->st;
    GstStructure* evento = nullptr;
    GstVideoOverlayComposition* comp = nullptr;
    GstVideoInfo vi;
    struct Roi {
        int id, x, y, w, h;
        double confianza;
        std::string nombre, cargo;
    };
    std::vector<Roi> rois;
    std::int64_t edadMs = 0;
    {
        std::lock_guard<std::mutex> l(st->mu);
        if (st->nuevas) {
            st->nuevas = false;
            std::ostringstream o;
            for (std::size_t i = 0; i < st->pistas.size(); ++i) {
                const CajaN& c = st->pistas[i].caja;
                o << (i ? ";" : "") << decimal(c.x) << "," << decimal(c.y) << "," << decimal(c.ancho) << ","
                  << decimal(c.alto);
            }
            evento = gst_structure_new("ba/rostros", "caras", G_TYPE_STRING, o.str().c_str(), "json", G_TYPE_STRING,
                                       st->ultimoJson.c_str(), "cantidad", G_TYPE_INT, static_cast<int>(st->pistas.size()),
                                       "pts", G_TYPE_UINT64, static_cast<guint64>(st->ptsAnalisis), "ms", G_TYPE_DOUBLE,
                                       st->msAnalisis, "detector", G_TYPE_STRING, st->detectorUsado.c_str(), nullptr);
        }
        if (st->anchoVideo > 0)
            for (const Pista& p : st->pistas) {
                const auto e = st->etiquetas.find(p.id);
                rois.push_back({p.id, static_cast<int>(p.caja.x * st->anchoVideo), static_cast<int>(p.caja.y * st->altoVideo),
                                static_cast<int>(p.caja.ancho * st->anchoVideo), static_cast<int>(p.caja.alto * st->altoVideo),
                                p.confianza, e != st->etiquetas.end() ? e->second.nombre : std::string(),
                                e != st->etiquetas.end() ? e->second.cargo : std::string()});
            }
        edadMs = st->vistoUs > 0 ? (ahoraUs() - st->vistoUs) / 1000 : 0;
        if (BA_ROSTROS_GET_CLASS(self)->memoria == Memoria::Sistema && st->marcar && st->comp != nullptr && st->hayInfo) {
            comp = gst_video_overlay_composition_ref(st->comp);
            vi = st->infoVideo;
        }
    }
    if (evento != nullptr) {
        GstPad* src = gst_element_get_static_pad(priv(self)->sale, "src");
        gst_pad_push_event(src, gst_event_new_custom(GST_EVENT_CUSTOM_DOWNSTREAM, evento));
        gst_object_unref(src);
    }
    if (rois.empty() && comp == nullptr) return GST_PAD_PROBE_OK;
    GstBuffer* b = gst_buffer_make_writable(GST_PAD_PROBE_INFO_BUFFER(info));
    GST_PAD_PROBE_INFO_DATA(info) = b;
    for (const Roi& r : rois) {
        GstVideoRegionOfInterestMeta* m = gst_buffer_add_video_region_of_interest_meta(
            b, "face", static_cast<guint>(std::max(0, r.x)), static_cast<guint>(std::max(0, r.y)),
            static_cast<guint>(std::max(1, r.w)), static_cast<guint>(std::max(1, r.h)));
        m->id = r.id;
        gst_video_region_of_interest_meta_add_param(
            m, gst_structure_new("ba-rostro", "id", G_TYPE_INT, r.id, "confianza", G_TYPE_DOUBLE, r.confianza, "nombre",
                                 G_TYPE_STRING, r.nombre.c_str(), "cargo", G_TYPE_STRING, r.cargo.c_str(), "edad-ms",
                                 G_TYPE_INT64, static_cast<gint64>(edadMs), nullptr));
    }
    if (comp != nullptr) {
        GstVideoFrame f;
        if (gst_video_frame_map(&f, &vi, b, GST_MAP_READWRITE)) {
            gst_video_overlay_composition_blend(comp, &f);
            gst_video_frame_unmap(&f);
        }
        gst_video_overlay_composition_unref(comp);
    }
    return GST_PAD_PROBE_OK;
}

/// En GL, gloverlaycompositor no dibuja si lo de abajo dice que sabe mostrar la
/// meta de overlay (la deja pasar para que la dibuje otro): gldownload lo dice, y
/// el resto de la cadena del motor no la dibuja. Se le saca esa respuesta.
GstPadProbeReturn sinMetaOverlay(GstPad*, GstPadProbeInfo* info, gpointer) {
    GstQuery* q = GST_PAD_PROBE_INFO_QUERY(info);
    if (GST_QUERY_TYPE(q) == GST_QUERY_ALLOCATION) {
        guint i = 0;
        while (gst_query_find_allocation_meta(q, GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE, &i))
            gst_query_remove_nth_allocation_meta(q, i);
    } else if (GST_QUERY_TYPE(q) == GST_QUERY_CAPS) {
        // Ni la característica de caps que la anuncia.
        GstCaps* c = nullptr;
        gst_query_parse_caps_result(q, &c);
        if (c != nullptr && !gst_caps_is_any(c)) {
            GstCaps* limpio = gst_caps_new_empty();
            for (guint k = 0; k < gst_caps_get_size(c); ++k) {
                GstCapsFeatures* f = gst_caps_get_features(c, k);
                if (f != nullptr && gst_caps_features_contains(f, GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION)) continue;
                gst_caps_append_structure_full(limpio, gst_structure_copy(gst_caps_get_structure(c, k)),
                                               f != nullptr ? gst_caps_features_copy(f) : nullptr);
            }
            gst_query_set_caps_result(q, limpio);
            gst_caps_unref(limpio);
        }
    }
    return GST_PAD_PROBE_OK;
}

/// Y a la entrada del compositor, las caps con la meta anunciada: si entran
/// iguales a como salen, gloverlaycompositor queda pasante y no dibuja nada.
GstPadProbeReturn conMetaOverlay(GstPad*, GstPadProbeInfo* info, gpointer) {
    GstEvent* e = GST_PAD_PROBE_INFO_EVENT(info);
    if (GST_EVENT_TYPE(e) != GST_EVENT_CAPS) return GST_PAD_PROBE_OK;
    GstCaps* c = nullptr;
    gst_event_parse_caps(e, &c);
    if (c == nullptr || gst_caps_get_size(c) == 0) return GST_PAD_PROBE_OK;
    GstCapsFeatures* f = gst_caps_get_features(c, 0);
    if (f != nullptr && gst_caps_features_contains(f, GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION))
        return GST_PAD_PROBE_OK;
    GstCaps* nueva = gst_caps_copy(c);
    GstCapsFeatures* nf = gst_caps_features_copy(gst_caps_get_features(nueva, 0));
    gst_caps_features_add(nf, GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION);
    gst_caps_set_features(nueva, 0, nf);
    GST_PAD_PROBE_INFO_DATA(info) = gst_event_new_caps(nueva);
    gst_caps_unref(nueva);
    gst_event_unref(e);
    return GST_PAD_PROBE_OK;
}

void armar(BaRostros* self) {
    BaRostrosPrivate* p = priv(self);
    const Memoria mem = BA_ROSTROS_GET_CLASS(self)->memoria;
    GstElement* tee = gst_element_factory_make("tee", "t");
    p->sale = gst_element_factory_make("identity", "sale");
    GstElement* cola = gst_element_factory_make("queue", "cola");
    p->tasa = gst_element_factory_make("videorate", "tasa");
    GstElement* bajar = mem == Memoria::Gl      ? gst_element_factory_make("gldownload", "bajar")
                        : mem == Memoria::D3d12 ? gst_element_factory_make("d3d12download", "bajar")
                                                : nullptr;
    GstElement* conv = gst_element_factory_make("videoconvertscale", "conv");
    p->tam = gst_element_factory_make("capsfilter", "tam");
    GstElement* ver = gst_element_factory_make("appsink", "ver");
    GstElement* dibujar = mem == Memoria::Gl ? gst_element_factory_make("gloverlaycompositor", "dibujar") : nullptr;
    if (tee == nullptr || p->sale == nullptr || cola == nullptr || p->tasa == nullptr || conv == nullptr ||
        p->tam == nullptr || ver == nullptr || (mem != Memoria::Sistema && bajar == nullptr)) {
        GST_ERROR_OBJECT(self, "faltan elementos de GStreamer para barostros");
        return;
    }
    g_object_set(tee, "allow-not-linked", TRUE, nullptr);
    g_object_set(cola, "leaky", 2, "max-size-buffers", 1u, "max-size-time", static_cast<guint64>(0), "max-size-bytes", 0u,
                 nullptr);
    g_object_set(p->tasa, "drop-only", TRUE, "max-rate", 5, nullptr);
    GstCaps* c = gst_caps_from_string("video/x-raw,format=BGRA");
    g_object_set(p->tam, "caps", c, nullptr);
    g_object_set(conv, "add-borders", FALSE, nullptr);
    gst_caps_unref(c);
    g_object_set(ver, "sync", FALSE, "async", FALSE, "max-buffers", 1u, "drop", TRUE, "emit-signals", FALSE, nullptr);
    GstAppSinkCallbacks cb{};
    cb.new_sample = alMuestra;
    gst_app_sink_set_callbacks(GST_APP_SINK(ver), &cb, self, nullptr);

    gst_bin_add_many(GST_BIN(self), tee, p->sale, cola, p->tasa, conv, p->tam, ver, nullptr);
    if (bajar != nullptr) gst_bin_add(GST_BIN(self), bajar);
    if (dibujar != nullptr) gst_bin_add(GST_BIN(self), dibujar);
    // El camino principal.
    if (dibujar != nullptr)
        gst_element_link_many(tee, dibujar, p->sale, nullptr);
    else
        gst_element_link(tee, p->sale);
    // La rama del análisis.
    if (bajar != nullptr)
        gst_element_link_many(tee, cola, p->tasa, bajar, conv, p->tam, ver, nullptr);
    else
        gst_element_link_many(tee, cola, p->tasa, conv, p->tam, ver, nullptr);

    GstPad* in = gst_element_get_static_pad(tee, "sink");
    GstPad* salePad = gst_element_get_static_pad(p->sale, "sink");
    GstPad* out = gst_element_get_static_pad(p->sale, "src");
    gst_pad_add_probe(in, static_cast<GstPadProbeType>(GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM),
                      alEntrar, self, nullptr);
    gst_pad_add_probe(salePad, GST_PAD_PROBE_TYPE_BUFFER, alSalir, self, nullptr);
    if (dibujar != nullptr) {
        GstPad* dp = gst_element_get_static_pad(dibujar, "sink");
        gst_pad_add_probe(dp, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, conMetaOverlay, nullptr, nullptr);
        gst_object_unref(dp);
    }
    if (dibujar != nullptr)
        gst_pad_add_probe(salePad, static_cast<GstPadProbeType>(GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM | GST_PAD_PROBE_TYPE_PULL),
                          sinMetaOverlay, nullptr, nullptr);
    GstElementClass* ek = GST_ELEMENT_GET_CLASS(self);
    gst_element_add_pad(GST_ELEMENT(self), gst_ghost_pad_new_from_template("sink", in, gst_element_class_get_pad_template(ek, "sink")));
    gst_element_add_pad(GST_ELEMENT(self), gst_ghost_pad_new_from_template("src", out, gst_element_class_get_pad_template(ek, "src")));
    gst_object_unref(in);
    gst_object_unref(salePad);
    gst_object_unref(out);
}

std::string textoOr(const GValue* v, const char* porDefecto) {
    const gchar* t = g_value_get_string(v);
    return t != nullptr ? t : porDefecto;
}

void ponerPropiedad(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    BaRostrosPrivate* p = priv(o);
    Estado* st = p->st;
    int fps = -1, ancho = -1;
    {
        std::lock_guard<std::mutex> l(st->mu);
        switch (id) {
            case PROP_ACTIVO: st->activo = g_value_get_boolean(v); break;
            case PROP_FPS:
                st->fps = std::clamp(g_value_get_double(v), 0.5, 30.0);
                fps = static_cast<int>(std::lround(std::max(1.0, st->fps)));
                break;
            case PROP_RECONOCER: st->reconocer = g_value_get_boolean(v); break;
            case PROP_CONFIANZA: st->confianza = std::clamp(g_value_get_double(v), 0.1, 1.0); break;
            case PROP_DETECTOR: st->detector = textoOr(v, "auto"); break;
            case PROP_MODELOS: st->modelos = textoOr(v, ""); break;
            case PROP_ANCHO:
                st->anchoAnalisis = std::clamp(g_value_get_int(v), 160, 1920);
                ancho = st->anchoAnalisis;
                break;
            case PROP_MARCAR: st->marcar = g_value_get_boolean(v); break;
            case PROP_ESTILO: st->estilo = textoOr(v, "recuadro"); break;
            case PROP_COLOR: st->color = g_value_get_uint(v); break;
            case PROP_GROSOR: st->grosor = std::clamp(g_value_get_int(v), 1, 40); break;
            case PROP_TEXTO: st->texto = textoOr(v, ""); break;
            case PROP_TEXTO_POSICION: st->textoPosicion = textoOr(v, "arriba"); break;
            case PROP_TEXTO_TAMANO: st->textoTamano = std::clamp(g_value_get_double(v), 0.01, 0.2); break;
            case PROP_ETIQUETAS: leerEtiquetas(st, g_value_get_string(v)); break;
            default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps); break;
        }
        if (id >= PROP_MARCAR && id <= PROP_TEXTO_TAMANO) rehacerComposicion(st);
    }
    if (fps > 0 && p->tasa != nullptr) g_object_set(p->tasa, "max-rate", fps, nullptr);
    if (ancho > 0 && p->tam != nullptr) {
        GstVideoInfo vi;
        bool hay = false;
        {
            std::lock_guard<std::mutex> l(st->mu);
            vi = st->infoVideo;
            hay = st->hayInfo;
        }
        if (hay) fijarTamAnalisis(GST_ELEMENT(o), vi, ancho); // si no, al llegar los caps
    }
}

void leerPropiedad(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    Estado* st = priv(o)->st;
    std::lock_guard<std::mutex> l(st->mu);
    switch (id) {
        case PROP_ACTIVO: g_value_set_boolean(v, st->activo); break;
        case PROP_FPS: g_value_set_double(v, st->fps); break;
        case PROP_RECONOCER: g_value_set_boolean(v, st->reconocer); break;
        case PROP_CONFIANZA: g_value_set_double(v, st->confianza); break;
        case PROP_DETECTOR: g_value_set_string(v, st->detector.c_str()); break;
        case PROP_MODELOS: g_value_set_string(v, st->modelos.c_str()); break;
        case PROP_ANCHO: g_value_set_int(v, st->anchoAnalisis); break;
        case PROP_MARCAR: g_value_set_boolean(v, st->marcar); break;
        case PROP_ESTILO: g_value_set_string(v, st->estilo.c_str()); break;
        case PROP_COLOR: g_value_set_uint(v, st->color); break;
        case PROP_GROSOR: g_value_set_int(v, st->grosor); break;
        case PROP_TEXTO: g_value_set_string(v, st->texto.c_str()); break;
        case PROP_TEXTO_POSICION: g_value_set_string(v, st->textoPosicion.c_str()); break;
        case PROP_TEXTO_TAMANO: g_value_set_double(v, st->textoTamano); break;
        case PROP_ETIQUETAS: {
            nlohmann::json a = nlohmann::json::array();
            for (const auto& [i, e] : st->etiquetas) a.push_back({{"id", i}, {"nombre", e.nombre}, {"cargo", e.cargo}});
            g_value_set_string(v, a.dump().c_str());
            break;
        }
        case PROP_CARAS: g_value_set_string(v, st->ultimoJson.c_str()); break;
        case PROP_ERROR: g_value_set_string(v, st->error.c_str()); break;
        case PROP_MS: g_value_set_double(v, st->msAnalisis); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps); break;
    }
}

} // namespace

static void ba_rostros_init(BaRostros* self) { priv(self)->st = new Estado(); }

static void ba_rostros_finalize(GObject* o) {
    Estado* st = priv(o)->st;
    if (st != nullptr) {
        if (st->comp != nullptr) gst_video_overlay_composition_unref(st->comp);
        delete st;
    }
    G_OBJECT_CLASS(ba_rostros_parent_class)->finalize(o);
}

static void ba_rostros_class_init(BaRostrosClass* k) {
    auto* go = G_OBJECT_CLASS(k);
    go->set_property = ponerPropiedad;
    go->get_property = leerPropiedad;
    go->finalize = ba_rostros_finalize;
    const auto rw = static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING);
    const auto ro = static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property(go, PROP_ACTIVO, g_param_spec_boolean("activo", "Activo", "Analizar (si no, la imagen solo pasa)", TRUE, rw));
    g_object_class_install_property(go, PROP_FPS, g_param_spec_double("fps", "Análisis por segundo", "Cuántos cuadros por segundo se analizan", 0.5, 30, 5, rw));
    g_object_class_install_property(go, PROP_RECONOCER, g_param_spec_boolean("reconocer", "Reconocer", "Describir cada cara (el vector para la libreta de Personas)", FALSE, rw));
    g_object_class_install_property(go, PROP_CONFIANZA, g_param_spec_double("confianza", "Confianza mínima", "Descarta lo dudoso (0,1..1)", 0.1, 1, 0.7, rw));
    g_object_class_install_property(go, PROP_DETECTOR, g_param_spec_string("detector", "Detector", "auto (Vision en la Mac si no reconoce), vision o yunet", "auto", rw));
    g_object_class_install_property(go, PROP_MODELOS, g_param_spec_string("modelos", "Modelos", "Carpeta de yunet.onnx y sface.onnx (vacío = la del bundle)", "", rw));
    g_object_class_install_property(go, PROP_ANCHO, g_param_spec_int("ancho-analisis", "Ancho del análisis", "A qué ancho se achica el cuadro que se analiza", 160, 1920, 640, rw));
    g_object_class_install_property(go, PROP_MARCAR, g_param_spec_boolean("marcar", "Overlay", "Dibujar sobre cada cara", FALSE, rw));
    g_object_class_install_property(go, PROP_ESTILO, g_param_spec_string("estilo", "Estilo", "recuadro, esquinas, relleno, tapar o pixelar", "recuadro", rw));
    g_object_class_install_property(go, PROP_COLOR, g_param_spec_uint("color", "Color", "ARGB de la marca y del texto", 0, G_MAXUINT32, 0xFFFFC83C, rw));
    g_object_class_install_property(go, PROP_GROSOR, g_param_spec_int("grosor", "Grosor", "Del borde, en píxeles del video", 1, 40, 4, rw));
    g_object_class_install_property(go, PROP_TEXTO, g_param_spec_string("texto", "Texto", "Plantilla: {nombre} {cargo} {id} {confianza} {n} (vacío = sin texto)", "", rw));
    g_object_class_install_property(go, PROP_TEXTO_POSICION, g_param_spec_string("texto-posicion", "Posición del texto", "arriba o abajo de la cara", "arriba", rw));
    g_object_class_install_property(go, PROP_TEXTO_TAMANO, g_param_spec_double("texto-tamano", "Tamaño del texto", "Fracción del alto del video", 0.01, 0.2, 0.035, rw));
    g_object_class_install_property(go, PROP_ETIQUETAS, g_param_spec_string("etiquetas", "Etiquetas", "JSON [{id, nombre, cargo}]: quién es cada cara", "[]", rw));
    g_object_class_install_property(go, PROP_CARAS, g_param_spec_string("caras", "Caras", "Lo último que vio (JSON)", "", ro));
    g_object_class_install_property(go, PROP_ERROR, g_param_spec_string("error", "Error", "Lo último que salió mal", "", ro));
    g_object_class_install_property(go, PROP_MS, g_param_spec_double("ms", "Milisegundos", "Lo que tardó el último análisis", 0, 1e6, 0, ro));
}

// Las tres fábricas: la misma clase con otra memoria y otras plantillas.
// Se arma en el init de la VARIANTE: durante el de la clase base, GObject
// todavía muestra la clase base (sin la memoria ni las plantillas de la variante).
#define BA_ROSTROS_VARIANTE(nombre, Nombre, mem, caps_texto, descripcion)                                         \
    G_BEGIN_DECLS                                                                                                 \
    G_DECLARE_FINAL_TYPE(Nombre, nombre, BA, Nombre, BaRostros)                                                    \
    G_END_DECLS                                                                                                   \
    struct _##Nombre {                                                                                            \
        BaRostros parent;                                                                                         \
    };                                                                                                            \
    G_DEFINE_TYPE(Nombre, nombre, BA_TYPE_ROSTROS)                                                                \
    static void nombre##_init(Nombre* self) { armar(BA_ROSTROS(self)); }                                         \
    static void nombre##_class_init(Nombre##Class* k) {                                                           \
        BA_ROSTROS_CLASS(k)->memoria = mem;                                                                       \
        auto* ek = GST_ELEMENT_CLASS(k);                                                                          \
        gst_element_class_set_static_metadata(ek, descripcion, "Filter/Analyzer/Video",                           \
                                              "Encuentra, sigue y reconoce caras sin demorar la imagen",          \
                                              "Broadcast Appliance");                                             \
        GstCaps* caps = gst_caps_from_string(caps_texto);                                                         \
        gst_element_class_add_pad_template(ek, gst_pad_template_new("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps)); \
        gst_element_class_add_pad_template(ek, gst_pad_template_new("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps));   \
        gst_caps_unref(caps);                                                                                     \
    }

BA_ROSTROS_VARIANTE(ba_rostros_sis, BaRostrosSis, Memoria::Sistema, "video/x-raw", "Rostros")
BA_ROSTROS_VARIANTE(ba_rostros_gl, BaRostrosGl, Memoria::Gl, "video/x-raw(memory:GLMemory)", "Rostros (GPU)")
BA_ROSTROS_VARIANTE(ba_rostros_d3d, BaRostrosD3d, Memoria::D3d12, "video/x-raw(memory:D3D12Memory)", "Rostros (D3D12)")

extern "C" gboolean ba_registrar_rostros(GstPlugin* p) {
    // Los modelos van en el bundle al lado de los plugins: <bundle>/data/rostros.
    if (const gchar* env = g_getenv("BA_ROSTROS_MODELOS"); env != nullptr && *env != '\0') {
        gCarpetaModelos = env;
    } else if (const gchar* f = gst_plugin_get_filename(p); f != nullptr) {
        gchar* dir = g_path_get_dirname(f);
        gchar* m = g_build_filename(dir, "..", "data", "rostros", nullptr);
        gCarpetaModelos = m;
        g_free(m);
        g_free(dir);
    }
    bool ok = gst_element_register(p, "barostros", GST_RANK_NONE, ba_rostros_sis_get_type()) &&
              gst_element_register(p, "barostrosgl", GST_RANK_NONE, ba_rostros_gl_get_type());
#if defined(_WIN32)
    ok = ok && gst_element_register(p, "barostrosd3d12", GST_RANK_NONE, ba_rostros_d3d_get_type());
#endif
    return ok;
}
