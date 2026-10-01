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
    /// :returns: el nombre de la perilla que el navegador SÍ tiene
    ///           ("jitterBufferTarget" | "playoutDelayHint"), o null si no
    ///           tiene ninguna.
    aplicar(r, ms) {
      if (!r) return null;
      // OJO CON COMO SE PREGUNTA. Asignarle a un objeto una propiedad que no
      // conoce no falla: JavaScript se la inventa, y leerla después devuelve
      // el valor que uno mismo puso. O sea que «asigno y leo de vuelta» le da
      // que sí a CUALQUIER navegador, incluso a uno que no tiene la perilla.
      // Eso hizo dar por cerrado un tema que estaba abierto. Hay que
      // preguntar por la cadena de prototipos ANTES de escribir, que es lo
      // único que distingue una perilla de verdad de una inventada.
      let cual = null;
      if ("jitterBufferTarget" in r) {
        try { r.jitterBufferTarget = ms; cual = "jitterBufferTarget"; } catch (e) {}
      }
      if ("playoutDelayHint" in r) {
        try { r.playoutDelayHint = ms / 1000; if (!cual) cual = "playoutDelayHint"; } catch (e) {}
      }
      return cual;
    },

    /// A todos los receivers de una PeerConnection. Devuelve el nombre de la
    /// perilla que se pudo usar, o null si este navegador no tiene ninguna.
    aTodos(pc, ms) {
      let cual = null;
      if (!pc || !pc.getReceivers) return null;
      for (const r of pc.getReceivers()) {
        const v = this.aplicar(r, ms);
        if (v !== null) cual = v;
      }
      return cual;
    },
  };
})();
