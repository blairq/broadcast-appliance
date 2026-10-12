// El marco de la página del evento en vivo. Le pregunta a la sala cómo va el
// evento (/api/evento, cada 2 s), muestra la franja de arriba y pone abajo la
// página de la sección que se trata; cuando el guion avanza, cambia sola. A la
// página de adentro le pasa el estado (postMessage) para que vivo.js lo use.
"use strict";

// Los enlaces de invitados de antes («/#room=…&pin=…») van a /invitados/: el
// fragmento no llega al servidor, así que el desvío lo hace la página.
function aInvitados() {
  const h = location.hash;
  if (/(^#|&)(room|pin)=/.test(h)) location.replace("/invitados/" + h);
}
aInvitados();
window.addEventListener("hashchange", aInvitados);

// Un id por pestaña, para que la sala cuente cuántos miran (sin datos: un número al azar).
const idMirada = (() => {
  try {
    let v = sessionStorage.getItem("ba-evento-v");
    if (!v) { v = Math.random().toString(36).slice(2, 12); sessionStorage.setItem("ba-evento-v", v); }
    return v;
  } catch (e) { return Math.random().toString(36).slice(2, 12); }
})();
const $ = (id) => document.getElementById(id);
let ultimo = null;      // el último estado
let paginaActual = "";  // lo que muestra el iframe
let desfase = 0;        // reloj de la sala - reloj de acá

const ESTADOS = { proximo: "Próximamente", en_vivo: "En vivo", pausa: "En pausa", terminado: "Terminó" };

function mmss(seg) {
  seg = Math.max(0, Math.round(seg));
  const h = Math.floor(seg / 3600), m = Math.floor((seg % 3600) / 60), s = seg % 60;
  return (h ? h + ":" + String(m).padStart(2, "0") : String(m)) + ":" + String(s).padStart(2, "0");
}

function pintarReloj() {
  if (!ultimo) return;
  const ahora = Date.now() + desfase;
  let t = "";
  if (ultimo.estado === "en_vivo" || ultimo.estado === "pausa") {
    if (ultimo.arrancoMs) t = "lleva " + mmss((ahora - ultimo.arrancoMs) / 1000);
  } else if (ultimo.estado === "proximo" && ultimo.inicioMs && ultimo.inicioMs > ahora) {
    t = "empieza en " + mmss((ultimo.inicioMs - ahora) / 1000);
  }
  $("reloj").textContent = t;
}

function aplicar(e) {
  ultimo = e;
  if (e.ahoraMs) desfase = e.ahoraMs - Date.now();
  if (!e.activo) {
    $("barra").classList.add("oculto");
    $("pagina").classList.add("oculto");
    $("sinEvento").classList.remove("oculto");
    paginaActual = "";
    return;
  }
  $("sinEvento").classList.add("oculto");
  $("pagina").classList.remove("oculto");
  if (e.color) document.documentElement.style.setProperty("--color", e.color);
  document.title = e.nombre || "Evento en vivo";
  $("barra").classList.toggle("oculto", e.barra === false);
  $("estado").textContent = ESTADOS[e.estado] || e.estado || "";
  $("estado").className = "vivo " + (e.estado || "");
  $("nombre").textContent = e.nombre || "";
  $("ahora").innerHTML = "";
  if (e.seccion) {
    const b = document.createElement("b");
    b.textContent = "Ahora: ";
    $("ahora").append(b, document.createTextNode(e.seccion.nombre));
  }
  $("sigue").textContent = e.siguiente ? "Sigue: " + e.siguiente.nombre : "";
  pintarReloj();
  // La página: cambia cuando cambia la sección (o cuando se regeneró el sitio).
  const p = (e.pagina || "portada.html") + (e.version ? "?v=" + e.version : "");
  if (p !== paginaActual) {
    paginaActual = p;
    $("pagina").src = "/evento/" + p;
  }
  try { $("pagina").contentWindow.postMessage({ tipo: "evento", estado: e }, location.origin); } catch (err) {}
}

async function traer() {
  try {
    const r = await fetch("/api/evento?v=" + idMirada, { cache: "no-store" });
    if (r.ok) aplicar(await r.json());
  } catch (err) { /* sin conexión: se reintenta en la próxima vuelta */ }
}

// La página nueva pide el estado apenas carga (vivo.js): se lo damos ya.
window.addEventListener("message", (ev) => {
  if (ev.origin === location.origin && ev.data && ev.data.tipo === "evento?" && ultimo)
    ev.source.postMessage({ tipo: "evento", estado: ultimo }, location.origin);
});

traer();
setInterval(traer, 2000);
setInterval(pintarReloj, 1000);
