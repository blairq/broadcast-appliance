// La sesión remota del integrante (docs/funciones/SESION-REMOTA.md). Fase R1: entrar con el
// link personal y el PIN, mostrar la cámara propia y mandar una foto de vez en
// cuando para que la presidencia lo vea antes de habilitarlo. El audio y el
// video por WebRTC, la palabra y el voto llegan en las fases siguientes.
"use strict";

const $ = (id) => document.getElementById(id);
const frag = new URLSearchParams(location.hash.slice(1));
const integrante = frag.get("i") || "";
const token = frag.get("t") || "";

const conLink = integrante !== "" && token.length === 64;
let ws = null;
let pin = "";          // el PIN o la contraseña: solo en memoria, para reconectar sin volver a pedirlo
let usuario = "";
let estado = "conectado";
let reintento = 1000;
let cortadoAProposito = false;
let timerFoto = null;
let pidio = false;
let palabraHasta = 0;   // ms en que se le termina el tiempo (0 = no tiene la palabra)
let timerReloj = null;
let vot = null;         // la votación abierta (o pausada)
let timerVot = null;
let desfase = 0;        // reloj del servidor - reloj de acá (para la cuenta regresiva)
let timerPing = null;
let lugar = "afuera";    // "sala" (red local) o "afuera": en la sala, el sonido arranca apagado (acople)

function mostrar(id) {
  for (const s of ["entrar", "sala", "cortado"]) $(s).classList.toggle("oculto", s !== id);
}

function error(texto) {
  $("error").textContent = texto;
  $("error").classList.toggle("oculto", !texto);
}

function conexion(viva) {
  $("conexion").className = "conexion " + (viva ? "viva" : "apagada");
}

function ponerEstado(nuevo, texto) {
  estado = nuevo;
  const hab = nuevo === "habilitado";
  $("estado").className = "estado " + (hab ? "habilitado" : "esperando");
  $("estadoTitulo").textContent = hab ? "Estás habilitado: contás como presente" : "Esperando que te habiliten";
  $("estadoTexto").textContent = texto || (hab
    ? "La presidencia te habilitó. Dejá la cámara prendida durante la sesión."
    : "La presidencia te va a ver por la cámara y te habilita. Hasta entonces no contás para el quórum.");
  programarFotos();
  $("pedir").disabled = !hab;
  // Lo que mira en el celular: solo habilitado (no gasta un lugar de la sala mientras espera).
  if (hab) iniciarVista(); else cerrarVista();
}

// --- La palabra ---------------------------------------------------------------------
function ponerPedido(turno) {
  pidio = turno > 0;
  $("pedir").textContent = pidio ? "Cancelar mi pedido" : "Pedir la palabra";
  $("pedir").classList.toggle("pedido", pidio);
  $("turno").textContent = turno > 0 ? (turno === 1 ? "Sos el próximo" : "Tu turno: " + turno) : "";
}

let debate = null;  // {activo, habla, segundos}: en un debate la palabra la maneja el turno

function ponerPalabra(activa, segundos, titulo) {
  clearInterval(timerReloj);
  $("palabra").classList.toggle("oculto", !activa);
  $("palabra").classList.remove("minuto", "terminado", "espera");
  $("pedir").classList.toggle("oculto", activa);
  if (!activa) { palabraHasta = 0; microfono(pttActivo); return; }
  // En un debate, el micrófono solo en su turno.
  const habla = !debate || debate.habla;
  microfono(habla || pttActivo);
  $("palabra").classList.toggle("espera", !habla);
  $("palabraTitulo").textContent = titulo || "Tenés la palabra";
  $("palabraTexto").textContent = "";
  // Sin límite (0): sin cuenta regresiva.
  $("reloj").classList.toggle("oculto", !(segundos > 0));
  if (!(segundos > 0)) { palabraHasta = 1; return; }
  palabraHasta = Date.now() + segundos * 1000;
  const pintar = () => {
    const s = Math.round((palabraHasta - Date.now()) / 1000);
    const a = Math.abs(s);
    $("reloj").textContent = (s < 0 ? "-" : "") + Math.floor(a / 60) + ":" + String(a % 60).padStart(2, "0");
  };
  pintar();
  timerReloj = setInterval(pintar, 250);
  if (navigator.vibrate) navigator.vibrate([150, 80, 150]);
}

function ponerDebate(m) {
  const empieza = m.activo && !debate;
  debate = m.activo ? { habla: !!m.habla, segundos: m.segundos || 0 } : null;
  if (empieza && vistas.some((v) => v.id === "debate") && vista !== "debate") { vistaAntesDebate = vista; elegirVista("debate"); }
  if (!debate) { ponerPalabra(palabraHasta > 0, 0); return; }
  ponerPalabra(true, debate.habla ? debate.segundos : 0,
               debate.habla ? "Debate: es tu turno" : "Debate: habla el otro, esperá tu turno");
}

function avisoTiempo(aviso) {
  if (aviso === "ultimo_minuto") {
    $("palabra").classList.add("minuto");
    $("palabraTexto").textContent = "Queda 1 minuto";
    if (navigator.vibrate) navigator.vibrate(300);
  } else if (aviso === "terminado") {
    $("palabra").classList.remove("minuto");
    $("palabra").classList.add("terminado");
    $("palabraTitulo").textContent = "Se terminó tu tiempo";
    $("palabraTexto").textContent = "Redondeá, por favor";
    if (navigator.vibrate) navigator.vibrate([300, 100, 300, 100, 300]);
  }
}

// --- La votación --------------------------------------------------------------------
function ponerVotacion(m) {
  desfase = (m.ahora_ms || Date.now()) - Date.now();
  if (m.estado === "abierta" || m.estado === "pausada") {
    const nueva = !vot || vot.numero !== m.numero;
    vot = m;
    $("votacion").classList.remove("oculto");
    $("resultado").classList.add("oculto");
    if (nueva && navigator.vibrate) navigator.vibrate([400, 150, 400]);
    pintarVotacion();
    clearInterval(timerVot);
    timerVot = setInterval(pintarReloj, 250);
    pintarReloj();
  } else {
    vot = null;
    clearInterval(timerVot);
    $("votacion").classList.add("oculto");
  }
}

function pintarVotacion() {
  if (!vot) return;
  $("votNumero").textContent = vot.numero ? "N.º " + vot.numero : "";
  $("votTema").textContent = vot.tema || "Votación";
  const pausada = vot.estado === "pausada";
  $("votEstado").textContent = pausada ? "En pausa: esperá." : "Tocá tu voto. Lo podés cambiar mientras esté abierta.";
  const caja = $("votOpciones");
  caja.textContent = "";
  for (const o of (vot.opciones || [])) {
    const b = document.createElement("button");
    b.type = "button";
    b.textContent = o.texto;
    b.disabled = pausada || vencida();
    if (vot.mi_voto === o.id) b.classList.add("elegida");
    b.addEventListener("click", () => {
      if (ws && ws.readyState === 1) ws.send(JSON.stringify({ type: "voto", votacion: vot.numero, opcion: o.id }));
    });
    caja.appendChild(b);
  }
  const elegida = (vot.opciones || []).find((o) => o.id === vot.mi_voto);
  $("votMio").textContent = elegida ? "Tu voto: " + elegida.texto : "Todavía no votaste.";
}

function vencida() {
  return vot && vot.hasta_ms > 0 && Date.now() + desfase > vot.hasta_ms;
}

function pintarReloj() {
  if (!vot) return;
  if (!vot.hasta_ms) { $("votReloj").textContent = ""; return; }
  const s = Math.max(0, Math.ceil((vot.hasta_ms - (Date.now() + desfase)) / 1000));
  $("votReloj").textContent = Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0");
  $("votReloj").classList.toggle("poco", s <= 10);
  if (s === 0) pintarVotacion();
}

function mostrarResultado(m) {
  $("votacion").classList.add("oculto");
  $("resultado").classList.remove("oculto");
  $("resTema").textContent = m.tema || "Votación";
  const caja = $("resTotales");
  caja.textContent = "";
  for (const o of (m.opciones || [])) {
    const d = document.createElement("div");
    const a = document.createElement("span"); a.textContent = o.texto;
    const b = document.createElement("strong"); b.textContent = String((m.totales || {})[o.id] || 0);
    d.appendChild(a); d.appendChild(b); caja.appendChild(d);
  }
}

// --- La cámara y las fotos ------------------------------------------------------
let local = null;       // la cámara y el micrófono propios
async function prenderCamara() {
  try {
    local = await navigator.mediaDevices.getUserMedia({
      video: { width: 640, height: 360 },
      audio: { echoCancellation: true, noiseSuppression: true, autoGainControl: true },
    });
  } catch (e) {
    // Sin micrófono (o sin permiso): por lo menos la cámara.
    try { local = await navigator.mediaDevices.getUserMedia({ video: { width: 640, height: 360 }, audio: false }); }
    catch (e2) { local = null; }
  }
  if (local) {
    // El micrófono solo se abre cuando tiene la palabra o la presidencia le habla.
    for (const t of local.getAudioTracks()) t.enabled = false;
    $("propia").srcObject = new MediaStream(local.getVideoTracks());
    $("sinCamara").classList.add("oculto");
    // Con el permiso dado, la lista de cámaras ya es la de verdad (antes algunos navegadores dicen una sola).
    navigator.mediaDevices.enumerateDevices().then((l) => { camaras = l.filter((d) => d.kind === "videoinput").length; pintarCompartir(); }).catch(() => {});
  } else {
    $("sinCamara").classList.remove("oculto");
  }
}

function microfono(abierto) {
  if (!local) return;
  for (const t of local.getAudioTracks()) t.enabled = abierto;
}

// --- La conexión directa con la consola de la presidencia (WebRTC) -----------------------
// La consola ofrece; acá se contesta con la cámara y el micrófono. El gateway solo pasa
// los mensajes (sin medios): anda en la red de la sala; desde afuera necesita TURN (R5).
const conexiones = new Map(); // consola -> RTCPeerConnection
let pttActivo = false;

async function rtc(m) {
  const de = m.de;
  if (!de || de.indexOf("consola:") !== 0) return;
  let pc = conexiones.get(de);
  if (m.tipo === "offer") {
    if (pc) pc.close();
    pc = new RTCPeerConnection({ iceServers: [] });
    conexiones.set(de, pc);
    if (local) for (const t of local.getTracks()) pc.addTrack(t, local);
    pc.ontrack = (ev) => { $("presidencia").srcObject = ev.streams[0] || new MediaStream([ev.track]); };
    pc.onicecandidate = (ev) => {
      if (ev.candidate && ws && ws.readyState === 1) ws.send(JSON.stringify({ type: "rtc", a: de, candidato: ev.candidate }));
    };
    pc.onconnectionstatechange = () => {
      if (pc.connectionState === "failed" || pc.connectionState === "closed") conexiones.delete(de);
    };
    await pc.setRemoteDescription({ type: "offer", sdp: m.sdp });
    const answer = await pc.createAnswer();
    await pc.setLocalDescription(answer);
    ws.send(JSON.stringify({ type: "rtc", a: de, tipo: "answer", sdp: answer.sdp }));
  } else if (m.candidato && pc) {
    try { await pc.addIceCandidate(m.candidato); } catch (e) {}
  } else if (m.tipo === "cerrar" && pc) {
    pc.close();
    conexiones.delete(de);
  }
}

// --- A la sala (R3): con la palabra, el micrófono y la cámara van a la app ----------
// El gateway lo recibe como una entrada más («Remoto con la palabra»): suena en la sala
// y en la transmisión, con los filtros del rack. Solo manda (no recibe nada por acá).
let pcSala = null;

async function iniciarSala() {
  cerrarSala();
  if (!local) await prenderCamara();
  if (!local || !ws || ws.readyState !== 1) return;
  const pc = new RTCPeerConnection({ iceServers: [] });
  pcSala = pc;
  for (const t of local.getTracks()) pc.addTransceiver(t, { direction: "sendonly", streams: [local] });
  pc.onicecandidate = (ev) => {
    if (ev.candidate && ws && ws.readyState === 1) ws.send(JSON.stringify({ type: "medios-ice", candidate: ev.candidate }));
  };
  pintarCompartir();
  pc.onconnectionstatechange = () => {
    $("aSala").classList.toggle("oculto", pc.connectionState !== "connected");
    if (pc.connectionState === "failed" && pcSala === pc) setTimeout(() => { if (pcSala === pc) iniciarSala(); }, 2000);
  };
  const oferta = await pc.createOffer();
  await pc.setLocalDescription(oferta);
  ws.send(JSON.stringify({ type: "medios-oferta", sdp: oferta.sdp }));
}

function cerrarSala() {
  dejarDeCompartir(false);
  if (pcSala) { try { pcSala.close(); } catch (e) {} }
  pcSala = null;
  $("aSala").classList.add("oculto");
  pintarCompartir();
}

// --- Mostrar algo mientras tiene la palabra ------------------------------------------------
// Un segundo video en la misma conexión hacia la sala: en la app es la entrada «Remoto con la
// palabra — contenido». En una compu, su pantalla; en el celular (que no deja compartir la
// pantalla), la otra cámara, para mostrar un papel o un objeto.
let contenido = null;   // {track, transceptor}
let camaras = 0;
navigator.mediaDevices && navigator.mediaDevices.enumerateDevices &&
  navigator.mediaDevices.enumerateDevices().then((l) => { camaras = l.filter((d) => d.kind === "videoinput").length; pintarCompartir(); }).catch(() => {});

function pintarCompartir() {
  const puede = !!pcSala;
  $("compartir").classList.toggle("oculto", !puede);
  const pantalla = !!(navigator.mediaDevices && navigator.mediaDevices.getDisplayMedia);
  $("compartirPantalla").classList.toggle("oculto", !puede || !!contenido || !pantalla);
  $("compartirCamara").classList.toggle("oculto", !puede || !!contenido || camaras < 2);
  $("dejarDeCompartir").classList.toggle("oculto", !contenido);
}

async function renegociar() {
  if (!pcSala || !ws || ws.readyState !== 1) return;
  const oferta = await pcSala.createOffer();
  await pcSala.setLocalDescription(oferta);
  ws.send(JSON.stringify({ type: "medios-oferta", sdp: oferta.sdp }));
}

async function compartir(cual) {
  if (!pcSala || contenido) return;
  let flujo;
  try {
    flujo = cual === "pantalla"
      ? await navigator.mediaDevices.getDisplayMedia({ video: { frameRate: 15 }, audio: false })
      : await navigator.mediaDevices.getUserMedia({ video: { facingMode: { exact: "environment" }, width: 1280, height: 720 }, audio: false });
  } catch (e) {
    if (cual !== "pantalla") {
      try { flujo = await navigator.mediaDevices.getUserMedia({ video: { facingMode: "environment" }, audio: false }); } catch (e2) {}
    }
    if (!flujo) { error(cual === "pantalla" ? "No se pudo compartir la pantalla." : "No se pudo abrir la otra cámara."); return; }
  }
  const track = flujo.getVideoTracks()[0];
  if (!track || !pcSala) { for (const t of flujo.getTracks()) t.stop(); return; }
  track.onended = () => dejarDeCompartir(true); // «Dejar de compartir» del navegador
  contenido = { track, transceptor: pcSala.addTransceiver(track, { direction: "sendonly", streams: [flujo] }) };
  pintarCompartir();
  await renegociar();
}

function dejarDeCompartir(avisar) {
  if (!contenido) return;
  const c = contenido;
  contenido = null;
  try { c.track.stop(); } catch (e) {}
  if (pcSala && avisar !== false) {
    try { c.transceptor.sender.replaceTrack(null); c.transceptor.direction = "inactive"; } catch (e) {}
    renegociar();
  }
  pintarCompartir();
}

// --- Lo que mira (WebRTC que solo recibe): su cámara, la salida principal u otra escena ---
// El gateway le manda el canal elegido; cambiar de canal no reconecta. «Yo» = su cámara grande
// (sigue llegando el sonido, por si la presidencia u operador le hablan). Mirando un canal, su
// cámara queda en un recuadro.
let vistas = [];          // [{id, nombre}] que armó la secretaría
let vista = leerVista();  // la elegida: "yo" | id
let pcVista = null;
let vistaAntesDebate = "";
let sonidoPrendido = false;
let sonidoAntesPtt = null;

function leerVista() { try { return localStorage.getItem("ba-sesion-vista") || ""; } catch (e) { return ""; } }
function guardarVista(v) { try { localStorage.setItem("ba-sesion-vista", v); } catch (e) {} }

function ponerVistas(lista) {
  const habiaDebate = vistas.some((v) => v.id === "debate");
  vistas = Array.isArray(lista) ? lista : [];
  // Apareció el canal del debate y soy uno de los dos: paso solo a verlo (nos vemos entre los dos).
  if (!habiaDebate && debate && vistas.some((v) => v.id === "debate") && vista !== "debate") { vistaAntesDebate = vista; vista = "debate"; }
  // Se terminó el debate: vuelve a lo que miraba antes.
  if (vista === "debate" && !vistas.some((v) => v.id === "debate")) vista = vistaAntesDebate || "";
  if (vista !== "yo" && !vistas.some((v) => v.id === vista)) vista = vistas.length ? vistas[0].id : "yo";
  const caja = $("botonesVista");
  caja.textContent = "";
  for (const v of [{ id: "yo", nombre: "Yo" }].concat(vistas)) {
    const b = document.createElement("button");
    b.type = "button";
    b.textContent = v.nombre || v.id;
    b.dataset.vista = v.id;
    b.addEventListener("click", () => elegirVista(v.id));
    caja.appendChild(b);
  }
  $("vistas").classList.toggle("oculto", vistas.length === 0);
  pintarVista();
  if (pcVista && ws && ws.readyState === 1) ws.send(JSON.stringify({ type: "vista", vista }));
}

function elegirVista(v) {
  vista = v;
  guardarVista(v);
  pintarVista();
  if (ws && ws.readyState === 1) ws.send(JSON.stringify({ type: "vista", vista: v }));
  activarSonido(); // tocar es el gesto que deja sonar al navegador
}

function pintarVista() {
  for (const b of $("botonesVista").children) b.classList.toggle("elegida", b.dataset.vista === vista);
  const viendoCanal = vista !== "yo" && vistas.length > 0;
  $("canal").classList.toggle("oculto", !viendoCanal);
  $("propia").classList.toggle("recuadro", viendoCanal);
  const conImagen = $("canal").srcObject && pcVista && pcVista.connectionState === "connected";
  $("sinCanal").classList.toggle("oculto", !viendoCanal || !!conImagen);
}

async function iniciarVista() {
  if (pcVista || !ws || ws.readyState !== 1) return;
  const pc = new RTCPeerConnection({ iceServers: [] });
  pcVista = pc;
  pc.addTransceiver("video", { direction: "recvonly" });
  pc.addTransceiver("audio", { direction: "recvonly" });
  const flujo = new MediaStream();
  pc.ontrack = (ev) => {
    flujo.addTrack(ev.track);
    $("canal").srcObject = flujo;
    $("sonidoSala").srcObject = flujo;
    pintarVista();
  };
  pc.onicecandidate = (ev) => {
    if (ev.candidate && ws && ws.readyState === 1) ws.send(JSON.stringify({ type: "vista-ice", candidate: ev.candidate }));
  };
  pc.onconnectionstatechange = () => {
    pintarVista();
    if ((pc.connectionState === "failed" || pc.connectionState === "closed") && pcVista === pc) {
      pcVista = null;
      if (estado === "habilitado") setTimeout(iniciarVista, 3000);
    }
  };
  const oferta = await pc.createOffer();
  await pc.setLocalDescription(oferta);
  ws.send(JSON.stringify({ type: "vista-oferta", sdp: oferta.sdp, vista }));
}

function cerrarVista() {
  if (pcVista) { try { pcVista.close(); } catch (e) {} }
  pcVista = null;
  $("canal").srcObject = null;
  $("sonidoSala").srcObject = null;
  pintarVista();
}

function ponerSonido(on) {
  sonidoPrendido = on;
  $("sonidoSala").muted = !on;
  if (on) $("sonidoSala").play().catch(() => {});
  $("sonido").textContent = on ? "🔊 Sonido de la sala" : "🔇 Sonido de la sala";
  $("sonido").classList.toggle("prendido", on);
}
// El navegador no deja sonar nada hasta que la persona toca la página: el primer toque lo destraba.
function activarSonido() { if (sonidoPrendido || pttActivo) $("sonidoSala").play().catch(() => {}); }

function mandarFoto() {
  const v = $("propia");
  if (!ws || ws.readyState !== 1 || !v.videoWidth) return;
  const c = $("lienzo");
  c.getContext("2d").drawImage(v, 0, 0, c.width, c.height);
  const datos = c.toDataURL("image/jpeg", 0.6).split(",")[1] || "";
  if (datos) ws.send(JSON.stringify({ type: "foto", jpeg: datos }));
}

function programarFotos() {
  clearInterval(timerFoto);
  // Mientras espera, una por segundo (la presidencia lo está mirando); habilitado, cada 3 s
  // (es su cara en la banca del recinto que ve el operador).
  timerFoto = setInterval(mandarFoto, estado === "habilitado" ? 3000 : 1000);
}

// --- El WebSocket -----------------------------------------------------------------
function conectar() {
  const proto = location.protocol === "https:" ? "wss:" : "ws:";
  ws = new WebSocket(proto + "//" + location.host + "/ws?sesion=1");
  ws.onopen = () => ws.send(JSON.stringify(conLink ? { type: "auth", i: integrante, t: token, pin }
                                                   : { type: "auth", u: usuario, clave: pin }));
  ws.onmessage = (ev) => {
    let m;
    try { m = JSON.parse(ev.data); } catch (e) { return; }
    recibir(m);
  };
  ws.onclose = () => {
    conexion(false);
    cerrarVista();
    clearInterval(timerPing);
    if (cortadoAProposito || !pin) return;
    setTimeout(conectar, reintento);
    reintento = Math.min(reintento * 2, 15000);
  };
}

function recibir(m) {
  switch (m.type) {
    case "auth":
      if (!m.ok) {
        pin = "";
        cortadoAProposito = true;
        mostrar("entrar");
        $("botonEntrar").disabled = false;
        error(m.error || "No se pudo entrar.");
        return;
      }
      reintento = 1000;
      conexion(true);
      $("quien").textContent = (m.nombre || "") + (m.banca ? " · Banca " + m.banca : "");
      lugar = m.lugar || "afuera";
      cerrarVista(); // la de la conexión anterior no sigue
      ponerSonido(lugar !== "sala"); // en la sala, el sonido apagado: el celular acoplaría
      mostrar("sala");
      prenderCamara().then(programarFotos);
      clearInterval(timerPing);
      timerPing = setInterval(() => ws && ws.readyState === 1 && ws.send('{"type":"ping"}'), 20000);
      ws.send(JSON.stringify({ type: "hola" }));
      break;
    case "estado":
      ponerEstado(m.estado, m.texto);
      break;
    case "fila":
      ponerPedido(m.turno || 0);
      break;
    case "palabra":
      if (debate && m.activa) break; // en el debate manda el turno
      if (!m.activa) debate = null;
      ponerPalabra(!!m.activa, m.segundos || 0);
      break;
    case "debate":
      ponerDebate(m);
      break;
    case "tiempo":
      avisoTiempo(m.aviso);
      break;
    case "rtc":
      rtc(m);
      break;
    case "medios":
      if (m.activo) iniciarSala(); else cerrarSala();
      if (m.error) error(m.error);
      break;
    case "medios-respuesta":
      if (pcSala) pcSala.setRemoteDescription({ type: "answer", sdp: m.sdp }).catch(() => {});
      break;
    case "medios-ice":
      if (pcSala && m.candidate) pcSala.addIceCandidate(m.candidate).catch(() => {});
      break;
    case "ptt": {
      pttActivo = !!m.activo;
      $("pttAviso").classList.toggle("oculto", !pttActivo);
      // El operador desde la app le habla por el sonido de su vista (sin abrirle el micrófono);
      // la presidencia, por la conexión directa (y él le contesta).
      const delOperador = m.consola === "operador";
      $("pttTitulo").textContent = delOperador ? "Te están hablando desde la cabina" : "La presidencia te está hablando";
      if (!delOperador) microfono(pttActivo || (palabraHasta > 0 && (!debate || debate.habla)));
      if (delOperador) {
        if (pttActivo) { sonidoAntesPtt = sonidoPrendido; ponerSonido(true); }
        else if (sonidoAntesPtt !== null) { ponerSonido(sonidoAntesPtt); sonidoAntesPtt = null; }
      }
      if (pttActivo && navigator.vibrate) navigator.vibrate(200);
      break;
    }
    case "vistas":
      ponerVistas(m.lista);
      break;
    case "vista-respuesta":
      if (pcVista) pcVista.setRemoteDescription({ type: "answer", sdp: m.sdp }).catch(() => {});
      break;
    case "vista-ice":
      if (pcVista && m.candidate) pcVista.addIceCandidate(m.candidate).catch(() => {});
      break;
    case "vista-error":
      $("sinCanal").textContent = m.error || "No llega la imagen.";
      break;
    case "votacion":
      ponerVotacion(m);
      break;
    case "voto":
      if (m.ok) { if (vot) vot.mi_voto = m.opcion; pintarVotacion(); if (navigator.vibrate) navigator.vibrate(120); }
      else { $("votMio").textContent = m.error || "No se pudo votar."; }
      break;
    case "resultado":
      mostrarResultado(m);
      break;
    case "info":
      if ("punto" in m) $("punto").textContent = m.punto || "—";
      if ("hablando" in m) $("hablando").textContent = m.hablando || "—";
      if ("proximos" in m) {
        const ol = $("proximos");
        ol.textContent = "";
        for (const t of (m.proximos || [])) { const li = document.createElement("li"); li.textContent = t; ol.appendChild(li); }
        if (!(m.proximos || []).length) { const li = document.createElement("li"); li.textContent = "—"; ol.appendChild(li); }
      }
      break;
    case "cortado":
      cortadoAProposito = true;
      pin = "";
      $("cortadoTexto").textContent = m.motivo || "Se cerró la sesión.";
      mostrar("cortado");
      break;
  }
}

// --- Arranque -----------------------------------------------------------------------
$("formPin").addEventListener("submit", (e) => {
  e.preventDefault();
  pin = $("pin").value.trim();
  usuario = $("usuario").value.trim();
  $("pin").value = "";
  if (!pin || (!conLink && !usuario)) return;
  error("");
  $("botonEntrar").disabled = true;
  cortadoAProposito = false;
  conectar();
});
$("pedir").addEventListener("click", () => {
  if (!ws || ws.readyState !== 1) return;
  ws.send(JSON.stringify({ type: "palabra", accion: pidio ? "cancelar" : "pedir" }));
  // Se ve enseguida; la fila de verdad la confirma la app (mensaje «fila»).
  ponerPedido(pidio ? 0 : 99);
  if (pidio) $("turno").textContent = "Pedido enviado";
});
$("cerrarResultado").addEventListener("click", () => $("resultado").classList.add("oculto"));
$("sonido").addEventListener("click", () => ponerSonido(!sonidoPrendido));
$("compartirPantalla").addEventListener("click", () => compartir("pantalla"));
$("compartirCamara").addEventListener("click", () => compartir("camara"));
$("dejarDeCompartir").addEventListener("click", () => dejarDeCompartir(true));
document.addEventListener("pointerdown", activarSonido, { passive: true });
$("reintentar").addEventListener("click", () => {
  mostrar("entrar");
  $("botonEntrar").disabled = false;
});

if (!conLink) {
  // Sin link: usuario y contraseña.
  $("entrarTexto").textContent = integrante || token
    ? "Este link está incompleto. Abrí el link personal que te mandó la secretaría, o entrá con tu usuario y contraseña."
    : "Entrá con el usuario y la contraseña que te dio la secretaría (o abrí tu link personal).";
  $("usuario").classList.remove("oculto");
  $("formPin").classList.add("dos");
  $("pin").placeholder = "Contraseña";
  $("pin").setAttribute("inputmode", "text");
  $("pin").removeAttribute("pattern");
  $("usuario").focus();
}
mostrar("entrar");
