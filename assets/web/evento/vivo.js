// vivo.js — la API y los componentes del sitio del evento en vivo (las páginas
// de la carpeta Web/ del evento). Adentro del marco recibe el estado del marco;
// abierta sola, se lo pregunta a la sala (/api/evento) cada 3 s.
//
// Lo que completa (atributos data-vivo):
//   estado          En vivo / Próximamente / En pausa / Terminó (+ clase en_vivo, pausa…)
//   seccion         la sección que se está tratando        siguiente   la que sigue
//   cuenta          «Empieza en 2:10:05» (texto)            transcurrido cuánto lleva
//   cuenta-bloques  días / horas / min / seg en cajas (se esconde al empezar)
//   agenda          una lista: <li data-seccion="<id>"> (marca hecha / actual; se rehace
//                   sola si cambia el guion, con nombre, hora prevista y duración)
//   programa        se esconde mientras el guion no tiene secciones
//   reproductor     el vivo embebido, con pestañas YouTube / Twitch / Facebook
//                   (data-plataforma="youtube" para elegir cuál va primero)
//   vivos           botones «Ver en YouTube / Twitch / Facebook»
//   donde           se esconde si no hay dónde verlo
//   progreso        el ancho de una barra (sección actual / total); progreso-texto
// Componentes: [data-acordeon] (preguntas), [data-menu] (menú del celular).
//
// La API: BAEvento.on(fn) llama a fn(estado) ya y cada vez que cambia; BAEvento.off(fn),
// BAEvento.estado(), BAEvento.ahora() (la hora de la sala), BAEvento.duracion(seg),
// BAEvento.hora(ms) («21:00» en el huso del evento). Y el evento «evento» de window.
"use strict";
(function () {
  const ESTADOS = { proximo: "Próximamente", en_vivo: "En vivo", pausa: "En pausa", terminado: "Terminó" };
  const NOMBRES = { youtube: "YouTube", twitch: "Twitch", facebook: "Facebook" };
  let ultimo = null, desfase = 0, ultimaFirma = "", elegida = "";
  const oyentes = new Set();

  const $$ = (sel) => document.querySelectorAll(sel);
  function mmss(seg) {
    seg = Math.max(0, Math.round(seg));
    const d = Math.floor(seg / 86400), h = Math.floor((seg % 86400) / 3600), m = Math.floor((seg % 3600) / 60), s = seg % 60;
    if (d) return d + (d === 1 ? " día " : " días ") + h + " h";
    return (h ? h + ":" + String(m).padStart(2, "0") : String(m)) + ":" + String(s).padStart(2, "0");
  }
  function duracionCorta(seg) {
    const m = Math.ceil(seg / 60);
    return m >= 60 ? Math.floor(m / 60) + " h" + (m % 60 ? " " + (m % 60) + " min" : "") : m + " min";
  }
  function hora(ms) {
    if (!ms) return "";
    try {
      return new Intl.DateTimeFormat("es-AR", { hour: "2-digit", minute: "2-digit", timeZone: (ultimo && ultimo.huso) || undefined })
        .format(new Date(ms));
    } catch (e) { return new Date(ms).toTimeString().slice(0, 5); }
  }
  function poner(nombre, texto) { for (const el of $$('[data-vivo="' + nombre + '"]')) el.textContent = texto; }

  // --- Relojes -------------------------------------------------------------------
  function relojes() {
    if (!ultimo) return;
    const ahora = Date.now() + desfase;
    const falta = ultimo.estado === "proximo" && ultimo.inicioMs > ahora ? (ultimo.inicioMs - ahora) / 1000 : 0;
    poner("cuenta", falta ? "Empieza en " + mmss(falta) : "");
    poner("transcurrido", ultimo.arrancoMs && (ultimo.estado === "en_vivo" || ultimo.estado === "pausa")
      ? mmss((ahora - ultimo.arrancoMs) / 1000) : "");
    for (const caja of $$('[data-vivo="cuenta-bloques"]')) {
      caja.hidden = !falta;
      if (!falta) continue;
      const s = Math.floor(falta);
      const partes = [[Math.floor(s / 86400), "Días"], [Math.floor((s % 86400) / 3600), "Horas"],
                      [Math.floor((s % 3600) / 60), "Min"], [s % 60, "Seg"]];
      if (caja.children.length !== 4) {
        caja.textContent = "";
        for (const [, et] of partes) {
          const d = document.createElement("div");
          const b = document.createElement("b"); b.textContent = "00";
          const sp = document.createElement("span"); sp.textContent = et;
          d.append(b, sp);
          caja.appendChild(d);
        }
      }
      partes.forEach(([v], i) => { caja.children[i].querySelector("b").textContent = String(v).padStart(2, "0"); });
    }
  }

  // --- El programa ------------------------------------------------------------------
  function rehacerAgendas(secciones) {
    const firma = secciones.map((s) => [s.id, s.nombre, s.inicioPrevistoMs, s.duracionSeg].join("\u0001")).join("\u0002");
    for (const ol of $$('[data-vivo="agenda"]')) {
      if (ol.dataset.firma === firma) continue;
      ol.dataset.firma = firma;
      // Si la lista escrita ya coincide con el guion (ids en el mismo orden), se respeta tal cual.
      const ids = [...ol.querySelectorAll("[data-seccion]")].map((li) => li.dataset.seccion).join(",");
      if (ids && ids === secciones.map((s) => s.id).join(",") && !ol.hasAttribute("data-rehacer")) continue;
      ol.textContent = "";
      for (const s of secciones) {
        const li = document.createElement("li");
        li.dataset.seccion = s.id;
        const a = document.createElement("a");
        a.href = s.pagina;
        const n = document.createElement("span"); n.className = "nombre"; n.textContent = s.nombre;
        const m = document.createElement("span"); m.className = "meta";
        m.textContent = [hora(s.inicioPrevistoMs), s.duracionSeg ? duracionCorta(s.duracionSeg) : ""].filter(Boolean).join(" · ");
        a.append(n, m);
        li.appendChild(a);
        ol.appendChild(li);
      }
    }
    for (const el of $$('[data-vivo="programa"]')) el.hidden = !secciones.length;
  }

  // --- Dónde verlo en vivo ------------------------------------------------------------
  function embedDe(v) {
    // Twitch exige el host de la página que lo embebe.
    return v.plataforma === "twitch" ? v.embed + "&parent=" + encodeURIComponent(location.hostname) : v.embed;
  }
  function pintarVivos(vivos) {
    for (const el of $$('[data-vivo="donde"]')) el.hidden = !vivos.length;
    for (const nav of $$('[data-vivo="vivos"]')) {
      const firma = JSON.stringify(vivos.map((v) => [v.plataforma, v.url, v.enVivo]));
      if (nav.dataset.firma === firma) continue;
      nav.dataset.firma = firma;
      nav.textContent = "";
      for (const v of vivos) {
        const a = document.createElement("a");
        a.href = v.url; a.target = "_blank"; a.rel = "noopener";
        a.className = "plataforma " + v.plataforma + (v.enVivo ? " al-aire" : "");
        a.textContent = "Ver en " + (v.nombre || NOMBRES[v.plataforma] || v.plataforma);
        nav.appendChild(a);
      }
    }
    for (const caja of $$('[data-vivo="reproductor"]')) pintarReproductor(caja, vivos);
  }
  function pintarReproductor(caja, vivos) {
    const pref = elegida || caja.dataset.plataforma;
    const v = vivos.find((x) => x.plataforma === pref) || vivos.find((x) => x.enVivo) || vivos[0];
    const firma = JSON.stringify([vivos.map((x) => [x.plataforma, x.embed, x.enVivo]), v && v.plataforma]);
    if (caja.dataset.firma === firma) return;
    caja.dataset.firma = firma;
    caja.textContent = "";
    if (!v) {
      const vacio = document.createElement("div");
      vacio.className = "reproductor-vacio";
      vacio.textContent = caja.dataset.vacio || "El vivo aparece acá cuando empiece la transmisión.";
      caja.appendChild(vacio);
      return;
    }
    if (vivos.length > 1) {
      const tabs = document.createElement("div");
      tabs.className = "pestanas";
      tabs.setAttribute("role", "tablist");
      tabs.setAttribute("aria-label", "Plataforma");
      for (const x of vivos) {
        const b = document.createElement("button");
        b.type = "button";
        b.setAttribute("role", "tab");
        b.setAttribute("aria-selected", String(x === v));
        if (x.enVivo) b.className = "al-aire";
        b.textContent = x.nombre || NOMBRES[x.plataforma] || x.plataforma;
        b.addEventListener("click", () => { elegida = x.plataforma; pintarVivos(vivos); });
        tabs.appendChild(b);
      }
      caja.appendChild(tabs);
    }
    const marco = document.createElement("div");
    marco.className = "reproductor";
    const f = document.createElement("iframe");
    f.src = embedDe(v);
    f.title = "En vivo por " + (v.nombre || v.plataforma);
    f.allow = "autoplay; encrypted-media; picture-in-picture; fullscreen";
    f.allowFullscreen = true;
    marco.appendChild(f);
    caja.appendChild(marco);
  }

  // --- Aplicar el estado ------------------------------------------------------------------
  function aplicar(e) {
    if (!e) return;
    ultimo = e;
    if (e.ahoraMs) desfase = e.ahoraMs - Date.now();
    for (const el of $$('[data-vivo="estado"]')) {
      el.textContent = ESTADOS[e.estado] || "";
      el.classList.remove("proximo", "en_vivo", "pausa", "terminado");
      if (e.estado) el.classList.add(e.estado);
    }
    // Antes de empezar, lo escrito en la página (el nombre de esa sección) se respeta.
    if (e.seccion) poner("seccion", e.seccion.nombre);
    else if (e.estado === "terminado") poner("seccion", "Terminó");
    if (e.siguiente) poner("siguiente", e.siguiente.nombre);
    else if (e.estado === "en_vivo" || e.estado === "pausa") poner("siguiente", "El cierre");
    else if (e.estado === "terminado") poner("siguiente", "—");
    rehacerAgendas(e.secciones || []);
    const porId = {};
    for (const s of e.secciones || []) porId[s.id] = s.estado;
    for (const li of $$('[data-vivo="agenda"] [data-seccion]')) {
      const est = porId[li.dataset.seccion];
      li.classList.toggle("hecha", est === "hecha");
      li.classList.toggle("actual", est === "actual");
    }
    const n = (e.secciones || []).length, i = (e.secciones || []).findIndex((s) => s.estado === "actual");
    for (const b of $$('[data-vivo="progreso"]')) b.style.width = (n && i >= 0 ? (100 * (i + 1)) / n : 0) + "%";
    poner("progreso-texto", i >= 0 ? "Sección " + String(i + 1).padStart(2, "0") + " de " + String(n).padStart(2, "0") : "");
    pintarVivos(e.vivos || []);
    relojes();
    const firma = JSON.stringify(Object.assign({}, e, { ahoraMs: 0 }));
    if (firma === ultimaFirma) return;
    ultimaFirma = firma;
    window.dispatchEvent(new CustomEvent("evento", { detail: e }));
    for (const fn of oyentes) { try { fn(e); } catch (err) { console.error(err); } }
  }

  // --- Componentes ------------------------------------------------------------------------
  function componentes() {
    // Preguntas: botones con aria-expanded que muestran su respuesta (aria-controls).
    for (const b of $$("[data-acordeon] button[aria-controls]")) {
      b.addEventListener("click", () => {
        const abierto = b.getAttribute("aria-expanded") === "true";
        b.setAttribute("aria-expanded", String(!abierto));
        const r = document.getElementById(b.getAttribute("aria-controls"));
        if (r) r.hidden = abierto;
      });
    }
    // El menú del celular.
    for (const b of $$("[data-menu]")) {
      const menu = document.getElementById(b.getAttribute("aria-controls"));
      if (!menu) continue;
      const cerrar = () => { b.setAttribute("aria-expanded", "false"); menu.hidden = true; };
      b.addEventListener("click", () => {
        const abierto = b.getAttribute("aria-expanded") === "true";
        b.setAttribute("aria-expanded", String(!abierto));
        menu.hidden = abierto;
      });
      for (const a of menu.querySelectorAll("a")) a.addEventListener("click", cerrar);
      document.addEventListener("keydown", (ev) => { if (ev.key === "Escape") cerrar(); });
    }
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
  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", componentes);
  else componentes();
  if (enMarco) window.parent.postMessage({ tipo: "evento?" }, location.origin);
  else { traer(); setInterval(traer, 3000); }
  setInterval(relojes, 1000);

  window.BAEvento = {
    on(fn) { oyentes.add(fn); if (ultimo) { try { fn(ultimo); } catch (err) { console.error(err); } } return fn; },
    off(fn) { oyentes.delete(fn); },
    estado: () => ultimo,
    ahora: () => Date.now() + desfase,
    duracion: mmss,
    hora,
  };
})();
