// ============================================================================
// src/motor/Prioridad.cpp — ver Prioridad.hpp
// ============================================================================
#include "Prioridad.hpp"

#include "Reloj.hpp" // registrar
#include "i18n/I18n.hpp"
#include "ajustes/Ajustes.hpp"
#include "platform/Energia.hpp"
#include "platform/Hilo.hpp"
#include "platform/Nucleos.hpp" // la topología y el grupo de medios (una sola fuente con el gateway)

#include "platform/TiempoReal.hpp"

#include <gst/audio/gstaudioaggregator.h>
#include <gst/base/gstaggregator.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <pthread/qos.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace ba::motor {

using json = nlohmann::json;
using ba::i18n::llenar;
using ba::i18n::traducir;

namespace {

std::atomic<bool> g_activa{false}; ///< procesador.prioridad (lo fija prioridadDelProceso)
std::once_flag g_unaVez;
std::mutex g_topoMu;
json g_topo = json::object(); ///< lo que se supo del procesador y lo que se aplicó

/// El nombre de un objeto de GStreamer, o "?". Los pipelines de gst_parse_launch se
/// llaman «pipeline<N>»: queda «p<N>», que en Linux (15 bytes) deja lugar al elemento.
std::string nombreGst(GstObject* o, bool esPipeline) {
    if (o == nullptr) return "?";
    gchar* n = gst_object_get_name(o);
    std::string s = n != nullptr ? n : "?";
    g_free(n);
    if (esPipeline && s.rfind("pipeline", 0) == 0 && s.size() > 8) s = "p" + s.substr(8);
    return s;
}

bool deFabrica(GstElement* e, const char* nombre) {
    GstElementFactory* f = e != nullptr ? gst_element_get_factory(e) : nullptr;
    return f != nullptr && std::strcmp(GST_OBJECT_NAME(f), nombre) == 0;
}

/// ¿El hilo de este elemento mueve audio con plazo? La salida de un mezclador de audio
/// (cada bus: lo que sigue —volumen, limitador, medidores, el anillo— corre en ese
/// hilo), el que trae el audio del gateway (baanillo) y el que se lo lleva (la cola de
/// adelante de un baanillosink). Los demás, aunque sean de audio, tienen colchón.
bool audioConPlazo(GstElement* dueno) {
    if (dueno == nullptr) return false;
    if (GST_IS_AUDIO_AGGREGATOR(dueno) || deFabrica(dueno, "baanillo")) return true;
    // Desde el dueño hacia abajo, hasta otro hilo (una cola, un aggregator) o un sumidero.
    GstElement* e = GST_ELEMENT(gst_object_ref(dueno));
    for (int paso = 0; paso < 8; ++paso) {
        if (deFabrica(e, "baanillosink")) {
            gst_object_unref(e);
            return true;
        }
        GstPad* src = gst_element_get_static_pad(e, "src");
        GstPad* par = src != nullptr ? gst_pad_get_peer(src) : nullptr;
        GstElement* sig = par != nullptr ? gst_pad_get_parent_element(par) : nullptr;
        if (par != nullptr) gst_object_unref(par);
        if (src != nullptr) gst_object_unref(src);
        gst_object_unref(e);
        if (sig == nullptr) return false;
        if (GST_IS_AGGREGATOR(sig) || deFabrica(sig, "queue")) {
            gst_object_unref(sig);
            return false;
        }
        e = sig;
    }
    gst_object_unref(e);
    return false;
}

GstBusSyncReply alMensaje(GstBus*, GstMessage* m, gpointer pipeline) {
    if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_STREAM_STATUS) {
        GstStreamStatusType t;
        GstElement* dueno = nullptr;
        gst_message_parse_stream_status(m, &t, &dueno);
        // Corre en el hilo nuevo (GStreamer reusa los hilos de su pool: cada ENTER lo
        // vuelve a nombrar). «<pipeline>/<elemento>»: en `sample`, en los volcados y en el
        // perfil por hilo se sabe de quién es la CPU. El de un aggregator (glvideomixer,
        // d3d12compositor, audiomixer) es el que junta las capas o las entradas a su hora:
        // el que marca el ritmo.
        if (t == GST_STREAM_STATUS_TYPE_ENTER) {
            ba::platform::nombrarHilo(nombreGst(GST_OBJECT(pipeline), true) + "/" + nombreGst(GST_OBJECT(dueno), false));
            prioridadDelHilo(audioConPlazo(dueno)                             ? ClaseHilo::Audio
                             : dueno != nullptr && GST_IS_AGGREGATOR(dueno) ? ClaseHilo::Critico
                                                                            : ClaseHilo::Medios);
        } else if (t == GST_STREAM_STATUS_TYPE_LEAVE) {
            // El hilo vuelve al pool de GStreamer y puede tocarle video: sin tiempo real.
            ba::platform::soltarTiempoReal();
        }
    }
    return GST_BUS_PASS;
}

} // namespace

void prioridadDelHilo(ClaseHilo c) {
    // Al grupo de medios (todas las clases: son los hilos del motor y sus pipelines).
    // (platform/Nucleos: no hace nada si no se armó)
    ba::platform::nucleos::alGrupoMedios();
    if (!g_activa.load(std::memory_order_relaxed)) return;
    if (c == ClaseHilo::Audio) {
        if (ba::platform::tiempoRealAudio(10)) return;
        // El sistema no dejó (Linux sin RLIMIT_RTPRIO ni RLIMIT_NICE, Windows sin
        // MMCSS): como los que marcan el ritmo. Se avisa una vez.
        static std::atomic<bool> avisado{false};
        if (!avisado.exchange(true))
            registrar(Nivel::Info, traducir("motor", "procesador: los hilos del audio no pueden pasar a tiempo real "
                                                     "(en Linux hace falta RLIMIT_RTPRIO o RLIMIT_NICE)"));
        c = ClaseHilo::Critico;
    }
#if defined(__APPLE__)
    // En macOS solo los Critico suben (QoS interactiva: prefieren los núcleos de
    // rendimiento). Medido en la M1 Pro (2026-10-20, contadores del kernel, misma versión):
    // con QoS interactiva en TODOS los de streaming, ~23,8 mil cambios de contexto por
    // segundo y la CPU variando de 62 a 84 %; sin ella, ~18,9 mil y 59 % estable. Se
    // desplazan entre ellos y esperan a la cola de GL de GStreamer, que queda más abajo (le
    // mandan con dispatch_async y esperan con una condición: macOS no le sube la QoS).
    if (c == ClaseHilo::Critico) pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#elif defined(_WIN32)
    SetThreadPriority(GetCurrentThread(), c == ClaseHilo::Critico  ? THREAD_PRIORITY_HIGHEST
                                          : c == ClaseHilo::Medios ? THREAD_PRIORITY_ABOVE_NORMAL
                                                                   : THREAD_PRIORITY_NORMAL);
#ifdef THREAD_POWER_THROTTLING_CURRENT_VERSION
    // Sin EcoQoS: el ahorro de energía es lo que en un Intel híbrido lo manda a los núcleos E.
    THREAD_POWER_THROTTLING_STATE s{};
    s.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
    s.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
    s.StateMask = 0;
    SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &s, sizeof s);
#endif
#else
    if (c != ClaseHilo::Motor &&
        setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), c == ClaseHilo::Critico ? -10 : -5) != 0) {
        static std::atomic<bool> avisado{false};
        if (!avisado.exchange(true))
            registrar(Nivel::Info, traducir("motor", "procesador: los hilos del video y el audio no pueden subir de prioridad "
                                                     "(hace falta CAP_SYS_NICE o RLIMIT_NICE)"));
    }
#endif
}

void prioridadEnPipeline(GstElement* pipeline) {
    // Siempre, aunque no haya prioridad ni grupo de medios: el nombre de los hilos también
    // sale de acá (prioridadDelHilo no hace nada si los dos están apagados).
    if (pipeline == nullptr || !GST_IS_PIPELINE(pipeline)) return;
    GstBus* bus = gst_element_get_bus(pipeline);
    if (bus == nullptr) return;
    // El pipeline sin referencia: el bus es suyo y el manejador muere con él.
    gst_bus_set_sync_handler(bus, alMensaje, pipeline, nullptr);
    gst_object_unref(bus);
}

void prioridadDelProceso() {
    std::call_once(g_unaVez, []() {
        const bool activa = ajustes::siNo("procesador.prioridad", true);
        g_activa = activa;
        json topo = ba::platform::nucleos::topologia();
        if (activa) {
            // Sin App Nap (macOS) ni EcoQoS (Windows): el ahorro por proceso, el mismo que
            // usan el gateway y los gráficos (platform/Energia).
            ba::platform::sinAhorroDeEnergia();
#if defined(_WIN32)
            SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
#endif
        }
        topo["prioridad"] = activa;
        topo["grupo_medios"] = ajustes::siNo("procesador.mediosJuntos", false) ? ba::platform::nucleos::armarGrupoMedios()
                                                                                : std::string("apagado");
        registrar(Nivel::Info, llenar(activa ? traducir("motor", "procesador: %1; prioridad del video y el audio: sí; video y "
                                                                 "audio juntos: %2")
                                             : traducir("motor", "procesador: %1; prioridad del video y el audio: no; video y "
                                                                 "audio juntos: %2"),
                                      {ba::platform::nucleos::resumen(topo),topo["grupo_medios"].get<std::string>()}));
        std::lock_guard<std::mutex> l(g_topoMu);
        g_topo = std::move(topo);
    });
}

json topologiaCpu() {
    json t;
    {
        std::lock_guard<std::mutex> l(g_topoMu);
        t = g_topo;
    }
    const auto rt = ba::platform::estadoTiempoReal();
    t["tiempo_real"] = {{"hilos", rt.hilos}, {"fallidos", rt.fallidos}, {"modo", rt.modo}};
    return t;
}

} // namespace ba::motor
