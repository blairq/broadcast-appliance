/* ============================================================================
   live.js — Video en vivo de los participantes para las vistas web
   (consola del operador y tablero de oradores; el mismo archivo en las dos).

   B reenvia los paquetes de video TAL COMO los manda el invitado (relay estilo
   SFU, sin re-codificar): el invitado sube una sola vez, y cada vista abre su
   propia PeerConnection recvonly contra B. Senalizacion en un solo pedido
   (sin goteo): offer con candidatos -> answer con candidatos.

   Uso:
     const live = BALive.create({ endpoint: "/api/v1/view", headers: {...} });
     el.append(live.video(participantId, "fila")); // <video> por lugar (se reusa
                                                    // entre renders; una sola
                                                    // conexion por participante)
     live.keep(new Set([...ids]));             // cierra lo que ya no se muestra
   ========================================================================== */
(function () {
  "use strict";

  function gathered(pc, ms) {
    if (pc.iceGatheringState === "complete") return Promise.resolve();
    return new Promise((resolve) => {
      const t = setTimeout(resolve, ms);
      pc.addEventListener("icegatheringstatechange", () => {
        if (pc.iceGatheringState === "complete") { clearTimeout(t); resolve(); }
      });
    });
  }

  function create(opts) {
    const endpoint = opts.endpoint;
    const headers = Object.assign({ "Content-Type": "application/json" }, opts.headers || {});
    const entries = new Map(); // id -> {videos, stream, pc, viewer, frames, stalls}
    const failedAt = new Map(); // id -> ms (no reintentar enseguida)

    function video(id, place) {
      let e = entries.get(id);
      if (!e) {
        e = { videos: new Map(), stream: null, pc: null, viewer: 0, frames: -1, stalls: 0, opening: false };
        entries.set(id, e);
      }
      const key = place || "";
      let v = e.videos.get(key);
      if (!v) {
        v = document.createElement("video");
        v.muted = true;
        v.autoplay = true;
        v.playsInline = true;
        v.className = "live";
        if (e.stream) v.srcObject = e.stream;
        e.videos.set(key, v);
      }
      if (!e.pc && !e.opening && Date.now() - (failedAt.get(id) || 0) > 8000) open(id, e);
      // Moverlo de lugar en el DOM no debe dejarlo en pausa.
      if (v.paused && v.srcObject) setTimeout(() => v.play().catch(() => {}), 0);
      return v;
    }

    async function open(id, e) {
      e.opening = true;
      if (window.BAIce) await BAIce.listo().catch(() => {});
      const pc = new RTCPeerConnection(window.BAIce ? BAIce.config() : { iceServers: [] });
      e.pc = pc;
      try {
        pc.addTransceiver("video", { direction: "recvonly" });
        pc.ontrack = (ev) => {
          e.stream = ev.streams[0] || new MediaStream([ev.track]);
          for (const v of e.videos.values()) {
            v.srcObject = e.stream;
            v.play().catch(() => {});
          }
        };
        pc.onconnectionstatechange = () => {
          if (pc.connectionState === "failed" || pc.connectionState === "closed") close(id, true);
        };
        await pc.setLocalDescription(await pc.createOffer());
        await gathered(pc, 1500);
        const r = await fetch(endpoint, {
          method: "POST",
          credentials: "same-origin",
          headers,
          body: JSON.stringify({ participant: id, sdp: pc.localDescription.sdp }),
        });
        const j = await r.json().catch(() => ({}));
        if (!r.ok || !j.ok) throw new Error(j.error || String(r.status));
        if (e.pc !== pc) { // se cerro mientras tanto
          fetch(`${endpoint}/${j.viewer}`, { method: "DELETE", credentials: "same-origin", headers }).catch(() => {});
          return;
        }
        e.viewer = j.viewer;
        await pc.setRemoteDescription({ type: "answer", sdp: j.sdp });
      } catch (err) {
        close(id, true);
      } finally {
        e.opening = false;
      }
    }

    function close(id, failed) {
      const e = entries.get(id);
      if (!e) return;
      if (failed) failedAt.set(id, Date.now());
      const pc = e.pc;
      e.pc = null;
      if (pc) { try { pc.close(); } catch (x) {} }
      if (e.viewer) {
        fetch(`${endpoint}/${e.viewer}`, { method: "DELETE", credentials: "same-origin", headers }).catch(() => {});
        e.viewer = 0;
      }
      e.frames = -1;
      e.stream = null;
      if (!failed) entries.delete(id);
    }

    // Lo que no se muestra mas se cierra (libera la sesion en B).
    function keep(ids) {
      for (const id of [...entries.keys()]) if (!ids.has(id)) close(id, false);
    }

    // Vigia: si una vista conectada deja de decodificar, se pide un keyframe
    // (B lo pide al invitado); si sigue muerta, se reabre.
    setInterval(async () => {
      for (const [id, e] of entries) {
        if (!e.pc || !e.viewer || e.pc.connectionState !== "connected") continue;
        let decoded = 0;
        try {
          const stats = await e.pc.getStats();
          stats.forEach((s) => { if (s.type === "inbound-rtp" && s.kind === "video") decoded = s.framesDecoded || 0; });
        } catch (x) { continue; }
        if (decoded === e.frames) {
          e.stalls++;
          if (e.stalls === 1 || e.stalls === 3) {
            fetch(`${endpoint}/${e.viewer}/keyframe`, { method: "POST", credentials: "same-origin", headers }).catch(() => {});
          } else if (e.stalls >= 6) {
            close(id, true); // se reabre en el proximo render
          }
        } else {
          e.stalls = 0;
        }
        e.frames = decoded;
      }
    }, 2000);

    window.addEventListener("pagehide", () => { for (const id of [...entries.keys()]) close(id, false); });
    return { video, keep, close: (id) => close(id, false) };
  }

  window.BALive = { create };
})();
