// ============================================================================
// src/gst-plugins/ipc/entrega.cpp — baentrega / batoma: el audio de una entrada a los
// mezcladores, SIN un hilo por rama
//
// intersink/intersrc pasan cada buffer por una cola (el appsrc del intersrc) con SU
// hilo: una entrada en dos mezcladores (el programa y los retornos) son dos hilos más y
// tres despertadas por bloque de 10 ms. Con 40 invitados, la mayor parte de los cambios
// de contexto de la app. Acá no hay un hilo por rama: la entrega deja el buffer en una
// cola por toma y UN repartidor por mezclador (ver abajo) lo empuja al pad:
//
//   baentrega producer-name=X   (sink) guarda las caps y el segmento, y deja cada buffer
//                               en la cola de cada batoma de X que esté andando
//   batoma producer-name=X      (fuente, sin hilo propio) lo que entrega X; antes del
//                               primero, stream-start, caps y el segmento del productor (con
//                               su desplazamiento: la hora de un archivo), y de nuevo si cambian
//
// SOLO para entregar a un agregador en vivo (audiomixer, baretornos): toma el buffer
// sin esperar al reloj. batoma empuja solo con SU pipeline en PLAYING (un mezclador
// parado no consume).
//
// La latencia que declara batoma: la del pipeline del productor (la que le reparte su
// pipeline a baentrega con el evento LATENCY), como un intersrc.
//
// NUNCA ESPERA A UN MEZCLADOR (2026-11-02). El que entrega es el lector común de los
// anillos: el audio de TODOS los invitados en un hilo (comun.hpp). El pad de un agregador
// en vivo hace esperar a gst_pad_push hasta su próxima vuelta cuando guarda más que su
// espera (adelantado.hpp), y en un bus de oído eso pasa con lo fresco: empujando desde la
// entrega, cada bloque de cada invitado esperaba una vuelta de algún mezclador y el
// lector común iba al ritmo de los mezcladores (con 10 invitados, una vuelta de 400 ms;
// con 40, el audio llegaba a la mezcla con 1,1 s). Ahora:
//
//   baentrega   deja el bloque (con sus caps y su segmento) en la COLA de cada batoma y
//               avisa al repartidor de su mezclador; no espera a nadie
//   repartidor  UN hilo por pipeline de mezclador (no por rama): saca de cada cola de a un
//               bloque por toma y por pasada y lo empuja a su pad cuando le toca
//               (adelantado.hpp): lo TARDE se tira (el agregador ya pasó), lo ADELANTADO
//               espera en su cola (una demora propia), lo demás entra. Si el pad está
//               lleno, espera a SU agregador: es el ritmo de ese mezclador y no atrasa al
//               lector ni a los otros mezcladores.
//
// La cola de cada toma tiene tope (kTopeCola): lo adelantado más que eso se tira
// (`adelantados`); lo de un segmento viejo (cambió el desplazamiento de la entrada) se
// tira al llegar lo del nuevo. Lo que llega tarde se cuenta en `tarde`.
//
// EL MEZCLADOR NO SE DESPIERTA CON CADA BLOQUE (2026-11-02). El agregador en vivo espera al
// reloj, y cada buffer que llega a cualquiera de sus pads lo despierta (SRC_BROADCAST en
// gstaggregator.c) para mirar TODOS sus pads: con 40 invitados, 40 despertadas y 40 × 40
// candados por vuelta y por mezclador (44 mil cambios de contexto/s en la app). Los
// mezcladores del motor (bamezcla, baretornos) esperan en cambio a sus entradas con
// esperarEntradas (comun.hpp): cuando piden más datos, se quedan acá hasta que el
// repartidor de su pipeline termine una pasada con todo (una despertada por vuelta) o
// hasta su misma hora límite. Lo que el repartidor empuja mientras tanto no los despierta.
// ============================================================================

#include "comun.hpp"
#include "adelantado.hpp"
#include "tiempos.hpp"

#include <gst/base/gstaggregator.h>
#include <gst/base/gstbasesink.h>
#include <gst/gst.h>

#include "platform/Hilo.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

/// Un bloque que la entrega dejó para una toma y todavía no se empujó.
struct Pendiente {
    GstBuffer* b = nullptr;     ///< con referencia
    GstCaps* caps = nullptr;    ///< con referencia (o nullptr)
    GstSegment seg{};
    std::uint64_t serie = 0;    ///< la del segmento (Productor::serieSegmento)
    std::uint64_t encolado = 0; ///< cuándo (tiempos.hpp; 0 = no se mide)

    void soltar() {
        if (b != nullptr) gst_buffer_unref(b);
        if (caps != nullptr) gst_caps_unref(caps);
        b = nullptr;
        caps = nullptr;
    }
};

/// Lo adelantado que espera en la cola de una toma, como mucho (bloques de 10 ms: ~1,3 s).
constexpr std::size_t kTopeCola = 128;

struct Repartidor;

struct Toma {
    std::mutex mu;              ///< el pad y lo de abajo: lo toma el repartidor mientras empuja
    GstElement* e = nullptr;    ///< bajo mu: nullptr = ya no se le empuja
    GstPad* pad = nullptr;
    bool eventos = true;
    std::uint64_t segmentoVisto = 0;
    GstCaps* caps = nullptr;
    /// Bajo mu: el agregador al que llega (buscado con el primer buffer después de
    /// engancharse; nullptr si no termina en uno), con referencia.
    GstElement* agregador = nullptr;
    bool buscado = false;
    GstClockTime ultimoFin = GST_CLOCK_TIME_NONE; ///< bajo mu: el fin de lo último entregado (tiempo de ejecución)
    std::atomic<std::uint64_t> adelantados{0};    ///< tirados por esperar más que la cola (adelantados)
    std::atomic<std::uint64_t> tarde{0};          ///< tirados porque el agregador ya había pasado

    /// La cola: con SU candado (la entrega deja ahí sin esperar al que empuja).
    std::mutex colaMu;
    std::deque<Pendiente> cola;      ///< bajo colaMu
    std::shared_ptr<Repartidor> rep; ///< bajo colaMu: el de su mezclador (nullptr = no enganchada)

    void soltarDestino() {
        if (agregador != nullptr) gst_object_unref(agregador);
        agregador = nullptr;
        buscado = false;
        ultimoFin = GST_CLOCK_TIME_NONE;
    }
    /// Con colaMu.
    void vaciarCola() {
        for (Pendiente& x : cola) x.soltar();
        cola.clear();
    }
    ~Toma() {
        soltarDestino();
        vaciarCola();
        if (caps != nullptr) gst_caps_unref(caps);
    }
};

/// El agregador al que llega lo que sale por `desde` (batoma → volume → level →
/// audiomixer.sink_N): se siguen los pares por elementos de una sola salida hasta un pad de
/// agregador. Con referencia; nullptr si no termina en uno.
GstElement* agregadorDe(GstPad* desde) {
    GstPad* par = gst_pad_get_peer(desde);
    for (int i = 0; par != nullptr && i < 8; ++i) {
        GstElement* el = gst_pad_get_parent_element(par);
        const bool deAgregador = GST_IS_AGGREGATOR_PAD(par);
        gst_object_unref(par);
        par = nullptr;
        if (el == nullptr) break;
        if (deAgregador) return el;
        GstPad* sale = nullptr;
        GST_OBJECT_LOCK(el);
        if (el->numsrcpads == 1) sale = GST_PAD(gst_object_ref(el->srcpads->data));
        GST_OBJECT_UNLOCK(el);
        gst_object_unref(el);
        if (sale == nullptr) break;
        par = gst_pad_get_peer(sale);
        gst_object_unref(sale);
    }
    if (par != nullptr) gst_object_unref(par);
    return nullptr;
}

/// Lo que se mira de un agregador para decidir los turnos (adelantado.hpp), una vez por
/// pasada del repartidor y no por bloque: gst_aggregator_get_latency toma el candado del
/// agregador (el que usa su hilo para esperar) y g_object_get busca las propiedades por
/// nombre; con 40 tomas eran 40 veces por vuelta del mezclador. La hora sí se lee con cada
/// bloque (un push puede esperar una vuelta del agregador).
struct VistaAgregador {
    GstElement* agregador = nullptr; ///< sin referencia: solo para reconocerlo en la pasada
    GstClock* reloj = nullptr;       ///< con referencia (nullptr: no está en vivo o sin reloj)
    std::uint64_t latencia = 0, tope = 0, base = 0;
};

/// Las vistas de una pasada (se sueltan al terminarla).
struct Vistas {
    std::vector<VistaAgregador> v;
    void soltar() {
        for (VistaAgregador& x : v)
            if (x.reloj != nullptr) gst_object_unref(x.reloj);
        v.clear();
    }
    ~Vistas() { soltar(); }

    const VistaAgregador& de(GstElement* agg) {
        for (const VistaAgregador& x : v)
            if (x.agregador == agg) return x;
        VistaAgregador x;
        x.agregador = agg;
        const GstClockTime lat = gst_aggregator_get_latency(GST_AGGREGATOR(agg));
        GstClock* reloj = gst_element_get_clock(agg);
        if (reloj != nullptr && GST_CLOCK_TIME_IS_VALID(lat)) {
            guint64 latencia = 0, minima = 0;
            g_object_get(agg, "latency", &latencia, "min-upstream-latency", &minima, nullptr);
            x.reloj = reloj;
            x.latencia = lat;
            x.tope = latencia + minima;
            x.base = gst_element_get_base_time(agg);
        } else if (reloj != nullptr) {
            gst_object_unref(reloj);
        }
        v.push_back(x);
        return v.back();
    }
};

/// Cuándo le toca a un bloque que termina en `fin` (adelantado.hpp). Con el candado de la toma.
ba::ipc_gst::Turno turnoDe(Toma& t, GstClockTime fin, GstClockTime bloque, Vistas& vistas) {
    if (!t.buscado) {
        t.buscado = true;
        t.agregador = agregadorDe(t.pad);
    }
    if (t.agregador == nullptr || !GST_CLOCK_TIME_IS_VALID(fin)) return ba::ipc_gst::Turno::Ya;
    const VistaAgregador& v = vistas.de(t.agregador);
    if (v.reloj == nullptr) return ba::ipc_gst::Turno::Ya; // no está en vivo (o sin reloj): no espera a nadie
    const GstClockTime t0 = gst_clock_get_time(v.reloj);
    ba::ipc_gst::PadAgregador p;
    p.ultimoFin = GST_CLOCK_TIME_IS_VALID(t.ultimoFin) ? t.ultimoFin : 0;
    p.latencia = v.latencia;
    p.tope = v.tope;
    p.ahora = t0 > v.base ? t0 - v.base : 0;
    // Esperar a la próxima vuelta del agregador es lo de siempre (lo fresco queda en el
    // borde): una vuelta de holgura. Lo que no entra en eso espera en su cola.
    return ba::ipc_gst::turno(p, fin, bloque);
}

struct Productor {
    std::mutex mu;
    std::vector<std::shared_ptr<Toma>> tomas; ///< bajo mu
    GstCaps* caps = nullptr;                  ///< bajo mu
    GstSegment segmento{};                    ///< bajo mu
    std::uint64_t serieSegmento = 0;          ///< bajo mu: crece con cada segmento (nunca vuelve atrás)
    bool conSegmento = false;                 ///< bajo mu: el pipeline de la entrega ya mandó uno
    std::atomic<GstClockTime> latencia{0};
};

std::mutex g_mu;
std::map<std::string, std::weak_ptr<Productor>> g_productores;

std::shared_ptr<Productor> productor(const std::string& nombre) {
    std::lock_guard<std::mutex> l(g_mu);
    auto& w = g_productores[nombre];
    std::shared_ptr<Productor> p = w.lock();
    if (!p) {
        p = std::make_shared<Productor>();
        gst_segment_init(&p->segmento, GST_FORMAT_TIME);
        w = p;
    }
    return p;
}

/// En el hilo de un repartidor: la pasada le mandó un evento (stream-start, caps, segmento) a
/// algún pad. El agregador lo atiende antes que los buffers: si está esperando a sus
/// entradas (esperarEntradas), que deje de esperar.
thread_local bool tl_evento = false;

/// Empuja el primero de la cola de `t` si le toca (lo que falte de eventos antes). ¿Hizo
/// algo (lo empujó o lo tiró)? Con el candado de la toma mientras empuja: si el pad está
/// lleno, gst_pad_push espera a la próxima vuelta de SU agregador.
bool repartirUno(Toma& t, Vistas& vistas) {
    std::lock_guard<std::mutex> l(t.mu);
    Pendiente x;
    {
        std::lock_guard<std::mutex> c(t.colaMu);
        if (t.cola.empty()) return false;
        // Solo con la batoma andando y sin que estén parando su pipeline (comun.hpp): así no
        // se le empuja a un mezclador en pausa. Lo de antes, afuera.
        if (t.e == nullptr || !ba::ipc_gst::corriendo(t.e)) {
            t.vaciarCola();
            return false;
        }
        // Con referencias propias: la entrega puede vaciar la cola mientras tanto.
        x = t.cola.front();
        gst_buffer_ref(x.b);
        if (x.caps != nullptr) gst_caps_ref(x.caps);
    }
    if (t.eventos) {
        gchar* id = gst_pad_create_stream_id(t.pad, t.e, nullptr);
        GstEvent* ss = gst_event_new_stream_start(id);
        g_free(id);
        gst_event_set_group_id(ss, gst_util_group_id_next());
        gst_pad_push_event(t.pad, ss);
        tl_evento = true;
    }
    if (x.caps != nullptr && (t.eventos || t.caps == nullptr || !gst_caps_is_equal(t.caps, x.caps))) {
        if (t.caps != nullptr) gst_caps_unref(t.caps);
        t.caps = gst_caps_ref(x.caps);
        gst_pad_push_event(t.pad, gst_event_new_caps(x.caps));
        tl_evento = true;
    }
    if (t.eventos || t.segmentoVisto != x.serie) {
        gst_pad_push_event(t.pad, gst_event_new_segment(&x.seg));
        tl_evento = true;
        t.segmentoVisto = x.serie;
    }
    t.eventos = false;
    GstBuffer* b = x.b;
    const GstClockTime dur = GST_BUFFER_DURATION_IS_VALID(b) ? GST_BUFFER_DURATION(b) : 10 * GST_MSECOND;
    const GstClockTime rt =
        GST_BUFFER_PTS_IS_VALID(b) ? gst_segment_to_running_time(&x.seg, GST_FORMAT_TIME, GST_BUFFER_PTS(b)) : GST_CLOCK_TIME_NONE;
    const GstClockTime fin = GST_CLOCK_TIME_IS_VALID(rt) ? rt + dur : GST_CLOCK_TIME_NONE;
    const ba::ipc_gst::Turno turno = turnoDe(t, fin, dur, vistas);
    if (turno == ba::ipc_gst::Turno::Despues) { // adelantado: espera en su cola
        x.soltar();
        return false;
    }
    {
        std::lock_guard<std::mutex> c(t.colaMu);
        // La entrega pudo haber vaciado la cola (otro segmento): se saca solo si sigue ahí.
        if (t.cola.empty() || t.cola.front().b != b) {
            x.soltar();
            return true;
        }
        t.cola.front().soltar();
        t.cola.pop_front();
    }
    if (turno == ba::ipc_gst::Turno::Tarde) {
        ++t.tarde;
        x.soltar();
        return true;
    }
    namespace ti = ba::ipc_gst::tiempos;
    const std::uint64_t t0 = x.encolado != 0 ? ti::ahora() : 0;
    if (t0 != 0) ti::cola.anotar(t0 - x.encolado);
    gst_pad_push(t.pad, b); // se lo lleva
    x.b = nullptr;
    x.soltar();
    if (t0 != 0) ti::toma.anotar(ti::ahora() - t0);
    t.ultimoFin = fin;
    return true;
}

/// El repartidor de un mezclador: UN hilo por pipeline con batomas (ver la cabecera).
struct Repartidor {
    std::mutex mu;
    std::condition_variable cv;
    std::vector<std::shared_ptr<Toma>> tomas; ///< bajo mu
    bool hay = false;                         ///< bajo mu: algo nuevo en alguna cola
    bool fin = false;                         ///< bajo mu: sin tomas, el hilo termina

    /// El mezclador que espera a sus entradas (esperarEntradas): una pasada que empujó algo
    /// lo despierta UNA vez (no cada push, como su espera de siempre).
    std::mutex muMezclador;
    std::condition_variable cvMezclador;
    std::uint64_t pasadas = 0;  ///< bajo muMezclador
    std::uint64_t conEvento = 0; ///< bajo muMezclador: la última pasada que mandó un evento
    std::uint64_t eventoVisto = 0; ///< bajo muMezclador: hasta dónde los vio el mezclador al esperar
    int esperando = 0;          ///< bajo muMezclador

    void avisarMezclador(bool evento) {
        {
            std::lock_guard<std::mutex> l(muMezclador);
            ++pasadas;
            if (evento) conEvento = pasadas;
            if (esperando == 0) return;
        }
        cvMezclador.notify_all();
    }

    void avisar() {
        {
            std::lock_guard<std::mutex> l(mu);
            hay = true;
        }
        cv.notify_one();
    }

    void bucle() {
        ba::platform::nombrarHilo("mezcla-reparto");
        ba::ipc_gst::prioridadDeAudio();
        std::vector<std::shared_ptr<Toma>> lista;
        Vistas vistas;
        for (;;) {
            {
                std::unique_lock<std::mutex> l(mu);
                // Sin aviso, igual cada 10 ms: lo adelantado que espera en una cola entra a su hora.
                cv.wait_for(l, std::chrono::milliseconds(10), [this] { return hay || fin; });
                if (fin) return;
                hay = false;
                lista = tomas;
            }
            // De a un bloque por toma y por pasada: ninguna se queda con el repartidor.
            for (bool algo = true; algo;) {
                algo = false;
                vistas.soltar(); // la latencia de cada agregador, de nuevo en cada pasada
                tl_evento = false;
                for (const auto& t : lista) algo = repartirUno(*t, vistas) || algo;
                if (algo || tl_evento) avisarMezclador(tl_evento);
            }
            vistas.soltar();
            lista.clear();
        }
    }
};

/// Los repartidores, por pipeline (bajo g_mu).
std::map<GstObject*, std::shared_ptr<Repartidor>> g_repartidores;

GstObject* raizDe(GstElement* e) {
    GstObject* r = GST_OBJECT(e);
    while (GST_OBJECT_PARENT(r) != nullptr) r = GST_OBJECT_PARENT(r);
    return r;
}

/// Suma `t` al repartidor del pipeline de `e` (lo crea, con su hilo, si es la primera).
std::shared_ptr<Repartidor> sumarARepartidor(GstElement* e, const std::shared_ptr<Toma>& t) {
    std::shared_ptr<Repartidor> r;
    bool nuevo = false;
    {
        std::lock_guard<std::mutex> l(g_mu);
        auto& x = g_repartidores[raizDe(e)];
        if (!x) {
            x = std::make_shared<Repartidor>();
            nuevo = true;
        }
        r = x;
        std::lock_guard<std::mutex> lr(r->mu);
        if (std::find(r->tomas.begin(), r->tomas.end(), t) == r->tomas.end()) r->tomas.push_back(t);
    }
    // El hilo vive lo que haya tomas; no se lo espera (puede estar empujando a un mezclador
    // que se está parando: se suelta cuando ese pipeline baja a READY).
    if (nuevo) std::thread([r]() { r->bucle(); }).detach();
    return r;
}

/// Saca `t` de su repartidor; sin tomas, su hilo termina.
void sacarDeRepartidor(const std::shared_ptr<Repartidor>& r, const std::shared_ptr<Toma>& t) {
    if (!r) return;
    bool vacio = false;
    {
        std::lock_guard<std::mutex> g(g_mu);
        {
            std::lock_guard<std::mutex> l(r->mu);
            r->tomas.erase(std::remove(r->tomas.begin(), r->tomas.end(), t), r->tomas.end());
            vacio = r->tomas.empty();
            if (vacio) r->fin = true;
        }
        if (vacio)
            for (auto it = g_repartidores.begin(); it != g_repartidores.end(); ++it)
                if (it->second == r) {
                    g_repartidores.erase(it);
                    break;
                }
    }
    if (vacio) r->cv.notify_one();
}

/// Los avisos de este hilo: enseguida, o juntos hasta despertarPendientes (comun.hpp).
thread_local bool tl_diferir = false;
thread_local std::vector<std::shared_ptr<Repartidor>> tl_pendientes;

void avisar(const std::shared_ptr<Repartidor>& r) {
    if (!tl_diferir) {
        r->avisar();
        return;
    }
    if (std::find(tl_pendientes.begin(), tl_pendientes.end(), r) == tl_pendientes.end()) tl_pendientes.push_back(r);
}

/// Deja `b` en la cola de `t`, sin esperar a nadie. El repartidor al que hay que avisarle
/// (nullptr: la toma no está enganchada).
std::shared_ptr<Repartidor> dejar(Toma& t, GstBuffer* b, GstCaps* caps, const GstSegment& seg, std::uint64_t serie) {
    std::lock_guard<std::mutex> c(t.colaMu);
    if (!t.rep) return nullptr;
    // Otro segmento (cambió el desplazamiento de la entrada: una demora propia nueva, un
    // realineo): lo encolado con el de antes ya no va.
    if (!t.cola.empty() && t.cola.back().serie != serie) t.vaciarCola();
    if (t.cola.size() >= kTopeCola) {
        t.cola.front().soltar();
        t.cola.pop_front();
        ++t.adelantados;
    }
    Pendiente x;
    x.b = gst_buffer_ref(b);
    x.caps = caps != nullptr ? gst_caps_ref(caps) : nullptr;
    gst_segment_copy_into(&seg, &x.seg);
    x.serie = serie;
    x.encolado = ba::ipc_gst::tiempos::activos() ? ba::ipc_gst::tiempos::ahora() : 0;
    t.cola.push_back(x);
    return t.rep;
}

} // namespace

namespace {

/// ¿Todas las entradas de `agg` tienen un bloque? (lo que mira el agregador antes de su espera)
bool todasConDatos(GstAggregator* agg) {
    thread_local std::vector<GstPad*> pads;
    pads.clear();
    GST_OBJECT_LOCK(agg);
    for (GList* l = GST_ELEMENT(agg)->sinkpads; l != nullptr; l = l->next) pads.push_back(GST_PAD(gst_object_ref(l->data)));
    GST_OBJECT_UNLOCK(agg);
    bool todas = true;
    for (GstPad* p : pads) {
        // Sin el candado del agregador: has_buffer recorta el bloque (el clip del agregador).
        if (todas && !gst_aggregator_pad_has_buffer(GST_AGGREGATOR_PAD(p)) && !gst_aggregator_pad_is_eos(GST_AGGREGATOR_PAD(p)))
            todas = false;
        gst_object_unref(p);
    }
    pads.clear();
    return todas;
}

/// Lo más que se espera de una vez (por si la hora del agregador se fue lejos): nunca traba
/// un cambio de estado más que esto.
constexpr auto kTopeEspera = std::chrono::milliseconds(100);

} // namespace

namespace ba::ipc_gst {

void esperarEntradas(GstAggregator* agg) {
    std::shared_ptr<Repartidor> rep;
    {
        std::lock_guard<std::mutex> l(g_mu);
        auto it = g_repartidores.find(raizDe(GST_ELEMENT(agg)));
        if (it == g_repartidores.end()) return; // sin batomas: la espera de siempre
        rep = it->second;
    }
    GstAggregatorClass* k = GST_AGGREGATOR_GET_CLASS(agg);
    const GstClockTime lat = gst_aggregator_get_latency(agg);
    const GstClockTime prox = k->get_next_time != nullptr ? k->get_next_time(agg) : GST_CLOCK_TIME_NONE;
    if (!GST_CLOCK_TIME_IS_VALID(lat) || !GST_CLOCK_TIME_IS_VALID(prox)) return; // no está en vivo: espera él
    GstClock* reloj = gst_element_get_clock(GST_ELEMENT(agg));
    if (reloj == nullptr) return;
    // La misma hora límite que su espera: lo que no llegue para entonces no entra (y la
    // salida no se demora ni un poco más que antes).
    const GstClockTime limite = gst_element_get_base_time(GST_ELEMENT(agg)) + prox + lat;
    namespace ti = tiempos;
    const std::uint64_t t0 = ti::activos() ? ti::ahora() : 0;
    const auto tope = std::chrono::steady_clock::now() + kTopeEspera;
    for (;;) {
        std::uint64_t visto = 0;
        bool evento = false;
        {
            // Los eventos desde la última vez que esperó (pudieron llegar mientras sumaba).
            std::lock_guard<std::mutex> l(rep->muMezclador);
            visto = rep->pasadas;
            evento = rep->conEvento > rep->eventoVisto;
            rep->eventoVisto = rep->conEvento;
        }
        // Un evento en algún pad (una entrada que llega, otras caps, otro segmento): el
        // agregador lo atiende antes que los buffers y has_buffer no lo ve. Que lo atienda ya.
        if (evento || GST_PAD_IS_FLUSHING(agg->srcpad) || todasConDatos(agg)) break;
        const GstClockTime ahora = gst_clock_get_time(reloj);
        if (ahora >= limite) break;
        const auto hasta = std::min(std::chrono::steady_clock::now() + std::chrono::nanoseconds(limite - ahora), tope);
        std::unique_lock<std::mutex> l(rep->muMezclador);
        ++rep->esperando;
        const bool hubo = rep->cvMezclador.wait_until(l, hasta, [&] { return rep->pasadas != visto; });
        --rep->esperando;
        if (!hubo && std::chrono::steady_clock::now() >= tope) break;
    }
    gst_object_unref(reloj);
    if (t0 != 0) ti::espera.anotar(ti::ahora() - t0);
}

void diferirAvisos(bool si) { tl_diferir = si; }
void despertarPendientes() {
    for (const auto& r : tl_pendientes) r->avisar();
    tl_pendientes.clear();
}
} // namespace ba::ipc_gst

// ============================================================================ baentrega

G_BEGIN_DECLS
#define BA_TYPE_ENTREGA (ba_entrega_get_type())
G_DECLARE_FINAL_TYPE(BaEntrega, ba_entrega, BA, ENTREGA, GstBaseSink)
G_END_DECLS

struct _BaEntrega {
    GstBaseSink parent;
    std::mutex* mu;               ///< el nombre y `p`
    std::string* nombre;
    std::shared_ptr<Productor>* p; ///< el registrado (nullptr fuera de READY..PLAYING)
};

G_DEFINE_TYPE(BaEntrega, ba_entrega, GST_TYPE_BASE_SINK)

enum { PROP_E_0, PROP_E_PRODUCTOR };

static GstStaticPadTemplate entregaTpl = GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

static std::shared_ptr<Productor> deEntrega(BaEntrega* e) {
    std::lock_guard<std::mutex> l(*e->mu);
    return *e->p;
}

static gboolean entregaArrancar(GstBaseSink* bs) {
    auto* e = BA_ENTREGA(bs);
    std::lock_guard<std::mutex> l(*e->mu);
    *e->p = productor(*e->nombre);
    return TRUE;
}

static gboolean entregaParar(GstBaseSink* bs) {
    auto* e = BA_ENTREGA(bs);
    std::shared_ptr<Productor> p;
    {
        std::lock_guard<std::mutex> l(*e->mu);
        p = std::move(*e->p);
        *e->p = nullptr;
    }
    if (p) {
        std::lock_guard<std::mutex> l(p->mu);
        if (p->caps != nullptr) gst_caps_unref(p->caps);
        p->caps = nullptr;
        p->conSegmento = false; // al volver a andar manda uno nuevo (y la serie sigue: las tomas lo reenvían)
    }
    return TRUE;
}

static gboolean entregaEvento(GstBaseSink* bs, GstEvent* ev) {
    if (std::shared_ptr<Productor> p = deEntrega(BA_ENTREGA(bs))) {
        if (GST_EVENT_TYPE(ev) == GST_EVENT_CAPS) {
            GstCaps* c = nullptr;
            gst_event_parse_caps(ev, &c);
            std::lock_guard<std::mutex> l(p->mu);
            if (p->caps != nullptr) gst_caps_unref(p->caps);
            p->caps = c != nullptr ? gst_caps_ref(c) : nullptr;
        } else if (GST_EVENT_TYPE(ev) == GST_EVENT_SEGMENT) {
            // Ya trae el desplazamiento del pad (gst_pad_set_offset: el de un archivo).
            const GstSegment* s = nullptr;
            gst_event_parse_segment(ev, &s);
            std::lock_guard<std::mutex> l(p->mu);
            gst_segment_copy_into(s, &p->segmento);
            ++p->serieSegmento;
            p->conSegmento = true;
        }
    }
    // EOS, flush y lo demás no pasan a los mezcladores (como intersink event-types="<>").
    return GST_BASE_SINK_CLASS(ba_entrega_parent_class)->event(bs, ev);
}

static GstFlowReturn entregaRender(GstBaseSink* bs, GstBuffer* b) {
    std::shared_ptr<Productor> p = deEntrega(BA_ENTREGA(bs));
    if (!p) return GST_FLOW_OK;
    std::vector<std::shared_ptr<Toma>> tomas;
    GstCaps* caps = nullptr;
    GstSegment seg;
    std::uint64_t serie = 0;
    {
        std::lock_guard<std::mutex> l(p->mu);
        if (p->tomas.empty()) return GST_FLOW_OK;
        tomas = p->tomas;
        caps = p->caps != nullptr ? gst_caps_ref(p->caps) : nullptr;
        gst_segment_copy_into(&p->segmento, &seg);
        serie = p->conSegmento ? p->serieSegmento : 0;
    }
    if (serie != 0)
        for (const auto& t : tomas)
            if (std::shared_ptr<Repartidor> r = dejar(*t, b, caps, seg, serie)) avisar(r);
    if (caps != nullptr) gst_caps_unref(caps);
    return GST_FLOW_OK;
}

/// La latencia del pipeline (la que reparte a sus sinks): la que declara cada batoma.
static gboolean entregaMandarEvento(GstElement* el, GstEvent* ev) {
    if (GST_EVENT_TYPE(ev) == GST_EVENT_LATENCY) {
        GstClockTime lat = 0;
        gst_event_parse_latency(ev, &lat);
        if (std::shared_ptr<Productor> p = deEntrega(BA_ENTREGA(el))) p->latencia = lat;
    }
    return GST_ELEMENT_CLASS(ba_entrega_parent_class)->send_event(el, ev);
}

static void entregaPoner(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    auto* e = BA_ENTREGA(o);
    if (id != PROP_E_PRODUCTOR) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
        return;
    }
    const gchar* x = g_value_get_string(v);
    std::lock_guard<std::mutex> l(*e->mu);
    *e->nombre = x != nullptr ? x : "";
}

static void entregaLeer(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    auto* e = BA_ENTREGA(o);
    if (id != PROP_E_PRODUCTOR) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
        return;
    }
    std::lock_guard<std::mutex> l(*e->mu);
    g_value_set_string(v, e->nombre->c_str());
}

static void entregaFinalizar(GObject* o) {
    auto* e = BA_ENTREGA(o);
    delete e->p;
    delete e->nombre;
    delete e->mu;
    G_OBJECT_CLASS(ba_entrega_parent_class)->finalize(o);
}

static void ba_entrega_class_init(BaEntregaClass* k) {
    auto* go = G_OBJECT_CLASS(k);
    auto* ge = GST_ELEMENT_CLASS(k);
    auto* bs = GST_BASE_SINK_CLASS(k);
    go->set_property = entregaPoner;
    go->get_property = entregaLeer;
    go->finalize = entregaFinalizar;
    g_object_class_install_property(
        go, PROP_E_PRODUCTOR,
        g_param_spec_string("producer-name", "Productor", "El nombre con que lo encuentran las batoma", "",
                            static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));
    gst_element_class_set_static_metadata(ge, "Entrega sin hilo", "Sink/Generic",
                                          "Entrega cada buffer, en el mismo hilo, a las batoma del mismo nombre (hacia un "
                                          "agregador en vivo)",
                                          "Broadcast Appliance");
    gst_element_class_add_static_pad_template(ge, &entregaTpl);
    ge->send_event = entregaMandarEvento;
    bs->start = entregaArrancar;
    bs->stop = entregaParar;
    bs->event = entregaEvento;
    bs->render = entregaRender;
}

static void ba_entrega_init(BaEntrega* e) {
    e->mu = new std::mutex();
    e->nombre = new std::string();
    e->p = new std::shared_ptr<Productor>();
    gst_base_sink_set_sync(GST_BASE_SINK(e), FALSE);
    gst_base_sink_set_async_enabled(GST_BASE_SINK(e), FALSE);
    gst_base_sink_set_last_sample_enabled(GST_BASE_SINK(e), FALSE);
    gst_base_sink_set_qos_enabled(GST_BASE_SINK(e), FALSE);
}

// ============================================================================ batoma

G_BEGIN_DECLS
#define BA_TYPE_TOMA (ba_toma_get_type())
G_DECLARE_FINAL_TYPE(BaToma, ba_toma, BA, TOMA, GstElement)
G_END_DECLS

struct _BaToma {
    GstElement parent;
    std::mutex* mu; ///< el nombre y `p`
    std::string* nombre;
    std::shared_ptr<Productor>* p; ///< al que está enganchada (en PLAYING)
    std::shared_ptr<Toma>* t;
};

G_DEFINE_TYPE(BaToma, ba_toma, GST_TYPE_ELEMENT)

enum { PROP_T_0, PROP_T_PRODUCTOR, PROP_T_ADELANTADOS, PROP_T_TARDE };

static GstStaticPadTemplate tomaTpl = GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS_ANY);

static void enganchar(BaToma* t) {
    std::shared_ptr<Productor> p;
    {
        std::lock_guard<std::mutex> l(*t->mu);
        p = productor(*t->nombre);
        *t->p = p;
    }
    Toma& tm = **t->t;
    {
        std::lock_guard<std::mutex> l(tm.mu);
        tm.e = GST_ELEMENT(t);
        tm.eventos = true;
        tm.soltarDestino(); // se busca de nuevo con el primer buffer (el mezclador pudo cambiar)
    }
    std::shared_ptr<Repartidor> r = sumarARepartidor(GST_ELEMENT(t), *t->t);
    {
        std::lock_guard<std::mutex> c(tm.colaMu);
        tm.rep = std::move(r);
    }
    std::lock_guard<std::mutex> l(p->mu);
    p->tomas.push_back(*t->t);
}

static void desenganchar(BaToma* t) {
    std::shared_ptr<Productor> p;
    {
        std::lock_guard<std::mutex> l(*t->mu);
        p = std::move(*t->p);
        *t->p = nullptr;
    }
    if (p) {
        std::lock_guard<std::mutex> l(p->mu);
        p->tomas.erase(std::remove(p->tomas.begin(), p->tomas.end(), *t->t), p->tomas.end());
    }
    // Sin esperar al repartidor (puede estar empujándole a este mezclador, que se está parando).
    std::shared_ptr<Repartidor> r;
    {
        std::lock_guard<std::mutex> c((*t->t)->colaMu);
        r = std::move((*t->t)->rep);
        (*t->t)->rep = nullptr;
        (*t->t)->vaciarCola();
    }
    sacarDeRepartidor(r, *t->t);
}

/// Al bajar a READY y al destruirla: si el repartidor le está empujando, se espera; después
/// no la toca. (Al salir de PLAYING no se espera: el repartidor puede estar trabado en un
/// mezclador que ya está en pausa, y eso se suelta cuando el mezclador baja a READY, antes
/// que la batoma, que es fuente.)
static void soltarToma(BaToma* t) {
    std::lock_guard<std::mutex> l((*t->t)->mu);
    (*t->t)->e = nullptr;
    (*t->t)->soltarDestino();
    std::lock_guard<std::mutex> c((*t->t)->colaMu);
    (*t->t)->vaciarCola();
}

static gboolean tomaConsulta(GstPad* pad, GstObject* padre, GstQuery* q) {
    if (GST_QUERY_TYPE(q) == GST_QUERY_LATENCY) {
        auto* t = BA_TOMA(padre);
        std::shared_ptr<Productor> p;
        {
            std::lock_guard<std::mutex> l(*t->mu);
            p = *t->p;
        }
        gst_query_set_latency(q, TRUE, p ? p->latencia.load() : 0, GST_CLOCK_TIME_NONE);
        return TRUE;
    }
    return gst_pad_query_default(pad, padre, q);
}

static gboolean tomaEvento(GstPad*, GstObject*, GstEvent* ev) {
    gst_event_unref(ev); // lo que sube (reconfigurar, QoS) no vuelve al productor
    return TRUE;
}

static GstStateChangeReturn tomaCambiar(GstElement* e, GstStateChange c) {
    auto* t = BA_TOMA(e);
    if (c == GST_STATE_CHANGE_PLAYING_TO_PAUSED) desenganchar(t);
    GstStateChangeReturn r = GST_ELEMENT_CLASS(ba_toma_parent_class)->change_state(e, c);
    if (r == GST_STATE_CHANGE_FAILURE) return r;
    switch (c) {
        case GST_STATE_CHANGE_READY_TO_PAUSED:
        case GST_STATE_CHANGE_PLAYING_TO_PAUSED: r = GST_STATE_CHANGE_NO_PREROLL; break; // en vivo
        case GST_STATE_CHANGE_PAUSED_TO_PLAYING: enganchar(t); break;
        case GST_STATE_CHANGE_PAUSED_TO_READY: soltarToma(t); break;
        default: break;
    }
    return r;
}

static void tomaPoner(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    auto* t = BA_TOMA(o);
    if (id != PROP_T_PRODUCTOR) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
        return;
    }
    const gchar* x = g_value_get_string(v);
    std::lock_guard<std::mutex> l(*t->mu);
    *t->nombre = x != nullptr ? x : "";
}

static void tomaLeer(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    auto* t = BA_TOMA(o);
    if (id == PROP_T_ADELANTADOS) {
        g_value_set_uint64(v, (*t->t)->adelantados.load());
        return;
    }
    if (id == PROP_T_TARDE) {
        g_value_set_uint64(v, (*t->t)->tarde.load());
        return;
    }
    if (id != PROP_T_PRODUCTOR) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
        return;
    }
    std::lock_guard<std::mutex> l(*t->mu);
    g_value_set_string(v, t->nombre->c_str());
}

static void tomaFinalizar(GObject* o) {
    auto* t = BA_TOMA(o);
    desenganchar(t); // por si se soltó sin pararla
    soltarToma(t);
    delete t->t;
    delete t->p;
    delete t->nombre;
    delete t->mu;
    G_OBJECT_CLASS(ba_toma_parent_class)->finalize(o);
}

static void ba_toma_class_init(BaTomaClass* k) {
    auto* go = G_OBJECT_CLASS(k);
    auto* ge = GST_ELEMENT_CLASS(k);
    go->set_property = tomaPoner;
    go->get_property = tomaLeer;
    go->finalize = tomaFinalizar;
    g_object_class_install_property(
        go, PROP_T_PRODUCTOR,
        g_param_spec_string("producer-name", "Productor", "La baentrega de la que toma", "",
                            static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));
    g_object_class_install_property(
        go, PROP_T_ADELANTADOS,
        g_param_spec_uint64("adelantados", "Adelantados",
                            "Bloques tirados por llegar más adelantados que lo que espera su cola", 0, G_MAXUINT64, 0,
                            static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        go, PROP_T_TARDE,
        g_param_spec_uint64("tarde", "Tarde", "Bloques tirados porque el agregador ya había pasado su hora", 0, G_MAXUINT64, 0,
                            static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
    gst_element_class_set_static_metadata(ge, "Toma sin hilo", "Source/Generic",
                                          "Lo que entrega una baentrega, empujado por el repartidor de su mezclador (hacia un agregador en vivo)",
                                          "Broadcast Appliance");
    gst_element_class_add_static_pad_template(ge, &tomaTpl);
    ge->change_state = tomaCambiar;
}

static void ba_toma_init(BaToma* t) {
    t->mu = new std::mutex();
    t->nombre = new std::string();
    t->p = new std::shared_ptr<Productor>();
    t->t = new std::shared_ptr<Toma>(std::make_shared<Toma>());
    GstPad* pad = gst_pad_new_from_static_template(&tomaTpl, "src");
    gst_pad_set_query_function(pad, tomaConsulta);
    gst_pad_set_event_function(pad, tomaEvento);
    gst_pad_use_fixed_caps(pad);
    gst_element_add_pad(GST_ELEMENT(t), pad);
    (*t->t)->pad = pad;
    GST_OBJECT_FLAG_SET(t, GST_ELEMENT_FLAG_SOURCE);
}

extern "C" gboolean ba_registrar_entrega(GstPlugin* p) {
    return gst_element_register(p, "baentrega", GST_RANK_NONE, BA_TYPE_ENTREGA) &&
           gst_element_register(p, "batoma", GST_RANK_NONE, BA_TYPE_TOMA);
}
