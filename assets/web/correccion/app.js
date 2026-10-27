// El puesto del corrector de subtítulos (T44). Ver index.html.
"use strict";
(async () => {
  const clave = new URLSearchParams(location.hash.slice(1)).get("k") || "";
  const lista = document.getElementById("lista");
  const vacio = document.getElementById("vacio");
  const estado = document.getElementById("estado");
  const tarjetas = new Map();   // id -> {el, ta, resta, barra, vence, total, editado}
  let activa = null;            // id de la frase con el foco
  let desfase = 0;              // (edad del dato en el servidor) para la cuenta regresiva

  function mostrarEstado(tj, cls) { estado.textContent = tj; estado.className = "estado " + (cls || ""); }

  async function mandar(id, texto, descartar) {
    try {
      const r = await fetch("/api/correccion", { method: "POST", headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ k: clave, id, texto, descartar: !!descartar }) });
      if (!r.ok) mostrarEstado(BAi18n.llenar(t("no se pudo mandar (%1)"), r.status), "mal");
    } catch (e) { mostrarEstado(t("sin conexión"), "mal"); }
    const tj = tarjetas.get(id);
    if (tj) { tj.el.remove(); tarjetas.delete(id); }
    enfocarSiguiente();
  }

  function enfocarSiguiente() {
    const ids = [...tarjetas.keys()];
    if (!ids.length) { activa = null; return; }
    if (!tarjetas.has(activa)) activa = ids[0];
    marcar();
  }
  function marcar() {
    for (const [id, tj] of tarjetas) tj.el.classList.toggle("activa", id === activa);
    const tj = tarjetas.get(activa);
    if (tj && document.activeElement !== tj.ta) tj.ta.focus();
  }

  function crear(p) {
    const el = document.createElement("div");
    el.className = "frase";
    el.innerHTML = '<div class="cabecera"><span class="flujo"></span><span class="resta"></span></div>' +
      '<textarea rows="2"></textarea><div class="barra"><div></div></div>' +
      '<div class="botones"><button class="si"></button><button class="no"></button></div>';
    el.querySelector(".si").textContent = t("Aprobar ⏎");
    el.querySelector(".no").textContent = t("Descartar");
    el.querySelector(".flujo").textContent = p.flujo + (p.origen ? " · " + p.origen : "");
    const ta = el.querySelector("textarea");
    ta.value = p.texto;
    const tj = { el, ta, resta: el.querySelector(".resta"), barra: el.querySelector(".barra > div"),
                vence: Date.now() + (p.resta_ms || 0), total: Math.max(1, p.resta_ms || 1), editado: false, original: p.texto };
    ta.addEventListener("input", () => { tj.editado = ta.value !== tj.original; });
    ta.addEventListener("focus", () => { activa = p.id; marcar(); });
    ta.addEventListener("keydown", (e) => {
      if (e.key === "Enter" && !e.shiftKey) { e.preventDefault(); mandar(p.id, tj.editado ? ta.value : undefined, false); }
      else if (e.key === "Escape") { e.preventDefault(); mandar(p.id, undefined, true); }
      else if (e.key === "ArrowDown" && e.altKey === false && ta.selectionStart === ta.value.length) mover(1);
      else if (e.key === "ArrowUp" && ta.selectionStart === 0) mover(-1);
    });
    el.querySelector(".si").addEventListener("click", () => mandar(p.id, tj.editado ? ta.value : undefined, false));
    el.querySelector(".no").addEventListener("click", () => mandar(p.id, undefined, true));
    lista.appendChild(el);
    tarjetas.set(p.id, tj);
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
      if (r.status === 403) { mostrarEstado(t("la clave no sirve (pedile el enlace nuevo al operador)"), "mal"); return; }
      const d = await r.json();
      mostrarEstado(t("conectado"), "ok");
      desfase = d.edad_ms || 0;
      const vistos = new Set();
      for (const p of d.pendientes || []) {
        vistos.add(p.id);
        if (!tarjetas.has(p.id)) crear(p);
        const tj = tarjetas.get(p.id);
        tj.vence = Date.now() + Math.max(0, (p.resta_ms || 0) - desfase);
      }
      // Las que ya salieron (solas o por otro corrector): fuera.
      for (const [id, tj] of tarjetas) if (!vistos.has(id)) { tj.el.remove(); tarjetas.delete(id); }
      vacio.classList.toggle("oculto", tarjetas.size > 0);
      enfocarSiguiente();
    } catch (e) { mostrarEstado(t("sin conexión"), "mal"); }
  }

  function tic() {
    const ahora = Date.now();
    for (const [, tj] of tarjetas) {
      const s = Math.max(0, (tj.vence - ahora) / 1000);
      tj.resta.textContent = BAi18n.llenar(t("sale en %1 s"), s.toFixed(1));
      tj.resta.classList.toggle("poco", s < 2);
      tj.barra.style.width = Math.max(0, Math.min(100, 100 * (tj.vence - ahora) / tj.total)) + "%";
    }
  }

  // Los textos, en el idioma del evento.
  await BAi18n.iniciar({ contexto: "web/correccion", preferir: "evento" });
  if (!clave) mostrarEstado(t("falta la clave en el enlace (#k=…)"), "mal");
  else { pedir(); setInterval(pedir, 500); setInterval(tic, 100); }
})();
