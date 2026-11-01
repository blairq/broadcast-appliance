// ============================================================================
// La cámara en dos calidades, y la sala elige cuál viaja (BACapas).
//
// La cámara se publica DOS veces en la misma conexión, con la MISMA pista: la
// capa ALTA (hasta 720p, ~1,5 Mbps) y la BAJA (~180p, 15 cps, ~150 kbps). La
// pantalla compartida va aparte, como siempre. Al ofrecer, la página le dice a
// la sala qué transceptor es cada capa (`capas: {alta: mid, baja: mid}` en la
// oferta) y la sala le contesta cuál prender: {type:"capas", alta, baja}. Lo
// pausado no se codifica ni viaja. Contrato en docs/arquitectura/06-canal-web.md
// §7; el porqué (y por qué no simulcast con rid), en src/gateway/Capas.hpp.
//
// Arranca con las dos prendidas hasta que la sala diga. El tope que pone el
// operador por la red o la grilla (`calidad-*`) es sobre la ALTA: la baja ya es
// el piso.
// ============================================================================
(() => {
  "use strict";

  const ALTA = { alto: 720, bitrate: 1500000, cps: 0 };
  const BAJA = { alto: 180, bitrate: 150000, cps: 15 };

  /// :param pc: la RTCPeerConnection.
  /// :param pista: la pista de video de la cámara.
  /// :param flujo: el MediaStream al que pertenece (msid).
  /// :param recibir: la alta también recibe (por ahí le baja el retorno de la sala).
  function publicar(pc, pista, flujo, recibir) {
    const alto = () => (pista.getSettings && pista.getSettings().height) || ALTA.alto;
    const encoding = (n) => {
      const e = { active: true, maxBitrate: n.bitrate, scaleResolutionDownBy: Math.max(1, alto() / n.alto) };
      if (n.cps) e.maxFramerate = n.cps;
      return e;
    };
    // La alta primero: es el primer video, el que lleva el retorno y el que entiende
    // una sala de antes (que no sabe de capas y toma el primer video como la cámara).
    const alta = pc.addTransceiver(pista, { direction: recibir ? "sendrecv" : "sendonly", streams: [flujo], sendEncodings: [encoding(ALTA)] });
    const baja = pc.addTransceiver(pista, { direction: "sendonly", streams: [flujo], sendEncodings: [encoding(BAJA)] });
    let activas = { alta: true, baja: true };
    let tope = null;
    let cola = Promise.resolve(); // setParameters de a uno (dos a la vez, el segundo falla)

    function poner(t, n, activa) {
      cola = cola.then(async () => {
        const p = t.sender.getParameters();
        if (!p.encodings || !p.encodings.length) p.encodings = [{}];
        const e = p.encodings[0];
        const q = encoding(n);
        e.active = activa;
        e.maxBitrate = q.maxBitrate;
        e.scaleResolutionDownBy = q.scaleResolutionDownBy;
        if (q.maxFramerate) e.maxFramerate = q.maxFramerate; else delete e.maxFramerate;
        try { await t.sender.setParameters(p); } catch (err) { /* todavía sin negociar: se repone al conectar */ }
      });
      return cola;
    }
    function nivelAlta() {
      // El tope achica la alta, nunca la agranda.
      return tope && tope.alto < ALTA.alto ? { alto: tope.alto, bitrate: Math.min(tope.bitrate, ALTA.bitrate), cps: tope.cps || 0 } : ALTA;
    }
    function aplicar() {
      poner(alta, nivelAlta(), activas.alta);
      return poner(baja, BAJA, activas.baja);
    }

    return {
      alta, baja,
      /// Para la oferta: {alta: mid, baja: mid} (después de setLocalDescription).
      mids() { return alta.mid && baja.mid ? { alta: alta.mid, baja: baja.mid } : undefined; },
      /// La sala pidió qué capas prender ({type:"capas", alta, baja}).
      pedido(m) { activas = { alta: !!m.alta, baja: !!m.baja }; return aplicar(); },
      /// El tope de la alta ({alto, bitrate, cps} o null = sin tope).
      topar(n) { tope = n || null; return aplicar(); },
      /// Conexión recién conectada: el codificador arranca de fábrica.
      reponer: aplicar,
      activas() { return { alta: activas.alta, baja: activas.baja }; },
    };
  }

  window.BACapas = { publicar, ALTA, BAJA };
})();
