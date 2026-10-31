/* ============================================================================
   /comun/tema.js — el tema del evento en cualquier página (docs/planes/PLAN-TEMA-EVENTO.md)

   El gateway publica en /comun/tema.json qué tema va: el de las consolas (`tema`, lo
   aplica neuro.js) y el del EVENTO abierto (`evento`: versión, nombre, logo, colores y
   alcance; null sin evento o sin tema). Los colores y la letra los pone la hoja
   /comun/tema.css, que arma el gateway con todo calculado (contraste incluido): acá no
   se calcula nada, solo se enlaza la versión que toca y se pone lo que no es CSS.

   Una página se anota así:
     <html data-ev="publico">   la gente de afuera (tema completo, por omisión)
     <html data-ev="control">   una consola de operador (por omisión, el acento y el logo)
     <html data-ev="acento">    solo el acento, siempre (el prompter: el texto no se toca)
     <link rel="stylesheet" href="/comun/tema.css" />   después de la hoja de la página
     <script src="/comun/tema.js" defer></script>
   y donde quiera el logo:  <img data-tema-logo alt="" hidden />  (se muestra si hay logo)
   y el nombre del evento:  <span data-tema-nombre>Lo de siempre</span>

   Lo que hace: cambia el <link> a /comun/tema.css?v=<versión> (así se guarda en la caché
   y un cambio se ve sin recargar), pone el logo, el favicon y el theme-color, guarda una
   copia en el aparato (para no pintar la página sin tema un instante al abrir) y vuelve a
   mirar cada 15 s y al volver a la pestaña. Avisa con el evento `ba-tema-datos` en
   document (detail: lo de tema.json), que es de donde neuro.js toma el de las consolas:
   un solo pedido para los dos.

   BA.temaEvento: datos() (lo último de tema.json), nivel() (lo que le toca a esta página:
   "nada" | "acento" | "todo").
   ============================================================================ */
(() => {
  "use strict";
  const BA = (window.BA = window.BA || {});
  if (BA.temaEvento) return; // ya cargado (una página que lo incluye dos veces)
  const raiz = document.documentElement;
  const CLAVE = "ba-tema-evento";
  const NIVELES = { nada: 0, acento: 1, todo: 2 };
  let datos = null;

  // A qué familia pertenece la página: lo primero que diga data-ev.
  const familia = () => {
    const f = (raiz.getAttribute("data-ev") || "").split(/\s+/);
    return f.includes("publico") ? "publico" : f.includes("control") ? "control" : f.includes("acento") ? "acento" : "";
  };
  const nivel = (ev) => {
    if (!ev) return "nada";
    const f = familia();
    if (f === "acento") return "acento";
    if (!f) return "nada";
    const n = ev.alcance && ev.alcance[f];
    return NIVELES[n] !== undefined ? n : f === "publico" ? "todo" : "acento";
  };

  // El <link> de la hoja: el que puso la página o uno nuevo al final del <head>.
  const enlace = () => {
    let l = document.querySelector('link[rel="stylesheet"][href^="/comun/tema.css"]');
    if (!l && document.head) {
      l = document.createElement("link");
      l.rel = "stylesheet";
      document.head.appendChild(l);
    }
    return l;
  };

  // Lo que la página tenía antes del tema (para devolverlo si el tema se va).
  const original = new Map();
  const recordar = (el, attr) => {
    if (!original.has(el)) original.set(el, {});
    const o = original.get(el);
    if (!(attr in o)) o[attr] = el.getAttribute(attr);
  };
  const devolver = (el, attr) => {
    const o = original.get(el);
    if (!o || !(attr in o)) return;
    if (o[attr] === null) el.removeAttribute(attr);
    else el.setAttribute(attr, o[attr]);
  };

  const aplicar = (ev) => {
    const n = nivel(ev);
    const con = NIVELES[n] > 0;
    const v = ev && ev.v ? String(ev.v) : "";
    raiz.setAttribute("data-tema-evento", con ? n : "nada");
    const l = enlace();
    const href = v ? "/comun/tema.css?v=" + encodeURIComponent(v) : "/comun/tema.css";
    if (l && l.getAttribute("href") !== href) l.setAttribute("href", href);
    if (!document.body) return; // el resto, cuando esté la página (ver DOMContentLoaded)

    // El logo: el claro sobre fondo oscuro si lo hay, si no el de siempre.
    const logo = con && ev ? (ev.modo !== "claro" && ev.logoClaro) || ev.logo || "" : "";
    document.querySelectorAll("img[data-tema-logo]").forEach((img) => {
      if (logo) {
        if (img.getAttribute("src") !== logo) img.setAttribute("src", logo);
        img.hidden = false;
      } else {
        img.removeAttribute("src");
        img.hidden = true;
      }
      if (!img.getAttribute("alt") && ev && ev.nombre) img.setAttribute("alt", ev.nombre);
    });
    // El nombre del evento donde la página lo pide.
    document.querySelectorAll("[data-tema-nombre]").forEach((el) => {
      if (!("temaNombreOriginal" in el.dataset)) el.dataset.temaNombreOriginal = el.textContent;
      el.textContent = con && ev && ev.nombre ? ev.nombre : el.dataset.temaNombreOriginal;
    });
    // El favicon: el del evento (o su logo).
    const fav = con && ev ? ev.favicon || ev.logo || "" : "";
    let icono = document.querySelector('link[rel="icon"]');
    if (!icono && fav) {
      icono = document.createElement("link");
      icono.rel = "icon";
      document.head.appendChild(icono);
    }
    if (icono) {
      recordar(icono, "href");
      recordar(icono, "type");
      if (fav) {
        icono.setAttribute("href", fav);
        icono.removeAttribute("type");
      } else {
        devolver(icono, "href");
        devolver(icono, "type");
      }
    }
    // La barra del navegador del celular, del color del fondo (solo con el tema completo).
    const meta = document.querySelector('meta[name="theme-color"]');
    if (meta) {
      recordar(meta, "content");
      if (n === "todo" && ev && ev.fondo) meta.setAttribute("content", ev.fondo);
      else devolver(meta, "content");
    }
  };

  const recibir = (d, guardar) => {
    if (!d || typeof d !== "object") return;
    datos = d;
    aplicar(d.evento || null);
    if (guardar) {
      try { localStorage.setItem(CLAVE, JSON.stringify(d)); } catch (_) {}
    }
    BA.temaDatos = d;
    document.dispatchEvent(new CustomEvent("ba-tema-datos", { detail: d }));
  };

  BA.temaEvento = {
    datos() { return datos; },
    nivel() { return nivel(datos && datos.evento); },
  };

  // La copia del aparato, ya: la hoja de esa versión suele estar en la caché.
  try {
    const c = JSON.parse(localStorage.getItem(CLAVE) || "null");
    if (c) recibir(c, false);
  } catch (_) {}

  const leer = () => fetch("/comun/tema.json", { cache: "no-store" })
    .then((r) => (r.ok ? r.json() : null))
    .then((d) => recibir(d, true))
    .catch(() => {});
  leer();
  setInterval(leer, 15000);
  document.addEventListener("visibilitychange", () => { if (!document.hidden) leer(); });
  if (document.readyState === "loading")
    document.addEventListener("DOMContentLoaded", () => aplicar(datos && datos.evento));
})();
