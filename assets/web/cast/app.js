// ============================================================================
// El programa en vivo, en una pantalla. Ver index.html para el por qué.
//
// COMO ENTRA. Por /ws?pantalla=1 con un token de ingreso. NO es un invitado:
// no tiene nombre, no aparece en la lista de la sala, no pasa por el lobby y
// no puede escribir en el chat.
//
// Y no puede mandar audio ni video. Eso no depende de que esta página se porte
// bien —el token viaja hasta el televisor y lo puede tener cualquiera—: el
// servidor le mira el SDP al offer y le corta la conexión si ofrece subir algo
// (ver SignalingServer::soloRecibe). Lo que sigue es para que esta página sea
// coherente con eso, no para que sea seguro.
//
// EL RETARDO. WebRTC ya es el camino de baja latencia, pero el navegador por
// las suyas guarda un colchón (jitter buffer) generoso, pensado para que una
// videollamada no se entrecorte. Acá la prioridad es la otra: que lo que se ve
// en la pantalla vaya con lo que pasa en la sala. `jitterBufferTarget` (antes
// `playoutDelayHint`) es la perilla: cuántos milisegundos retener antes de
// mostrar. Se puede fijar con ?buffer=N; por defecto 80 ms, que en una red
// cableada es holgado y en wifi cargado todavía aguanta.
//
// EL AUDIO. Un televisor no tiene a nadie que haga clic, y los navegadores no
// dejan sonar audio sin un gesto. Por eso el VIDEO arranca en mudo (siempre
// anda) y el audio se intenta aparte: si el navegador lo traba, se reintenta
// solo. Con ?audio=0 ni se intenta, que es lo que conviene cuando el sonido de
// la sala sale por los parlantes y no por el televisor.
// ============================================================================
(() => {
  "use strict";

  const $ = (id) => document.getElementById(id);
  const q = new URLSearchParams(location.search);
  const token = q.get("token") || "";
  const nombre = q.get("nombre") || "Pantalla";
  const conAudio = q.get("audio") !== "0";
  const bufferMs = Math.max(0, parseInt(q.get("buffer") || "80", 10) || 0);
  const hud = q.get("hud") === "1";

  const st = {
    ws: null,
    pc: null,
    reintento: 0,
    cerrado: false,
    ultimoCuadro: -1,
    quieto: 0,
  };

  function avisar(texto, pie) {
    $("aviso-texto").textContent = texto;
    $("aviso-pie").textContent = pie || "";
    $("aviso").classList.remove("oculto");
  }
  function alAire() { $("aviso").classList.add("oculto"); }

  // --- Señalización ---------------------------------------------------------

  function enviar(m) {
    if (st.ws && st.ws.readyState === WebSocket.OPEN) st.ws.send(JSON.stringify(m));
  }

  function conectar() {
    if (st.cerrado) return;
    const proto = location.protocol === "https:" ? "wss" : "ws";
    // Reenganche si ya tenemos sesión: el servidor nos devuelve la misma y no
    // aparecemos dos veces en la sala.
    // Siempre con token: una pantalla no se "reengancha" porque no tiene nada
    // que retomar (no hay sesión en la sala, no hay lugar guardado).
    const args = `pantalla=1&token=${encodeURIComponent(token)}`;
    const ws = new WebSocket(`${proto}://${location.host}/ws?${args}`);
    st.ws = ws;

    ws.onopen = () => { st.reintento = 0; };

    ws.onmessage = (ev) => {
      let m;
      try { m = JSON.parse(ev.data); } catch (e) { return; }
      if (m.type === "hello") {
        avisar("Esperando el programa…", nombre);
        armarPeer();
      } else if (m.type === "answer" && st.pc) {
        st.pc.setRemoteDescription({ type: "answer", sdp: m.sdp }).catch(() => {});
      } else if (m.type === "ice" && st.pc && m.candidate) {
        st.pc.addIceCandidate(m.candidate).catch(() => {});
      } else if (m.type === "rejected") {
        // No se reintenta en loop: se dice por qué y se queda quieta.
        st.cerrado = true;
        if (m.reason === "solo_mirar") {
          // No debería pasar nunca desde acá: es el servidor diciendo que el
          // offer ofrecía subir algo. Si aparece, es que esta página cambió.
          avisar("El servidor me cortó", "Esta pantalla solo puede mirar");
        } else {
          avisar("La sala no me dejó entrar", m.reason === "locked" ? "Está cerrada" : "No hay lugar");
        }
      } else if (m.type === "busy") {
        avisar("No hay lugar para otra pantalla", "Probá cerrando alguna");
      }
    };

    ws.onclose = () => {
      if (st.cerrado) return;
      // Reintento con espera creciente, con tope: un televisor puede quedar
      // días prendido y no tiene sentido que martille la red.
      st.reintento = Math.min(st.reintento + 1, 6);
      const espera = Math.min(1000 * 2 ** st.reintento, 30000);
      avisar("Reconectando…", `En ${Math.round(espera / 1000)} s`);
      setTimeout(conectar, espera);
    };
  }

  // --- Medios ---------------------------------------------------------------

  function armarPeer() {
    if (st.pc) { try { st.pc.close(); } catch (e) {} }
    const pc = new RTCPeerConnection();
    st.pc = pc;

    // Solo recibir, y el servidor lo VERIFICA: si el offer ofreciera subir
    // algo, corta la conexión. No se pide cámara ni micrófono (no hay, y
    // pedirlos abriría un permiso que en un televisor nadie puede contestar).
    const vt = pc.addTransceiver("video", { direction: "recvonly" });
    const at = pc.addTransceiver("audio", { direction: "recvonly" });

    // La perilla del retardo. Se pone en los dos sentidos porque el nombre
    // cambió: `jitterBufferTarget` es el estándar nuevo, `playoutDelayHint` lo
    // que entienden los Chrome de antes — y adentro de un televisor suele
    // haber un Chrome de antes.
    const buffer = (r) => {
      try { r.jitterBufferTarget = bufferMs; } catch (e) {}
      try { r.playoutDelayHint = bufferMs / 1000; } catch (e) {}
    };
    buffer(vt.receiver);
    buffer(at.receiver);

    pc.ontrack = (ev) => {
      const s = ev.streams[0] || new MediaStream([ev.track]);
      if (ev.track.kind === "video") {
        $("video").srcObject = s;
        $("video").play().catch(() => {});
      } else if (conAudio) {
        $("audio").srcObject = s;
        sonar();
      }
    };

    pc.onicecandidate = (ev) => { if (ev.candidate) enviar({ type: "ice", candidate: ev.candidate.toJSON() }); };

    pc.onconnectionstatechange = () => {
      if (st.pc !== pc) return;
      if (pc.connectionState === "connected") return; // el aviso lo saca el primer cuadro
      if (pc.connectionState === "failed") {
        avisar("Se cortó el video", "Reconectando…");
        setTimeout(() => { if (st.pc === pc && !st.cerrado) armarPeer(); }, 1500);
      }
    };

    negociar(pc);
  }

  async function negociar(pc) {
    try {
      const offer = await pc.createOffer();
      await pc.setLocalDescription(offer);
      enviar({ type: "offer", sdp: offer.sdp });
    } catch (e) {
      setTimeout(() => { if (st.pc === pc && !st.cerrado) armarPeer(); }, 2000);
    }
  }

  // El audio se intenta y se reintenta: si el navegador lo traba por no haber
  // habido un gesto, a veces se destraba solo más adelante.
  function sonar() {
    const a = $("audio");
    a.muted = false;
    const p = a.play();
    if (p && p.catch) p.catch(() => setTimeout(sonar, 3000));
  }

  // --- Vigilancia -----------------------------------------------------------
  // Que la conexión diga "connected" no quiere decir que se vea algo: puede
  // estar llegando todo negro. Lo que importa son los CUADROS.
  setInterval(async () => {
    const v = $("video");
    const cuadros = v.getVideoPlaybackQuality ? v.getVideoPlaybackQuality().totalVideoFrames : v.webkitDecodedFrameCount;
    if (typeof cuadros === "number" && cuadros > 0 && cuadros !== st.ultimoCuadro) {
      st.ultimoCuadro = cuadros;
      st.quieto = 0;
      alAire();
    } else if (st.pc && st.pc.connectionState === "connected") {
      // Diez segundos sin un cuadro nuevo con la conexión "sana": se rearma.
      if (++st.quieto >= 10) {
        st.quieto = 0;
        avisar("Sin imagen", "Reintentando…");
        armarPeer();
      }
    }

    if (!hud || !st.pc) return;
    try {
      const stats = await st.pc.getStats();
      let txt = "";
      stats.forEach((r) => {
        if (r.type !== "inbound-rtp") return;
        txt += `${r.kind}  ${r.framesPerSecond || 0} fps  perdidos ${r.packetsLost || 0}\n` +
               `  buffer ${(1000 * (r.jitterBufferDelay || 0) / (r.jitterBufferEmittedCount || 1)).toFixed(0)} ms` +
               `  jitter ${((r.jitter || 0) * 1000).toFixed(0)} ms\n`;
      });
      $("hud").textContent = txt || "sin datos";
      $("hud").classList.remove("oculto");
    } catch (e) { /* getStats puede no estar en un televisor viejo */ }
  }, 1000);

  if (!token) {
    avisar("Falta el token", "Esta pantalla se abre desde la app, no a mano");
    return;
  }
  avisar("Conectando…");
  conectar();
})();
