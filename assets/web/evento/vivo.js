// vivo.js — el estado en vivo del evento para las páginas de cada sección
// (las de la carpeta Web/ del evento). Adentro del marco lo recibe del marco;
// abierta sola, se lo pregunta a la sala cada 3 s.
//
// Completa los elementos con data-vivo:
//   estado        En vivo / Próximamente / En pausa / Terminó (y la clase en_vivo, pausa…)
//   seccion       la sección que se está tratando
//   siguiente     la que sigue
//   cuenta        cuánto falta para empezar
//   transcurrido  cuánto lleva el evento
//   agenda        una lista con <li data-seccion="<id>">: marca hecha / actual
// Y avisa en window con el evento «evento» (e.detail = el estado entero).
"use strict";
(function () {
  const ESTADOS = { proximo: "Próximamente", en_vivo: "En vivo", pausa: "En pausa", terminado: "Terminó" };
  let ultimo = null, desfase = 0, ultimaFirma = "";
  const oyentes = new Set();

  function mmss(seg) {
    seg = Math.max(0, Math.round(seg));
    const d = Math.floor(seg / 86400), h = Math.floor((seg % 86400) / 3600), m = Math.floor((seg % 3600) / 60), s = seg % 60;
    if (d) return d + (d === 1 ? " día " : " días ") + h + " h";
    return (h ? h + ":" + String(m).padStart(2, "0") : String(m)) + ":" + String(s).padStart(2, "0");
  }
  function poner(nombre, texto) {
    for (const el of document.querySelectorAll('[data-vivo="' + nombre + '"]')) el.textContent = texto;
  }
  function relojes() {
    if (!ultimo) return;
    const ahora = Date.now() + desfase;
    poner("cuenta", ultimo.estado === "proximo" && ultimo.inicioMs > ahora ? "Empieza en " + mmss((ultimo.inicioMs - ahora) / 1000) : "");
    poner("transcurrido", ultimo.arrancoMs && (ultimo.estado === "en_vivo" || ultimo.estado === "pausa")
      ? mmss((ahora - ultimo.arrancoMs) / 1000) : "");
  }
  // El programa sigue al guion: si se agregaron, sacaron o renombraron secciones
  // después de escribir la página, la lista se rehace (con sus enlaces).
  function rehacerAgendas(secciones) {
    const firma = secciones.map((s) => s.id + "\u0001" + s.nombre).join("\u0002");
    for (const ol of document.querySelectorAll('[data-vivo="agenda"]')) {
      if (ol.dataset.firma === firma) continue;
      const actuales = [...ol.querySelectorAll("[data-seccion]")].map((li) => li.dataset.seccion + "\u0001" + li.textContent.trim().split("\n")[0]);
      ol.dataset.firma = firma;
      if (!secciones.length) continue;
      const iguales = actuales.length === secciones.length &&
        secciones.every((s, i) => (actuales[i] || "").startsWith(s.id + "\u0001" + s.nombre));
      if (iguales) continue;
      ol.textContent = "";
      for (const s of secciones) {
        const li = document.createElement("li");
        li.dataset.seccion = s.id;
        const a = document.createElement("a");
        a.href = s.pagina;
        a.textContent = s.nombre;
        li.appendChild(a);
        if (s.duracionSeg > 0) {
          const d = document.createElement("span");
          d.className = "dur";
          const m = Math.ceil(s.duracionSeg / 60);
          d.textContent = m >= 60 ? Math.floor(m / 60) + " h " + (m % 60 ? (m % 60) + " min" : "") : m + " min";
          li.appendChild(d);
        }
        ol.appendChild(li);
      }
    }
  }
  function aplicar(e) {
    if (!e) return;
    ultimo = e;
    if (e.ahoraMs) desfase = e.ahoraMs - Date.now();
    for (const el of document.querySelectorAll('[data-vivo="estado"]')) {
      el.textContent = ESTADOS[e.estado] || "";
      el.classList.remove("proximo", "en_vivo", "pausa", "terminado");
      if (e.estado) el.classList.add(e.estado);
    }
    poner("seccion", e.seccion ? e.seccion.nombre : "");
    poner("siguiente", e.siguiente ? e.siguiente.nombre : (e.estado === "terminado" ? "" : "el cierre"));
    rehacerAgendas(e.secciones || []);
    for (const el of document.querySelectorAll('[data-vivo="programa"]')) el.hidden = !(e.secciones || []).length;
    const porId = {};
    for (const s of e.secciones || []) porId[s.id] = s.estado;
    for (const li of document.querySelectorAll('[data-vivo="agenda"] [data-seccion]')) {
      const est = porId[li.dataset.seccion];
      li.classList.toggle("hecha", est === "hecha");
      li.classList.toggle("actual", est === "actual");
    }
    relojes();
    // Solo cuando cambió algo (la hora de la sala cambia siempre y no cuenta).
    const firma = JSON.stringify(Object.assign({}, e, { ahoraMs: 0 }));
    if (firma === ultimaFirma) return;
    ultimaFirma = firma;
    window.dispatchEvent(new CustomEvent("evento", { detail: e }));
    for (const fn of oyentes) { try { fn(e); } catch (err) { console.error(err); } }
  }

  const enMarco = window.parent !== window;
  window.addEventListener("message", (ev) => {
    if (ev.origin === location.origin && ev.data && ev.data.tipo === "evento") aplicar(ev.data.estado);
  });
  async function traer() {
    try {
      const r = await fetch("/api/evento", { cache: "no-store" });
      if (r.ok) aplicar(await r.json());
    } catch (err) { /* se reintenta */ }
  }
  if (enMarco) window.parent.postMessage({ tipo: "evento?" }, location.origin);
  else { traer(); setInterval(traer, 3000); }
  setInterval(relojes, 1000);
  // La API para las páginas del evento (docs/funciones/EVENTO-WEB.md):
  //   BAEvento.on(fn)      fn(estado) ya (si hay) y cada vez que el evento cambia
  //   BAEvento.off(fn)
  //   BAEvento.estado()    el último estado (o null)
  //   BAEvento.ahora()     la hora de la sala en ms (para relojes propios)
  //   BAEvento.duracion(s) «1:05», «2 h 10 min»…
  window.BAEvento = {
    on(fn) { oyentes.add(fn); if (ultimo) { try { fn(ultimo); } catch (err) { console.error(err); } } return fn; },
    off(fn) { oyentes.delete(fn); },
    estado: () => ultimo,
    ahora: () => Date.now() + desfase,
    duracion: mmss,
  };
})();
