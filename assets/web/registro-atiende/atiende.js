// La atención remota del registro por tótem (docs/funciones/REGISTRO-TOTEM.md, «Atención remota»).
// Para gente del equipo: ver todas las conversaciones de los tótems, mirar y escuchar una (un
// WebRTC con el gateway: baja la cámara del tótem y lo que se oye ahí; sube tu micrófono, que
// solo se usa mientras la atendés), atenderla (el asistente se calla y tu voz le llega solo a ese
// tótem) y completar o corregir los datos. La app decide todo; esta página pide y muestra.
"use strict";

const $ = (id) => document.getElementById(id);
const token = new URLSearchParams(location.hash.slice(1)).get("t") || "";
const kClaveNombre = "ba.registroAtiende.nombre";

let ws = null;
let reintento = 1000;
let terminado = false;
let timerPing = 0;
let aconn = "";          // quién soy para el gateway ("a3")
let nombre = "";
let lista = [];          // las conversaciones, como las manda la app
let elegida = "";        // el conn que miro
let canal = 0;           // el canal que me da el gateway para esa conversación (0 = sin cámara)
let pc = null;
let audioTx = null;      // el transceptor del audio: baja lo del tótem, sube mi micrófono
let remoto = null;       // lo que baja (video y audio del tótem)
let mic = null;          // mi micrófono, solo mientras atiendo
let pidoAtender = 0;     // cuándo pedí atender (0 = no estoy esperando la confirmación)
const kEsperaAtenderMs = 5000;
let micMudo = false;
let camposDe = "";       // de qué conversación y con qué claves está armado el formulario
const sucios = new Map(); // clave -> cuándo lo mandé (hasta que la app lo devuelve, no se pisa)
const kSucioMs = 1500;
let timerAviso = 0;

// --- Lo de siempre ----------------------------------------------------------------------------
function leerNombre() {
  try { return localStorage.getItem(kClaveNombre) || ""; } catch (e) { return ""; }
}
function guardarNombre(n) {
  try { localStorage.setItem(kClaveNombre, n); } catch (e) {}
}

function mostrar(id) {
  for (const s of ["entrar", "atencion", "cortado"]) $(s).classList.toggle("oculto", s !== id);
}

function conexion(viva) {
  $("conexion").classList.toggle("viva", viva);
  $("conexion").classList.toggle("apagada", !viva);
}

function aviso(texto) {
  $("aviso").textContent = texto;
  $("aviso").classList.remove("oculto");
  clearTimeout(timerAviso);
  timerAviso = setTimeout(() => $("aviso").classList.add("oculto"), 5000);
}

function cortar(texto) {
  terminado = true;
  cerrarMedios();
  $("cortadoTexto").textContent = texto;
  mostrar("cortado");
  if (ws) { try { ws.close(); } catch (e) {} }
}

function mandar(m) {
  if (ws && ws.readyState === 1) ws.send(JSON.stringify(m));
}

function conversacion(conn) {
  return lista.find((c) => c.conn === conn) || null;
}

// --- Los medios: mirar y escuchar (y hablar, atendiendo) ----------------------------------------
async function iniciarMedios() {
  if (pc) return;
  if (window.BAIce) await BAIce.listo();
  if (pc || !ws || ws.readyState !== 1) return;
  const p = new RTCPeerConnection(window.BAIce ? BAIce.config() : { iceServers: [] });
  pc = p;
  remoto = new MediaStream();
  // Un audio de ida y vuelta (por ahí oigo al tótem y, atendiendo, sube mi voz) y un video que
  // solo recibe (la cámara del tótem). Qué conversación es lo cambia el gateway, sin renegociar.
  audioTx = p.addTransceiver("audio", { direction: "sendrecv" });
  p.addTransceiver("video", { direction: "recvonly" });
  if (mic) audioTx.sender.replaceTrack(mic.getAudioTracks()[0] || null).catch(() => {});
  p.ontrack = (ev) => {
    remoto.addTrack(ev.track);
    $("video").srcObject = remoto;
    $("sonido").srcObject = remoto;
    oir();
  };
  p.onicecandidate = (ev) => {
    if (ev.candidate) mandar({ type: "ice", candidate: ev.candidate });
  };
  p.onconnectionstatechange = () => {
    if (p.connectionState === "failed" && pc === p) {
      cerrarMedios(false);
      setTimeout(() => { if (elegida) iniciarMedios(); }, 2000);
    }
  };
  const oferta = await p.createOffer();
  await p.setLocalDescription(oferta);
  if (pc !== p) return;
  mandar({ type: "oferta", sdp: oferta.sdp });
}

function cerrarMedios(conMic = true) {
  if (pc) { try { pc.close(); } catch (e) {} }
  pc = null;
  audioTx = null;
  remoto = null;
  $("video").srcObject = null;
  $("sonido").srcObject = null;
  if (conMic) soltarMic();
}

// El navegador no deja sonar nada hasta que se toca la página: si no arranca, un botón.
function oir() {
  $("sonido").play().then(() => $("oir").classList.add("oculto"))
    .catch(() => { if ($("sonido").srcObject) $("oir").classList.remove("oculto"); });
  $("video").play().catch(() => {});
}

async function pedirMic() {
  if (mic) return true;
  try {
    mic = await navigator.mediaDevices.getUserMedia({
      audio: { echoCancellation: true, noiseSuppression: true, autoGainControl: true },
    });
  } catch (e) {
    mic = null;
    return false;
  }
  micMudo = false;
  if (audioTx) audioTx.sender.replaceTrack(mic.getAudioTracks()[0] || null).catch(() => {});
  return true;
}

function soltarMic() {
  if (audioTx) audioTx.sender.replaceTrack(null).catch(() => {});
  if (mic) for (const t of mic.getTracks()) { try { t.stop(); } catch (e) {} }
  mic = null;
  micMudo = false;
}

// --- La lista ------------------------------------------------------------------------------------
function textoPaso(c) {
  if (c.atendida) return c.atiendeConn === aconn ? t("La atendés vos") : c.paso || t("atendida");
  return c.paso || c.estado || "";
}

function pintarLista() {
  const ul = $("conversaciones");
  const vistas = new Set();
  for (const c of lista) {
    vistas.add(c.conn);
    let li = ul.querySelector('li[data-conn="' + c.conn + '"]');
    if (!li) {
      li = document.createElement("li");
      li.dataset.conn = c.conn;
      li.tabIndex = 0;
      li.innerHTML = '<div class="arriba"><span class="totem"></span><span class="lugar"></span></div>' +
                     '<span class="paso"></span><span class="ultimo"></span>';
      li.addEventListener("click", () => elegir(c.conn));
      li.addEventListener("keydown", (ev) => { if (ev.key === "Enter") elegir(c.conn); });
      ul.appendChild(li);
    }
    li.querySelector(".totem").textContent = (c.totem || c.id || "") + (c.quien ? " · " + c.quien : "");
    // Con cámara no quiere decir conversando: el tótem sigue conectado entre una persona y otra.
    const charla = c.estado !== "esperando" && c.estado !== "listo";
    li.querySelector(".lugar").textContent = c.canal === 0 ? t("sin cámara") : charla ? t("conversando") : t("esperando a alguien");
    li.querySelector(".paso").textContent = textoPaso(c);
    li.querySelector(".ultimo").textContent = c.oido ? "«" + c.oido + "»" : c.dice ? BAi18n.llenar(t("Asistente: «%1»"), c.dice) : "";
    li.classList.toggle("elegida", c.conn === elegida);
    li.classList.toggle("atendida", !!c.atendida && c.atiendeConn !== aconn);
    li.classList.toggle("mia", !!c.atendida && c.atiendeConn === aconn);
  }
  for (const li of Array.from(ul.children)) if (!vistas.has(li.dataset.conn)) li.remove();
  $("vacia").classList.toggle("oculto", lista.length > 0);
  $("cuantas").textContent = lista.length ? "(" + lista.length + ")" : "";
}

function elegir(conn) {
  if (conn === elegida) return;
  elegida = conn;
  canal = 0;
  camposDe = "";
  sucios.clear();
  mandar({ type: "mirar", conn });
  iniciarMedios(); // dentro del toque: el sonido queda destrabado
  oir();
  pintarLista();
  pintarDetalle();
}

// --- El detalle ------------------------------------------------------------------------------------
const kTipoInput = { correo: "email", telefono: "tel", numero: "number" };

function armarCampos(c) {
  const form = $("campos");
  form.textContent = "";
  for (const f of c.editables || []) {
    const fila = document.createElement("div");
    fila.className = "campo";
    fila.dataset.clave = f.clave;
    const id = "campo-" + f.clave;
    const et = document.createElement("label");
    et.htmlFor = id;
    et.textContent = f.etiqueta || f.clave;
    if (f.obligatorio) {
      const o = document.createElement("span");
      o.className = "obligatorio";
      o.textContent = " *";
      et.appendChild(o);
    }
    let control;
    if (f.tipo === "sino" || (f.tipo === "opcion" && Array.isArray(f.opciones) && f.opciones.length)) {
      control = document.createElement("select");
      const opciones = f.tipo === "sino" ? ["Sí", "No"] : f.opciones;
      // El valor que viaja queda en el original; lo que se ve, traducido.
      const etiquetas = f.tipo === "sino" ? { "Sí": t("Sí"), "No": t("No") } : {};
      control.appendChild(new Option("—", ""));
      for (const o of opciones) control.appendChild(new Option(etiquetas[o] || o, o));
    } else {
      control = document.createElement("input");
      control.type = kTipoInput[f.tipo] || "text";
      control.inputMode = f.tipo === "telefono" ? "tel" : f.tipo === "numero" ? "numeric" : f.tipo === "correo" ? "email" : "text";
      control.autocapitalize = f.tipo === "nombre" ? "words" : "off";
      control.maxLength = 200;
    }
    control.id = id;
    // Se manda al terminar de escribir (o al elegir): como si la persona lo hubiera tipeado.
    control.addEventListener("change", () => {
      const v = control.value.trim();
      sucios.set(f.clave, Date.now());
      mandar({ type: "dato", conn: elegida, clave: f.clave, valor: v });
    });
    fila.appendChild(et);
    fila.appendChild(control);
    form.appendChild(fila);
  }
}

function ponerValores(c) {
  for (const f of c.editables || []) {
    const fila = $("campos").querySelector('.campo[data-clave="' + f.clave + '"]');
    if (!fila) continue;
    const control = fila.querySelector("input, select");
    const valor = f.valor === undefined || f.valor === null ? "" : String(f.valor);
    fila.classList.toggle("listo", !!f.listo);
    fila.classList.toggle("pidiendo", !!(c.pedido && c.pedido.clave === f.clave && !f.listo));
    // Lo que se está escribiendo no se pisa; lo que mandé, un momento (lo que vuelve viene como lo
    // muestra el tótem: «11 4567 8901» aunque se haya escrito todo junto).
    if (document.activeElement === control) continue;
    if (sucios.has(f.clave)) {
      if (Date.now() - sucios.get(f.clave) < kSucioMs) continue;
      sucios.delete(f.clave);
    }
    if (control.value !== valor) control.value = valor;
  }
}

function pintarDetalle() {
  const c = conversacion(elegida);
  $("elegi").classList.toggle("oculto", !!c);
  $("elegida").classList.toggle("oculto", !c);
  if (!c) return;
  const mia = !!c.atendida && c.atiendeConn === aconn;
  const otra = !!c.atendida && !mia;
  $("dTotem").textContent = (c.totem || c.id || "") + (c.quien ? " · " + c.quien : "");
  $("dPaso").textContent = textoPaso(c);
  $("dOido").textContent = c.oido || "—";
  $("dDice").textContent = c.dice || "—";
  $("dAtiende").classList.toggle("oculto", !c.atendida);
  $("dAtiende").classList.toggle("otra", otra);
  $("dAtiende").textContent = mia ? t("La atendés vos") : c.atiende ? BAi18n.llenar(t("La atiende %1"), c.atiende) : t("La atiende alguien del equipo");
  $("atender").classList.toggle("oculto", !!c.atendida);
  $("atender").disabled = !(c.canal > 0);
  $("atender").title = c.canal > 0 ? "" : t("No está frente al tótem (sin cámara ni micrófono)");
  for (const b of ["devolver", "guardar", "cancelar"]) $(b).classList.toggle("oculto", !mia);
  $("silenciar").classList.toggle("oculto", !(mia && mic));
  $("silenciar").textContent = micMudo ? t("Activar mi micrófono") : t("Silenciar mi micrófono");
  $("microfono").classList.toggle("oculto", !(mia && mic));
  $("microfono").classList.toggle("mudo", micMudo);
  $("microfono").textContent = micMudo ? t("● Micrófono silenciado") : t("● Te escucha");
  $("sinVideo").classList.toggle("oculto", canal > 0 && !!remoto && remoto.getVideoTracks().length > 0);
  $("sinVideo").textContent = canal > 0 ? t("Esperando la cámara del tótem…") : t("Esta conversación no tiene la cámara conectada.");
  const firma = c.conn + "|" + (c.editables || []).map((f) => f.clave + ":" + f.tipo).join(",");
  if (firma !== camposDe) {
    camposDe = firma;
    armarCampos(c);
  }
  ponerValores(c);
  // Mi micrófono, solo mientras la atiendo (si dejé de atenderla de cualquier forma, se suelta).
  // Recién pedida, la lista que llega todavía puede ser la de antes: se espera la confirmación
  // (si no, el micrófono se soltaba en el mismo instante en que se abría y la persona no me oía).
  const atiendoAlguna = lista.some((x) => x.atendida && x.atiendeConn === aconn);
  if (atiendoAlguna) pidoAtender = 0;
  const esperando = pidoAtender > 0 && Date.now() - pidoAtender < kEsperaAtenderMs;
  if (!atiendoAlguna && !esperando && mic) soltarMic();
}

// --- El WebSocket ------------------------------------------------------------------------------------
function conectar() {
  const proto = location.protocol === "https:" ? "wss:" : "ws:";
  ws = new WebSocket(proto + "//" + location.host + "/ws?registro_atiende=1");
  ws.onopen = () => mandar({ type: "auth", t: token, nombre });
  ws.onmessage = (ev) => {
    let m;
    try { m = JSON.parse(ev.data); } catch (e) { return; }
    recibir(m);
  };
  ws.onclose = () => {
    conexion(false);
    clearInterval(timerPing);
    cerrarMedios();
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
      aconn = m.aconn || "";
      nombre = m.nombre || nombre;
      $("quien").textContent = nombre;
      clearInterval(timerPing);
      timerPing = setInterval(() => mandar({ type: "ping" }), 20000);
      mostrar("atencion");
      if (elegida) { mandar({ type: "mirar", conn: elegida }); iniciarMedios(); } // volvió la conexión
      break;
    case "error":
      cortar(m.error || t("Este enlace de atención no es válido."));
      break;
    case "cortado":
      cortar(m.motivo || t("Se cerró la atención."));
      break;
    case "conversaciones":
      $("evento").textContent = m.evento || "";
      lista = Array.isArray(m.lista) ? m.lista : [];
      if (elegida && !conversacion(elegida)) {
        elegida = "";
        mandar({ type: "mirar", conn: "" });
      }
      pintarLista();
      pintarDetalle();
      break;
    case "mirando":
      if ((m.conn || "") === elegida) canal = m.canal || 0;
      pintarDetalle();
      break;
    case "respuesta":
      if (pc) pc.setRemoteDescription({ type: "answer", sdp: m.sdp }).catch(() => {});
      break;
    case "ice":
      if (pc && m.candidate) pc.addIceCandidate(m.candidate).catch(() => {});
      break;
    case "medios-error":
      cerrarMedios(false);
      aviso(m.error || t("No se pudo ver ni oír el tótem ahora; reintento en un rato."));
      setTimeout(() => { if (elegida && !pc) iniciarMedios(); }, 4000);
      break;
    case "aviso":
      // Lo que la app no pudo hacer (otra persona ya la atiende, un dato que no se entiende…).
      if (m.conn && m.conn === elegida) sucios.clear();
      aviso(m.texto || "");
      pintarDetalle();
      break;
    case "pong":
      break;
  }
}

// --- Botones -------------------------------------------------------------------------------------------
$("atender").addEventListener("click", async () => {
  const c = conversacion(elegida);
  if (!c) return;
  iniciarMedios();
  oir();
  // El micrófono se pide acá, con el toque (algunos navegadores lo exigen).
  pidoAtender = Date.now();
  const conMic = await pedirMic();
  if (!conMic) aviso(t("Sin micrófono: la persona no te va a oír, pero podés completar los datos."));
  mandar({ type: "atender", conn: c.conn });
});
$("devolver").addEventListener("click", () => mandar({ type: "devolver", conn: elegida }));
$("guardar").addEventListener("click", () => mandar({ type: "guardar", conn: elegida }));
$("cancelar").addEventListener("click", () => {
  if (confirm("¿Cancelar este registro? No se guarda nada.")) mandar({ type: "cancelar", conn: elegida });
});
$("silenciar").addEventListener("click", () => {
  if (!mic) return;
  micMudo = !micMudo;
  for (const t of mic.getAudioTracks()) t.enabled = !micMudo;
  pintarDetalle();
});
$("oir").addEventListener("click", oir);
$("video").addEventListener("loadeddata", () => pintarDetalle());
$("campos").addEventListener("submit", (ev) => ev.preventDefault());
$("quien").addEventListener("click", () => {
  $("nombre").value = nombre;
  mostrar("entrar");
});
$("formNombre").addEventListener("submit", (ev) => {
  ev.preventDefault();
  const n = $("nombre").value.trim();
  if (!n) { $("nombre").focus(); return; }
  const cambio = n !== nombre;
  nombre = n;
  guardarNombre(n);
  $("quien").textContent = n;
  if (!ws) { conectar(); return; }
  // Otro nombre: se vuelve a entrar con él (es lo que ve la persona del tótem).
  if (cambio && ws.readyState === 1) ws.close();
  else mostrar("atencion");
});
document.addEventListener("click", () => { if ($("sonido").srcObject) $("sonido").play().catch(() => {}); });

// --- Arranque ---------------------------------------------------------------------------------------------
(async function () {
  // Los textos, en el idioma del evento.
  await BAi18n.iniciar({ contexto: "web/registro-atiende", preferir: "evento" });
  if (!token) {
    cortar(t("Este enlace de atención no está completo. Pedí el enlace de nuevo."));
    return;
  }
  nombre = leerNombre();
  $("quien").textContent = nombre || t("Tu nombre");
  if (!nombre) { mostrar("entrar"); $("nombre").focus(); return; }
  conectar();
})();
