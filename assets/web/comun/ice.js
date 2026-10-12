// assets/web/comun/ice.js — los servidores ICE que da la caja (TURN propio)
//
// En la red de la sala WebRTC anda directo (candidatos host) y esto no cambia
// nada: el gateway contesta iceServers vacío. Entrando desde afuera (pinggy)
// contesta el TURN de la caja con credenciales efímeras y política "relay"
// (docs/funciones/TURN.md). Cada página lo usa así:
//
//   const pc = new RTCPeerConnection(window.BAIce ? BAIce.config() : { iceServers: [] });
//   // o, donde se puede esperar:  await BAIce.listo();  antes de crearla
//
// GET /api/ice → {"iceServers":[…], "politica":"all"|"relay", "ttl":segundos}.
// Si falla (o no existe la ruta, o un gateway viejo) queda [] como siempre.
(function () {
  "use strict";
  let actual = { iceServers: [] };
  let vence = 0;          // ms; 0 = todavía no se pidió
  let pendiente = null;
  let renovar = 0;

  function pedir() {
    if (pendiente) return pendiente;
    const ctl = typeof AbortController !== "undefined" ? new AbortController() : null;
    const corte = ctl ? setTimeout(() => ctl.abort(), 4000) : 0;
    pendiente = fetch("/api/ice", { credentials: "same-origin", cache: "no-store", signal: ctl ? ctl.signal : undefined })
      .then((r) => (r.ok ? r.json() : null))
      .then((j) => {
        const lista = j && Array.isArray(j.iceServers) ? j.iceServers : [];
        actual = { iceServers: lista };
        if (j && j.politica === "relay" && lista.length) actual.iceTransportPolicy = "relay";
        const ttl = (j && Number(j.ttl)) || 0;
        // Se renueva a mitad de vida: una credencial vencida no consigue relay.
        vence = Date.now() + (ttl > 0 ? ttl * 500 : 10 * 60 * 1000);
        clearTimeout(renovar);
        if (ttl > 0) renovar = setTimeout(pedir, ttl * 500);
      })
      .catch(() => { vence = Date.now() + 30 * 1000; }) // sin ruta / sin red: lo de siempre, y se reintenta
      .finally(() => { clearTimeout(corte); pendiente = null; });
    return pendiente;
  }

  window.BAIce = {
    /// Configuración para RTCPeerConnection (sincrónica: lo último que llegó).
    config() {
      if (Date.now() > vence) pedir();
      const c = { iceServers: actual.iceServers.slice() };
      if (actual.iceTransportPolicy) c.iceTransportPolicy = actual.iceTransportPolicy;
      return c;
    },
    /// Espera la respuesta si hace falta (con tope de 4 s) y devuelve config().
    async listo() {
      if (Date.now() > vence) await pedir();
      return this.config();
    },
  };
  pedir();
})();
