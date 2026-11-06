// ============================================================================
// src/gst-plugins/ipc/videocomun.cpp — bavideoanillocomun: bavideoanillo SIN hilo propio
//
// Lo mismo que bavideoanillo (video.cpp: el video de un canal compartido, BGRA o NV12,
// a su tamaño real, con la hora que le puso el gateway, 1 cuadro de latencia), pero lo
// lee el lector común del video (comun.hpp): un hilo para todos los canales, una vuelta
// cada 1/60 s, en vez de un hilo por canal despertado por su timbre. Lo usa el motor para
// los canales de las entradas (Entrada.cpp: la cámara y la pantalla de cada lugar, el
// remoto, la grilla).
//
// En cada vuelta empuja TODOS los cuadros nuevos del canal, en orden (receiveSiguiente: los
// que el canal todavía guarda; los de los invitados tienen 6 buffers), cada uno con la hora
// de su CONTENIDO si el gateway la puso (FrameMeta::contenidoQpc: la de libwebrtc, la
// captura del invitado más la espera de red, sincronizada con su audio) y si no con la de
// publicación. Así la ráfaga que llega después de un cuadro clave atrasado no se tira: cada
// cuadro tiene su hora pareja y la latencia del compositor la absorbe
// (docs/local/hora-de-captura.md). `perdidos`: los que el canal ya no tenía al leer.
//
// `cps-max` (0 = todos): como mucho tantos cuadros por segundo. Una entrada que nadie
// mira pasa 2 por segundo (Entrada::ponerEnUso); con esto los otros ni se copian.
//
//   gst-launch-1.0 bavideoanillocomun canal=BA.Guest.1 ! videoconvert ! autovideosink
// ============================================================================

#include "comun.hpp"
#include "hora.hpp"
#include "sinhilo.hpp"

#include "ipc/SpoutTransport.hpp"

#include <gst/gst.h>
#include <gst/video/video.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <string>

namespace {

GstCaps* capsDe(int w, int h, ba::ipc::FormatoCanal f) {
    return gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, f == ba::ipc::FormatoCanal::NV12 ? "NV12" : "BGRA", "width",
                               G_TYPE_INT, w, "height", G_TYPE_INT, h, "framerate", GST_TYPE_FRACTION, 0, 1,
                               // sin el píxel cuadrado, d3d12upload fija 1/2147483647 y no negocia
                               "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1, nullptr);
}

class LecturaVideo : public ba::ipc_gst::Lectura {
   public:
    GstElement* e = nullptr;
    ba::ipc_gst::SalidaSinHilo salida;
    std::string canal; ///< bajo mu (la propiedad)
    std::atomic<int> cpsMax{0};
    ba::ipc::SpoutReceiver rx;
    bool suscripto = false;
    std::string suscriptoCanal;
    GstCaps* caps = nullptr; ///< las del último cuadro
    int ancho = 0, alto = 0;
    ba::ipc::FormatoCanal formato = ba::ipc::FormatoCanal::BGRA;
    std::chrono::steady_clock::time_point reintento{}, ultimo{};
    GstClockTime ultimoPts = GST_CLOCK_TIME_NONE; ///< la hora del último empujado (siempre hacia adelante)

    /// La hora del contenido, PAREJA. La de libwebrtc trae el temblor con que el invitado
    /// capturó (su temporizador: ±3-6 ms) y el compositor toma un cuadro por tic: con las horas
    /// cerca del borde de un tic, ese temblor hace que dos caigan en el mismo (uno no se ve y el
    /// siguiente tic repite: un salto de 67 ms, ~1 por segundo medido). Acá cada hora se predice
    /// con la anterior más el período (medido) y se corrige 1/16 de lo que se aparta: el
    /// temblor baja a una fracción y lo que queda es la deriva lenta entre los dos relojes (un
    /// salto cada mucho). Un apartamiento grande (un hueco de verdad, otra capa, otro ritmo)
    /// vuelve a tomar la hora tal cual. Es aritmética: nada espera ni se retiene.
    struct Parejo {
        GstClockTime hora = GST_CLOCK_TIME_NONE; ///< la última suavizada
        GstClockTime cruda = GST_CLOCK_TIME_NONE; ///< la última sin suavizar
        double periodo = 0;                      ///< ns (0 = todavía no)
        void olvidar() { hora = cruda = GST_CLOCK_TIME_NONE, periodo = 0; }
        GstClockTime suavizar(GstClockTime h) {
            if (!GST_CLOCK_TIME_IS_VALID(hora) || !GST_CLOCK_TIME_IS_VALID(cruda) || h <= cruda) {
                const bool reinicio = !GST_CLOCK_TIME_IS_VALID(cruda) || h <= cruda;
                if (reinicio) periodo = 0;
                cruda = hora = h;
                return h;
            }
            const double d = static_cast<double>(h - cruda);
            cruda = h;
            // El período: lo que se mueve entre cuadros (de 8 a 200 ms: 5 a 120 cps).
            if (d >= 8e6 && d <= 200e6) periodo = periodo == 0 ? d : periodo + (d - periodo) / 32;
            if (periodo == 0) return hora = h;
            double prevista = static_cast<double>(hora) + periodo;
            double aparta = static_cast<double>(h) - prevista;
            // Uno o más cuadros que no vinieron (el invitado los salteó): la predicción salta
            // esos períodos. Más de 3, o para atrás más de medio: otra cosa (un salto de hora,
            // otro ritmo): se toma tal cual.
            const double k = std::round(aparta / periodo);
            if (k < 0 || k > 3) return hora = h;
            prevista += k * periodo;
            aparta -= k * periodo;
            const double nueva = prevista + aparta / 16;
            hora = static_cast<GstClockTime>(nueva);
            return hora;
        }
    } parejo;
    std::atomic<std::uint64_t> perdidos{0};       ///< publicados que ya no estaban al leer (la propiedad)

    ~LecturaVideo() override {
        salida.reiniciar();
        if (caps != nullptr) gst_caps_unref(caps);
    }

    void cerrar() {
        rx = ba::ipc::SpoutReceiver{};
        suscripto = false;
        ancho = alto = 0;
        formato = ba::ipc::FormatoCanal::BGRA;
        ultimoPts = GST_CLOCK_TIME_NONE;
        parejo.olvidar();
    }

    void leer() override {
        if (e == nullptr || !ba::ipc_gst::corriendo(e)) return; // lo están bajando: ver comun.hpp
        // Otro canal o el gateway lo rehízo: resuscribirse (si no, queda congelado).
        if (suscripto && (canal != suscriptoCanal || rx.stale())) cerrar();
        const auto ahora = std::chrono::steady_clock::now();
        if (!suscripto) {
            if (canal.empty() || ahora < reintento) return;
            const std::wstring w(canal.begin(), canal.end());
            if (!rx.subscribe(w.c_str())) {
                rx = ba::ipc::SpoutReceiver{};
                reintento = ahora + std::chrono::milliseconds(250);
                return;
            }
            suscripto = true;
            suscriptoCanal = canal;
        }
        if (!rx.hasData()) return;
        // Pocos cuadros (nadie la mira): el que llega antes de tiempo ni se copia, y del
        // resto solo el último.
        const int c = cpsMax.load();
        if (c > 0 && ahora - ultimo < std::chrono::microseconds(1'000'000 / c)) return;
        ultimo = ahora;
        // Los dos relojes, una vez por vuelta.
        const ba::ipc_gst::TraductorQpc t(e);
        if (c > 0) {
            uno(false, t);
            return;
        }
        // Todos los nuevos, en orden (ningún cuadro tirado: tras un cuadro clave que llegó
        // tarde vienen 3-4 juntos), cada uno con la hora de su contenido. Como mucho los que
        // guarda el canal (el resto ya no está).
        for (int k = 0; k < ba::ipc::kMaxBuffersCanal && uno(true, t); ++k) {
        }
    }

    /// Lee un cuadro (`enOrden`: el más viejo que no se leyó; si no, el último) y lo empuja.
    /// false si no había.
    bool uno(bool enOrden, const ba::ipc_gst::TraductorQpc& t) {
        const int w = rx.width(), h = rx.height();
        if (w <= 0 || h <= 0) return false;
        const ba::ipc::FormatoCanal fmt = rx.formato();
        GstBuffer* b = gst_buffer_new_allocate(nullptr, ba::ipc::bytesDeCuadro(w, h, fmt), nullptr);
        GstMapInfo map;
        gst_buffer_map(b, &map, GST_MAP_WRITE);
        ba::ipc::FrameMeta meta{};
        std::uint64_t perdidosAhora = 0;
        const bool hay = enOrden ? rx.receiveSiguiente(map.data, map.size, meta, &perdidosAhora) : rx.receive(map.data, map.size, meta);
        gst_buffer_unmap(b, &map);
        if (perdidosAhora != 0) perdidos.fetch_add(perdidosAhora, std::memory_order_relaxed);
        if (!hay) {
            gst_buffer_unref(b);
            return false;
        }
        // Su tamaño real (más chico que el canal; solo en BGRA): solo esos bytes, con las filas juntas.
        const int cw = meta.ancho != 0 ? static_cast<int>(meta.ancho) : w;
        const int ch = meta.alto != 0 ? static_cast<int>(meta.alto) : h;
        if (cw != w || ch != h) gst_buffer_resize(b, 0, static_cast<gssize>(cw) * ch * 4);
        if (caps == nullptr || cw != ancho || ch != alto || fmt != formato) {
            if (caps != nullptr) gst_caps_unref(caps);
            caps = capsDe(cw, ch, fmt);
            ancho = cw;
            alto = ch;
            formato = fmt;
        }
        // La hora, en el reloj del pipeline: la de su CONTENIDO si el gateway la sabe (un
        // invitado: la que le dio libwebrtc, su captura más la espera de red y sincronizada
        // con su audio; puede ser unos ms del futuro), si no la de publicación. Así una ráfaga
        // que llegó junta sale con sus horas parejas y la latencia del compositor la absorbe.
        GstClockTime pts = t.hora(meta.contenidoQpc, GST_SECOND);
        if (GST_CLOCK_TIME_IS_VALID(pts))
            pts = parejo.suavizar(pts);
        else
            parejo.olvidar();
        if (!GST_CLOCK_TIME_IS_VALID(pts)) pts = t.hora(meta.qpc);
        if (!GST_CLOCK_TIME_IS_VALID(pts)) pts = t.ahora(); // sin sello creíble: la de ahora (como horaDeQpc)
        // Siempre hacia adelante (si la hora de libwebrtc vuelve atrás, p. ej. al rehacer su
        // estimación, el cuadro va pegado al anterior: un PTS que retrocede confunde al
        // videorate y al compositor).
        if (GST_CLOCK_TIME_IS_VALID(pts) && GST_CLOCK_TIME_IS_VALID(ultimoPts) && pts <= ultimoPts) pts = ultimoPts + 1;
        ultimoPts = pts;
        GST_BUFFER_PTS(b) = pts;
        GST_BUFFER_DURATION(b) = GST_CLOCK_TIME_NONE;
        // El número de publicación del cuadro en el canal: quien mide ve los que el emisor
        // publicó y no se leyeron (motor/MedidorCeldas: la grilla de personas).
        GST_BUFFER_OFFSET(b) = rx.ultimoPublicado();
        salida.empujar(e, b, caps);
        return true;
    }
};

} // namespace

G_BEGIN_DECLS
#define BA_TYPE_VIDEO_ANILLO_COMUN (ba_video_anillo_comun_get_type())
G_DECLARE_FINAL_TYPE(BaVideoAnilloComun, ba_video_anillo_comun, BA, VIDEO_ANILLO_COMUN, GstElement)
G_END_DECLS

struct _BaVideoAnilloComun {
    GstElement parent;
    std::shared_ptr<LecturaVideo>* l;
};

G_DEFINE_TYPE(BaVideoAnilloComun, ba_video_anillo_comun, GST_TYPE_ELEMENT)

enum { PROP_0, PROP_CANAL, PROP_CPS_MAX, PROP_VUELTAS, PROP_PERDIDOS };

static GstStaticPadTemplate srcTpl = GST_STATIC_PAD_TEMPLATE(
    "src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS("video/x-raw, format=(string){ BGRA, NV12 }, width=(int)[1, MAX], height=(int)[1, MAX], framerate=(fraction)0/1"));

static gboolean consultar(GstPad* pad, GstObject* padre, GstQuery* q) {
    return ba::ipc_gst::consultaSinHilo(pad, padre, q, GST_SECOND / 30, GST_SECOND / 5);
}

static GstStateChangeReturn cambiar(GstElement* e, GstStateChange t) {
    auto& l = *BA_VIDEO_ANILLO_COMUN(e)->l;
    if (t == GST_STATE_CHANGE_PLAYING_TO_PAUSED) ba::ipc_gst::quitar(ba::ipc_gst::Medio::Video, l);
    GstStateChangeReturn r = GST_ELEMENT_CLASS(ba_video_anillo_comun_parent_class)->change_state(e, t);
    if (r == GST_STATE_CHANGE_FAILURE) return r;
    switch (t) {
        case GST_STATE_CHANGE_READY_TO_PAUSED: {
            std::lock_guard<std::mutex> g(l->mu);
            l->salida.reiniciar();
            l->ultimoPts = GST_CLOCK_TIME_NONE; // un segmento nuevo
            l->parejo.olvidar();
            r = GST_STATE_CHANGE_NO_PREROLL; // en vivo
            break;
        }
        case GST_STATE_CHANGE_PAUSED_TO_PLAYING: ba::ipc_gst::registrar(ba::ipc_gst::Medio::Video, l); break;
        case GST_STATE_CHANGE_PLAYING_TO_PAUSED: r = GST_STATE_CHANGE_NO_PREROLL; break;
        case GST_STATE_CHANGE_PAUSED_TO_READY: {
            std::lock_guard<std::mutex> g(l->mu);
            l->cerrar();
            l->salida.reiniciar();
            break;
        }
        default: break;
    }
    return r;
}

static void ponerPropiedad(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    auto& l = *BA_VIDEO_ANILLO_COMUN(o)->l;
    switch (id) {
        case PROP_CANAL: {
            const gchar* x = g_value_get_string(v);
            std::lock_guard<std::mutex> g(l->mu);
            l->canal = x != nullptr ? x : "";
            break;
        }
        case PROP_CPS_MAX: l->cpsMax = g_value_get_int(v); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static void leerPropiedad(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    auto& l = *BA_VIDEO_ANILLO_COMUN(o)->l;
    switch (id) {
        case PROP_CANAL: {
            std::lock_guard<std::mutex> g(l->mu);
            g_value_set_string(v, l->canal.c_str());
            break;
        }
        case PROP_CPS_MAX: g_value_set_int(v, l->cpsMax.load()); break;
        case PROP_PERDIDOS: g_value_set_uint64(v, l->perdidos.load()); break;
        case PROP_VUELTAS: {
            const ba::ipc_gst::Vueltas x = ba::ipc_gst::vueltas(ba::ipc_gst::Medio::Video);
            g_value_take_boxed(v, gst_structure_new("vueltas", "n", G_TYPE_UINT64, x.n, "ns", G_TYPE_UINT64, x.ns, "tarde",
                                                    G_TYPE_UINT64, x.tarde, "muy-tarde", G_TYPE_UINT64, x.muyTarde,
                                                    "max-ns", G_TYPE_UINT64, x.maxNs, nullptr));
            break;
        }
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static void finalizar(GObject* o) {
    auto* a = BA_VIDEO_ANILLO_COMUN(o);
    ba::ipc_gst::quitar(ba::ipc_gst::Medio::Video, *a->l); // por si se soltó sin pararlo
    {
        std::lock_guard<std::mutex> g((*a->l)->mu);
        (*a->l)->e = nullptr;
        (*a->l)->salida.pad = nullptr;
    }
    delete a->l;
    G_OBJECT_CLASS(ba_video_anillo_comun_parent_class)->finalize(o);
}

static void ba_video_anillo_comun_class_init(BaVideoAnilloComunClass* k) {
    auto* go = G_OBJECT_CLASS(k);
    auto* ge = GST_ELEMENT_CLASS(k);
    go->set_property = ponerPropiedad;
    go->get_property = leerPropiedad;
    go->finalize = finalizar;
    g_object_class_install_property(
        go, PROP_CANAL,
        g_param_spec_string("canal", "Canal", "El canal de video (BA.Guest.<n>, BA.GuestShare.<n>, BA.Remoto.1…)", "",
                            static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        go, PROP_CPS_MAX,
        g_param_spec_int("cps-max", "Cuadros por segundo como mucho", "0 = todos los que lleguen", 0, 240, 0,
                         static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING)));
    g_object_class_install_property(
        go, PROP_PERDIDOS,
        g_param_spec_uint64("perdidos", "Cuadros perdidos",
                            "Los que el productor publicó y el canal ya no tenía al leerlos (el lector se atrasó más que sus buffers)",
                            0, G_MAXUINT64, 0, static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        go, PROP_VUELTAS,
        g_param_spec_boxed("vueltas", "Vueltas del lector común del video",
                           "Desde que arrancó: n, ns (la suma), tarde (más que su período), muy-tarde (más de 2,4 períodos), max-ns",
                           GST_TYPE_STRUCTURE, static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
    gst_element_class_set_static_metadata(ge, "Canal de video (lector común)", "Source/Video",
                                          "El video de un canal de memoria compartida, leído por el hilo común de los canales",
                                          "Broadcast Appliance");
    gst_element_class_add_static_pad_template(ge, &srcTpl);
    ge->change_state = cambiar;
}

static void ba_video_anillo_comun_init(BaVideoAnilloComun* a) {
    a->l = new std::shared_ptr<LecturaVideo>(std::make_shared<LecturaVideo>());
    GstPad* pad = gst_pad_new_from_static_template(&srcTpl, "src");
    gst_pad_set_query_function(pad, consultar);
    gst_pad_set_event_function(pad, ba::ipc_gst::eventoSinHilo);
    gst_pad_use_fixed_caps(pad);
    gst_element_add_pad(GST_ELEMENT(a), pad);
    (*a->l)->e = GST_ELEMENT(a);
    (*a->l)->salida.pad = pad;
    GST_OBJECT_FLAG_SET(a, GST_ELEMENT_FLAG_SOURCE);
}

extern "C" gboolean ba_registrar_video_anillo_comun(GstPlugin* p) {
    return gst_element_register(p, "bavideoanillocomun", GST_RANK_NONE, BA_TYPE_VIDEO_ANILLO_COMUN);
}
