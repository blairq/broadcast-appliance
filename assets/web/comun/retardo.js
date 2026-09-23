// ============================================================================
// La perilla del retardo de reproducción, en un solo lugar.
//
// Un navegador retiene lo que le llega antes de mostrarlo (el jitter buffer),
// pensado para que una videollamada no se entrecorte cuando la red hipa. Acá
// la prioridad es la otra: que lo que se ve vaya con lo que pasa en la sala.
//
// DOS NOMBRES PARA LO MISMO, y hay que poner los dos:
//   - `jitterBufferTarget`, el estándar nuevo, en MILISEGUNDOS.
//   - `playoutDelayHint`, el viejo de Chrome, en SEGUNDOS.
// Adentro de un televisor suele haber un Chrome de hace años, así que el que
// sobra se ignora solo y el que sirve pega.
//
// SE VUELVE A PONER EN `ontrack`. Poner la perilla en el transceiver antes de
// negociar no siempre queda: según la versión, el valor se pierde cuando llega
// el track de verdad. Ponerlo dos veces no cuesta nada y ahorra el caso en que
// parece configurado y no lo está.
//
// SE LEE DE VUELTA. `aplicar` devuelve lo que el navegador dice tener después
// de asignarlo: si devuelve null, esa versión no conoce la perilla y no hay
// nada que hacer del lado del cliente. Es un dato que hay que poder ver desde
// la cabina, no una suposición.
// ============================================================================
(() => {
  "use strict";

  window.BARetardo = {
    /// :param r: un RTCRtpReceiver.
    /// :param ms: milisegundos a retener (0 = lo menos posible).
    /// :returns: lo que quedó puesto, o null si el navegador no la conoce.
    aplicar(r, ms) {
      if (!r) return null;
      try { r.jitterBufferTarget = ms; } catch (e) {}
      try { r.playoutDelayHint = ms / 1000; } catch (e) {}
      try {
        if (typeof r.jitterBufferTarget === "number") return r.jitterBufferTarget;
        if (typeof r.playoutDelayHint === "number") return r.playoutDelayHint * 1000;
      } catch (e) {}
      return null;
    },

    /// A todos los receivers de una PeerConnection. Devuelve lo que quedó
    /// puesto en el último que la conocía (null si ninguno).
    aTodos(pc, ms) {
      let puesto = null;
      if (!pc || !pc.getReceivers) return null;
      for (const r of pc.getReceivers()) {
        const v = this.aplicar(r, ms);
        if (v !== null) puesto = v;
      }
      return puesto;
    },
  };
})();
