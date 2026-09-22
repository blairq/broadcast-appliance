// Rótulos desde el celular. Ver el comentario de index.html.
//
// Dos reglas que vienen de usarlo en vivo:
//   1. Lo que se escribe NO sale solo. Sale con el botón. Un rótulo que cambia
//      mientras alguien escribe es un desastre en pantalla.
//   2. Mientras el campo está enfocado, la actualización del servidor NO lo
//      pisa. Si no, el texto que uno está escribiendo desaparece cada segundo.
"use strict";

const API = "/api/rotulos";

/// La clave sale del fragmento (#k=...) y se guarda para las recargas. El
/// fragmento no lo manda el navegador al servidor, que es de lo que se trata.
function claveDelEnlace() {
  const h = new URLSearchParams(location.hash.replace(/^#/, ""));
  const k = h.get("k");
  if (k) {
    try { sessionStorage.setItem("rotulos.k", k); } catch (e) { /* modo privado */ }
    return k;
  }
  try { return sessionStorage.getItem("rotulos.k") || ""; } catch (e) { return ""; }
}

let clave = claveDelEnlace();
let ultimo = "";          // firma del estado dibujado, para no redibujar al pedo
const escrito = {};       // lo que se está escribiendo, por fuente y campo

const $ = (id) => document.getElementById(id);

function mostrarPin(msg) {
  $("pin").classList.remove("hidden");
  if (msg) $("pinMsg").textContent = msg;
}

$("pinOk").addEventListener("click", () => {
  clave = $("pinCampo").value.trim();
  try { sessionStorage.setItem("rotulos.k", clave); } catch (e) { /* nada */ }
  $("pin").classList.add("hidden");
  mirar();
});

async function mirar() {
  if (!clave) { mostrarPin(); return; }
  let r;
  try {
    r = await fetch(API + "?k=" + encodeURIComponent(clave), { cache: "no-store" });
  } catch (e) {
    $("estado").textContent = "sin conexión";
    return;
  }
  if (r.status === 403) { mostrarPin("La clave no sirve. Pedile el QR al operador."); return; }
  if (!r.ok) { $("estado").textContent = "error " + r.status; return; }
  dibujar(await r.json());
}

async function encargar(id, accion, valores, grafico) {
  const cuerpo = { clave: clave, id: id, accion: accion };
  if (valores) cuerpo.valores = valores;
  if (grafico) cuerpo.grafico = grafico;
  try {
    const r = await fetch(API, {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify(cuerpo)
    });
    if (r.status === 403) { mostrarPin("La clave no sirve."); return; }
    avisar(id, accion === "sacar" ? "sacado" : "mandado");
  } catch (e) {
    avisar(id, "no salió");
  }
}

function avisar(id, texto) {
  const e = document.querySelector('[data-aviso="' + id + '"]');
  if (!e) return;
  e.textContent = texto;
  clearTimeout(e._t);
  e._t = setTimeout(() => { e.textContent = ""; }, 2500);
}

function dibujar(est) {
  $("escena").textContent = est.escena ? est.escena : "sin escena al aire";
  $("estado").textContent = est.viva ? "" : "la app no responde";
  $("estado").className = "estado" + (est.viva ? "" : " mal");

  const fuentes = Array.isArray(est.fuentes) ? est.fuentes : [];
  // Solo se redibuja si cambió algo de la ESTRUCTURA. Los valores que llegan
  // del servidor no pisan lo que se está escribiendo (ver regla 2).
  const firma = JSON.stringify(fuentes.map((f) => [f.id, f.grafico, (f.campos || []).map((c) => c.clave), f.enAire]));
  if (firma === ultimo) return;
  ultimo = firma;

  const lista = $("lista");
  lista.textContent = "";
  if (fuentes.length === 0) {
    const p = document.createElement("p");
    p.className = "vacio";
    p.textContent = "La escena que está al aire no tiene ningún gráfico. " +
                    "Cuando el operador cambie de escena, esto se llena solo.";
    lista.appendChild(p);
    return;
  }

  for (const f of fuentes) {
    const card = document.createElement("section");
    card.className = "tarjeta";

    const h = document.createElement("h2");
    h.textContent = f.fuente;
    const chip = document.createElement("span");
    chip.className = "chip" + (f.enAire ? " aire" : "");
    chip.textContent = f.titulo || f.grafico || "";
    h.appendChild(chip);
    card.appendChild(h);

    const campos = {};
    for (const c of (f.campos || [])) {
      const lab = document.createElement("label");
      lab.textContent = c.titulo || c.clave;
      const inp = document.createElement("input");
      inp.type = "text";
      inp.value = (f.valores && f.valores[c.clave] !== undefined) ? String(f.valores[c.clave]) : "";
      inp.autocomplete = "off";
      const guardado = escrito[f.id] && escrito[f.id][c.clave];
      if (guardado !== undefined) inp.value = guardado;
      inp.addEventListener("input", () => {
        if (!escrito[f.id]) escrito[f.id] = {};
        escrito[f.id][c.clave] = inp.value;
      });
      lab.appendChild(inp);
      card.appendChild(lab);
      campos[c.clave] = inp;
    }

    const fila = document.createElement("div");
    fila.className = "botones";

    const juntar = () => {
      const v = {};
      for (const k in campos) v[k] = campos[k].value;
      return v;
    };

    const aire = document.createElement("button");
    aire.className = "primario";
    aire.textContent = "Al aire";
    aire.addEventListener("click", () => encargar(f.id, "aire", juntar(), f.grafico));
    fila.appendChild(aire);

    const bucle = document.createElement("button");
    bucle.textContent = "En bucle";
    bucle.addEventListener("click", () => encargar(f.id, "bucle", juntar(), f.grafico));
    fila.appendChild(bucle);

    const sacar = document.createElement("button");
    sacar.textContent = "Sacar";
    sacar.addEventListener("click", () => encargar(f.id, "sacar", null, null));
    fila.appendChild(sacar);

    const aviso = document.createElement("span");
    aviso.className = "aviso";
    aviso.dataset.aviso = String(f.id);
    fila.appendChild(aviso);

    card.appendChild(fila);
    lista.appendChild(card);
  }
}

mirar();
setInterval(mirar, 1500);
window.addEventListener("hashchange", () => { clave = claveDelEnlace(); ultimo = ""; mirar(); });
