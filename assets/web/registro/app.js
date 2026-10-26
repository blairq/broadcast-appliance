// El registro de personas por tótem (docs/funciones/REGISTRO-TOTEM.md). La página es solo la
// cara y los oídos: la conversación (qué se pregunta, qué se entendió, cuándo termina) la lleva
// la app, que manda por el gateway lo que hay que mostrar. Acá: entrar con el enlace del tótem,
// pedir un canal, mandar el micrófono y la cámara por WebRTC (y oír al asistente por la misma
// conexión) y mostrar en grande lo que llega. La cara se reconoce en la app, sobre ese video.
"use strict";

const $ = (id) => document.getElementById(id);
const frag = new URLSearchParams(location.hash.slice(1));
const busqueda = new URLSearchParams(location.search);
const totem = frag.get("r") || "";
const token = frag.get("t") || "";
// Un tótem: arranca solo y, al terminar con una persona, se queda con su canal para la siguiente.
const kiosco = busqueda.get("kiosco") === "1";
let soloPantalla = busqueda.get("pantalla") === "1";

let ws = null;
let reintento = 1000;
let terminado = false;   // cortado o enlace inválido: no se reconecta
let timerPing = 0;
let medios = null;       // la cámara y el micrófono
let empezado = false;    // ya hay cámara y micrófono (la persona tocó, o el kiosco arrancó solo)
let canal = 0;           // el canal (1..8) que dio el gateway; 0 = ninguno
let timerCanal = 0;
let empezarConCanal = false; // tocó «Empezar» sin canal: al llegar el canal, se le avisa a la app
let pc = null;
let timerGracias = 0;
let pedido = null;       // el campo que se está pidiendo {clave, etiqueta, tipo, ejemplo}
let titulo = "";
let estadoActual = "esperando";

const kTextoEspera = "Mirá a la cámara para empezar.";
const kEstados = { esperando: "Esperando", escuchando: "Te escucho", hablando: "Hablando", pensando: "Pensando",
                   foto: "Mirá a la cámara", documento: "Mostrá tu documento", listo: "Listo", error: "Problema" };

function mostrar(id) {
  for (const s of ["inicio", "registro", "gracias", "qr", "cortado"]) $(s).classList.toggle("oculto", s !== id);
}

function conexion(viva) {
  $("conexion").classList.toggle("viva", viva);
  $("conexion").classList.toggle("apagada", !viva);
}

function cortar(texto) {
  terminado = true;
  pararMedios();
  $("cortadoTexto").textContent = texto;
  mostrar("cortado");
  if (ws) { try { ws.close(); } catch (e) {} }
}

// --- La pantalla que solo muestra: el QR de esta misma página para el celular ----------
function urlParaCelular() {
  const u = new URL(location.href);
  u.searchParams.delete("pantalla");
  u.searchParams.delete("kiosco");
  return u.toString(); // con el fragmento (#r=…&t=…): es lo que abre el celular
}

function mostrarQr() {
  soloPantalla = true;
  pararMedios();
  // Lo dibuja el gateway (la misma que usan los televisores): sin bibliotecas en la página.
  $("qrImagen").src = "/pantalla/qr.svg?texto=" + encodeURIComponent(urlParaCelular());
  $("qrTitulo").textContent = titulo;
  mostrar("qr");
}

// ¿Tiene cámara y micrófono? Un Chromecast o una TV no: muestra el QR. Sin contexto seguro
// (http) tampoco hay getUserMedia.
async function puedeRegistrar() {
  const md = navigator.mediaDevices;
  if (!md || !md.getUserMedia) return false;
  if (!md.enumerateDevices) return true;
  try {
    const lista = await md.enumerateDevices();
    return lista.some((d) => d.kind === "audioinput") && lista.some((d) => d.kind === "videoinput");
  } catch (e) {
    return true; // no se sabe: que lo diga getUserMedia
  }
}

// --- Empezar: cámara, micrófono y sonido (el navegador pide un toque para el sonido) -------
async function empezar() {
  if (empezado || soloPantalla) return;
  $("empezar").disabled = true;
  let falla = null;
  try {
    medios = await navigator.mediaDevices.getUserMedia({
      video: { width: { ideal: 1280 }, height: { ideal: 720 }, facingMode: "user" },
      audio: { echoCancellation: true, noiseSuppression: true, autoGainControl: true },
    });
  } catch (e) {
    medios = null;
    falla = e;
  }
  $("empezar").disabled = false;
  if (!medios || !medios.getAudioTracks().length || !medios.getVideoTracks().length) {
    pararMedios();
    if (falla && (falla.name === "NotAllowedError" || falla.name === "SecurityError")) {
      $("inicioTexto").textContent = "Sin permiso para la cámara y el micrófono no te puedo registrar. Revisá los permisos del navegador y tocá de nuevo.";
      mostrar("inicio");
      return;
    }
    mostrarQr(); // no hay con qué: que se registren desde el celular
    return;
  }
  empezado = true;
  $("propia").srcObject = new MediaStream(medios.getVideoTracks());
  if (kiosco && document.documentElement.requestFullscreen) document.documentElement.requestFullscreen().catch(() => {});
  if (navigator.wakeLock) navigator.wakeLock.request("screen").catch(() => {});
  oir(); // dentro del toque: destraba el sonido para cuando hable el asistente
  limpiar();
  mostrar("registro");
  pedirCanal();
}

function pararMedios() {
  cerrarAudio();
  if (medios) for (const t of medios.getTracks()) { try { t.stop(); } catch (e) {} }
  medios = null;
  empezado = false;
  $("propia").srcObject = null;
}

// --- El canal de audio ------------------------------------------------------------------
function pedirCanal() {
  clearTimeout(timerCanal);
  if (!empezado || canal || !ws || ws.readyState !== 1) return;
  ws.send(JSON.stringify({ type: "canal" }));
}

function tomarCanal(k) {
  clearTimeout(timerCanal);
  if (!k) {
    // Están todos ocupados: se espera y se vuelve a pedir.
    $("espera").classList.remove("oculto");
    timerCanal = setTimeout(pedirCanal, 3000);
    return;
  }
  canal = k;
  $("espera").classList.add("oculto");
  iniciarAudio();
  if (empezarConCanal) {
    empezarConCanal = false;
    ws.send(JSON.stringify({ type: "empezar" }));
  }
}

function soltarCanal() {
  clearTimeout(timerCanal);
  cerrarAudio();
  if (canal && ws && ws.readyState === 1) ws.send(JSON.stringify({ type: "soltar" }));
  canal = 0;
}

// Suben el micrófono y la cámara (el gateway rechaza cualquier otra cosa): la cámara va a la app,
// que reconoce la cara y la puede mostrar (un monitor, una escena). La voz del asistente baja por
// la pista del micrófono, y así el navegador cancela su eco y no se transcribe a sí mismo.
async function iniciarAudio() {
  cerrarAudio();
  if (window.BAIce) await BAIce.listo();
  if (!ws || ws.readyState !== 1 || !medios || !canal) return;
  const pista = medios.getAudioTracks()[0];
  if (!pista) return;
  const p = new RTCPeerConnection(window.BAIce ? BAIce.config() : { iceServers: [] });
  pc = p;
  const camara = medios.getVideoTracks()[0];
  const flujo = new MediaStream(camara ? [pista, camara] : [pista]);
  p.addTransceiver(pista, { direction: "sendrecv", streams: [flujo] });
  if (camara) p.addTransceiver(camara, { direction: "sendonly", streams: [flujo] });
  p.ontrack = (ev) => {
    $("voz").srcObject = ev.streams[0] || new MediaStream([ev.track]);
    oir();
  };
  p.onicecandidate = (ev) => {
    if (ev.candidate && ws && ws.readyState === 1) ws.send(JSON.stringify({ type: "ice", candidate: ev.candidate }));
  };
  p.onconnectionstatechange = () => {
    if (p.connectionState === "failed" && pc === p) setTimeout(() => { if (pc === p && canal) iniciarAudio(); }, 2000);
  };
  const oferta = await p.createOffer();
  await p.setLocalDescription(oferta);
  if (pc !== p || !ws || ws.readyState !== 1) return;
  ws.send(JSON.stringify({ type: "oferta", sdp: oferta.sdp }));
}

function cerrarAudio() {
  if (pc) { try { pc.close(); } catch (e) {} }
  pc = null;
  $("voz").srcObject = null;
}

// El navegador no deja sonar nada hasta que la persona toca la página: si no arranca, un botón.
function oir() {
  $("voz").play().then(() => $("oir").classList.add("oculto"))
    .catch(() => { if ($("voz").srcObject) $("oir").classList.remove("oculto"); });
}

// --- Lo que se muestra --------------------------------------------------------------------
function ponerEstado(e) {
  if (!kEstados[e]) return;
  estadoActual = e;
  const el = $("estado");
  el.className = "estado " + e;
  $("estadoTexto").textContent = kEstados[e];
  const charlando = e === "escuchando" || e === "hablando" || e === "pensando" || e === "foto" || e === "documento";
  document.body.classList.toggle("foto", e === "foto");
  // Mostrando el documento: la cámara enmarcada, como en la foto (la app busca el código).
  document.body.classList.toggle("documento", e === "documento");
  $("cancelar").classList.toggle("oculto", !charlando);
  $("arrancar").classList.toggle("oculto", charlando);
}

function ponerCampos(lista) {
  const ul = $("campos");
  ul.textContent = "";
  for (const c of Array.isArray(lista) ? lista : []) {
    const li = document.createElement("li");
    if (c.listo) li.classList.add("listo");
    if (pedido && pedido.clave === c.clave && !c.listo) li.classList.add("pidiendo");
    const et = document.createElement("span");
    et.className = "etiqueta";
    et.textContent = c.etiqueta || c.clave || "";
    const va = document.createElement("span");
    va.className = "valor";
    va.textContent = c.valor === undefined || c.valor === null ? "" : String(c.valor);
    li.appendChild(et);
    li.appendChild(va);
    ul.appendChild(li);
  }
}

const kTipoInput = { correo: "email", telefono: "tel", numero: "number" };

function ponerPedido(m) {
  pedido = m && m.clave ? m : null;
  if (!pedido) { $("caja").classList.add("oculto"); return; }
  $("cajaEtiqueta").textContent = (pedido.etiqueta || pedido.clave) +
    (pedido.tipo === "sino" ? "" : pedido.tipo === "documento" ? " — si no lo lee, escribí el número acá" : " — si preferís, escribilo acá");
  const sino = pedido.tipo === "sino";
  $("sino").classList.toggle("oculto", !sino);
  $("cajaValor").parentElement.classList.toggle("oculto", sino);
  const input = $("cajaValor");
  input.type = kTipoInput[pedido.tipo] || "text";
  input.inputMode = pedido.tipo === "telefono" ? "tel" : pedido.tipo === "numero" || pedido.tipo === "documento" ? "numeric"
                  : pedido.tipo === "correo" ? "email" : "text";
  input.autocapitalize = pedido.tipo === "nombre" ? "words" : "off";
  input.placeholder = pedido.ejemplo || "";
  input.value = "";
  $("caja").classList.remove("oculto");
}

function mandarEscrito(valor) {
  if (!pedido || !ws || ws.readyState !== 1) return;
  const v = String(valor).trim();
  if (!v) return;
  ws.send(JSON.stringify({ type: "escrito", clave: pedido.clave, valor: v }));
  $("cajaValor").value = "";
}

function limpiar() {
  clearTimeout(timerGracias);
  ponerAtiende(false);
  $("dice").textContent = kTextoEspera;
  $("oido").textContent = "";
  $("oido").classList.add("oculto");
  ponerPedido(null);
  ponerCampos([]);
  ponerEstado("esperando");
}

function gracias(texto) {
  ponerPedido(null);
  $("graciasTexto").textContent = texto || "¡Listo! Ya quedaste registrado.";
  // En el celular de cada uno, la conversación terminó: el canal queda para otro. El tótem se
  // lo queda para la persona que sigue.
  if (!kiosco) soltarCanal();
  $("otra").classList.toggle("oculto", kiosco);
  mostrar("gracias");
  clearTimeout(timerGracias);
  timerGracias = setTimeout(volverAEsperar, 8000);
}

function volverAEsperar() {
  if (soloPantalla || terminado || !empezado) return;
  limpiar();
  if (!canal) $("dice").textContent = kiosco ? kTextoEspera : "Si querés registrar a otra persona, tocá Empezar.";
  mostrar("registro");
}

// --- El WebSocket ----------------------------------------------------------------------------
function conectar() {
  const proto = location.protocol === "https:" ? "wss:" : "ws:";
  ws = new WebSocket(proto + "//" + location.host + "/ws?registro=1");
  ws.onopen = () => ws.send(JSON.stringify({ type: "auth", r: totem, t: token }));
  ws.onmessage = (ev) => {
    let m;
    try { m = JSON.parse(ev.data); } catch (e) { return; }
    recibir(m);
  };
  ws.onclose = () => {
    conexion(false);
    clearInterval(timerPing);
    clearTimeout(timerCanal);
    cerrarAudio();
    canal = 0; // el gateway lo soltó con la conexión: al volver se pide otro
    if (terminado) return;
    setTimeout(conectar, reintento);
    reintento = Math.min(reintento * 2, 15000);
  };
}

function recibir(m) {
  switch (m.type) {
    case "hola":
      reintento = 1000;
      conexion(true);
      if (!titulo && m.nombre) { titulo = m.nombre; $("titulo").textContent = titulo; }
      clearInterval(timerPing);
      timerPing = setInterval(() => ws && ws.readyState === 1 && ws.send('{"type":"ping"}'), 20000);
      if (soloPantalla) mostrarQr();
      else if (empezado) { mostrar("registro"); pedirCanal(); } // volvió la conexión en el medio
      else { mostrar("inicio"); if (kiosco) empezar(); }
      break;
    case "error":
      cortar(m.error || "Este enlace de registro no es válido.");
      break;
    case "cortado":
      cortar(m.motivo || "Se cerró esta pantalla de registro.");
      break;
    case "canal":
      tomarCanal(m.canal || 0);
      break;
    case "respuesta":
      if (pc) pc.setRemoteDescription({ type: "answer", sdp: m.sdp }).catch(() => {});
      break;
    case "ice":
      if (pc && m.candidate) pc.addIceCandidate(m.candidate).catch(() => {});
      break;
    case "audio-error":
      // No se pudo abrir el audio ahora: se reintenta en un rato (la app sigue con la pantalla táctil).
      cerrarAudio();
      setTimeout(() => { if (canal && !pc) iniciarAudio(); }, 3000);
      break;
    case "pantalla":
      titulo = m.titulo || titulo;
      $("titulo").textContent = titulo || "Registro";
      document.title = titulo || "Registro";
      $("subtitulo").textContent = m.subtitulo || "";
      $("subtitulo").classList.toggle("oculto", !m.subtitulo);
      $("qrTitulo").textContent = titulo;
      break;
    case "dice":
      if (!$("gracias").classList.contains("oculto")) mostrar("registro");
      $("dice").textContent = m.texto || "";
      break;
    case "oido":
      $("oido").textContent = m.texto || "";
      $("oido").classList.toggle("oculto", !m.texto);
      break;
    case "estado":
      ponerEstado(m.estado);
      break;
    case "campos":
      ponerCampos(m.lista);
      break;
    case "pedir":
      ponerPedido(m);
      break;
    case "listo":
      gracias(m.texto);
      break;
    case "reiniciar":
      if (empezado && !soloPantalla) { limpiar(); mostrar("registro"); }
      break;
    case "atiende":
      ponerAtiende(!!m.activo, m.nombre);
      break;
    case "pong":
      break;
  }
}

// --- La atiende una persona (la atención remota) -------------------------------------------
// Alguien del equipo tomó la conversación: el asistente se calla y lo que se oye por el
// parlante es esa persona. Se dice en grande, para que nadie piense que se colgó.
function ponerAtiende(activo, quien) {
  const el = $("atiende");
  el.textContent = activo ? "Te atiende " + (quien || "una persona del equipo") : "";
  el.classList.toggle("oculto", !activo);
  if (activo) {
    $("estado").className = "estado atendida";
    $("estadoTexto").textContent = "Te atiende una persona";
    $("cancelar").classList.remove("oculto");
    $("arrancar").classList.add("oculto");
    oir(); // su voz llega por la misma pista que la del asistente
  } else if ($("estado").classList.contains("atendida")) {
    ponerEstado(estadoActual === "atendida" ? "escuchando" : estadoActual);
  }
}

// --- Botones ------------------------------------------------------------------------------
$("empezar").addEventListener("click", empezar);
$("oir").addEventListener("click", oir);
$("arrancar").addEventListener("click", () => {
  if (!ws || ws.readyState !== 1) return;
  if (!canal) { empezarConCanal = true; pedirCanal(); return; }
  ws.send(JSON.stringify({ type: "empezar" }));
});
$("cancelar").addEventListener("click", () => {
  if (ws && ws.readyState === 1) ws.send(JSON.stringify({ type: "cancelar" }));
});
$("caja").addEventListener("submit", (ev) => { ev.preventDefault(); mandarEscrito($("cajaValor").value); });
$("si").addEventListener("click", () => mandarEscrito("sí"));
$("no").addEventListener("click", () => mandarEscrito("no"));
$("otra").addEventListener("click", () => {
  clearTimeout(timerGracias);
  volverAEsperar();
  empezarConCanal = true;
  pedirCanal();
});
// Cualquier toque destraba el sonido (por si el primero no alcanzó).
document.addEventListener("click", () => { if ($("voz").srcObject) $("voz").play().catch(() => {}); });

// --- Arranque -------------------------------------------------------------------------------
(async function () {
  if (!totem || !token) {
    cortar("Este enlace de registro no está completo. Escaneá de nuevo el código o pedí el enlace.");
    return;
  }
  if (!soloPantalla && !(await puedeRegistrar())) soloPantalla = true;
  if (soloPantalla) mostrarQr();
  conectar();
})();
