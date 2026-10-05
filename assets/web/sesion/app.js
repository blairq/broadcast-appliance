// La sesión remota del legislador (docs/SESION-REMOTA.md). Fase R1: entrar con el
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
let timerPing = null;

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
}

// --- La cámara y las fotos ------------------------------------------------------
async function prenderCamara() {
  try {
    const s = await navigator.mediaDevices.getUserMedia({ video: { width: 640, height: 360 }, audio: false });
    $("propia").srcObject = s;
    $("sinCamara").classList.add("oculto");
  } catch (e) {
    $("sinCamara").classList.remove("oculto");
  }
}

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
      mostrar("sala");
      prenderCamara().then(programarFotos);
      clearInterval(timerPing);
      timerPing = setInterval(() => ws && ws.readyState === 1 && ws.send('{"type":"ping"}'), 20000);
      ws.send(JSON.stringify({ type: "hola" }));
      break;
    case "estado":
      ponerEstado(m.estado, m.texto);
      break;
    case "info":
      if ("punto" in m) $("punto").textContent = m.punto || "—";
      if ("hablando" in m) $("hablando").textContent = m.hablando || "—";
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
