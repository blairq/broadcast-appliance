/* ============================================================================
   Web/guest/app.js — App web del invitado (spec §8.3)

   Flujo:
     1. join(): POST /api/join {room, pin} -> token de un solo uso.
     2. Dispositivos: getUserMedia (max 720p30), opcional.
     3. WebSocket /ws?token=&room=&name= -> la sala responde "hello" con un id
        y una CLAVE propia del participante, y lo deja en el LOBBY ("lobby",
        con su posicion) hasta que el operador lo admite ("admitted").
     4. Admitido: RTCPeerConnection (el invitado es el OFFERER) con camara +
        micro; compartir pantalla/ventana agrega un segundo track de video y
        renegocia. Archivos: POST /api/v1/files con la clave (X-BA-Key).
     5. Reconexion: /ws?resume=<id>&key=<clave>. Id y clave viven en
        sessionStorage, asi una recarga de la pagina vuelve al mismo lugar.

   Framing: los mensajes viajan como frames BINARIOS WS con el encoding Frame
   del proyecto (4 bytes LE + JSON, spec §4.1).
   ========================================================================== */

(function () {
  "use strict";

  const MAX_W = 1280, MAX_H = 720, FPS = 30;
  const RECONNECT_BASE_MS = 1000, RECONNECT_MAX_MS = 15000;
  const SESSION_KEY = "ba.guest.session";
  const LINK_KEY = "ba.guest.link"; // sala y PIN del enlace: sobreviven a una recarga
  const PING_MS = 5000, PING_DEAD_MS = 12000; // latido del WebSocket
  const MEDIA_GRACE_MS = 5000;                // medios caidos antes de rearmar la conexion

  const state = {
    token: null, room: null, name: "",
    id: null, key: null,
    ws: null, wsOpened: false, pc: null,
    localStream: null, screenStream: null, screenSender: null,
    audioCtx: null, analyser: null, levelRaf: 0,
    admitted: false, slot: 0,
    mic: true, cam: true, hand: false,
    maxUploadBytes: 0,
    reconnectDelay: RECONNECT_BASE_MS, reconnectTimer: 0, resumeFailures: 0,
    lastRx: 0, pongSeen: false, pingTimer: 0, mediaTimer: 0, wakeLock: null,
    left: false, chatOpen: false, chatAuto: false, unread: 0,
    info: "", contact: "",
    speaker: null, clockOffset: 0, speakerAt: 0, speakerTimer: 0, avatarTimer: 0,
  };

  // --- DOM ----------------------------------------------------------------
  const $ = (id) => document.getElementById(id);
  const screens = ["join", "check", "lobby", "room", "end"];
  function show(name) {
    for (const s of screens) $("screen-" + s).classList.toggle("hidden", s !== name);
    document.body.classList.toggle("in-room", name === "room");
  }
  function toast(text, kind) {
    const el = document.createElement("div");
    el.className = "toast " + (kind || "");
    el.textContent = text;
    $("toasts").appendChild(el);
    setTimeout(() => el.classList.add("out"), 3500);
    setTimeout(() => el.remove(), 4000);
  }

  // --- Framing ------------------------------------------------------------
  function encodeFrame(obj) {
    const json = new TextEncoder().encode(JSON.stringify(obj));
    const buf = new Uint8Array(4 + json.length);
    new DataView(buf.buffer).setUint32(0, json.length, true);
    buf.set(json, 4);
    return buf;
  }
  function decodeFrame(u8) {
    if (u8.length < 4) return null;
    const len = new DataView(u8.buffer, u8.byteOffset, u8.byteLength).getUint32(0, true);
    if (u8.length < 4 + len) return null;
    try { return JSON.parse(new TextDecoder().decode(u8.subarray(4, 4 + len))); } catch (e) { return null; }
  }
  function send(obj) {
    if (state.ws && state.ws.readyState === WebSocket.OPEN) state.ws.send(encodeFrame(obj));
  }

  // --- Sesion persistente (sobrevive a una recarga) -----------------------
  function saveSession() {
    try {
      sessionStorage.setItem(SESSION_KEY, JSON.stringify({ id: state.id, key: state.key, room: state.room, name: state.name }));
    } catch (e) {}
  }
  function loadSession() {
    try { return JSON.parse(sessionStorage.getItem(SESSION_KEY) || "null"); } catch (e) { return null; }
  }
  function clearSession() {
    try { sessionStorage.removeItem(SESSION_KEY); } catch (e) {}
    state.id = state.key = null;
  }

  const LOBBY_TEXT = $("lobby-text").textContent;

  // --- 1) Entrada ---------------------------------------------------------
  async function join() {
    const name = $("name").value.trim();
    const room = $("room").value.trim();
    const pin = $("pin").value.trim();
    const err = $("join-error");
    err.textContent = "";
    if (!name) { err.textContent = "Contanos tu nombre."; return; }
    if (!pin) { err.textContent = "Ingresá el PIN que te pasó el operador."; return; }
    const btn = $("btn-join"), btnText = btn.textContent;
    btn.disabled = true;
    btn.textContent = "Conectando…";
    // Con mala red el pedido puede colgarse: se corta a los 10 s.
    const ctl = new AbortController();
    const timeout = setTimeout(() => ctl.abort(), 10000);
    try {
      // Sin sala, el servidor usa la suya (y la devuelve en data.room).
      const res = await fetch("/api/join", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(room ? { room, pin } : { pin }),
        signal: ctl.signal,
      });
      const data = await res.json().catch(() => ({}));
      if (!res.ok || !data.ok) {
        err.textContent = {
          pin: room ? "PIN o sala incorrectos. Revisá los datos que te pasó el operador." : "PIN incorrecto.",
          rate: "Demasiados intentos desde este equipo. Esperá un minuto.",
          locked: "La sala está cerrada en este momento.",
        }[data.error] || (res.status === 503
          ? "El servidor está muy ocupado. Probá de nuevo en un momento."
          : "No se pudo entrar. Revisá los datos que te pasó el operador.");
        return;
      }
      resetControls();
      state.token = data.token;
      state.room = data.room;
      state.name = name;
      state.info = $("info").value.trim();
      state.contact = $("contact").value.trim();
      state.maxUploadBytes = data.maxUploadBytes || 0;
      try {
        localStorage.setItem("ba.guest.name", name);
        localStorage.setItem("ba.guest.info", state.info);
        localStorage.setItem("ba.guest.contact", state.contact);
      } catch (e) {}
      show("check");
      await checkDevices();
    } catch (e) {
      err.textContent = "Sin conexión con el servidor. Revisá tu internet y probá de nuevo.";
    } finally {
      clearTimeout(timeout);
      btn.disabled = false;
      btn.textContent = btnText;
    }
  }

  // Un ingreso nuevo arranca con microfono y camara abiertos y los botones
  // de acuerdo: si no, al volver a entrar quedaria el boton en rojo con el
  // microfono abierto.
  function resetControls() {
    state.mic = state.cam = true;
    state.hand = false;
    paintControls();
  }
  function paintControls() {
    const paint = (id, on, onText, offText) => {
      const b = $(id);
      b.classList.toggle("off", !on);
      b.setAttribute("aria-pressed", on ? "false" : "true");
      b.title = on ? onText : offText;
      b.setAttribute("aria-label", b.title);
    };
    paint("btn-mic", state.mic, "Silenciar el micrófono", "Micrófono silenciado: tocá para activarlo");
    paint("btn-cam", state.cam, "Apagar la cámara", "Cámara apagada: tocá para encenderla");
    $("btn-hand").classList.toggle("active", state.hand);
  }

  // --- 2) Dispositivos ----------------------------------------------------
  const VIDEO_WANT = { width: { ideal: MAX_W }, height: { ideal: MAX_H }, frameRate: { ideal: FPS } };
  const AUDIO_WANT = { echoCancellation: true, noiseSuppression: true, autoGainControl: true };

  // Pide camara y microfono; si juntos fallan (notebook sin camara, camara
  // tomada por otra app), prueba cada uno por separado: el orador no se queda
  // sin microfono por un problema de camara. Devuelve {stream, error}.
  async function acquireMedia() {
    if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) return { stream: null, error: "unsupported" };
    let first = null;
    for (const c of [{ video: VIDEO_WANT, audio: AUDIO_WANT }, { audio: AUDIO_WANT }, { video: VIDEO_WANT }]) {
      try {
        return { stream: await navigator.mediaDevices.getUserMedia(c), error: first };
      } catch (e) {
        first = first || e.name || "error";
        if (e.name === "NotAllowedError" || e.name === "SecurityError") break; // permiso negado: no insistir
      }
    }
    return { stream: null, error: first };
  }

  function mediaErrorText(error, stream) {
    if (!window.isSecureContext) return "Este enlace no es seguro (http) y el navegador no deja usar la cámara ni el micrófono. Pedile al operador el enlace que empieza con https.";
    if (error === "unsupported") return "Este navegador no permite usar la cámara ni el micrófono. Probá con Chrome, Edge, Firefox o Safari actualizados.";
    const have = stream ? (stream.getAudioTracks().length ? "Entrás solo con micrófono. " : "Entrás solo con cámara, sin micrófono. ") : "";
    if (error === "NotAllowedError" || error === "SecurityError")
      return "El navegador tiene bloqueado el permiso. Tocá el candado de la barra de direcciones, permití la cámara y el micrófono, y recargá la página. También podés entrar sin ellos.";
    if (error === "NotFoundError" || error === "OverconstrainedError")
      return have + (stream ? "No encontramos el otro dispositivo." : "No encontramos ni cámara ni micrófono en este equipo. Podés entrar sin ellos.");
    if (error === "NotReadableError" || error === "AbortError")
      return have + "Otra aplicación está usando la cámara o el micrófono (Zoom, Meet, Teams…): cerrala y recargá la página" + (stream ? "." : ", o entrá sin ellos.");
    return have + (stream ? "" : "No se pudo usar la cámara ni el micrófono. Revisá los permisos, o entrá sin ellos.");
  }

  async function checkDevices() {
    const err = $("check-error");
    const start = $("btn-start"), startText = start.textContent;
    err.textContent = "";
    // Hasta que el navegador conteste no se puede pedir entrar: si no, se
    // entraria sin mandar audio ni video y sin darse cuenta.
    start.disabled = true;
    start.textContent = "Aceptá el permiso de cámara y micrófono…";
    const { stream, error } = await acquireMedia();
    start.disabled = false;
    start.textContent = startText;
    if (stream) {
      setLocalStream(stream);
      const mark = (id, ok, label) => {
        $(id).textContent = label + (ok ? " ✓" : " ✕");
        $(id).classList.toggle("ok", ok);
      };
      mark("cam-state", stream.getVideoTracks().length > 0, "Cámara");
      mark("mic-state", stream.getAudioTracks().length > 0, "Micrófono");
    }
    if (error || !stream) err.textContent = mediaErrorText(error, stream);
  }

  function setLocalStream(stream) {
    state.localStream = stream;
    // El stream nuevo respeta lo que dicen los botones (silenciado = silenciado).
    stream.getAudioTracks().forEach((t) => (t.enabled = state.mic));
    stream.getVideoTracks().forEach((t) => (t.enabled = state.cam));
    $("local-preview").srcObject = stream;
    $("self-video").srcObject = stream;
    // Si la conexion ya estaba armada sin medios, se rearma con ellos.
    if (state.pc && state.ws && state.ws.readyState === WebSocket.OPEN) startPeer();
    try {
      state.audioCtx = state.audioCtx || new (window.AudioContext || window.webkitAudioContext)();
      const src = state.audioCtx.createMediaStreamSource(stream);
      state.analyser = state.audioCtx.createAnalyser();
      state.analyser.fftSize = 256;
      src.connect(state.analyser);
      const data = new Uint8Array(state.analyser.fftSize);
      const tick = () => {
        state.analyser.getByteFrequencyData(data);
        let sum = 0;
        for (let i = 0; i < data.length; i++) sum += data[i];
        $("level").style.width = Math.min(100, (sum / data.length / 255) * 160) + "%";
        state.levelRaf = requestAnimationFrame(tick);
      };
      cancelAnimationFrame(state.levelRaf);
      tick();
    } catch (e) { /* sin medidor: no es critico */ }
  }

  function stopLocalMedia() {
    for (const s of [state.localStream, state.screenStream]) if (s) s.getTracks().forEach((t) => t.stop());
    state.localStream = state.screenStream = null;
    state.screenSender = null;
    cancelAnimationFrame(state.levelRaf);
  }

  // --- 3) WebSocket de sala -----------------------------------------------
  function connect() {
    clearTimeout(state.reconnectTimer);
    const proto = location.protocol === "https:" ? "wss" : "ws";
    const q = state.id && state.key
      ? `resume=${encodeURIComponent(state.id)}&key=${encodeURIComponent(state.key)}`
      : `token=${encodeURIComponent(state.token)}&room=${encodeURIComponent(state.room)}&name=${encodeURIComponent(state.name)}` +
        `&info=${encodeURIComponent(state.info)}&contact=${encodeURIComponent(state.contact)}`;
    const ws = new WebSocket(`${proto}://${location.host}/ws?${q}`);
    ws.binaryType = "arraybuffer";
    state.ws = ws;
    state.wsOpened = false;
    ws.onopen = () => {
      state.wsOpened = true;
      state.resumeFailures = 0;
      state.reconnectDelay = RECONNECT_BASE_MS;
      setConn("ok", "Conectado");
      startPing(ws);
      keepAwake();
    };
    ws.onmessage = (ev) => {
      state.lastRx = Date.now();
      if (ev.data instanceof ArrayBuffer) {
        const msg = decodeFrame(new Uint8Array(ev.data));
        if (msg) onMessage(msg);
      }
    };
    let closed = false;
    ws.onclose = async () => {
      if (state.ws !== ws || closed) return; // el latido puede forzar este camino antes que el navegador
      closed = true;
      clearInterval(state.pingTimer);
      teardownPeer();
      if (state.left) return;
      const opened = state.wsOpened;
      if (!state.id) {
        end("No se pudo entrar", "El ingreso venció. Volvé a intentarlo.");
        return;
      }
      setConn("bad", "Reconectando…");
      if (!opened) {
        // El navegador no distingue "el servidor rechazo el reenganche" de "no
        // hay red". Solo cuenta como rechazo si el servidor SI responde: sin
        // red (cambio de wifi a datos) se sigue intentando sin perder el lugar.
        if (await serverReachable()) {
          if (state.ws !== ws || state.left) return;
          if (++state.resumeFailures >= 2) {
            clearSession();
            end("Tu lugar en la sala venció", "Pasó demasiado tiempo desconectado. Volvé a entrar.");
            return;
          }
        }
        if (state.ws !== ws || state.left) return;
      }
      state.reconnectTimer = setTimeout(connect, state.reconnectDelay);
      state.reconnectDelay = Math.min(state.reconnectDelay * 2, RECONNECT_MAX_MS);
    };
  }

  async function serverReachable() {
    if (navigator.onLine === false) return false;
    const ctl = new AbortController();
    const t = setTimeout(() => ctl.abort(), 4000);
    try {
      await fetch("/healthz", { cache: "no-store", signal: ctl.signal });
      return true; // cualquier respuesta HTTP: el servidor esta
    } catch (e) {
      return false;
    } finally {
      clearTimeout(t);
    }
  }

  // Latido: un TCP medio abierto (cambio de red) no dispara onclose. Si el
  // servidor no contesta nada en PING_DEAD_MS, el socket se da por muerto.
  function startPing(ws) {
    clearInterval(state.pingTimer);
    state.lastRx = Date.now();
    state.pingTimer = setInterval(() => {
      if (state.ws !== ws) return clearInterval(state.pingTimer);
      // Solo se exige respuesta a un servidor que ya contesto un latido (uno
      // anterior a este cambio no lo hace y no hay que cortarle).
      if (state.pongSeen && Date.now() - state.lastRx > PING_DEAD_MS) {
        clearInterval(state.pingTimer);
        try { ws.close(); } catch (e) {}
        // close() puede tardar en avisar con la red caida: se fuerza el camino.
        setTimeout(() => { if (state.ws === ws && ws.onclose) ws.onclose(); }, 1500);
        return;
      }
      send({ type: "ping" });
    }, PING_MS);
  }

  // Vuelve la red o la pestaña al frente: reintentar YA, sin esperar el backoff.
  function reconnectNow() {
    if (state.left || !state.id) return;
    const ws = state.ws;
    if (ws && ws.readyState === WebSocket.OPEN) { send({ type: "ping" }); return; }
    if (ws && ws.readyState === WebSocket.CONNECTING) return;
    state.reconnectDelay = RECONNECT_BASE_MS;
    connect();
  }

  // Pantalla encendida mientras se esta en el lobby o en la sala: un celular
  // apoyado que se bloquea suspende la pestaña y se pierde el lugar.
  async function keepAwake() {
    if (!("wakeLock" in navigator) || state.left || !state.id || document.visibilityState !== "visible") return;
    try {
      if (state.wakeLock) return;
      state.wakeLock = await navigator.wakeLock.request("screen");
      state.wakeLock.addEventListener("release", () => { state.wakeLock = null; });
    } catch (e) { /* bateria baja o sin permiso: no es critico */ }
  }
  function releaseAwake() {
    try { if (state.wakeLock) state.wakeLock.release(); } catch (e) {}
    state.wakeLock = null;
  }

  function onMessage(msg) {
    switch (msg.type) {
      case "hello":
        state.id = msg.id;
        state.key = msg.key;
        state.name = msg.name || state.name;
        state.room = msg.room || state.room;
        saveSession();
        $("self-label").textContent = state.name;
        if (!state.admitted) show("lobby");
        sendMedia();
        startAvatar();
        break;
      case "publish":
        // La sala nos da lugar para publicar (preview del lobby o adentro).
        if (!state.pc) startPeer();
        break;
      case "busy":
        toast("La sala no tiene lugar para tu video en este momento", "bad");
        break;
      case "talk":
        $("talk-badge").classList.toggle("hidden", !msg.on);
        $("lobby-title").textContent = msg.on ? "Estás hablando con el operador" : "Esperando al operador";
        if (msg.on) toast("El operador te está hablando", "ok");
        break;
      case "lobby":
        state.admitted = false;
        show("lobby");
        $("lobby-pos").textContent = msg.position > 1 ? `Hay ${msg.position - 1} persona(s) antes que vos.` : "Sos el próximo.";
        break;
      case "admitted":
        state.admitted = true;
        state.slot = msg.slot;
        $("talk-badge").classList.add("hidden");
        // El chat que se abrio solo en el lobby se cierra al pasar: adentro
        // tapa el video, y el que lo abrio a mano se lo queda.
        if (state.chatAuto) {
          state.chatAuto = false;
          toggleChat(false);
        }
        show("room");
        toast(msg.backstage ? "Estás en la fila de oradores" : "¡Estás adentro!", "ok");
        // La MISMA conexion sigue: B solo cambia a donde van tus medios (del
        // operador al programa). Sin PC todavia (reenganche), se arma.
        if (!state.pc) startPeer();
        break;
      case "speaker":
        onSpeaker(msg);
        break;
      case "profile":
        state.name = msg.name || state.name;
        $("self-label").textContent = state.name;
        break;
      case "pong":
        state.pongSeen = true; // latido: ya conto como recibido
        break;
      case "rejected": {
        state.left = true;
        clearSession();
        const why = {
          locked: ["La sala está cerrada", "El operador cerró la sala a nuevos ingresos."],
          lobby_full: ["Hay mucha gente esperando", "La sala de espera está llena. Probá de nuevo en un minuto."],
          error: ["No se pudo entrar", "El servidor tuvo un problema. Probá de nuevo en un momento."],
        }[msg.reason] || ["No pudiste entrar", "El operador no te hizo pasar esta vez."];
        end(why[0], why[1]);
        break;
      }
      case "kicked":
        state.left = true;
        clearSession();
        end("Saliste de la sala", "El operador te sacó de la sala.");
        break;
      case "request":
        onRequest(msg.what);
        break;
      case "chat":
        addChat(msg.msg);
        break;
      case "shared":
        toast(`Compartiste «${msg.name}»`, "ok");
        break;
      case "answer":
        if (state.pc) state.pc.setRemoteDescription({ type: "answer", sdp: msg.sdp }).catch(() => {});
        break;
      case "ice":
        if (state.pc && msg.candidate) {
          const c = typeof msg.candidate === "string" ? { candidate: msg.candidate, sdpMLineIndex: 0 } : msg.candidate;
          state.pc.addIceCandidate(c).catch(() => {});
        }
        break;
    }
  }

  function onRequest(what) {
    if (what === "mute-mic" && state.mic) { setMic(false); toast("El operador te silenció el micrófono. Tocá 🎙️ para volver a activarlo."); }
    else if (what === "unmute-mic" && !state.mic) { setMic(true); toast("El operador activó tu micrófono", "ok"); }
    else if (what === "unmute-cam" && !state.cam) { setCam(true); toast("El operador encendió tu cámara", "ok"); }
    else if (what === "mute-cam" && state.cam) { setCam(false); toast("El operador te pidió apagar la cámara"); }
    else if (what === "stop-screen" && state.screenStream) { stopScreen(); toast("El operador dejó de mostrar tu pantalla"); }
    else if (what === "lower-hand" && state.hand) { setHand(false); toast("El operador bajó tu mano"); }
  }

  function sendMedia() {
    send({ type: "media", mic: state.mic, cam: state.cam, screen: !!state.screenStream, hand: state.hand });
  }

  // --- 4) WebRTC ----------------------------------------------------------
  function startPeer() {
    teardownPeer();
    const pc = new RTCPeerConnection({ iceServers: [] }); // el medio NO pasa por el tunel (spec §8.4)
    state.pc = pc;
    if (state.localStream) for (const t of state.localStream.getTracks()) pc.addTrack(t, state.localStream);
    if (state.screenStream) {
      const [t] = state.screenStream.getVideoTracks();
      if (t) state.screenSender = pc.addTrack(t, state.screenStream);
    }
    // Sin camara ni micro igual se negocia: se recibe el retorno.
    if (!state.localStream) {
      pc.addTransceiver("audio", { direction: "recvonly" });
      pc.addTransceiver("video", { direction: "recvonly" });
    }
    // Lo que manda B: en el lobby, el operador; adentro, el programa. Es el
    // mismo track (B cambia la fuente), asi que no hay corte al admitirte.
    pc.ontrack = (ev) => {
      const rs = ev.streams[0] || new MediaStream([ev.track]);
      if (ev.track.kind === "video") {
        $("remote-video").srcObject = rs;
        $("lobby-video").srcObject = rs;
        $("lobby-pulse").classList.add("hidden");
      }
      if (ev.track.kind === "audio") {
        $("remote-audio").srcObject = rs;
        playRemoteAudio();
      }
    };
    pc.onicecandidate = (ev) => { if (ev.candidate) send({ type: "ice", candidate: ev.candidate.toJSON() }); };
    pc.onconnectionstatechange = () => {
      if (state.pc !== pc) return;
      const cs = pc.connectionState;
      $("onair").classList.toggle("hidden", cs !== "connected");
      clearTimeout(state.mediaTimer);
      if (cs === "connected") { setConn("ok", "Conectado"); return; }
      if (cs !== "failed" && cs !== "disconnected") return;
      setConn("bad", "Recuperando audio y video…");
      // Un corte corto se arregla solo; si no vuelve, se arma una conexion
      // nueva (el servidor acepta una oferta fresca del mismo participante).
      state.mediaTimer = setTimeout(() => {
        if (state.pc !== pc || state.left) return;
        if (state.ws && state.ws.readyState === WebSocket.OPEN) startPeer();
      }, cs === "failed" ? 0 : MEDIA_GRACE_MS);
    };
    // Tambien renegocia al sumar/quitar la pantalla compartida.
    pc.onnegotiationneeded = async () => {
      try {
        const offer = await pc.createOffer();
        await pc.setLocalDescription(offer);
        send({ type: "offer", sdp: offer.sdp });
      } catch (e) { /* se reintenta al reconectar */ }
    };
  }

  // El navegador puede bloquear el audio si se llego sin un toque (enlace con
  // go=1, recarga): se ofrece un boton para destrabarlo.
  function playRemoteAudio() {
    const a = $("remote-audio");
    const p = a.play();
    if (!p || !p.catch) return;
    p.catch(() => {
      if ($("btn-unmute-remote")) return;
      const b = document.createElement("button");
      b.id = "btn-unmute-remote";
      b.className = "primary";
      b.textContent = "🔊 Tocá para escuchar";
      b.style.cssText = "position:fixed;left:50%;top:50%;transform:translate(-50%,-50%);z-index:50";
      b.addEventListener("click", () => {
        a.play().catch(() => {});
        if (state.audioCtx) state.audioCtx.resume().catch(() => {});
        b.remove();
      });
      document.body.appendChild(b);
    });
  }

  function teardownPeer() {
    clearTimeout(state.mediaTimer);
    if (state.pc) { try { state.pc.close(); } catch (e) {} }
    state.pc = null;
    state.screenSender = null;
    $("remote-video").srcObject = null;
    $("lobby-video").srcObject = null;
    $("lobby-pulse").classList.remove("hidden");
    $("remote-audio").srcObject = null;
    $("onair").classList.add("hidden");
  }

  // --- Controles ----------------------------------------------------------
  // Encender microfono o camara sin tener el dispositivo tomado (se entro
  // "sin camara ni microfono", o fallo al principio): se pide ahora.
  async function ensureTrack(kind) {
    const have = state.localStream && (kind === "audio" ? state.localStream.getAudioTracks() : state.localStream.getVideoTracks()).length > 0;
    if (have) return true;
    const { stream, error } = await acquireMedia();
    const got = stream && (kind === "audio" ? stream.getAudioTracks() : stream.getVideoTracks()).length > 0;
    if (!got) {
      if (stream) stream.getTracks().forEach((t) => t.stop());
      toast(error ? mediaErrorText(error, null) : (kind === "audio" ? "No encontramos un micrófono en este equipo." : "No encontramos una cámara en este equipo."), "bad");
      return false;
    }
    if (state.localStream) state.localStream.getTracks().forEach((t) => t.stop());
    setLocalStream(stream);
    if (!state.pc && state.ws && state.ws.readyState === WebSocket.OPEN) startPeer();
    return true;
  }
  async function setMic(on) {
    if (on && !(await ensureTrack("audio"))) on = false;
    state.mic = on;
    if (state.localStream) state.localStream.getAudioTracks().forEach((t) => (t.enabled = on));
    paintControls();
    sendMedia();
  }
  async function setCam(on) {
    if (on && !(await ensureTrack("video"))) on = false;
    state.cam = on;
    if (state.localStream) state.localStream.getVideoTracks().forEach((t) => (t.enabled = on));
    paintControls();
    sendMedia();
  }
  function setHand(on) {
    state.hand = on;
    paintControls();
    sendMedia();
  }

  async function startScreen() {
    if (!navigator.mediaDevices || !navigator.mediaDevices.getDisplayMedia) {
      toast("Este navegador no permite compartir pantalla", "bad");
      return;
    }
    try {
      const stream = await navigator.mediaDevices.getDisplayMedia({ video: { frameRate: { ideal: 30 } }, audio: false });
      const [track] = stream.getVideoTracks();
      track.contentHint = "detail";
      track.onended = () => stopScreen(); // "Dejar de compartir" del navegador
      state.screenStream = stream;
      $("share-video").srcObject = stream;
      $("share-tile").classList.remove("hidden");
      $("btn-screen").classList.add("active");
      if (state.pc) state.screenSender = state.pc.addTrack(track, stream);
      sendMedia();
    } catch (e) { /* el usuario cancelo */ }
  }

  function stopScreen() {
    if (!state.screenStream) return;
    state.screenStream.getTracks().forEach((t) => t.stop());
    if (state.pc && state.screenSender) { try { state.pc.removeTrack(state.screenSender); } catch (e) {} }
    state.screenStream = null;
    state.screenSender = null;
    $("share-tile").classList.add("hidden");
    $("btn-screen").classList.remove("active");
    sendMedia();
  }

  // --- Archivos -----------------------------------------------------------
  function uploadFile(file) {
    if (state.maxUploadBytes && file.size > state.maxUploadBytes) {
      toast(`«${file.name}» es muy grande (máx. ${Math.round(state.maxUploadBytes / 1048576)} MB)`, "bad");
      return;
    }
    const row = document.createElement("div");
    row.className = "upload";
    row.innerHTML = '<span class="uname"></span><div class="bar"><div></div></div><span class="upct">0%</span>';
    row.querySelector(".uname").textContent = file.name;
    $("uploads").appendChild(row);
    const bar = row.querySelector(".bar div"), pct = row.querySelector(".upct");

    const xhr = new XMLHttpRequest();
    xhr.open("POST", "/api/v1/files?name=" + encodeURIComponent(file.name));
    xhr.setRequestHeader("X-BA-Key", state.key || "");
    xhr.upload.onprogress = (e) => {
      if (!e.lengthComputable) return;
      const p = Math.round((e.loaded / e.total) * 100);
      bar.style.width = p + "%";
      pct.textContent = p + "%";
    };
    const done = (ok, text) => {
      row.classList.add(ok ? "ok" : "bad");
      pct.textContent = text;
      setTimeout(() => row.remove(), ok ? 2500 : 6000);
    };
    xhr.onload = () => {
      if (xhr.status === 201) return done(true, "✓");
      done(false, { 413: "muy grande", 415: "tipo no soportado", 429: "límite de archivos", 401: "sin permiso" }[xhr.status] || "error");
    };
    xhr.onerror = () => done(false, "sin conexión");
    xhr.send(file);
  }

  // --- Chat ---------------------------------------------------------------
  function addChat(m) {
    if (!m) return;
    const mine = m.fromId === state.id;
    const el = document.createElement("div");
    el.className = "msg" + (mine ? " mine" : "") + (m.fromId === "host" ? " host" : "");
    const who = document.createElement("span");
    who.className = "who";
    who.textContent = mine ? "Vos" : m.fromName + (m.to !== "all" && !mine ? " (a vos)" : "");
    const txt = document.createElement("span");
    txt.className = "txt";
    txt.textContent = m.text;
    el.append(who, txt);
    $("chat-log").appendChild(el);
    $("chat-log").scrollTop = $("chat-log").scrollHeight;
    if (!state.chatOpen && !mine) {
      // Esperando en el lobby no hay nada que tapar, y el mensaje del operador
      // (o del asistente que atiende) es justo lo que la persona tiene que
      // ver: se abre el chat en vez de dejarle un globito que no mira nadie.
      if (m.fromId === "host" && !state.admitted) {
        state.chatAuto = true;
        toggleChat(true);
        return;
      }
      state.unread++;
      $("unread").textContent = state.unread;
      $("unread").classList.remove("hidden");
      if (m.fromId === "host") toast("Operador: " + m.text);
    }
  }
  function toggleChat(open) {
    state.chatOpen = open === undefined ? !state.chatOpen : open;
    $("chat").classList.toggle("hidden", !state.chatOpen);
    if (state.chatOpen) {
      state.unread = 0;
      $("unread").classList.add("hidden");
      $("chat-input").focus();
    }
  }

  // --- Modo oradores -----------------------------------------------------
  // La sala manda el turno y el reloj; el cliente solo lo dibuja. El reloj del
  // servidor manda: se corrige la diferencia con el reloj local.

  // --- Vista del orador ---------------------------------------------------
  // El que habla no tiene por que mirar la proyeccion que tiene a la espalda:
  // su texto y los avisos del operador le llegan a su propia pantalla, de
  // frente. El estado sale del mismo teleprompter que ve la tablet
  // (GET /api/prompter es publico: es lo que va a decir en voz alta).
  //
  // El servidor manda desde donde y a que velocidad; aca se calcula la
  // posicion igual que en la tablet, asi el texto no salta con la red.
  const prompter = { texto: "", palabras: [], pos: 0, ppm: 140, corriendo: false,
                     desdeMs: 0, desfaseMs: 0, aviso: "", version: -1, timer: 0 };

  function prompterPos() {
    if (!prompter.corriendo) return prompter.pos;
    const ahora = Date.now() + prompter.desfaseMs;
    return Math.min(prompter.palabras.length,
                    prompter.pos + ((ahora - prompter.desdeMs) / 60000) * prompter.ppm);
  }

  function pintarOrador() {
    const panel = $("orador-panel");
    // Solo cuando le toca a esta persona y hay algo que leer. En la fila no:
    // le taparia el programa que esta mirando.
    const suTurno = state.speakerState === "onair";
    if (!suTurno || prompter.palabras.length === 0) {
      panel.classList.add("hidden");
      return;
    }
    panel.classList.remove("hidden");

    const av = $("orador-aviso");
    av.textContent = prompter.aviso;
    av.classList.toggle("hidden", !prompter.aviso);

    // Una ventana de palabras alrededor de donde va: en un celular no entra
    // el discurso entero y desplazar todo el texto seria caro.
    const i = Math.floor(prompterPos());
    const desde = Math.max(0, i - 4);
    const hasta = Math.min(prompter.palabras.length, i + 45);
    const cont = $("orador-texto");
    cont.textContent = "";
    for (let k = desde; k < hasta; k++) {
      const s = document.createElement("span");
      s.textContent = prompter.palabras[k] + " ";
      if (k < i) s.className = "leido";
      else if (k === i) s.className = "aqui";
      cont.appendChild(s);
    }
    const faltan = Math.max(0, prompter.palabras.length - prompterPos());
    $("orador-pie").textContent = prompter.corriendo
      ? "Quedan " + fmtClock((faltan / prompter.ppm) * 60000) + " de lectura"
      : "El texto está parado: lo arranca el operador";
  }

  async function leerPrompter() {
    try {
      const d = await (await fetch("/api/prompter", { cache: "no-store" })).json();
      prompter.desfaseMs = (d.ahoraMs || Date.now()) - Date.now();
      if (d.texto !== prompter.texto) {
        prompter.texto = d.texto || "";
        prompter.palabras = prompter.texto.split(/\s+/).filter((w) => w !== "");
      }
      prompter.pos = d.posicion || 0;
      prompter.ppm = d.ppm || 140;
      prompter.corriendo = !!d.corriendo;
      prompter.desdeMs = d.desdeMs || 0;
      if (d.aviso !== prompter.aviso && d.aviso) toast(d.aviso, "ok");
      prompter.aviso = d.aviso || "";
    } catch (e) { /* sin prompter, el panel simplemente no aparece */ }
  }

  /// Se consulta solo mientras esta en modo oradores: al resto de la sala el
  /// teleprompter no le importa y no hay por que pedirlo.
  function seguirPrompter(on) {
    clearInterval(prompter.timer);
    if (!on) {
      prompter.timer = 0;
      $("orador-panel").classList.add("hidden");
      return;
    }
    leerPrompter().then(pintarOrador);
    prompter.timer = setInterval(() => { leerPrompter(); }, 1000);
  }

  function onSpeaker(msg) {
    state.speaker = msg;
    state.speakerAt = Date.now();
    if (msg.serverNowMs) state.clockOffset = msg.serverNowMs - state.speakerAt;
    const prev = state.speakerState;
    state.speakerState = msg.state;
    if (msg.state === "onair" && prev !== "onair") toast("¡Estás al aire!", "ok");
    clearInterval(state.speakerTimer);
    state.speakerTimer = setInterval(renderSpeaker, 200);
    renderSpeaker();
    // El teleprompter se sigue mientras esta al aire o a punto de estarlo.
    seguirPrompter(msg.state === "onair" || msg.state === "queued");
  }

  function fmtClock(ms) {
    const neg = ms < 0;
    const s = Math.floor(Math.abs(ms) / 1000);
    const txt = Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0");
    return (neg ? "+" : "") + txt;
  }

  function renderSpeaker() {
    if (prompter.timer) pintarOrador();
    const m = state.speaker;
    const bar = $("speaker-bar");
    const visible = !!m && (m.state === "queued" || m.state === "onair" || m.state === "done") && state.admitted;
    bar.classList.toggle("hidden", !visible);
    if (!visible) { clearInterval(state.speakerTimer); return; }
    bar.classList.remove("queued", "onair", "warn", "over", "done");
    if (m.state === "onair") {
      const now = Date.now() + state.clockOffset;
      // Al entrar al aire la cuenta arranca; se pasa de cero, sigue en negativo.
      const left = m.allottedSec * 1000 - (now - m.startedMs);
      bar.classList.add("onair");
      $("sp-state").textContent = left < 0 ? "Te pasaste" : "Estás al aire";
      // Redondeo hacia arriba mientras queda tiempo: "0:01" hasta el ultimo instante.
      $("sp-clock").textContent = left >= 0 ? fmtClock(Math.ceil(left / 1000) * 1000) : fmtClock(left);
      if (left < 0) {
        bar.classList.add("over");
        $("sp-note").textContent = "Se terminó tu tiempo. Redondeá, por favor.";
      } else if (left <= 10000) {
        bar.classList.add("warn");
        $("sp-note").textContent = "Últimos segundos.";
      } else {
        $("sp-note").textContent = "Tiempo que te queda.";
      }
    } else if (m.state === "queued") {
      bar.classList.add("queued");
      const eta = Math.max(0, (m.etaSec || 0) * 1000 - (Date.now() - state.speakerAt));
      $("sp-state").textContent = m.position === 1 ? "Sos el próximo" : `Tu turno: ${m.position}º en la fila`;
      $("sp-clock").textContent = eta > 0 ? "~" + fmtClock(eta) : "ya";
      $("sp-note").textContent = `Mirás el programa en vivo. Vas a tener ${fmtClock(m.allottedSec * 1000)} para hablar.`;
    } else {
      bar.classList.add("done");
      $("sp-state").textContent = "Ya hablaste";
      $("sp-clock").textContent = fmtClock(m.usedMs || 0);
      $("sp-note").textContent = "¡Gracias! Podés seguir mirando el programa.";
      clearInterval(state.speakerTimer);
    }
  }

  // Foto chica para la tarjeta que ve la produccion (cada 5 s, solo con camara).
  function startAvatar() {
    clearInterval(state.avatarTimer);
    // Video propio fuera de pantalla: el del DOM puede estar oculto (lobby) y
    // un <video> oculto no siempre avanza de cuadro.
    const v = document.createElement("video");
    v.muted = true;
    v.playsInline = true;
    const snap = () => {
      if (!state.key || !state.cam || !state.localStream) return;
      if (v.srcObject !== state.localStream) { v.srcObject = state.localStream; v.play().catch(() => {}); return; }
      if (!v.videoWidth) return;
      const c = document.createElement("canvas");
      c.width = 160; c.height = 120;
      const g = c.getContext("2d");
      // Recorte centrado 4:3.
      const sw = Math.min(v.videoWidth, v.videoHeight * 4 / 3), sh = sw * 3 / 4;
      g.drawImage(v, (v.videoWidth - sw) / 2, (v.videoHeight - sh) / 2, sw, sh, 0, 0, 160, 120);
      c.toBlob((b) => {
        if (!b) return;
        fetch("/api/v1/avatar", { method: "POST", headers: { "X-BA-Key": state.key, "Content-Type": "image/jpeg" }, body: b }).catch(() => {});
      }, "image/jpeg", 0.7);
    };
    setTimeout(snap, 300);
    setTimeout(snap, 1500);
    state.avatarTimer = setInterval(snap, 5000);
  }

  // --- Estado / salida ----------------------------------------------------
  function setConn(cls, text) {
    const el = $("conn-state");
    el.className = "status " + cls;
    el.textContent = text;
    // El lobby no tiene ese cartel: alli el estado va en el texto de espera.
    if (!state.admitted) $("lobby-text").textContent = cls === "ok" ? LOBBY_TEXT : "Se cortó la conexión. Reconectando con la sala…";
  }

  function leave() {
    state.left = true;
    send({ type: "bye" });
    try { if (state.ws) state.ws.close(); } catch (e) {}
    teardownPeer();
    clearSession();
    end("Saliste de la sala", "Gracias por participar.");
  }

  function end(title, text) {
    clearTimeout(state.reconnectTimer);
    clearInterval(state.pingTimer);
    releaseAwake();
    const u = $("btn-unmute-remote");
    if (u) u.remove();
    teardownPeer();
    stopLocalMedia();
    toggleChat(false);
    clearInterval(state.avatarTimer);
    clearInterval(state.speakerTimer);
    state.speaker = null;
    $("speaker-bar").classList.add("hidden");
    state.admitted = false;
    $("end-title").textContent = title;
    $("end-text").textContent = text || "";
    show("end");
  }

  // --- Bootstrap ----------------------------------------------------------
  $("btn-join").addEventListener("click", join);
  $("pin").addEventListener("keydown", (e) => { if (e.key === "Enter") join(); });
  $("btn-start").addEventListener("click", () => { state.left = false; show("lobby"); connect(); });
  $("btn-nodevices").addEventListener("click", () => {
    stopLocalMedia();
    state.mic = state.cam = false;
    paintControls();
    state.left = false;
    show("lobby");
    connect();
  });
  $("btn-lobby-chat").addEventListener("click", () => toggleChat(true));
  $("btn-lobby-leave").addEventListener("click", leave);
  $("btn-mic").addEventListener("click", () => setMic(!state.mic));
  $("btn-cam").addEventListener("click", () => setCam(!state.cam));
  $("btn-screen").addEventListener("click", () => (state.screenStream ? stopScreen() : startScreen()));
  $("btn-file").addEventListener("click", () => $("file-input").click());
  $("file-input").addEventListener("change", (e) => {
    for (const f of e.target.files) uploadFile(f);
    e.target.value = "";
  });
  $("btn-hand").addEventListener("click", () => setHand(!state.hand));
  $("btn-chat").addEventListener("click", () => toggleChat());
  $("btn-chat-close").addEventListener("click", () => toggleChat(false));
  // Dentro de la sala, salir pide confirmacion: un toque sin querer obliga a
  // volver a pasar por el lobby en pleno vivo.
  $("btn-leave").addEventListener("click", () => { if (window.confirm("¿Salir de la sala?")) leave(); });
  window.addEventListener("online", reconnectNow);
  document.addEventListener("visibilitychange", () => {
    if (document.visibilityState !== "visible") return;
    reconnectNow();
    keepAwake();
  });
  $("btn-again").addEventListener("click", () => { state.left = false; show("join"); });
  $("chat-form").addEventListener("submit", (e) => {
    e.preventDefault();
    const t = $("chat-input").value.trim();
    if (!t) return;
    send({ type: "chat", text: t });
    $("chat-input").value = "";
  });
  if (!navigator.mediaDevices || !navigator.mediaDevices.getDisplayMedia) {
    $("btn-screen").disabled = true;
    $("btn-screen").title = "Tu navegador no permite compartir pantalla";
  }

  // Precarga: sala por URL (?room=) y nombre recordado. Un enlace de
  // invitacion puede traer sala, PIN y nombre en el FRAGMENTO
  // (#room=..&pin=..&name=..): el fragmento nunca viaja al servidor ni queda en
  // sus logs. Con go=1 entra directo (camara/micro por defecto).
  const params = new URLSearchParams(location.search);
  const hash = new URLSearchParams(location.hash.replace(/^#/, ""));
  // Sala y PIN del enlace se guardan para esta pestaña: recargar (la reaccion
  // tipica ante un problema) no deja al invitado sin datos que nunca conocio.
  let link = {};
  try { link = JSON.parse(sessionStorage.getItem(LINK_KEY) || "{}") || {}; } catch (e) {}
  if (hash.get("room") || params.get("room")) link.room = hash.get("room") || params.get("room");
  if (hash.get("pin")) link.pin = hash.get("pin");
  try { sessionStorage.setItem(LINK_KEY, JSON.stringify(link)); } catch (e) {}
  $("room").value = link.room || "";
  $("pin").value = link.pin || "";
  // Con la sala en el enlace, el campo sobra (y confunde): se esconde.
  if (link.room) {
    $("room").parentElement.classList.add("hidden");
    $("room").parentElement.parentElement.style.gridTemplateColumns = "1fr";
  }
  if (!window.isSecureContext) $("join-error").textContent = "Este enlace no es seguro (http): el navegador no va a dejar usar la cámara ni el micrófono. Pedile al operador el enlace que empieza con https.";
  // Con el asistente atendiendo el lobby, entrar es solo el nombre: los demas
  // datos se los pregunta el asistente por chat. No se espera la respuesta:
  // si el servidor tarda o no contesta, el formulario queda como estaba.
  fetch("/api/sala")
    .then((r) => r.json())
    .then((d) => { if (d && d.ok && d.pedirDatos === false) $("datos").classList.add("hidden"); })
    .catch(() => {});
  try {
    $("name").value = hash.get("name") || localStorage.getItem("ba.guest.name") || "";
    $("info").value = localStorage.getItem("ba.guest.info") || "";
    $("contact").value = localStorage.getItem("ba.guest.contact") || "";
  } catch (e) {}
  if (hash.has("pin")) history.replaceState(null, "", location.pathname + location.search); // no dejar el PIN a la vista
  const autoGo = hash.get("go") === "1";

  // Recarga de la pagina con una sesion viva: vuelve directo a su lugar.
  const saved = loadSession();
  if (saved && saved.id && saved.key) {
    Object.assign(state, { id: saved.id, key: saved.key, room: saved.room, name: saved.name || "" });
    show("lobby");
    $("lobby-text").textContent = "Reconectando con la sala…";
    const resume = () => connect();
    navigator.mediaDevices && navigator.mediaDevices.getUserMedia
      ? navigator.mediaDevices.getUserMedia({ video: true, audio: true }).then(setLocalStream, () => {}).finally(resume)
      : resume();
  } else {
    show("join");
    if (autoGo && $("name").value && $("room").value && $("pin").value) {
      join().then(() => { if (state.token) { state.left = false; show("lobby"); connect(); } });
    }
  }
})();
