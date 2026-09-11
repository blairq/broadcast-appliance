/* ============================================================================
   Web/guest/app.js — Lógica de la app web del invitado (spec §8.3)

   Flujo:
     1. join(): POST /api/join {room, pin} → {token}.
     2. checkDevices(): getUserMedia (máx 720p30) + medidor de nivel de mic.
     3. connect(): WebSocket /ws?token=..&slot=1 (señalización WebRTC).
        El invitado es el OFFERER: crea la RTCPeerConnection, agrega sus
        tracks locales (camara + micro) y envía {type:"offer", sdp}.
     4. B responde {type:"answer", sdp}; los candidatos ICE fluyen en ambos
        sentidos ({type:"ice", candidate}).
     5. Retorno: el video de programa (A→B) se muestra en #remote-video y el
        audio de la sala suena por #remote-audio.
     6. Reconexión automática con backoff; indicador "estás al aire".

   Framing: los mensajes de señalización viajan como frames BINARIOS WS que
   cargan el encoding Frame (4 bytes LE + JSON, spec §4.1). encodeFrame/
   decodeFrame reusan ese formato (idéntico a ba_core::ipc::Frame).
   ========================================================================== */

(function () {
  "use strict";

  // --- Constantes ---------------------------------------------------------
  const SLOT = 1; // este invitado usa el slot 1 (el operador asigna el resto)
  const MAX_W = 1280, MAX_H = 720, FPS = 30; // restricción de captura (720p30)
  const RECONNECT_BASE_MS = 1000;
  const RECONNECT_MAX_MS = 15000;

  // --- Estado -------------------------------------------------------------
  const state = {
    token: null,
    room: null,
    ws: null,
    pc: null,
    localStream: null,
    remoteStream: null,
    audioCtx: null,
    analyser: null,
    levelRaf: 0,
    reconnectDelay: RECONNECT_BASE_MS,
    reconnectTimer: 0,
    connecting: false,
    left: false,
  };

  // --- DOM ----------------------------------------------------------------
  const $ = (id) => document.getElementById(id);
  const screens = {
    join: $("screen-join"),
    check: $("screen-check"),
    room: $("screen-room"),
  };
  function show(name) {
    for (const k of Object.keys(screens)) screens[k].classList.toggle("hidden", k !== name);
  }

  // --- Framing (Frame: 4 bytes LE + JSON, spec §4.1) ----------------------
  function encodeFrame(obj) {
    const json = new TextEncoder().encode(JSON.stringify(obj));
    const buf = new Uint8Array(4 + json.length);
    const dv = new DataView(buf.buffer);
    dv.setUint32(0, json.length, true); // little-endian
    buf.set(json, 4);
    return buf;
  }
  function decodeFrame(u8) {
    if (u8.length < 4) return null;
    const dv = new DataView(u8.buffer, u8.byteOffset, u8.byteLength);
    const len = dv.getUint32(0, true);
    if (u8.length < 4 + len) return null;
    const json = new TextDecoder().decode(u8.subarray(4, 4 + len));
    try {
      return JSON.parse(json);
    } catch (e) {
      return null;
    }
  }

  // --- 1) Join (sala + PIN → token) ---------------------------------------
  async function join() {
    const room = $("room").value.trim();
    const pin = $("pin").value.trim();
    const errEl = $("join-error");
    errEl.textContent = "";
    if (!room || !pin) {
      errEl.textContent = "Ingresá la sala y el PIN.";
      return;
    }
    $("btn-join").disabled = true;
    try {
      const res = await fetch("/api/join", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ room, pin }),
      });
      const data = await res.json().catch(() => ({}));
      if (!res.ok || !data.ok) {
        errEl.textContent =
          data.error === "pin"
            ? "PIN incorrecto (o demasiados intentos: esperá un momento)."
            : "No se pudo entrar. Revisá la sala y el PIN.";
        $("btn-join").disabled = false;
        return;
      }
      state.token = data.token;
      state.room = data.room;
      await checkDevices();
      show("check");
    } catch (e) {
      errEl.textContent = "Sin conexión al servidor.";
      $("btn-join").disabled = false;
    }
  }

  // --- 2) Chequeo de dispositivos + medidor de nivel ----------------------
  async function checkDevices() {
    const errEl = $("check-error");
    errEl.textContent = "";
    try {
      const stream = await navigator.mediaDevices.getUserMedia({
        video: { width: { ideal: MAX_W }, height: { ideal: MAX_H }, frameRate: { ideal: FPS } },
        audio: { echoCancellation: true, noiseSuppression: true, autoGainControl: true },
      });
      state.localStream = stream;
      $("local-preview").srcObject = stream;

      // Medidor de nivel del micrófono (WebAudio AnalyserNode).
      try {
        state.audioCtx = new (window.AudioContext || window.webkitAudioContext)();
        const src = state.audioCtx.createMediaStreamSource(stream);
        state.analyser = state.audioCtx.createAnalyser();
        state.analyser.fftSize = 256;
        src.connect(state.analyser);
        startLevelMeter();
      } catch (e) {
        // Sin medidor sigue (no es crítico).
      }

      $("cam-state").textContent = "Cámara ✓";
      $("cam-state").classList.add("ok");
      $("mic-state").textContent = "Micrófono ✓";
      $("mic-state").classList.add("ok");
      $("btn-start").disabled = false;
    } catch (e) {
      errEl.textContent =
        "No se pudo acceder a cámara/micrófono. Revisá los permisos del navegador.";
    }
  }

  function startLevelMeter() {
    const data = new Uint8Array(state.analyser.fftSize);
    const el = $("level");
    function tick() {
      state.analyser.getByteFrequencyData(data);
      let sum = 0;
      for (let i = 0; i < data.length; i++) sum += data[i];
      const avg = sum / data.length / 255; // 0..1
      el.style.width = Math.min(100, avg * 160) + "%";
      state.levelRaf = requestAnimationFrame(tick);
    }
    tick();
  }

  // --- 3) Conexión WebSocket + señalización WebRTC ------------------------
  function connect() {
    if (state.connecting) return;
    state.connecting = true;
    const proto = location.protocol === "https:" ? "wss" : "ws";
    const url = `${proto}://${location.host}/ws?token=${encodeURIComponent(
      state.token
    )}&room=${encodeURIComponent(state.room)}&slot=${SLOT}`;
    const ws = new WebSocket(url);
    ws.binaryType = "arraybuffer";
    state.ws = ws;

    ws.onopen = () => {
      state.reconnectDelay = RECONNECT_BASE_MS;
      setConn("ok", "Conectado");
      startPeerConnection();
    };
    ws.onmessage = (ev) => {
      if (ev.data instanceof ArrayBuffer) {
        const msg = decodeFrame(new Uint8Array(ev.data));
        if (msg) handleSignaling(msg);
      }
    };
    ws.onclose = () => {
      state.connecting = false;
      teardownPeer();
      if (!state.left) scheduleReconnect();
    };
    ws.onerror = () => {
      /* onclose se encarga de la reconexión */
    };
  }

  function handleSignaling(msg) {
    if (!state.pc) return;
    if (msg.type === "answer") {
      state.pc.setRemoteDescription(new RTCSessionDescription(msg)).catch(() => {});
    } else if (msg.type === "ice") {
      if (msg.candidate) {
        state.pc.addIceCandidate(new RTCIceCandidate(msg.candidate)).catch(() => {});
      }
    }
  }

  function startPeerConnection() {
    const pc = new RTCPeerConnection({
      // El medio NO pasa por el túnel (spec §8.4): ICE host + srflx.
      iceServers: [],
    });
    state.pc = pc;

    // Tracks locales del invitado (cámara + mic) → B los publica en A.
    if (state.localStream) {
      for (const track of state.localStream.getTracks()) pc.addTrack(track, state.localStream);
    }

    // Retorno: cuando B nos manda sus tracks remotas (programa + audio de sala),
    // se conectan a los elementos de la UI.
    pc.ontrack = (ev) => {
      const [track] = ev.tracks;
      const rs = ev.streams[0] || new MediaStream([track]);
      state.remoteStream = rs;
      if (track.kind === "video") $("remote-video").srcObject = rs;
      if (track.kind === "audio") $("remote-audio").srcObject = rs;
    };

    // Candidatos ICE locales → B.
    pc.onicecandidate = (ev) => {
      if (ev.candidate && state.ws && state.ws.readyState === WebSocket.OPEN) {
        sendSignaling({ type: "ice", candidate: ev.candidate.toJSON() });
      }
    };

    // Cuando la conexión ICE está arriba, el invitado "está al aire".
    pc.onconnectionstatechange = () => {
      const on = pc.connectionState === "connected" || pc.connectionState === "completed";
      $("onair").classList.toggle("hidden", !on);
      setConn(on ? "ok" : "bad", on ? "Al aire" : "Reconectando…");
    };

    // El invitado es el OFFERER: crea el offer y lo envía a B.
    pc.onnegotiationneeded = async () => {
      try {
        const offer = await pc.createOffer();
        await pc.setLocalDescription(offer);
        sendSignaling({ type: "offer", sdp: offer });
      } catch (e) {
        /* la reconexión lo reintentará */
      }
    };
  }

  function sendSignaling(obj) {
    if (state.ws && state.ws.readyState === WebSocket.OPEN) {
      state.ws.send(encodeFrame(obj));
    }
  }

  function teardownPeer() {
    if (state.pc) {
      try {
        state.pc.close();
      } catch (e) {}
      state.pc = null;
    }
    $("remote-video").srcObject = null;
    $("remote-audio").srcObject = null;
    $("onair").classList.add("hidden");
  }

  // --- 6) Reconexión con backoff -----------------------------------------
  function scheduleReconnect() {
    setConn("bad", "Reconectando…");
    clearTimeout(state.reconnectTimer);
    state.reconnectTimer = setTimeout(() => {
      connect();
      state.reconnectDelay = Math.min(state.reconnectDelay * 2, RECONNECT_MAX_MS);
    }, state.reconnectDelay);
  }

  function setConn(cls, text) {
    const el = $("conn-state");
    el.className = "status " + (cls === "ok" ? "ok" : cls === "bad" ? "bad" : "");
    el.textContent = text;
  }

  // --- Controles de la sala ----------------------------------------------
  function toggleMute() {
    if (!state.localStream) return;
    const on = state.localStream.getAudioTracks()[0];
    if (on) {
      on.enabled = !on.enabled;
      $("btn-mute").textContent = on.enabled ? "Silenciar mic" : "Activar mic";
    }
  }
  function toggleCam() {
    if (!state.localStream) return;
    const on = state.localStream.getVideoTracks()[0];
    if (on) {
      on.enabled = !on.enabled;
      $("btn-cam").textContent = on.enabled ? "Apagar cámara" : "Encender cámara";
    }
  }
  function leave() {
    state.left = true;
    clearTimeout(state.reconnectTimer);
    if (state.ws) {
      try {
        sendSignaling({ type: "bye" });
        state.ws.close();
      } catch (e) {}
    }
    teardownPeer();
    if (state.localStream) state.localStream.getTracks().forEach((t) => t.stop());
    if (state.levelRaf) cancelAnimationFrame(state.levelRaf);
    if (state.audioCtx) state.audioCtx.close().catch(() => {});
    state.token = null;
    state.room = null;
    state.reconnectDelay = RECONNECT_BASE_MS;
    show("join");
  }

  // --- Bootstrap ----------------------------------------------------------
  $("btn-join").addEventListener("click", join);
  $("pin").addEventListener("keydown", (e) => {
    if (e.key === "Enter") join();
  });
  $("btn-start").addEventListener("click", () => {
    show("room");
    connect();
  });
  $("btn-mute").addEventListener("click", toggleMute);
  $("btn-cam").addEventListener("click", toggleCam);
  $("btn-leave").addEventListener("click", leave);

  // Si el operador cambia la orientación (celular vertical), el video de
  // retorno se ajusta solo por object-fit: contain (sin recorte).
  show("join");
})();
