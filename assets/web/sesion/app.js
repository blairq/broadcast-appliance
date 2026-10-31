// La sesión remota del integrante (docs/funciones/SESION-REMOTA.md). Mientras espera, el
// asistente lo puede atender hablando (docs/funciones/ATENCION.md). Fase R1: entrar con el
// link personal y el PIN, mostrar la cámara propia y mandar una foto de vez en
// cuando para que la presidencia lo vea antes de habilitarlo. El audio y el
// video por WebRTC, la palabra y el voto llegan en las fases siguientes.
"use strict";

const $ = (id) => document.getElementById(id);
const llenar = (s, ...v) => BAi18n.llenar(s, ...v);
const frag = new URLSearchParams(location.hash.slice(1));
const integrante = frag.get("i") || "";
const token = frag.get("t") || "";

const conLink = integrante !== "" && token.length === 64;
// El PIN adentro del link (opción «PIN en el QR» de la app): entra sin tipear nada.
const pinDelLink = conLink ? (frag.get("p") || "") : "";
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

let textoEstado = "";   // el texto del estado que mandó la app (si mandó)
function pintarEstado() {
  const hab = estado === "habilitado";
  $("estadoTitulo").textContent = hab ? t("Estás habilitado: contás como presente") : t("Esperando que te habiliten");
  $("estadoTexto").textContent = textoEstado || (hab
    ? t("La presidencia te habilitó. Dejá la cámara prendida durante la sesión.")
    : t("La presidencia te va a ver por la cámara y te habilita. Hasta entonces no contás para el quórum."));
}

function ponerEstado(nuevo, texto) {
  estado = nuevo;
  textoEstado = texto || "";
  const hab = nuevo === "habilitado";
  $("estado").className = "estado " + (hab ? "habilitado" : "esperando");
  pintarEstado();
  programarFotos();
  if (hab) cerrarAtencion(); // la atención es de la espera: habilitado, termina (la app también la apaga)
  $("pedir").disabled = !hab;
  // Lo que mira en el celular: solo habilitado (no gasta un lugar de la sala mientras espera).
  if (hab) iniciarVista(); else cerrarVista();
}

// --- La palabra ---------------------------------------------------------------------
let turnoActual = 0;
function ponerPedido(turno) {
  pidio = turno > 0;
  turnoActual = turno;
  $("pedir").textContent = pidio ? t("Cancelar mi pedido") : t("Pedir la palabra");
  $("pedir").classList.toggle("pedido", pidio);
  $("turno").textContent = turno > 0 ? (turno === 1 ? t("Sos el próximo") : llenar(t("Tu turno: %1"), turno)) : "";
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
  $("palabraTitulo").textContent = titulo || t("Tenés la palabra");
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
               debate.habla ? t("Debate: es tu turno") : t("Debate: habla el otro, esperá tu turno"));
}

function avisoTiempo(aviso) {
  if (aviso === "ultimo_minuto") {
    $("palabra").classList.add("minuto");
    $("palabraTexto").textContent = t("Queda 1 minuto");
    if (navigator.vibrate) navigator.vibrate(300);
  } else if (aviso === "terminado") {
    $("palabra").classList.remove("minuto");
    $("palabra").classList.add("terminado");
    $("palabraTitulo").textContent = t("Se terminó tu tiempo");
    $("palabraTexto").textContent = t("Redondeá, por favor");
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
  $("votNumero").textContent = vot.numero ? llenar(t("N.º %1"), vot.numero) : "";
  $("votTema").textContent = vot.tema || t("Votación");
  const pausada = vot.estado === "pausada";
  $("votEstado").textContent = pausada ? t("En pausa: esperá.") : t("Tocá tu voto. Lo podés cambiar mientras esté abierta.");
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
  $("votMio").textContent = elegida ? llenar(t("Tu voto: %1"), elegida.texto) : t("Todavía no votaste.");
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
  $("resTema").textContent = m.tema || t("Votación");
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
// los mensajes (sin medios): en la red de la sala va directo; desde afuera, por el TURN de
// la caja (../comun/ice.js, docs/funciones/TURN.md).
const conexiones = new Map(); // consola -> RTCPeerConnection
let pttActivo = false;

async function rtc(m) {
  const de = m.de;
  if (!de || de.indexOf("consola:") !== 0) return;
  let pc = conexiones.get(de);
  if (m.tipo === "offer") {
    if (pc) pc.close();
    pc = new RTCPeerConnection(window.BAIce ? BAIce.config() : { iceServers: [] }); // desde afuera, por el TURN de la caja
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
  if (window.BAIce) await BAIce.listo();
  if (!local || !ws || ws.readyState !== 1) return;
  const pc = new RTCPeerConnection(window.BAIce ? BAIce.config() : { iceServers: [] });
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

// --- Lo atiende el asistente hablando, mientras espera ---------------------------------------
// (docs/funciones/ATENCION.md) La app lo prende y lo apaga ({type:"atiende", on}): su micrófono
// va al gateway por un WebRTC de solo audio (el gateway rechaza cualquier otra cosa), se pasa a
// texto, y la respuesta del asistente le llega con voz por la misma conexión, solo a él. Nunca
// va a la sala ni a la transmisión. Termina al habilitarlo.
let pcAtiende = null;
let microAtiende = null; // una copia de la pista del micrófono: la de `local` sigue cerrada

async function iniciarAtencion() {
  cerrarAtencion();
  $("atiende").classList.remove("oculto");
  $("atiendeDice").classList.add("oculto");
  $("atiendeTexto").textContent = t("Tu micrófono está abierto solo para el asistente (no para la sala ni la transmisión) hasta que te habiliten.");
  if (navigator.vibrate) navigator.vibrate(120);
  try {
    // Con el permiso que ya dio al entrar alcanza; si no lo dio, el navegador lo pide ahora.
    const pista = local && local.getAudioTracks()[0];
    if (pista) {
      microAtiende = pista.clone();
    } else {
      const s = await navigator.mediaDevices.getUserMedia({
        audio: { echoCancellation: true, noiseSuppression: true, autoGainControl: true }, video: false });
      microAtiende = s.getAudioTracks()[0] || null;
    }
  } catch (e) {
    microAtiende = null;
  }
  if (!microAtiende) {
    $("atiendeTexto").textContent = t("Sin permiso para el micrófono el asistente no te puede escuchar. La presidencia te ve por la cámara igual.");
    if (ws && ws.readyState === 1) ws.send(JSON.stringify({ type: "atiende-error", error: "no dio permiso para el micrófono" }));
    return;
  }
  microAtiende.enabled = true;
  if (window.BAIce) await BAIce.listo();
  if (!ws || ws.readyState !== 1 || $("atiende").classList.contains("oculto")) { cerrarAtencion(); return; }
  const pc = new RTCPeerConnection(window.BAIce ? BAIce.config() : { iceServers: [] });
  pcAtiende = pc;
  // sendrecv: sube su voz y baja la del asistente por la misma pista.
  pc.addTransceiver(microAtiende, { direction: "sendrecv", streams: [new MediaStream([microAtiende])] });
  pc.ontrack = (ev) => {
    $("vozAtiende").srcObject = ev.streams[0] || new MediaStream([ev.track]);
    oirAtencion();
  };
  pc.onicecandidate = (ev) => {
    if (ev.candidate && ws && ws.readyState === 1) ws.send(JSON.stringify({ type: "atiende-ice", candidate: ev.candidate }));
  };
  pc.onconnectionstatechange = () => {
    if (pc.connectionState === "failed" && pcAtiende === pc) setTimeout(() => { if (pcAtiende === pc) iniciarAtencion(); }, 2000);
  };
  const oferta = await pc.createOffer();
  await pc.setLocalDescription(oferta);
  ws.send(JSON.stringify({ type: "atiende-oferta", sdp: oferta.sdp }));
}

// El navegador no deja sonar nada hasta que la persona toca la página: si no arranca solo, un botón.
function oirAtencion() {
  $("vozAtiende").play().then(() => $("atiendeOir").classList.add("oculto"))
    .catch(() => $("atiendeOir").classList.remove("oculto"));
}

function cerrarAtencion() {
  if (pcAtiende) { try { pcAtiende.close(); } catch (e) {} }
  pcAtiende = null;
  if (microAtiende) { try { microAtiende.stop(); } catch (e) {} }
  microAtiende = null;
  $("vozAtiende").srcObject = null;
  $("atiende").classList.add("oculto");
  $("atiendeOir").classList.add("oculto");
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
    if (!flujo) { error(cual === "pantalla" ? t("No se pudo compartir la pantalla.") : t("No se pudo abrir la otra cámara.")); return; }
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
  for (const v of [{ id: "yo", nombre: t("Yo") }].concat(vistas)) {
    const b = document.createElement("button");
    b.type = "button";
    b.textContent = v.nombre || v.id;
    b.dataset.vista = v.id;
    // Con varias salidas, cada una con su color (el mismo del tally y del multiview).
    if (/^#[0-9a-fA-F]{3,8}$/.test(v.color || "")) { b.classList.add("con-salida"); b.style.setProperty("--color-salida", v.color); }
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
  if (window.BAIce) await BAIce.listo();
  if (pcVista || !ws || ws.readyState !== 1) return;
  const pc = new RTCPeerConnection(window.BAIce ? BAIce.config() : { iceServers: [] });
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
  $("sonido").textContent = on ? t("🔊 Sonido de la sala") : t("🔇 Sonido de la sala");
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
    cerrarAtencion(); // si vuelve y lo seguían atendiendo, el gateway se lo vuelve a pedir
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
        error(t(m.error) || t("No se pudo entrar."));
        return;
      }
      reintento = 1000;
      conexion(true);
      $("quien").textContent = (m.nombre || "") + (m.banca ? " · " + llenar(t("Banca %1"), m.banca) : "");
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
      if (m.error) error(t(m.error));
      break;
    case "atiende":
      if (m.on && estado !== "habilitado") iniciarAtencion(); else cerrarAtencion();
      break;
    case "atiende-respuesta":
      if (pcAtiende) pcAtiende.setRemoteDescription({ type: "answer", sdp: m.sdp }).catch(() => {});
      break;
    case "atiende-ice":
      if (pcAtiende && m.candidate) pcAtiende.addIceCandidate(m.candidate).catch(() => {});
      break;
    case "atiende-dice":
      // Lo que le dijo el asistente, también escrito (por si no lo oyó bien).
      $("atiendeDice").textContent = llenar(t("Asistente: %1"), m.texto || "");
      $("atiendeDice").classList.toggle("oculto", !m.texto);
      break;
    case "atiende-fallo":
      if (pcAtiende) { try { pcAtiende.close(); } catch (e) {} }
      pcAtiende = null;
      $("atiendeTexto").textContent = (t(m.error) || t("No se pudo abrir el micrófono hacia el asistente.")) +
        " " + t("La presidencia te ve por la cámara igual.");
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
      $("pttTitulo").textContent = delOperador ? t("Te están hablando desde la cabina") : t("La presidencia te está hablando");
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
      $("sinCanal").textContent = t(m.error) || t("No llega la imagen.");
      break;
    case "votacion":
      ponerVotacion(m);
      break;
    case "voto":
      if (m.ok) { if (vot) vot.mi_voto = m.opcion; pintarVotacion(); if (navigator.vibrate) navigator.vibrate(120); }
      else { $("votMio").textContent = t(m.error) || t("No se pudo votar."); }
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
      $("cortadoTexto").textContent = t(m.motivo) || t("Se cerró la sesión.");
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
  if (pidio) $("turno").textContent = t("Pedido enviado");
});
$("cerrarResultado").addEventListener("click", () => $("resultado").classList.add("oculto"));
$("sonido").addEventListener("click", () => ponerSonido(!sonidoPrendido));
$("compartirPantalla").addEventListener("click", () => compartir("pantalla"));
$("compartirCamara").addEventListener("click", () => compartir("camara"));
$("dejarDeCompartir").addEventListener("click", () => dejarDeCompartir(true));
$("atiendeOir").addEventListener("click", oirAtencion);
document.addEventListener("pointerdown", activarSonido, { passive: true });
$("reintentar").addEventListener("click", () => {
  mostrar("entrar");
  $("botonEntrar").disabled = false;
});

// Los textos de la entrada sin link (usuario y contraseña).
function textosSinLink() {
  $("entrarTexto").textContent = integrante || token
    ? t("Este link está incompleto. Abrí el link personal que te mandó la secretaría, o entrá con tu usuario y contraseña.")
    : t("Entrá con el usuario y la contraseña que te dio la secretaría (o abrí tu link personal).");
  $("pin").placeholder = t("Contraseña");
}

// --- El idioma -------------------------------------------------------------------------
// Un selector chico arriba: los idiomas que hay; al cambiar, se vuelve a pintar.
function armarIdiomas() {
  const sel = $("idioma");
  const lista = BAi18n.idiomas;
  sel.classList.toggle("oculto", lista.length < 2);
  sel.textContent = "";
  for (const p of lista) {
    const o = document.createElement("option");
    o.value = p.codigo;
    o.textContent = p.nombre;
    sel.appendChild(o);
  }
  sel.value = BAi18n.idioma;
}
$("idioma").addEventListener("change", async (e) => {
  await BAi18n.elegir(e.target.value);
  armarIdiomas();
  if (!conLink) textosSinLink();
  pintarEstado();
  ponerPedido(turnoActual);
  ponerSonido(sonidoPrendido);
  pintarVotacion();
  const yo = $("botonesVista").querySelector('[data-vista="yo"]');
  if (yo) yo.textContent = t("Yo");
});

(async () => {
  // Los textos: el idioma del navegador del integrante (o el que elija arriba).
  await BAi18n.iniciar({ contexto: "web/sesion", preferir: "navegador" });
  armarIdiomas();
  if (pinDelLink) {
    pin = pinDelLink;
    $("botonEntrar").disabled = true;
    conectar();
  }
  if (!conLink) {
    // Sin link: usuario y contraseña.
    textosSinLink();
    $("usuario").classList.remove("oculto");
    $("formPin").classList.add("dos");
    $("pin").setAttribute("inputmode", "text");
    $("pin").removeAttribute("pattern");
    $("usuario").focus();
  }
  mostrar("entrar");
})();
