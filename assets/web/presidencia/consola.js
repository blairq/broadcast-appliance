// La consola de la presidencia (docs/funciones/SESION-REMOTA.md §8). La app manda el estado
// completo (recinto, remotos, fila, votación, quórum) y la consola pide cosas:
// habilitar, dar la palabra, sumar tiempo, votar… La app decide y vuelve a mandar
// el estado. El video del que habla y «apretar para hablar» van DIRECTO entre esta
// página y el navegador del integrante (WebRTC); el gateway solo pasa la señalización.
"use strict";

const $ = (id) => document.getElementById(id);
const frag = new URLSearchParams(location.hash.slice(1));
const consola = frag.get("c") || "";
const token = frag.get("t") || "";

let ws = null;
let pin = "";
let est = null;              // el último estado
let desfase = 0;             // reloj de la app - reloj de acá
let reintento = 1000;
let cortado = false;
const caras = new Map();     // id -> dataURL (la última foto)
const pedidasCuerpo = new Set();
let debateA = null, debateB = null; // {id, nombre} elegidos en el recinto antes de empezar

// --- Utilidades ---------------------------------------------------------------------
function el(tag, cls, texto) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (texto !== undefined) e.textContent = texto;
  return e;
}
function mandar(o) { if (ws && ws.readyState === 1) ws.send(JSON.stringify(o)); }
function avisar(texto, error) {
  const a = $("aviso");
  a.textContent = texto;
  a.className = "aviso" + (error ? " error" : "");
  clearTimeout(avisar.t);
  avisar.t = setTimeout(() => a.classList.add("oculto"), 4000);
}
function mmss(s) {
  const a = Math.abs(Math.round(s));
  return (s < 0 ? "-" : "") + Math.floor(a / 60) + ":" + String(a % 60).padStart(2, "0");
}
function iniciales(n) { return (n || "").split(" ").filter(Boolean).map((p) => p[0]).slice(0, 2).join("").toUpperCase(); }
function remotoDe(id) { return est && est.remotos ? est.remotos.find((r) => r.id === id) : null; }

function cara(id, nombre, cls) {
  const c = el("div", "cara" + (cls ? " " + cls : ""));
  const url = caras.get(id);
  if (url) { const i = el("img"); i.src = url; i.alt = ""; c.appendChild(i); }
  else c.textContent = iniciales(nombre);
  if (!url && id && !pedidasCuerpo.has(id)) { pedidasCuerpo.add(id); mandar({ type: "foto_cuerpo", id }); }
  return c;
}

// --- Entrar ---------------------------------------------------------------------------
$("formPin").addEventListener("submit", (e) => {
  e.preventDefault();
  pin = $("pin").value.trim();
  $("pin").value = "";
  if (!pin) return;
  $("error").classList.add("oculto");
  $("botonEntrar").disabled = true;
  cortado = false;
  conectar();
});
if (!consola || token.length !== 64) {
  $("entrarTexto").textContent = "Este link está incompleto: abrí el acceso de la consola que te dio la secretaría.";
  $("formPin").classList.add("oculto");
}

function conectar() {
  const proto = location.protocol === "https:" ? "wss:" : "ws:";
  ws = new WebSocket(proto + "//" + location.host + "/ws?presidencia=1");
  ws.onopen = () => ws.send(JSON.stringify({ type: "auth", c: consola, t: token, pin }));
  ws.onmessage = (ev) => { let m; try { m = JSON.parse(ev.data); } catch (e) { return; } recibir(m); };
  ws.onclose = () => {
    $("conexion").className = "punto-con apagado";
    if (cortado || !pin) return;
    setTimeout(conectar, reintento);
    reintento = Math.min(reintento * 2, 15000);
  };
}

function recibir(m) {
  switch (m.type) {
    case "auth":
      if (!m.ok) {
        pin = ""; cortado = true;
        $("botonEntrar").disabled = false;
        $("error").textContent = m.error || "No se pudo entrar.";
        $("error").classList.remove("oculto");
        return;
      }
      reintento = 1000;
      $("conexion").className = "punto-con vivo";
      $("entrar").classList.add("oculto");
      $("consola").classList.remove("oculto");
      mandar({ type: "hola" });
      setInterval(() => mandar({ type: "ping" }), 20000);
      break;
    case "estado":
      est = m;
      desfase = (m.ahora_ms || Date.now()) - Date.now();
      pintar();
      break;
    case "cast":
      if (m.url && $("programa").getAttribute("src") !== m.url) { $("programa").src = m.url; $("sinPrograma").classList.add("oculto"); }
      break;
    case "foto":
    case "foto_cuerpo":
      if (m.jpeg) {
        // La de la cámara en vivo manda sobre la del Cuerpo.
        if (m.type === "foto" || !caras.has(m.id)) caras.set(m.id, "data:image/jpeg;base64," + m.jpeg);
        pintarCaras(m.id);
      }
      break;
    case "rtc":
      rtcMensaje(m);
      break;
    case "error":
      avisar(m.texto || "No se pudo.", true);
      break;
    case "cortado":
      cortado = true;
      avisar(m.motivo || "Se cerró la consola.", true);
      break;
  }
}

// --- Pintar todo -------------------------------------------------------------------------
function pintar() {
  if (!est) return;
  $("cuerpo").textContent = est.cuerpo ? " · " + est.cuerpo : "";
  $("punto").textContent = est.punto ? "En tratamiento: " + est.punto : "";
  const q = est.quorum || {};
  $("quorum").textContent = "Quórum " + (q.presentes ?? "—") + (q.necesarios ? " / " + q.necesarios : "");
  $("quorum").className = "chip " + (q.configurado ? (q.hay_quorum ? "bien" : "mal") : "");
  const rem = est.remotos || [];
  $("remotos").textContent = "Remotos " + rem.filter((r) => r.estado === "habilitado").length + " / " + rem.filter((r) => r.conectado).length;
  $("central").textContent = est.central && est.central.conectada ? "Central conectada" : "Sin central";
  $("central").className = "chip " + (est.central && est.central.conectada ? "bien" : "");
  if (document.activeElement !== $("asignado")) $("asignado").value = (est.fila || {}).asignado_fabrica_s || 180;
  pintarDebate();
  $("hablaYo").textContent = est.presidencia_habla ? "🎙 Hablando (soltar)" : "🎙 Hablo yo";
  $("hablaYo").classList.toggle("rojo", !!est.presidencia_habla);
  $("hablaYo").disabled = !est.con_central;
  $("hablaYo").title = est.con_central ? "Calla a todos y abre el micrófono de la presidencia" : "Hace falta la central";
  if (document.activeElement !== $("votSegundos")) $("votSegundos").value = est.voto_s ?? 60;
  pintarRecinto();
  pintarEsperan();
  pintarFila();
  pintarOrador();
  pintarVotacion();
  pedirFotosVivas();
}

// El hemiciclo: cada fila un arco alrededor de la presidencia; las caras en vivo.
function pintarRecinto() {
  const r = est.recinto || {};
  const caja = $("recinto");
  caja.textContent = "";
  const filas = Math.max(1, r.filas || 1), cols = Math.max(1, r.columnas || 1);
  const pedidos = new Set(((est.fila || {}).cola || []).map((c) => c.id));
  const centralPide = new Set(((est.central || {}).pedidos || []).map((p) => p.banca));
  const abiertos = new Set((est.central || {}).abiertos || []);
  const palabra = (est.fila || {}).al_aire ? est.fila.al_aire.id : "";
  let presentes = 0, total = 0;
  for (const b of r.bancas || []) {
    const i = b.integrante;
    if (!i) continue;
    total++;
    const remoto = remotoDe(i.id);
    const d = el("div", "banca");
    // Posición en el arco (en % de la caja: el centro abajo).
    const radio = 0.44 + 0.52 * (filas <= 1 ? 1 : b.fila / (filas - 1));
    const ang = Math.PI * (1 - (b.columna + 0.5) / cols);
    d.style.left = (50 + 46 * radio * Math.cos(ang)) + "%";
    d.style.top = (92 - 84 * radio * Math.sin(ang)) + "%";
    if (remoto && remoto.estado === "habilitado") { d.classList.add("remoto"); presentes++; }
    else if (remoto && remoto.estado === "esperando") d.classList.add("espera");
    else if (abiertos.has(b.banca) || (est.central && est.central.conectada)) { d.classList.add("sala"); presentes++; }
    else d.classList.add("ausente");
    if (pedidos.has(i.id) || centralPide.has(b.banca)) d.classList.add("pidio");
    if (palabra === i.id || abiertos.has(b.banca)) d.classList.add("palabra");
    d.dataset.id = i.id;
    d.title = i.nombre + " · Banca " + b.banca;
    d.appendChild(cara(i.id, i.nombre));
    d.appendChild(el("span", "num", b.banca));
    d.addEventListener("click", (ev) => menuBanca(ev, b, i, remoto));
    caja.appendChild(d);
  }
  caja.appendChild(el("div", "presidencia-mesa", "PRESIDENCIA"));
  $("resumenRecinto").textContent = total ? "· " + presentes + " de " + total : "";
}

function pintarCaras(id) {
  for (const n of document.querySelectorAll('[data-id="' + CSS.escape(id) + '"] .cara, .cara[data-id="' + CSS.escape(id) + '"]')) {
    const url = caras.get(id);
    if (!url) continue;
    let img = n.querySelector("img");
    if (!img) { n.textContent = ""; img = el("img"); img.alt = ""; n.appendChild(img); }
    img.src = url;
  }
  if (est && est.fila && est.fila.al_aire && est.fila.al_aire.id === id && !rtcVivo(id)) {
    $("fotoOrador").src = caras.get(id);
  }
}

function pintarEsperan() {
  const caja = $("esperan");
  caja.textContent = "";
  const esperan = (est.remotos || []).filter((r) => r.estado === "esperando");
  $("tarjetaEsperan").classList.toggle("oculto", esperan.length === 0);
  for (const r of esperan) {
    const it = el("div", "item");
    it.dataset.id = r.id;
    it.appendChild(cara(r.id, r.nombre));
    const d = el("div", "datos");
    d.appendChild(el("strong", "", r.nombre));
    d.appendChild(el("span", "", "Banca " + (r.banca || "—") + " · " + (r.lugar === "sala" ? "desde la sala" : "desde afuera")));
    it.appendChild(d);
    const b = el("button", "boton celeste chico", "Habilitar");
    b.addEventListener("click", () => mandar({ type: "habilitar", id: r.id }));
    it.appendChild(b);
    caja.appendChild(it);
  }
}

function pintarFila() {
  const caja = $("fila");
  caja.textContent = "";
  const cola = (est.fila || {}).cola || [];
  $("cuantosPidieron").textContent = cola.length ? "· " + cola.length : "";
  if (!cola.length) caja.appendChild(el("div", "vacia", "Nadie pidió la palabra."));
  cola.forEach((c, k) => {
    const it = el("div", "item");
    it.dataset.id = c.id;
    it.appendChild(el("span", "turno", String(k + 1)));
    it.appendChild(cara(c.id, c.nombre));
    const d = el("div", "datos");
    d.appendChild(el("strong", "", c.nombre));
    const sub = el("span", "", "Banca " + (c.banca || "—") + (c.bloque ? " · " + c.bloque : ""));
    if (c.remoto) sub.appendChild(el("span", "etiqueta", "REMOTO"));
    d.appendChild(sub);
    it.appendChild(d);
    if (c.remoto) it.appendChild(botonPtt(c.id, c.nombre));
    const dar = el("button", "boton celeste chico", "Dar");
    dar.title = "Darle la palabra";
    dar.addEventListener("click", () => mandar({ type: "dar_palabra", id: c.id }));
    it.appendChild(dar);
    const q = el("button", "boton chico", "✕");
    q.title = "Sacarlo de la fila";
    q.addEventListener("click", () => mandar({ type: "quitar_fila", id: c.id }));
    it.appendChild(q);
    caja.appendChild(it);
  });
}

// --- El que habla -----------------------------------------------------------------------------
let relojTimer = null;
function pintarOrador() {
  const f = est.fila || {};
  const a = f.al_aire;
  clearInterval(relojTimer);
  if (!a) {
    $("nombreOrador").textContent = "—";
    $("detalleOrador").textContent = "";
    $("restante").textContent = "--:--";
    $("arco").style.strokeDashoffset = "0";
    if (!pttCon) mostrarEnOrador(null);
    return;
  }
  $("nombreOrador").textContent = a.nombre;
  $("detalleOrador").textContent = "Banca " + (a.banca || "—") + (a.remoto ? " · desde el navegador" : " · en la sala");
  const pintarReloj = () => {
    const usado = ((Date.now() + desfase) - f.desde_ms) / 1000;
    const rest = (f.asignado_s || 0) - usado;
    $("restante").textContent = mmss(rest);
    const frac = Math.max(0, Math.min(1, rest / Math.max(1, f.asignado_s)));
    $("arco").style.strokeDashoffset = String(326.7 * (1 - frac));
    const an = document.querySelector(".anillo");
    an.classList.toggle("minuto", rest <= 60 && rest > 0);
    an.classList.toggle("fin", rest <= 0);
  };
  pintarReloj();
  relojTimer = setInterval(pintarReloj, 250);
  if (!pttCon) mostrarEnOrador(a.id, a.remoto);
}

// La pantalla «En uso de la palabra»: el video directo si es remoto; si no, su foto.
function mostrarEnOrador(id, remoto) {
  const v = $("videoOrador"), f = $("fotoOrador");
  if (!id) {
    v.srcObject = null; v.classList.add("oculto"); f.classList.add("oculto");
    $("sinOrador").classList.remove("oculto");
    return;
  }
  $("sinOrador").classList.add("oculto");
  if (remoto) {
    const pc = abrirConexion(id);
    if (pc && pc.flujo) { v.srcObject = pc.flujo; v.classList.remove("oculto"); f.classList.add("oculto"); return; }
  }
  v.classList.add("oculto");
  if (caras.get(id)) { f.src = caras.get(id); f.classList.remove("oculto"); }
  else { f.classList.add("oculto"); $("sinOrador").classList.remove("oculto"); $("sinOrador").textContent = "En la sala"; }
}

for (const b of document.querySelectorAll("[data-accion]")) {
  b.addEventListener("click", () => {
    const a = b.dataset.accion;
    if (a === "sumar") mandar({ type: "sumar_tiempo", segundos: Number(b.dataset.s) });
    else if (a === "terminar") mandar({ type: "terminar" });
    else if (a === "siguiente") mandar({ type: "siguiente" });
    else if (a === "silenciar") mandar({ type: "silenciar_todos" });
    else if (a === "debate-iniciar") {
      if (!debateA || !debateB) return avisar("Elegí A y B tocando sus bancas.", true);
      mandar({ type: "debate", accion: "iniciar", a: debateA.id, b: debateB.id, turno: "a" });
    }
    else if (a === "debate-turno") mandar({ type: "debate", accion: "turno", turno: b.dataset.t });
    else if (a === "debate-sumar") mandar({ type: "debate", accion: "sumar", segundos: 30 });
    else if (a === "debate-terminar") mandar({ type: "debate", accion: "terminar" });
    else if (a === "hablo") mandar({ type: "hablar_presidencia", activo: !(est && est.presidencia_habla) });
  });
}
$("asignado").addEventListener("change", () => {
  const s = Number($("asignado").value);
  if (s >= 10) mandar({ type: "tiempo", asignado_s: s });
});

// --- La votación -------------------------------------------------------------------------------
let votTimer = null;
function pintarVotacion() {
  const central = (est.central || {}).votacion || {};
  const v = est.votacion || {};
  // La de la central manda si está; si no, la de los celulares.
  const estado = central.estado && central.estado !== "sin_preparar" ? central.estado : v.estado;
  const abierta = estado === "abierta" || estado === "pausada";
  $("tarjetaVotacion").classList.toggle("abierta", abierta);
  $("votAbrir").classList.toggle("oculto", abierta);
  $("votEnCurso").classList.toggle("oculto", !abierta && estado !== "cerrada");
  $("estadoVot").textContent = estado === "abierta" ? "· ABIERTA" : estado === "pausada" ? "· en pausa" : estado === "cerrada" ? "· cerrada" : "";
  $("votQuorum").classList.toggle("oculto", estado !== "cerrada");
  $("votPausar").classList.toggle("oculto", !abierta);
  $("votCerrar").classList.toggle("oculto", !abierta);
  $("votPausar").textContent = estado === "pausada" ? "Reanudar" : "Pausar";
  $("votTemaActual").textContent = v.tema || central.tema || "";
  // Totales: los de la central (si hay) más los de los celulares.
  const opciones = v.opciones && v.opciones.length ? v.opciones
    : [{ id: "afirmativo", texto: "Afirmativo" }, { id: "negativo", texto: "Negativo" }, { id: "abstencion", texto: "Abstención" }];
  const tot = {};
  for (const o of opciones) tot[o.id] = ((central.totales || {})[o.id] || 0) + ((v.totales || {})[o.id] || 0);
  const max = Math.max(1, ...Object.values(tot));
  const barras = $("votBarras");
  barras.textContent = "";
  for (const o of opciones) {
    const b = el("div", "barra-voto " + o.id);
    const ll = el("div", "lleno");
    ll.style.width = (100 * tot[o.id] / max) + "%";
    b.appendChild(ll);
    const s = el("span");
    s.appendChild(el("b", "", o.texto));
    s.appendChild(el("b", "", String(tot[o.id])));
    b.appendChild(s);
    barras.appendChild(b);
  }
  clearInterval(votTimer);
  if (abierta && v.hasta_ms) {
    const r = () => { $("votReloj").textContent = mmss(Math.max(0, (v.hasta_ms - (Date.now() + desfase)) / 1000)); };
    r(); votTimer = setInterval(r, 250);
  } else $("votReloj").textContent = "";
  // Los próximos temas: un toque los pone como tema.
  const pr = $("proximos");
  pr.textContent = "";
  for (const p of (est.proximos || []).slice(0, 4)) {
    const b = el("button", "boton chico", p.n + ". " + p.titulo);
    b.addEventListener("click", () => { $("votTema").value = p.titulo; });
    pr.appendChild(b);
  }
}
$("votAbrirBoton").addEventListener("click", () => {
  const tema = $("votTema").value.trim();
  if (!tema) return avisar("Escribí el tema a votar.", true);
  mandar({ type: "votacion", accion: "abrir", tema, menu: $("votMenu").value, segundos: Number($("votSegundos").value) || 0 });
});
$("votPausar").addEventListener("click", () => {
  const pausada = $("votPausar").textContent === "Reanudar";
  mandar({ type: "votacion", accion: pausada ? "reanudar" : "pausar" });
});
$("votCerrar").addEventListener("click", () => mandar({ type: "votacion", accion: "cerrar" }));
$("votQuorum").addEventListener("click", () => {
  if (confirm("¿Llevar el resultado a Quórum como resultado oficial?")) mandar({ type: "votacion", accion: "a_quorum" });
});

// --- El debate ---------------------------------------------------------------------------------
const TIPOS_DEBATE = { sala_sala: "· dos de la sala", sala_remoto: "· sala y remoto", remoto_remoto: "· dos remotos" };
function pintarDebate() {
  const d = (est && est.debate) || {};
  const activo = !!d.activo;
  const nombre = (l, elegido) => activo && l ? l.nombre + (l.remoto ? " (remoto)" : "") : elegido ? elegido.nombre : null;
  $("debateNombreA").textContent = nombre(d.a, debateA) || "tocá una banca → «Debate: A»";
  $("debateNombreB").textContent = nombre(d.b, debateB) || "tocá una banca → «Debate: B»";
  $("debateA").classList.toggle("habla", activo && (d.turno === "a" || d.turno === "ambos"));
  $("debateB").classList.toggle("habla", activo && (d.turno === "b" || d.turno === "ambos"));
  $("debateTipo").textContent = activo ? (TIPOS_DEBATE[d.tipo] || "") : "";
  const r = d.restante_s || 0;
  $("debateReloj").textContent = !activo ? "" : d.turno === "nadie" ? "Nadie tiene el turno"
    : d.turno === "ambos" ? "Hablan los dos"
    : (d.turno === "a" ? "Turno de A" : "Turno de B") + (d.turno_s > 0 ? " · " + (r < 0 ? "-" : "") + Math.floor(Math.abs(r) / 60) + ":" + String(Math.abs(r) % 60).padStart(2, "0") : "");
  $("debateIniciar").classList.toggle("oculto", activo);
  for (const x of document.querySelectorAll(".debate-en")) x.classList.toggle("oculto", !activo);
  if (activo && d.aviso) $("debateReloj").title = d.aviso;
}

// --- El menú de una banca --------------------------------------------------------------------
function menuBanca(ev, b, i, remoto) {
  ev.stopPropagation();
  const m = $("menuBanca");
  m.textContent = "";
  m.appendChild(el("div", "titulo", i.nombre + " · Banca " + b.banca));
  const op = (texto, cls, fn) => { const x = el("button", "boton " + cls, texto); x.addEventListener("click", () => { fn(); cerrarMenu(); }); m.appendChild(x); };
  if (remoto && remoto.estado === "esperando") op("Habilitar (lo vi en cámara)", "celeste", () => mandar({ type: "habilitar", id: i.id }));
  op("Darle la palabra", "celeste", () => mandar({ type: "dar_palabra", id: i.id }));
  op("Debate: A", "", () => { debateA = { id: i.id, nombre: i.nombre }; pintarDebate(); });
  op("Debate: B", "", () => { debateB = { id: i.id, nombre: i.nombre }; pintarDebate(); });
  if (remoto && remoto.estado === "habilitado") {
    op("Deshabilitar", "", () => mandar({ type: "deshabilitar", id: i.id }));
  }
  if (remoto && remoto.conectado) {
    // Apretar para hablar desde la banca (no solo desde la fila): el menú queda abierto
    // mientras se usa, así se puede hablar más de una vez.
    const p = botonPtt(i.id, i.nombre);
    p.textContent = "🎙 Mantené apretado para hablarle";
    p.classList.remove("chico");
    p.addEventListener("click", (e) => e.stopPropagation());
    m.appendChild(p);
  }
  if (remoto && remoto.conectado) op("Sacar de la sesión", "rojo", () => { if (confirm("¿Sacar a " + i.nombre + " de la sesión?")) mandar({ type: "expulsar", id: i.id }); });
  const r = ev.currentTarget.getBoundingClientRect();
  m.style.left = Math.min(window.innerWidth - 230, r.right + 6) + "px";
  m.style.top = Math.min(window.innerHeight - 220, r.top) + "px";
  m.classList.remove("oculto");
}
function cerrarMenu() { $("menuBanca").classList.add("oculto"); }
document.addEventListener("click", cerrarMenu);

// --- Las caras en vivo ---------------------------------------------------------------------------
let ultimaRonda = 0;
function pedirFotosVivas() {
  if (Date.now() - ultimaRonda < 2500) return;
  ultimaRonda = Date.now();
  for (const r of (est.remotos || [])) if (r.conectado) mandar({ type: "foto", id: r.id });
}
setInterval(() => { if (est) pedirFotosVivas(); }, 3000);

// --- WebRTC directo con un integrante ----------------------------------------------------------
// Una conexión por integrante: video y audio de él hacia acá; el micrófono de la
// presidencia hacia él solo mientras se mantiene apretado 🎙.
const conexiones = new Map(); // id -> {pc, flujo, audio(sender)}
let micro = null;
let pttCon = "";

function rtcVivo(id) { const c = conexiones.get(id); return !!(c && c.flujo && c.pc.connectionState !== "failed"); }

function abrirConexion(id) {
  let c = conexiones.get(id);
  if (c && c.pc.connectionState !== "failed" && c.pc.connectionState !== "closed") return c;
  const pc = new RTCPeerConnection(window.BAIce ? BAIce.config() : { iceServers: [] }); // desde afuera, por el TURN de la caja
  c = { pc, flujo: null, audio: null };
  conexiones.set(id, c);
  pc.addTransceiver("video", { direction: "recvonly" });
  c.audio = pc.addTransceiver("audio", { direction: "sendrecv" }).sender;
  if (micro) c.audio.replaceTrack(micro.getAudioTracks()[0] || null);
  pc.ontrack = (ev) => {
    c.flujo = ev.streams[0] || c.flujo || new MediaStream();
    if (!ev.streams[0]) c.flujo.addTrack(ev.track);
    if (est && est.fila && est.fila.al_aire && est.fila.al_aire.id === id && !pttCon) mostrarEnOrador(id, true);
    if (pttCon === id) mostrarPtt(id);
  };
  pc.onicecandidate = (ev) => { if (ev.candidate) mandar({ type: "rtc", a: id, candidato: ev.candidate }); };
  pc.onconnectionstatechange = () => {
    if (pc.connectionState === "failed") { conexiones.delete(id); avisar("No se pudo conectar directo con el integrante (¿está en otra red? hace falta TURN).", true); }
  };
  (async () => {
    const offer = await pc.createOffer();
    await pc.setLocalDescription(offer);
    mandar({ type: "rtc", a: id, tipo: "offer", sdp: offer.sdp });
  })();
  return c;
}

async function rtcMensaje(m) {
  const c = conexiones.get(m.de);
  if (!c) return;
  if (m.tipo === "answer") { try { await c.pc.setRemoteDescription({ type: "answer", sdp: m.sdp }); } catch (e) {} }
  else if (m.candidato) { try { await c.pc.addIceCandidate(m.candidato); } catch (e) {} }
}

async function microfono() {
  if (micro) return micro;
  micro = await navigator.mediaDevices.getUserMedia({ audio: { echoCancellation: true, noiseSuppression: true } });
  for (const c of conexiones.values()) if (c.audio) c.audio.replaceTrack(micro.getAudioTracks()[0]);
  return micro;
}

function mostrarPtt(id) {
  const c = conexiones.get(id);
  $("sinOrador").classList.add("oculto");
  $("fotoOrador").classList.add("oculto");
  if (c && c.flujo) { $("videoOrador").srcObject = c.flujo; $("videoOrador").classList.remove("oculto"); $("audioRemoto").srcObject = c.flujo; }
  $("etiquetaPtt").classList.remove("oculto");
}

// 🎙 «Apretar para hablar»: lo pone en pantalla y se escuchan los dos mientras se mantiene.
function botonPtt(id, nombre) {
  const b = el("button", "boton chico ptt" + (pttCon === id ? " apretado" : ""), "🎙");
  b.title = "Mantené apretado para hablar con " + nombre;
  const empezar = (ev) => {
    ev.preventDefault();
    if (pttCon) return;
    pttCon = id;
    b.classList.add("apretado");
    // La conexión y el aviso salen ya; el micrófono se suma cuando el navegador lo da
    // (la primera vez pregunta, y eso puede tardar).
    const c = abrirConexion(id);
    mandar({ type: "ptt", id, activo: true });
    mostrarPtt(id);
    microfono().then((m) => {
      if (pttCon !== id) return;
      if (c.audio) c.audio.replaceTrack(m.getAudioTracks()[0]);
      for (const t of m.getAudioTracks()) t.enabled = true;
    }).catch(() => avisar("Sin micrófono: revisá el permiso del navegador.", true));
  };
  const terminar = () => {
    if (pttCon !== id) return;
    pttCon = "";
    b.classList.remove("apretado");
    if (micro) for (const t of micro.getAudioTracks()) t.enabled = false;
    $("audioRemoto").srcObject = null;
    $("etiquetaPtt").classList.add("oculto");
    mandar({ type: "ptt", id, activo: false });
    pintarOrador();
  };
  b.addEventListener("pointerdown", empezar);
  b.addEventListener("pointerup", terminar);
  b.addEventListener("pointerleave", terminar);
  b.addEventListener("pointercancel", terminar);
  return b;
}

// --- Profundidad: las tarjetas se inclinan hacia el puntero ------------------------------------
document.addEventListener("pointermove", (ev) => {
  for (const t of document.querySelectorAll(".inclinable")) {
    const r = t.getBoundingClientRect();
    if (ev.clientX < r.left || ev.clientX > r.right || ev.clientY < r.top || ev.clientY > r.bottom) {
      t.style.transform = "";
      continue;
    }
    const x = (ev.clientX - r.left) / r.width - 0.5, y = (ev.clientY - r.top) / r.height - 0.5;
    t.style.transform = "rotateY(" + (x * 6).toFixed(2) + "deg) rotateX(" + (-y * 6).toFixed(2) + "deg) translateZ(6px)";
  }
});

// --- El reloj de arriba ----------------------------------------------------------------------------
setInterval(() => {
  const d = new Date(Date.now() + desfase);
  $("reloj").textContent = String(d.getHours()).padStart(2, "0") + ":" + String(d.getMinutes()).padStart(2, "0");
}, 1000);
