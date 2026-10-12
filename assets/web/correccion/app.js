// El puesto del corrector de subtítulos (T44). Ver index.html.
"use strict";
(() => {
  const clave = new URLSearchParams(location.hash.slice(1)).get("k") || "";
  const lista = document.getElementById("lista");
  const vacio = document.getElementById("vacio");
  const estado = document.getElementById("estado");
  const tarjetas = new Map();   // id -> {el, ta, resta, barra, vence, total, editado}
  let activa = null;            // id de la frase con el foco
  let desfase = 0;              // (edad del dato en el servidor) para la cuenta regresiva

  function mostrarEstado(t, cls) { estado.textContent = t; estado.className = "estado " + (cls || ""); }

  async function mandar(id, texto, descartar) {
    try {
      const r = await fetch("/api/correccion", { method: "POST", headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ k: clave, id, texto, descartar: !!descartar }) });
      if (!r.ok) mostrarEstado("no se pudo mandar (" + r.status + ")", "mal");
    } catch (e) { mostrarEstado("sin conexión", "mal"); }
    const t = tarjetas.get(id);
    if (t) { t.el.remove(); tarjetas.delete(id); }
    enfocarSiguiente();
  }

  function enfocarSiguiente() {
    const ids = [...tarjetas.keys()];
    if (!ids.length) { activa = null; return; }
    if (!tarjetas.has(activa)) activa = ids[0];
    marcar();
  }
  function marcar() {
    for (const [id, t] of tarjetas) t.el.classList.toggle("activa", id === activa);
    const t = tarjetas.get(activa);
    if (t && document.activeElement !== t.ta) t.ta.focus();
  }

  function crear(p) {
    const el = document.createElement("div");
    el.className = "frase";
    el.innerHTML = '<div class="cabecera"><span class="flujo"></span><span class="resta"></span></div>' +
      '<textarea rows="2"></textarea><div class="barra"><div></div></div>' +
      '<div class="botones"><button class="si">Aprobar ⏎</button><button class="no">Descartar</button></div>';
    el.querySelector(".flujo").textContent = p.flujo + (p.origen ? " · " + p.origen : "");
    const ta = el.querySelector("textarea");
    ta.value = p.texto;
    const t = { el, ta, resta: el.querySelector(".resta"), barra: el.querySelector(".barra > div"),
                vence: Date.now() + (p.resta_ms || 0), total: Math.max(1, p.resta_ms || 1), editado: false, original: p.texto };
    ta.addEventListener("input", () => { t.editado = ta.value !== t.original; });
    ta.addEventListener("focus", () => { activa = p.id; marcar(); });
    ta.addEventListener("keydown", (e) => {
      if (e.key === "Enter" && !e.shiftKey) { e.preventDefault(); mandar(p.id, t.editado ? ta.value : undefined, false); }
      else if (e.key === "Escape") { e.preventDefault(); mandar(p.id, undefined, true); }
      else if (e.key === "ArrowDown" && e.altKey === false && ta.selectionStart === ta.value.length) mover(1);
      else if (e.key === "ArrowUp" && ta.selectionStart === 0) mover(-1);
    });
    el.querySelector(".si").addEventListener("click", () => mandar(p.id, t.editado ? ta.value : undefined, false));
    el.querySelector(".no").addEventListener("click", () => mandar(p.id, undefined, true));
    lista.appendChild(el);
    tarjetas.set(p.id, t);
  }

  function mover(d) {
    const ids = [...tarjetas.keys()];
    const i = ids.indexOf(activa);
    if (i < 0) return;
    activa = ids[Math.max(0, Math.min(ids.length - 1, i + d))];
    marcar();
  }

  async function pedir() {
    try {
      const r = await fetch("/api/correccion?k=" + encodeURIComponent(clave), { cache: "no-store" });
      if (r.status === 403) { mostrarEstado("la clave no sirve (pedile el enlace nuevo al operador)", "mal"); return; }
      const d = await r.json();
      mostrarEstado("conectado", "ok");
      desfase = d.edad_ms || 0;
      const vistos = new Set();
      for (const p of d.pendientes || []) {
        vistos.add(p.id);
        if (!tarjetas.has(p.id)) crear(p);
        const t = tarjetas.get(p.id);
        t.vence = Date.now() + Math.max(0, (p.resta_ms || 0) - desfase);
      }
      // Las que ya salieron (solas o por otro corrector): fuera.
      for (const [id, t] of tarjetas) if (!vistos.has(id)) { t.el.remove(); tarjetas.delete(id); }
      vacio.classList.toggle("oculto", tarjetas.size > 0);
      enfocarSiguiente();
    } catch (e) { mostrarEstado("sin conexión", "mal"); }
  }

  function tic() {
    const ahora = Date.now();
    for (const [, t] of tarjetas) {
      const s = Math.max(0, (t.vence - ahora) / 1000);
      t.resta.textContent = "sale en " + s.toFixed(1) + " s";
      t.resta.classList.toggle("poco", s < 2);
      t.barra.style.width = Math.max(0, Math.min(100, 100 * (t.vence - ahora) / t.total)) + "%";
    }
  }

  if (!clave) mostrarEstado("falta la clave en el enlace (#k=…)", "mal");
  else { pedir(); setInterval(pedir, 500); setInterval(tic, 100); }
})();
