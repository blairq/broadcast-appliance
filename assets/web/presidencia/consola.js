// La consola de la presidencia (docs/funciones/SESION-REMOTA.md §8). La app manda el estado
// completo (recinto, remotos, fila, votación, quórum) y la consola pide cosas:
// habilitar, dar la palabra, sumar tiempo, votar… La app decide y vuelve a mandar
// el estado. El video del que habla y «apretar para hablar» van DIRECTO entre esta
// página y el navegador del integrante (WebRTC); el gateway solo pasa la señalización.
"use strict";

const $ = (id) => document.getElementById(id);
// Los textos, en el idioma del evento. Lo que se pinta espera a que estén.
// (los componentes de neuro.js se traducen solos con el evento ba-idioma)
const listo = BAi18n.iniciar({ contexto: "web/presidencia", preferir: "evento" });
const llenar = (s, ...v) => BAi18n.llenar(s, ...v);
const frag = new URLSearchParams(location.hash.slice(1));
const consola = frag.get("c") || "";
const token = frag.get("t") || "";

let ws = null;
let pin = "";
let est = null;              // el último estado
let desfase = 0;             // reloj de la app - reloj de acá
let reintento = 1000;
let cortado = false;
let adentro = false;         // ya entró una vez (para el cartel de «reconectando»)
let pingTimer = null;
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
$("formPin").addEventListener("submit", async (e) => {
  e.preventDefault();
  await listo;
  pin = $("pin").value.trim();
  $("pin").value = "";
  if (!pin) return;
  $("error").classList.add("oculto");
  $("botonEntrar").disabled = true;
  cortado = false;
  conectar();
});
listo.then(() => {
  if (!consola || token.length !== 64) {
    $("entrarTexto").textContent = t("Este link está incompleto: abrí el acceso de la consola que te dio la secretaría.");
    $("formPin").classList.add("oculto");
  }
});

function conectar() {
  const proto = location.protocol === "https:" ? "wss:" : "ws:";
  ws = new WebSocket(proto + "//" + location.host + "/ws?presidencia=1");
  ws.onopen = () => ws.send(JSON.stringify({ type: "auth", c: consola, t: token, pin }));
  ws.onmessage = (ev) => { let m; try { m = JSON.parse(ev.data); } catch (e) { return; } recibir(m); };
  ws.onclose = () => {
    $("conexion").className = "punto-con apagado";
    $("sinConexion").classList.toggle("oculto", !adentro || cortado);
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
        $("error").textContent = t(m.error) || t("No se pudo entrar.");
        $("error").classList.remove("oculto");
        return;
      }
      reintento = 1000;
      adentro = true;
      $("conexion").className = "punto-con vivo";
      $("sinConexion").classList.add("oculto");
      $("entrar").classList.add("oculto");
      $("consola").classList.remove("oculto");
      mandar({ type: "hola" });
      // Uno solo: al reconectar se volvía a sumar otro intervalo cada vez.
      if (!pingTimer) pingTimer = setInterval(() => mandar({ type: "ping" }), 20000);
      despierta();
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
      avisar(t(m.texto) || t("No se pudo."), true);
      break;
    case "cortado":
      cortado = true;
      $("sinConexion").textContent = t(m.motivo) || t("Se cerró la consola.");
      $("sinConexion").classList.remove("oculto");
      avisar(t(m.motivo) || t("Se cerró la consola."), true);
      break;
  }
}

// --- Pintar todo -------------------------------------------------------------------------
function pintar() {
  if (!est) return;
  $("cuerpo").textContent = est.cuerpo || "";
  $("punto").textContent = est.punto ? llenar(t("En tratamiento: %1"), est.punto) : "";
  const q = est.quorum || {};
  $("quorum").textContent = llenar(t("Quórum %1"), (q.presentes ?? "—") + (q.necesarios ? " / " + q.necesarios : ""));
  $("quorum").className = "chip " + (q.configurado ? (q.hay_quorum ? "bien" : "mal") : "");
  const rem = est.remotos || [];
  $("remotos").textContent = llenar(t("Remotos %1 / %2"), rem.filter((r) => r.estado === "habilitado").length, rem.filter((r) => r.conectado).length);
  $("central").textContent = est.central && est.central.conectada ? t("Central conectada") : t("Sin central");
  $("central").className = "chip " + (est.central && est.central.conectada ? "bien" : "");
  if (document.activeElement !== $("asignado")) $("asignado").value = (est.fila || {}).asignado_fabrica_s || 180;
  pintarDebate();
  $("hablaYo").textContent = est.presidencia_habla ? t("🎙 Hablando (soltar)") : t("🎙 Hablo yo");
  $("hablaYo").classList.toggle("rojo", !!est.presidencia_habla);
  $("hablaYo").disabled = !est.con_central;
  $("hablaYo").title = est.con_central ? t("Calla a todos y abre el micrófono de la presidencia") : t("Hace falta la central");
  if (document.activeElement !== $("votSegundos")) $("votSegundos").value = est.voto_s ?? 60;
  // Lo que no se puede hacer, apagado (antes todo parecía andar y contestaba un error).
  const conPalabra = !!(est.fila || {}).al_aire;
  for (const b of document.querySelectorAll('[data-accion="sumar"], [data-accion="terminar"]')) b.disabled = !conPalabra;
  document.querySelector('[data-accion="siguiente"]').disabled = !((est.fila || {}).cola || []).length;
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
  const ocupadas = (r.bancas || []).filter((b) => b.integrante);
  // La de la presidencia va en la mesa, fuera de los arcos.
  const pres = r.presidencia && r.presidencia.integrante ? r.presidencia : null;
  if (pres) ocupadas.push(pres);
  if (!ocupadas.length) {
    const v = el("div", "recinto-vacio");
    v.appendChild(el("strong", "", t("Todavía no hay bancas")));
    v.appendChild(el("span", "", t("La secretaría carga los integrantes con su banca en la app (Asamblea → Integrantes) y aparecen acá.")));
    caja.appendChild(v);
    $("resumenRecinto").textContent = "";
    return;
  }
  // En píxeles y no en % de la caja: la caja es más alta que ancha y los arcos salían
  // estirados como una «V». Más bancas, más chicas (si no, en la fila de adentro se pisan).
  const grilla = r.distribucion === "grilla" || r.distribucion === "lista";
  caja.classList.toggle("en-grilla", grilla);
  const W = caja.clientWidth || 600, H = caja.clientHeight || 420;
  // La caja estira hasta el alto de la columna más larga (más que la pantalla): se usa
  // solo lo que se ve, así el hemiciclo no queda abajo, fuera de la vista.
  const Hv = Math.min(H, Math.max(360, window.innerHeight - 190));
  const BANCA = 82; // lo que ocupa una banca a escala 1 (64 × 76 y un poco de aire)
  let pos, escala;
  if (grilla) {
    // Las filas de adelante (la 0) abajo, cerca de la presidencia, como en la app.
    const dx = W / cols, dy = (Hv - 60) / filas;
    escala = Math.max(0.42, Math.min(1, dx / BANCA, dy / BANCA));
    pos = (b) => b === pres ? [W / 2 + 130, Hv - 22] : [dx * (b.columna + 0.5), Hv - 60 - dy * (b.fila + 0.5) + 10];
    caja.style.setProperty("--mesa", (H - Hv + 4).toFixed(0) + "px");
  } else {
    // Cada fila un arco alrededor de la presidencia (abajo, al centro). La caja suele
    // ser más alta que ancha: el arco se estira un poco para arriba (hasta 1,25 veces)
    // para usar ese alto, y el hemiciclo queda centrado en la caja.
    const Rx = Math.max(80, W / 2 - 36);
    const Ry = Math.max(80, Math.min(Hv - 90, Rx * 1.25));
    const cx = W / 2, cy = Math.min(Hv - 34, (Hv + Ry) / 2 + 10);
    const r0 = filas > 1 ? 0.5 : 1, paso = filas > 1 ? (1 - r0) / (filas - 1) : 0;
    const hueco = Math.min(Math.PI * r0 * (Rx + Ry) / 2 / cols, filas > 1 ? paso * Math.min(Rx, Ry) : Infinity);
    escala = Math.max(0.42, Math.min(1, hueco / BANCA));
    // Una banca ubicada a mano en la app trae x, y en anchos de banca desde la
    // presidencia (radio de adentro R0, 1,32 por fila): se pasa a estos arcos en polares.
    const R0 = Math.max(1.4 * cols / Math.PI, 1.4);
    pos = (b) => {
      if (b === pres) return [cx, cy - 46];
      let f = r0 + paso * b.fila;
      let ang = Math.PI * (1 - (b.columna + 0.5) / cols);
      if (typeof b.x === "number" && typeof b.y === "number") {
        const rho = Math.hypot(b.x, b.y);
        f = filas > 1 ? r0 + paso * (rho - R0) / 1.32 : rho / R0;
        ang = Math.atan2(-b.y, b.x);
      }
      return [cx + Rx * f * Math.cos(ang), cy - Ry * f * Math.sin(ang)];
    };
    caja.style.setProperty("--mesa", (H - cy - 30).toFixed(0) + "px");
  }
  caja.style.setProperty("--escala", escala.toFixed(3));
  for (const b of ocupadas) {
    const i = b.integrante;
    total++;
    const remoto = remotoDe(i.id);
    const d = el("div", "banca");
    const [x, y] = pos(b);
    d.style.left = x.toFixed(1) + "px";
    d.style.top = y.toFixed(1) + "px";
    if (remoto && remoto.estado === "habilitado") { d.classList.add("remoto"); presentes++; }
    else if (remoto && remoto.estado === "esperando") d.classList.add("espera");
    else if (abiertos.has(b.banca) || (est.central && est.central.conectada)) { d.classList.add("sala"); presentes++; }
    else d.classList.add("ausente");
    if (pedidos.has(i.id) || centralPide.has(b.banca)) d.classList.add("pidio");
    if (palabra === i.id || abiertos.has(b.banca)) d.classList.add("palabra");
    d.dataset.id = i.id;
    d.title = i.nombre + " · " + llenar(t("Banca %1"), b.banca);
    d.appendChild(cara(i.id, i.nombre));
    d.appendChild(el("span", "num", b.banca));
    d.addEventListener("click", (ev) => menuBanca(ev, b, i, remoto));
    caja.appendChild(d);
  }
  caja.appendChild(el("div", "presidencia-mesa", t("PRESIDENCIA")));
  $("resumenRecinto").textContent = total ? "· " + llenar(t("%1 de %2"), presentes, total) : "";
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
    d.appendChild(el("span", "", llenar(t("Banca %1"), r.banca || "—") + " · " + (r.lugar === "sala" ? t("desde la sala") : t("desde afuera"))));
    it.appendChild(d);
    const b = el("button", "boton celeste chico", t("Habilitar"));
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
  if (!cola.length) caja.appendChild(el("div", "vacia", t("Nadie pidió la palabra.")));
  cola.forEach((c, k) => {
    const it = el("div", "item");
    it.dataset.id = c.id;
    it.appendChild(el("span", "turno", String(k + 1)));
    it.appendChild(cara(c.id, c.nombre));
    const d = el("div", "datos");
    d.appendChild(el("strong", "", c.nombre));
    const sub = el("span", "", llenar(t("Banca %1"), c.banca || "—") + (c.bloque ? " · " + c.bloque : ""));
    if (c.remoto) sub.appendChild(el("span", "etiqueta", t("REMOTO")));
    d.appendChild(sub);
    it.appendChild(d);
    if (c.remoto) it.appendChild(botonPtt(c.id, c.nombre));
    const dar = el("button", "boton celeste chico", t("Dar"));
    dar.title = t("Darle la palabra");
    dar.addEventListener("click", () => mandar({ type: "dar_palabra", id: c.id }));
    it.appendChild(dar);
    const q = el("button", "boton chico", "✕");
    q.title = t("Sacarlo de la fila");
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
  $("detalleOrador").textContent = llenar(t("Banca %1"), a.banca || "—") + " · " + (a.remoto ? t("desde el navegador") : t("en la sala"));
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
  else { f.classList.add("oculto"); $("sinOrador").classList.remove("oculto"); $("sinOrador").textContent = t("En la sala"); }
}

for (const b of document.querySelectorAll("[data-accion]")) {
  b.addEventListener("click", () => {
    const a = b.dataset.accion;
    if (a === "sumar") mandar({ type: "sumar_tiempo", segundos: Number(b.dataset.s) });
    else if (a === "terminar") mandar({ type: "terminar" });
    else if (a === "siguiente") mandar({ type: "siguiente" });
    else if (a === "silenciar") mandar({ type: "silenciar_todos" });
    else if (a === "debate-iniciar") {
      if (!debateA || !debateB) return avisar(t("Elegí A y B tocando sus bancas."), true);
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
  $("estadoVot").textContent = estado === "abierta" ? "· " + t("ABIERTA") : estado === "pausada" ? "· " + t("en pausa") : estado === "cerrada" ? "· " + t("cerrada") : "";
  $("votQuorum").classList.toggle("oculto", estado !== "cerrada");
  $("votPausar").classList.toggle("oculto", !abierta);
  $("votCerrar").classList.toggle("oculto", !abierta);
  // Lo que hace el botón va en un dato aparte: el texto se traduce.
  $("votPausar").dataset.pausada = estado === "pausada" ? "1" : "";
  $("votPausar").textContent = estado === "pausada" ? t("Reanudar") : t("Pausar");
  $("votTemaActual").textContent = v.tema || central.tema || "";
  // Totales: los de la central (si hay) más los de los celulares.
  const opciones = v.opciones && v.opciones.length ? v.opciones
    : [{ id: "afirmativo", texto: t("Afirmativo") }, { id: "negativo", texto: t("Negativo") }, { id: "abstencion", texto: t("Abstención") }];
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
  if ((est.proximos || []).length) pr.appendChild(el("span", "tenue chico", t("Próximos puntos (un toque lo pone como tema):")));
  for (const p of (est.proximos || []).slice(0, 4)) {
    const b = el("button", "boton chico", p.n + ". " + p.titulo);
    b.addEventListener("click", () => { $("votTema").value = p.titulo; });
    pr.appendChild(b);
  }
}
$("votAbrirBoton").addEventListener("click", () => {
  const tema = $("votTema").value.trim();
  if (!tema) return avisar(t("Escribí el tema a votar."), true);
  mandar({ type: "votacion", accion: "abrir", tema, menu: $("votMenu").value, segundos: Number($("votSegundos").value) || 0 });
});
$("votPausar").addEventListener("click", () => {
  const pausada = $("votPausar").dataset.pausada === "1";
  mandar({ type: "votacion", accion: pausada ? "reanudar" : "pausar" });
});
$("votCerrar").addEventListener("click", () => mandar({ type: "votacion", accion: "cerrar" }));
$("votQuorum").addEventListener("click", () => {
  if (confirm(t("¿Llevar el resultado a Quórum como resultado oficial?"))) mandar({ type: "votacion", accion: "a_quorum" });
});

// --- El debate ---------------------------------------------------------------------------------
const TIPOS_DEBATE = () => ({ sala_sala: "· " + t("dos de la sala"), sala_remoto: "· " + t("sala y remoto"), remoto_remoto: "· " + t("dos remotos") });
function pintarDebate() {
  const d = (est && est.debate) || {};
  const activo = !!d.activo;
  const nombre = (l, elegido) => activo && l ? (l.remoto ? llenar(t("%1 (remoto)"), l.nombre) : l.nombre) : elegido ? elegido.nombre : null;
  $("debateNombreA").textContent = nombre(d.a, debateA) || t("tocá una banca → «Debate: A»");
  $("debateNombreB").textContent = nombre(d.b, debateB) || t("tocá una banca → «Debate: B»");
  $("debateA").classList.toggle("habla", activo && (d.turno === "a" || d.turno === "ambos"));
  $("debateB").classList.toggle("habla", activo && (d.turno === "b" || d.turno === "ambos"));
  $("debateTipo").textContent = activo ? (TIPOS_DEBATE()[d.tipo] || "") : "";
  const r = d.restante_s || 0;
  $("debateReloj").textContent = !activo ? "" : d.turno === "nadie" ? t("Nadie tiene el turno")
    : d.turno === "ambos" ? t("Hablan los dos")
    : (d.turno === "a" ? t("Turno de A") : t("Turno de B")) + (d.turno_s > 0 ? " · " + mmss(r) : "");
  $("debateReloj").classList.toggle("oculto", !activo);
  $("debateIniciar").classList.toggle("oculto", activo);
  for (const x of document.querySelectorAll(".debate-en")) x.classList.toggle("oculto", !activo);
  if (activo && d.aviso) $("debateReloj").title = d.aviso;
}

// --- El menú de una banca --------------------------------------------------------------------
function menuBanca(ev, b, i, remoto) {
  ev.stopPropagation();
  const m = $("menuBanca");
  m.textContent = "";
  m.appendChild(el("div", "titulo", i.nombre + " · " + llenar(t("Banca %1"), b.banca)));
  const op = (texto, cls, fn) => { const x = el("button", "boton " + cls, texto); x.addEventListener("click", () => { fn(); cerrarMenu(); }); m.appendChild(x); };
  if (remoto && remoto.estado === "esperando") op(t("Habilitar (lo vi en cámara)"), "celeste", () => mandar({ type: "habilitar", id: i.id }));
  op(t("Darle la palabra"), "celeste", () => mandar({ type: "dar_palabra", id: i.id }));
  op(t("Debate: A"), "", () => { debateA = { id: i.id, nombre: i.nombre }; pintarDebate(); });
  op(t("Debate: B"), "", () => { debateB = { id: i.id, nombre: i.nombre }; pintarDebate(); });
  if (remoto && remoto.estado === "habilitado") {
    op(t("Deshabilitar"), "", () => mandar({ type: "deshabilitar", id: i.id }));
  }
  if (remoto && remoto.conectado) {
    // Apretar para hablar desde la banca (no solo desde la fila): el menú queda abierto
    // mientras se usa, así se puede hablar más de una vez.
    const p = botonPtt(i.id, i.nombre);
    p.textContent = t("🎙 Mantené apretado para hablarle");
    p.classList.remove("chico");
    p.addEventListener("click", (e) => e.stopPropagation());
    m.appendChild(p);
  }
  if (remoto && remoto.conectado) op(t("Sacar de la sesión"), "rojo", () => { if (confirm(llenar(t("¿Sacar a %1 de la sesión?"), i.nombre))) mandar({ type: "expulsar", id: i.id }); });
  const r = ev.currentTarget.getBoundingClientRect();
  m.style.left = Math.min(window.innerWidth - 230, r.right + 6) + "px";
  m.style.top = Math.min(window.innerHeight - 220, r.top) + "px";
  m.classList.remove("oculto");
}
function cerrarMenu() { $("menuBanca").classList.add("oculto"); }
document.addEventListener("click", cerrarMenu);
document.addEventListener("keydown", (e) => { if (e.key === "Escape") cerrarMenu(); });

// Al cambiar el tamaño de la ventana (girar la tablet), se vuelven a acomodar las bancas.
let redibujar = null;
window.addEventListener("resize", () => { clearTimeout(redibujar); redibujar = setTimeout(() => { if (est) pintarRecinto(); }, 120); });

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
    if (pc.connectionState === "failed") { conexiones.delete(id); avisar(t("No se pudo conectar directo con el integrante (¿está en otra red? hace falta TURN)."), true); }
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
  b.title = llenar(t("Mantené apretado para hablar con %1"), nombre);
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
    }).catch(() => avisar(t("Sin micrófono: revisá el permiso del navegador."), true));
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

// --- Pantalla completa y pantalla prendida -------------------------------------------------------
// En una tablet sobre la mesa de la presidencia: que no se apague sola en medio de la
// sesión, y que se pueda usar a pantalla completa (sin la barra del navegador).
let candado = null;
async function despierta() {
  try { if ("wakeLock" in navigator && !candado) { candado = await navigator.wakeLock.request("screen"); candado.addEventListener("release", () => { candado = null; }); } }
  catch (e) { /* sin permiso o sin soporte: no importa */ }
}
document.addEventListener("visibilitychange", () => { if (document.visibilityState === "visible" && adentro) despierta(); });
const botonCompleta = $("pantallaCompleta");
if (!document.documentElement.requestFullscreen) botonCompleta.classList.add("oculto");
botonCompleta.addEventListener("click", () => {
  if (document.fullscreenElement) document.exitFullscreen();
  else document.documentElement.requestFullscreen().catch(() => {});
});
document.addEventListener("fullscreenchange", () => {
  botonCompleta.textContent = document.fullscreenElement ? "⤡" : "⤢";
  botonCompleta.title = document.fullscreenElement ? t("Salir de pantalla completa") : t("Pantalla completa");
});
