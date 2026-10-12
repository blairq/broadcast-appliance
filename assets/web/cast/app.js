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
  // Una escena que no es el programa: el canal que le dio la app (vacío: el
  // programa). Va también al reengancharse, para seguir en la misma escena.
  const canal = /^\d{1,2}$/.test(q.get("canal") || "") ? q.get("canal") : "";
  const conAudio = q.get("audio") !== "0";
  // 0 por defecto: lo menos que el navegador acepte retener. Era 80 ms, que
  // en una red cableada ya es holgado, pero «holgado» no es gratis: son 80 ms
  // que la pantalla va atras de la sala sin que nada este fallando. Con
  // ?buffer=N se sube si la red del lugar lo pide (wifi cargado, un tele que
  // entrecorta).
  const bufferMs = Math.max(0, parseInt(q.get("buffer") || "0", 10) || 0);
  const hud = q.get("hud") === "1";

  const st = {
    ws: null,
    pc: null,
    reintento: 0,
    cerrado: false,
    ultimoCuadro: -1,
    quieto: 0,
    medida: null,
    retardoPuesto: null,
    id: "",
    key: "",
  };

  // La sesion sobrevive a una recarga de la pagina: un televisor se recarga
  // solo mas seguido de lo que uno cree (memoria, el propio DashCast), y sin
  // esto cada recarga gastaria un token que ya no tiene.
  const SESION = "ba-pantalla-sesion";
  function guardarSesion() {
    try { sessionStorage.setItem(SESION, JSON.stringify({ id: st.id, key: st.key })); } catch (e) {}
  }
  (() => {
    try {
      const v = JSON.parse(sessionStorage.getItem(SESION) || "null");
      if (v && v.id) { st.id = v.id; st.key = v.key || ""; }
    } catch (e) {}
  })();

  function avisar(texto, pie) {
    $("aviso-texto").textContent = texto;
    $("aviso-pie").textContent = pie || "";
    $("aviso").classList.remove("oculto");
  }
  function alAire() { $("aviso").classList.add("oculto"); }

  // --- Señalización ---------------------------------------------------------

  function enviar(m) {
    if (st.ws && st.ws.readyState === WebSocket.OPEN) st.ws.send(window.BATrama.encode(m));
  }

  function conectar() {
    if (st.cerrado) return;
    const proto = location.protocol === "https:" ? "wss" : "ws";
    // Reenganche si ya tenemos sesión: el servidor nos devuelve la misma y no
    // aparecemos dos veces en la sala.
    // Siempre con token: una pantalla no se "reengancha" porque no tiene nada
    // que retomar (no hay sesión en la sala, no hay lugar guardado).
    // El token de ingreso es de UN SOLO USO. Despues de entrar la primera vez
    // se vuelve con el id y la clave que mando el servidor en el "hello": si
    // no, un corte de un segundo dejaria al televisor afuera para siempre.
    const args = (st.id
      ? `pantalla=1&resume=${encodeURIComponent(st.id)}&key=${encodeURIComponent(st.key)}`
      : `pantalla=1&token=${encodeURIComponent(token)}`) + (canal ? `&canal=${canal}` : "");
    const ws = new WebSocket(`${proto}://${location.host}/ws?${args}`);
    // Los mensajes del canal van envueltos en un frame binario (ver
    // ../comun/trama.js): sin esto llegan como Blob y no se leen.
    ws.binaryType = "arraybuffer";
    st.ws = ws;

    let abrio = false;
    ws.onopen = () => { abrio = true; st.reintento = 0; };

    ws.onmessage = (ev) => {
      const m = window.BATrama.decode(ev.data);
      if (!m) return;
      if (m.type === "hello") {
        if (m.id) { st.id = m.id; st.key = m.key || ""; guardarSesion(); }
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
      // Ni siquiera llego a abrir con un reenganche: esa sesion ya no existe
      // (se reinicio el gateway, o se vencio). Se tira y se prueba con el
      // token, que es lo unico que queda.
      if (!abrio && st.id) {
        st.id = "";
        st.key = "";
        guardarSesion();
      }
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
    const pc = new RTCPeerConnection(window.BAIce ? BAIce.config() : { iceServers: [] }); // desde afuera, por el TURN de la caja
    st.pc = pc;

    // Solo recibir, y el servidor lo VERIFICA: si el offer ofreciera subir
    // algo, corta la conexión. No se pide cámara ni micrófono (no hay, y
    // pedirlos abriría un permiso que en un televisor nadie puede contestar).
    const vt = pc.addTransceiver("video", { direction: "recvonly" });
    const at = pc.addTransceiver("audio", { direction: "recvonly" });

    // La perilla del retardo (ver ../comun/retardo.js).
    st.retardoPuesto = window.BARetardo.aplicar(vt.receiver, bufferMs);
    window.BARetardo.aplicar(at.receiver, bufferMs);

    pc.ontrack = (ev) => {
      // De nuevo: segun la version, el valor puesto en el transceiver se
      // pierde cuando llega el track de verdad.
      const puesto = window.BARetardo.aTodos(pc, bufferMs);
      if (puesto !== null) st.retardoPuesto = puesto;
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

    // Las medidas se toman SIEMPRE, no solo con ?hud=1: son las que viajan por
    // el canal de control, y el retardo de una pantalla de sala hay que poder
    // verlo desde la cabina sin ir hasta el televisor a mirarle la esquina.
    if (!st.pc) return;
    try {
      const stats = await st.pc.getStats();
      let txt = "";
      // `null` y `0` NO son lo mismo y confundirlos manda a buscar del lado
      // equivocado: un televisor que no informa una medida se lee igual que
      // uno que la informa en cero. Lo que el navegador no diga queda en null
      // y se muestra como «?».
      const m = { fps: 0, buffer: null, jitter: 0, perdidos: 0, rtt: null, proceso: null };
      const prom = (total, cuenta) =>
          typeof total === "number" && typeof cuenta === "number" && cuenta > 0
              ? Math.round(1000 * total / cuenta)
              : null;
      stats.forEach((r) => {
        if (r.type === "candidate-pair" && r.nominated && typeof r.currentRoundTripTime === "number")
          m.rtt = Math.round(r.currentRoundTripTime * 1000);
        if (r.type !== "inbound-rtp") return;
        const buffer = prom(r.jitterBufferDelay, r.jitterBufferEmittedCount);
        if (r.kind === "video") {
          m.fps = r.framesPerSecond || 0;
          m.buffer = buffer;
          m.jitter = Math.round((r.jitter || 0) * 1000);
          m.perdidos = r.packetsLost || 0;
          // Lo que tarda ESTE aparato desde que le llega el paquete hasta que
          // tiene el cuadro listo: decodificar mas lo suyo. Es el unico
          // numero que separa «llega tarde» de «tarda en mostrarlo», y en un
          // televisor la diferencia es toda la diferencia.
          m.proceso = prom(r.totalProcessingDelay, r.framesDecoded);
        }
        txt += `${r.kind}  ${r.framesPerSecond || 0} fps  perdidos ${r.packetsLost || 0}\n` +
               `  buffer ${buffer === null ? "?" : buffer} ms` +
               `  jitter ${((r.jitter || 0) * 1000).toFixed(0)} ms\n`;
      });
      st.medida = m;
      if (hud) {
        $("hud").textContent = (txt || "sin datos") +
            `ida y vuelta ${m.rtt === null ? "?" : m.rtt} ms\n` +
            `proceso del aparato ${m.proceso === null ? "?" : m.proceso} ms\n`;
        $("hud").classList.remove("oculto");
      }
    } catch (e) { /* getStats puede no estar en un televisor viejo */ }
  }, 1000);

  if (!token) {
    avisar("Falta el token", "Esta pantalla se abre desde la app, no a mano");
    return;
  }
  // El canal de control: dice en qué estado está (conectando, sin imagen, al
  // aire, cuántos cuadros) y acepta que le cambien lo que muestra. Sin esto,
  // desde la cabina no hay forma de saber si esta pantalla está al aire o
  // colgada en «conectando» —que es justo lo que pasó—.
  if (window.BACanal) {
    window.BACanal.abrir({
      // Con canal es una escena (no el programa): así figura en las superficies.
      tipo: canal ? "escena" : "programa",
      que: () => {
        const cs = st.pc ? st.pc.connectionState : "sin conexión";
        const v = $("video");
        const q = v.getVideoPlaybackQuality ? v.getVideoPlaybackQuality().totalVideoFrames : 0;
        if (st.cerrado) return "cerrado";
        const m = st.medida;
        // El retardo se dice SIEMPRE que se sepa: «al aire» sin un número al
        // lado no sirve para decidir si la pantalla va con la sala o va atrás.
        // Se dice si la perilla del retardo EXISTE en este navegador: un
        // televisor viejo que no la conoce se ve igual que uno que la tiene
        // puesta, y eso hace perder horas buscando del lado equivocado.
        const perilla = st.retardoPuesto === null
            ? " · SIN perilla de retardo (navegador viejo)"
            : " · " + st.retardoPuesto + "=" + bufferMs + " ms";
        const n = (v, u) => (v === null ? "?" : v + " " + u);
        const como = m ? " · " + m.fps + " fps · buffer " + n(m.buffer, "ms") +
                         " · proceso " + n(m.proceso, "ms") +
                         " · ida y vuelta " + n(m.rtt, "ms") +
                         (m.perdidos > 0 ? " · " + m.perdidos + " perdidos" : "") + perilla
                       : "";
        if (q > 0) return "al aire · " + q + " cuadros · " + cs + como;
        return "sin imagen · " + cs + como;
      },
      onMostrar: (url) => { location.href = url; },
    });
  }

  avisar("Conectando…");
  conectar();
})();
