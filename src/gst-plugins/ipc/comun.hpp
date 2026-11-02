// ============================================================================
// src/gst-plugins/ipc/comun.hpp — el lector común de los anillos: UN hilo por medio
//
// baanillo y bavideoanillo tienen cada uno su hilo (GstPushSrc): con 40 lugares en el
// cupo eran 80 hilos que se despertaban cada uno con su canal (y una cola atrás de cada
// uno, otros 80), y los cambios de contexto crecían con los invitados. Acá, el modelo
// «tirar»: UN hilo para todo el audio y UNO para todo el video recorren en cada vuelta
// los anillos que se están leyendo y empujan lo nuevo de cada uno por su elemento
// (baanillocomun, bavideoanillocomun: sin hilo propio). Las vueltas las marca el medio,
// no la cantidad de invitados:
//   - audio: cada 5 ms (bloques de 10 ms: el que llega espera a lo sumo 5 ms; el
//     mezclador lo espera bastante más, su latencia);
//   - video: cada 1/60 s (lo que entra a un lienzo de 30 sale a tiempo).
//
// Lo que empuja una vuelta corre ENTERO en este hilo: la cadena de cada entrada (la
// conversión, el rack, la subida a la GPU) hasta su salida. Por eso solo lo usa una
// cadena que no se traba (la del motor: termina en intersink/baentrega, sin esperar al
// reloj; los filtros de video van detrás de una cola). Una cadena armada a mano con
// bavideoanillo/baanillo sigue con su hilo propio. Lo que sí podía trabar: el pad de un
// mezclador lleno (lo de una entrada adelantada), que hacía esperar a gst_pad_push; batoma
// no lo empuja (adelantado.hpp). Y cada lectura entrega como mucho un anillo por vuelta.
//
// Concurrencia: cada Lectura tiene su candado; el hilo lo toma para leerla y empujar.
// quitar() (al salir de PLAYING) la saca de la lista y la apaga SIN esperar: el hilo
// puede estar empujando justo a una cadena que ya está en pausa (un sink que espera el
// preroll), y eso se destraba recién cuando el pipeline baja a READY (los sinks bajan
// antes que las fuentes y sueltan). esperar() (al bajar a READY y al destruirla) toma el
// candado: al volver, el hilo ya no la toca. Antes de empujar, el lector mira que el
// pipeline esté en PLAYING y vaya a seguir ahí (corriendo()).
// ============================================================================
#pragma once

#include <gst/gst.h>

#include <atomic>
#include <memory>
#include <mutex>

namespace ba::ipc_gst {

class Lectura {
   public:
    virtual ~Lectura() = default;
    /// En el hilo del lector, con `mu` tomado y la lectura activa: lo nuevo del canal, empujado.
    virtual void leer() = 0;
    std::mutex mu;
    std::atomic<bool> activa{false}; ///< registrada (el hilo la lee si sigue así con mu tomado)
};

/// ¿`e` (una fuente) está en PLAYING y su pipeline no lo están bajando? Al subir, el
/// pipeline pasa primero a lo de abajo (los sinks) y la fuente es la última: con ella en
/// PLAYING, lo de abajo ya anda. NO se mira el estado del pipeline entero: con un sink que
/// espera su primer buffer (intersink) sigue en PAUSED hasta que la fuente empuje. Al
/// bajar, el pipeline cambia su DESTINO antes de tocar a sus elementos: con eso no se le
/// empuja a una cadena que ya está en pausa.
inline bool corriendo(GstElement* e) {
    GST_OBJECT_LOCK(e);
    const bool anda = GST_STATE(e) == GST_STATE_PLAYING;
    GST_OBJECT_UNLOCK(e);
    if (!anda) return false;
    GstObject* raiz = GST_OBJECT(e);
    while (GST_OBJECT_PARENT(raiz) != nullptr) raiz = GST_OBJECT_PARENT(raiz);
    GST_OBJECT_LOCK(raiz);
    const bool sigue = GST_STATE_TARGET(raiz) == GST_STATE_PLAYING;
    GST_OBJECT_UNLOCK(raiz);
    return sigue;
}

enum class Medio { Audio, Video };

/// Desde PLAYING: el hilo del medio la lee en cada vuelta.
void registrar(Medio m, const std::shared_ptr<Lectura>& l);
/// Al salir de PLAYING: el hilo deja de leerla (sin esperar a que termine la vuelta).
void quitar(Medio m, const std::shared_ptr<Lectura>& l);
/// Al bajar a READY o al destruirla: espera a que el hilo la suelte; después no la toca.
inline void esperar(Lectura& l) { std::lock_guard<std::mutex> g(l.mu); }

} // namespace ba::ipc_gst
