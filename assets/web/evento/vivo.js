// vivo.js — la API y el armado del sitio del evento en vivo.
//
// LA PÁGINA SE ARMA SOLA CON LOS DATOS DEL EVENTO: las plantillas (Web/*.html de cada
// evento, que salen de assets/web/evento/plantilla) no traen datos escritos; todo lo
// pone este script con lo que da la sala en /api/evento: el nombre, la fecha, las
// secciones, el programa, anterior/siguiente, qué pasa ahora. Si cambia el guion, la
// página muestra lo nuevo sola, sin regenerar nada.
//
// Adentro del marco (la raíz de la sala) recibe el estado del marco; abierta sola, se lo
// pregunta a la sala cada 3 s.
//
// ── Lo que completa ──────────────────────────────────────────────────────────────────
// Textos (data-vivo="…"):
//   nombre  bajada  marca (la 1.ª palabra, para el fondo del hero)  anio
//   fecha-corta («LUN 12 OCT · 20:00»)  fecha-larga («lunes 12 de octubre, 20:00 h»)
//   mes  dia  hora-inicio  huso («Buenos Aires»)  n-secciones  minutos
//   estado (En vivo / Próximamente / En pausa / Terminó, + clase)  seccion (la que se
//   trata)  siguiente  cuenta («Empieza en 2:10:05» / «Empieza en breve»)  transcurrido
//   habla (quién tiene la palabra, si el guion lo dice)  aviso (lo que publica el operador)
//   en-la-sala / en-la-sala-texto (invitados e integrantes conectados)  mirando / mirando-texto
//   progreso-texto
//   En la página de una sección (<body data-seccion="<id>">): seccion-nombre
//   seccion-numero  seccion-total  seccion-duracion  seccion-hora  seccion-estado
//   anterior-nombre  proxima-nombre
// Listas y bloques que se rehacen solos:
//   agenda (el programa, <ol class="programa">)  marquesina  lista-secciones (enlaces)
//   cuenta-bloques (días/horas/min/seg)  reproductor (el vivo con pestañas YouTube /
//   Twitch / Facebook; data-plataforma elige cuál va primero)  vivos (botones)
//   progreso (el ancho de una barra)
// Visibilidad: data-vivo-si="fecha | huso | bajada | secciones | vivos | habla | aviso |
//   participar | votacion | proximo | en_vivo | terminado | no-proximo | franja (hay fecha o
//   ya empezó) | conectados (hay
//   alguien en la sala o más de uno mirando) | en-la-sala | mirando"  (se esconde si no).
// Enlaces: data-vivo-href="primera | anterior | proxima | publico".
// Componentes: [data-acordeon] (preguntas), [data-menu] (menú del celular).
//
// ── La API ──────────────────────────────────────────────────────────────────────────
// BAEvento.on(fn) llama a fn(estado) ya y cada vez que cambia; BAEvento.off(fn),
// BAEvento.estado(), BAEvento.ahora() (hora de la sala, ms), BAEvento.duracion(seg),
// BAEvento.hora(ms) («21:00» en el huso del evento). Y el evento «evento» de window.
"use strict";
(function () {
  const ESTADOS = { proximo: "Próximamente", en_vivo: "En vivo", pausa: "En pausa", terminado: "Terminó" };
  const NOMBRES = { youtube: "YouTube", twitch: "Twitch", facebook: "Facebook" };
  const MESES = ["ENE", "FEB", "MAR", "ABR", "MAY", "JUN", "JUL", "AGO", "SEP", "OCT", "NOV", "DIC"];
  const MESES_L = ["enero", "febrero", "marzo", "abril", "mayo", "junio", "julio", "agosto", "septiembre", "octubre", "noviembre", "diciembre"];
  const DIAS = ["DOM", "LUN", "MAR", "MIÉ", "JUE", "VIE", "SÁB"];
  const DIAS_L = ["domingo", "lunes", "martes", "miércoles", "jueves", "viernes", "sábado"];
  let ultimo = null, desfase = 0, ultimaFirma = "", elegida = "";
  const oyentes = new Set();
  const miSeccion = () => document.body && document.body.dataset.seccion;
  // Un id por pestaña, para que la sala cuente cuántos miran (sin datos: un número al azar).
  const idMirada = (() => {
    try {
      let v = sessionStorage.getItem("ba-evento-v");
      if (!v) { v = Math.random().toString(36).slice(2, 12); sessionStorage.setItem("ba-evento-v", v); }
      return v;
    } catch (e) { return Math.random().toString(36).slice(2, 12); }
  })();

  const $$ = (sel) => document.querySelectorAll(sel);
  const dos = (n) => String(n).padStart(2, "0");
  function mmss(seg) {
    seg = Math.max(0, Math.round(seg));
    const d = Math.floor(seg / 86400), h = Math.floor((seg % 86400) / 3600), m = Math.floor((seg % 3600) / 60), s = seg % 60;
    if (d) return d + (d === 1 ? " día " : " días ") + h + " h";
    return (h ? h + ":" + dos(m) : String(m)) + ":" + dos(s);
  }
  function duracionCorta(seg) {
    if (!seg) return "";
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
  /// La fecha del evento: {anio, mes (1-12), dia, diaSemana} de «YYYY-MM-DD».
  function fechaDe(f) {
    const m = /^(\d{4})-(\d{2})-(\d{2})$/.exec(f || "");
    if (!m) return null;
    const anio = +m[1], mes = +m[2], dia = +m[3];
    return { anio, mes, dia, diaSemana: new Date(Date.UTC(anio, mes - 1, dia)).getUTCDay() };
  }
  const husoCorto = (h) => (h || "").split("/").pop().replace(/_/g, " ");
  function poner(nombre, texto) { for (const el of $$('[data-vivo="' + nombre + '"]')) if (el.textContent !== texto) el.textContent = texto; }
  function si(nombre, vale) { for (const el of $$('[data-vivo-si="' + nombre + '"]')) el.hidden = !vale; }
  function href(nombre, url) { for (const el of $$('[data-vivo-href="' + nombre + '"]')) { if (url) el.href = url; el.hidden = !url; } }

  // --- Relojes -------------------------------------------------------------------
  function relojes() {
    if (!ultimo) return;
    const ahora = Date.now() + desfase;
    const proximo = ultimo.estado === "proximo";
    const falta = proximo && ultimo.inicioMs > ahora ? (ultimo.inicioMs - ahora) / 1000 : 0;
    poner("cuenta", falta ? "Empieza en " + mmss(falta) : proximo && ultimo.inicioMs ? "Empieza en breve" : "");
    poner("transcurrido", ultimo.arrancoMs && (ultimo.estado === "en_vivo" || ultimo.estado === "pausa")
      ? mmss((ahora - ultimo.arrancoMs) / 1000) : "—");
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
      partes.forEach(([v], i) => { caja.children[i].querySelector("b").textContent = dos(v); });
    }
  }

  // --- El evento y sus secciones --------------------------------------------------------
  function datosDelEvento(e) {
    const secciones = e.secciones || [];
    const f = fechaDe(e.fecha);
    const corta = [f ? DIAS[f.diaSemana] + " " + f.dia + " " + MESES[f.mes - 1] : "", e.inicio || ""].filter(Boolean).join(" · ");
    const larga = f ? DIAS_L[f.diaSemana] + " " + f.dia + " de " + MESES_L[f.mes - 1] + (e.inicio ? ", " + e.inicio + " h" : "") : (e.inicio ? e.inicio + " h" : "");
    const total = secciones.reduce((a, s) => a + (s.duracionSeg || 0), 0);
    const nombre = e.nombre || "El evento";
    poner("nombre", nombre);
    poner("bajada", e.bajada || (secciones.length ? secciones.length + (secciones.length === 1 ? " sección" : " secciones") +
      (total ? " · " + duracionCorta(total) : "") + " · en vivo por internet." : "En vivo por internet."));
    poner("marca", (nombre.split(/\s+/)[0] || "").replace(/[:.,;]$/, "") || nombre);
    poner("fecha-corta", corta || "En vivo");
    poner("fecha-larga", larga);
    poner("mes", f ? MESES[f.mes - 1] : "");
    poner("dia", f ? String(f.dia) : "");
    poner("anio", f ? String(f.anio) : String(new Date().getFullYear()));
    poner("hora-inicio", e.inicio || "—");
    poner("huso", husoCorto(e.huso));
    poner("n-secciones", dos(secciones.length));
    poner("minutos", total ? String(Math.ceil(total / 60)) : "—");
    si("fecha", !!f); si("huso", !!e.huso); si("franja", !!f || e.estado !== "proximo"); si("bajada", true); si("secciones", secciones.length > 0);
    // Marquesina: los nombres de las secciones, dos veces (la animación corre la mitad).
    for (const m of $$('[data-vivo="marquesina"]')) {
      const firma = secciones.map((s) => s.nombre).join("\u0001");
      if (m.dataset.firma === firma) continue;
      m.dataset.firma = firma;
      m.textContent = "";
      if (!secciones.length) continue;
      const vueltas = Math.max(1, Math.ceil(6 / secciones.length));
      for (let k = 0; k < 2 * vueltas; ++k)
        for (const s of secciones) { const sp = document.createElement("span"); sp.textContent = s.nombre; m.appendChild(sp); }
    }
    // Los enlaces a cada sección (el pie, un índice).
    for (const ul of $$('[data-vivo="lista-secciones"]')) {
      const firma = secciones.map((s) => s.id + s.nombre).join("|");
      if (ul.dataset.firma === firma) continue;
      ul.dataset.firma = firma;
      ul.textContent = "";
      secciones.forEach((s, i) => {
        const li = document.createElement("li");
        const a = document.createElement("a");
        a.href = s.pagina; a.textContent = dos(i + 1) + " · " + s.nombre;
        li.appendChild(a); ul.appendChild(li);
      });
    }
    href("primera", secciones.length ? secciones[0].pagina : "portada.html");
    // La página de una sección: todo lo de ESA sección sale del guion de ahora.
    const id = miSeccion();
    if (id) {
      const i = secciones.findIndex((s) => s.id === id);
      const s = secciones[i];
      for (const el of $$("[data-vivo-quitada]")) el.hidden = !!s;
      if (s) {
        poner("seccion-nombre", s.nombre);
        poner("seccion-numero", dos(i + 1));
        poner("seccion-total", dos(secciones.length));
        poner("seccion-duracion", duracionCorta(s.duracionSeg));
        poner("seccion-hora", hora(s.inicioEstimadoMs || s.inicioPrevistoMs));
        poner("seccion-estado", s.estado === "actual" ? (e.estado === "pausa" ? "En pausa" : "En vivo ahora")
          : s.estado === "hecha" ? "Ya pasó" : (s.inicioEstimadoMs || s.inicioPrevistoMs ? "Más tarde · " + hora(s.inicioEstimadoMs || s.inicioPrevistoMs) : "Más tarde"));
        for (const el of $$('[data-vivo="seccion-estado"]')) el.classList.toggle("en_vivo", s.estado === "actual");
        const ant = secciones[i - 1], prox = secciones[i + 1];
        poner("anterior-nombre", ant ? ant.nombre : "Portada");
        poner("proxima-nombre", prox ? prox.nombre : "El cierre");
        href("anterior", ant ? ant.pagina : "portada.html");
        href("proxima", prox ? prox.pagina : "cierre.html");
        document.title = s.nombre + " — " + nombre;
      }
    } else {
      document.title = nombre + (e.estado === "terminado" && /cierre/.test(location.pathname) ? " — Gracias" : "");
    }
  }

  // --- El programa ------------------------------------------------------------------
  function rehacerAgendas(secciones) {
    const firma = secciones.map((s) => [s.id, s.nombre, s.inicioPrevistoMs, s.inicioEstimadoMs, s.duracionSeg].join("\u0001")).join("\u0002");
    for (const ol of $$('[data-vivo="agenda"]')) {
      if (ol.dataset.firma === firma) continue;
      ol.dataset.firma = firma;
      ol.textContent = "";
      for (const s of secciones) {
        const li = document.createElement("li");
        li.dataset.seccion = s.id;
        const a = document.createElement("a");
        a.href = s.pagina;
        const n = document.createElement("span"); n.className = "nombre"; n.textContent = s.nombre;
        const m = document.createElement("span"); m.className = "meta";
        m.textContent = [hora(s.inicioEstimadoMs || s.inicioPrevistoMs), duracionCorta(s.duracionSeg)].filter(Boolean).join(" · ");
        a.append(n, m);
        li.appendChild(a);
        ol.appendChild(li);
      }
    }
    const porId = {};
    for (const s of secciones) porId[s.id] = s.estado;
    for (const li of $$('[data-vivo="agenda"] [data-seccion]')) {
      const est = porId[li.dataset.seccion];
      li.classList.toggle("hecha", est === "hecha");
      li.classList.toggle("actual", est === "actual");
      li.classList.toggle("esta", li.dataset.seccion === miSeccion());
    }
  }

  // --- Dónde verlo en vivo ------------------------------------------------------------
  function embedDe(v) {
    // Twitch exige el host de la página que lo embebe.
    return v.plataforma === "twitch" ? v.embed + "&parent=" + encodeURIComponent(location.hostname) : v.embed;
  }
  function pintarVivos(vivos) {
    si("vivos", vivos.length > 0);
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
    const secciones = e.secciones || [];
    for (const el of $$('[data-vivo="estado"]')) {
      el.textContent = ESTADOS[e.estado] || "";
      el.classList.remove("proximo", "en_vivo", "pausa", "terminado");
      if (e.estado) el.classList.add(e.estado);
    }
    si("proximo", e.estado === "proximo"); si("terminado", e.estado === "terminado");
    si("en_vivo", e.estado === "en_vivo" || e.estado === "pausa"); si("no-proximo", e.estado !== "proximo");
    poner("seccion", e.seccion ? e.seccion.nombre : e.estado === "terminado" ? "Terminó" : "Todavía no empezó");
    poner("siguiente", e.siguiente ? e.siguiente.nombre : e.estado === "terminado" ? "—" : e.seccion ? "El cierre" : (secciones[0] ? secciones[0].nombre : "—"));
    datosDelEvento(e);
    rehacerAgendas(secciones);
    const n = secciones.length, i = secciones.findIndex((s) => s.estado === "actual");
    for (const b of $$('[data-vivo="progreso"]')) b.style.width = (n && i >= 0 ? (100 * (i + 1)) / n : 0) + "%";
    poner("progreso-texto", i >= 0 ? "Sección " + dos(i + 1) + " de " + dos(n) : "");
    poner("habla", e.habla || ""); si("habla", !!e.habla);
    const aviso = e.aviso && e.aviso.texto ? e.aviso : null;
    poner("aviso", aviso ? aviso.texto : "");
    for (const el of $$('[data-vivo="aviso"]')) el.className = el.className.replace(/\baviso-\w+/g, "").trim() + (aviso ? " aviso-" + (aviso.nivel || "info") : "");
    si("aviso", !!aviso);
    // Cuánta gente: en la sala (invitados e integrantes conectados) y mirando la página.
    const enSala = e.enLaSala || 0, mirando = e.mirando || 0;
    poner("en-la-sala", String(enSala));
    poner("en-la-sala-texto", enSala === 1 ? "persona en la sala" : "personas en la sala");
    poner("mirando", String(mirando));
    poner("mirando-texto", mirando === 1 ? "mirando esta página" : "mirando esta página");
    si("en-la-sala", enSala > 0); si("mirando", mirando > 1); si("conectados", enSala > 0 || mirando > 1);
    si("participar", !!e.publicoUrl); href("publico", e.publicoUrl || ""); si("votacion", !!e.votacionAbierta);
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
      const r = await fetch("/api/evento?v=" + idMirada, { cache: "no-store" });
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
