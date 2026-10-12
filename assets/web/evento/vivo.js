// vivo.js — la API y el armado del sitio del evento en vivo.
//
// LA PÁGINA SE ARMA SOLA CON LOS DATOS DEL EVENTO: las plantillas (Web/*.html de cada
// evento, que salen de assets/web/evento/plantilla) no traen datos escritos; todo lo
// pone este script con lo que da la sala en /api/v1/evento: el nombre, la fecha, las
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
//   preguntas (las del público ya moderadas, una lista) · n-preguntas
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
//   alguien en la sala o más de uno mirando) | en-la-sala | mirando | preguntas (hay
//   preguntas para mostrar) | preguntas-o-participar"  (se esconde si no).
//   Lo que sabe el sistema: oradores-habla  oradores-detalle (bloque/cargo)  oradores-restante
//   (cuenta atrás)  oradores-fila (lista)  orden (el orden del día, lista)  quorum-presentes
//   quorum-necesarios  quorum-estado  asistencia-presentes  asistencia-total  votacion-pregunta
//   votacion-estado  votacion-votaron  votacion-resultado (barras)
//   Más visibilidad: invitados  transmitiendo  grabando  oradores  fila  orden  quorum
//   asistencia  votacion-info  resultado
//   La asamblea (si el evento lo es): asamblea-cuerpo  asamblea-presentes  asamblea-total
//   asamblea-quorum  integrantes (grilla: presente / remoto / habla)  punto-titulo
//   punto-expediente  votaciones (los resultados oficiales)  recinto (el plano de bancas:
//   grilla o hemiciclo, color por bloque, vacía = ausente, aro = habla)  bloques (la
//   leyenda: cada bloque con sus presentes). Visibilidad: asamblea  punto  votaciones
//   asamblea-quorum  recinto  bloques
//   Numeración: un [data-num] adentro de cada sección visible se numera solo («01», «02»…).
//   Letras: un [data-letra] en cada tarjeta visible de unas .columnas se pone solo (A, B, C…).
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
    const o = ultimo.oradores;
    if (o && o.habla && o.hastaMs && o.asignadoSeg) {
      const rest = (o.hastaMs - ahora) / 1000;
      poner("oradores-restante", rest > 0 ? "Le quedan " + mmss(rest) : "Se le terminó el tiempo");
    }
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
    // Cada uno lleva a su sección (con el mouse; con el teclado está el programa).
    for (const m of $$('[data-vivo="marquesina"]')) {
      const firma = secciones.map((s) => s.nombre + s.pagina).join("\u0001");
      if (m.dataset.firma === firma) continue;
      m.dataset.firma = firma;
      m.textContent = "";
      if (!secciones.length) continue;
      const vueltas = Math.max(1, Math.ceil(6 / secciones.length));
      for (let k = 0; k < 2 * vueltas; ++k)
        for (const s of secciones) {
          const a = document.createElement("a");
          a.href = s.pagina; a.textContent = s.nombre; a.tabIndex = -1;
          m.appendChild(a);
        }
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
    const firma = secciones.map((s) => [s.id, s.nombre, s.inicioPrevistoMs, s.inicioEstimadoMs, s.inicioRealMs, s.duracionSeg].join("\u0001")).join("\u0002");
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
        // Ya empezó: a qué hora y en qué minuto de la grabación (capítulo). Si no, cuándo se espera.
        const cap = s.inicioRealMs && ultimo && ultimo.arrancoMs ? "min " + mmss((s.inicioRealMs - ultimo.arrancoMs) / 1000) : "";
        m.textContent = (s.inicioRealMs ? ["Empezó " + hora(s.inicioRealMs), cap]
                                        : [hora(s.inicioEstimadoMs || s.inicioPrevistoMs), duracionCorta(s.duracionSeg)])
          .filter(Boolean).join(" · ");
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

  // --- Lo que sabe el resto del sistema ------------------------------------------------------
  function lista(sel, items, armar) {
    for (const el of $$(sel)) {
      const firma = JSON.stringify(items);
      if (el.dataset.firma === firma) continue;
      el.dataset.firma = firma;
      el.textContent = "";
      for (const it of items) el.appendChild(armar(it));
    }
  }
  const ESTADO_PUNTO = { pendiente: "Pendiente", en_tratamiento: "En tratamiento", votado: "Votado", comision: "A comisión", retirado: "Retirado" };
  function datosDelSistema(e) {
    si("invitados", !!e.invitados);
    const aire = e.aire || {};
    si("transmitiendo", !!aire.transmitiendo); si("grabando", !!aire.grabando);
    // Oradores: quién habla (y su tiempo) y la fila.
    const o = e.oradores || null;
    si("oradores", !!(o && (o.habla || (o.fila || []).length)));
    si("orador-habla", !!(o && o.habla));
    poner("oradores-habla", o && o.habla ? o.habla.nombre : "");
    poner("oradores-detalle", o && o.habla ? [o.habla.cargo, o.habla.bloque].filter(Boolean).join(" · ") : "");
    if (!(o && o.habla && o.asignadoSeg)) poner("oradores-restante", "");
    const fila = (o && o.fila) || [];
    si("fila", fila.length > 0);
    lista('[data-vivo="oradores-fila"]', fila, (p) => {
      const li = document.createElement("li");
      li.textContent = p.nombre;
      if (p.bloque) { const b = document.createElement("span"); b.className = "tenue"; b.textContent = " · " + p.bloque; li.appendChild(b); }
      return li;
    });
    // El orden del día.
    const puntos = e.ordenDelDia || [];
    si("orden", puntos.length > 0);
    lista('[data-vivo="orden"]', puntos, (p) => {
      const li = document.createElement("li");
      li.className = "punto " + (p.estado || "");
      const n = document.createElement("span"); n.className = "mono tenue"; n.textContent = dos(p.n);
      const t = document.createElement("div");
      const b = document.createElement("b"); b.textContent = p.titulo;
      const m = document.createElement("span"); m.className = "ceja";
      m.textContent = [p.expediente ? "Expte. " + p.expediente : "", ESTADO_PUNTO[p.estado] || p.estado, p.resultado].filter(Boolean).join(" · ");
      t.append(b, m);
      li.append(n, t);
      return li;
    });
    // Quórum y asistencia.
    const q = e.quorum || null;
    si("quorum", !!q);
    poner("quorum-presentes", q ? String(q.presentes) : ""); poner("quorum-necesarios", q ? String(q.necesarios) : "");
    poner("quorum-estado", q ? (q.hay ? "Hay quórum" : "Sin quórum") : "");
    const as = e.asistencia || null;
    si("asistencia", !!as);
    poner("asistencia-presentes", as ? String(as.presentes) : ""); poner("asistencia-total", as ? String(as.total) : "");
    // La votación (el resultado, solo cuando se puede mostrar).
    const v = e.votacion || null;
    si("votacion-info", !!(v && v.pregunta));
    poner("votacion-pregunta", v ? v.pregunta : "");
    poner("votacion-estado", v ? (v.abierta ? "Votación abierta" : "Votación cerrada") : "");
    for (const el of $$('[data-vivo="votacion-estado"]')) el.classList.toggle("en_vivo", !!(v && v.abierta));
    poner("votacion-votaron", v ? (v.podian ? v.votaron + " de " + v.podian + " votaron" : v.votaron + " votaron") : "");
    const res = (v && v.resultado) || [];
    si("resultado", res.length > 0);
    const total = res.reduce((a, r) => a + r.votos, 0) || 1;
    lista('[data-vivo="votacion-resultado"]', res, (r) => {
      const d = document.createElement("div"); d.className = "barra-voto";
      const top = document.createElement("div"); top.className = "barra-voto-top";
      const a = document.createElement("span"); a.textContent = r.opcion;
      const b = document.createElement("span"); b.className = "mono"; b.textContent = r.votos + " · " + Math.round((100 * r.votos) / total) + "%";
      top.append(a, b);
      const pista = document.createElement("div"); pista.className = "progreso";
      const llena = document.createElement("div"); llena.style.width = (100 * r.votos) / total + "%";
      pista.appendChild(llena);
      d.append(top, pista);
      return d;
    });
  }
  // El plano de bancas. Mismo dibujo que la app: en la grilla la presidencia arriba y la
  // fila 0 debajo; en el hemiciclo la presidencia abajo al centro y cada fila un arco (la
  // 0 la de adentro). Las columnas de izquierda a derecha vistas desde la cabina.
  const PALETA = ["#4ea8ff", "#ffb020", "#3ddc97", "#c77dff", "#ff6b9a", "#2ec4d6", "#a3d95b", "#ff8a3d", "#8f9bff", "#e0c36a"];
  function colorDeBloque(a) {
    const orden = (a.bloques || []).map((b) => b.bloque);
    return (b) => { const i = orden.indexOf(b || ""); return i < 0 ? "#8a8a8f" : PALETA[i % PALETA.length]; };
  }
  const NS = "http://www.w3.org/2000/svg";
  function svg(tag, at) { const el = document.createElementNS(NS, tag); for (const k in at) el.setAttribute(k, at[k]); return el; }
  function plano(a) {
    const r = a.recinto || null;
    const dibujable = !!(r && (r.distribucion === "grilla" || r.distribucion === "hemiciclo") && r.filas > 0 && r.columnas > 0 && (r.bancas || []).length);
    si("recinto", dibujable);
    if (!dibujable) return;
    const color = colorDeBloque(a);
    for (const caja of $$('[data-vivo="recinto"]')) {
      const firma = JSON.stringify([r, a.bloques]);
      if (caja.dataset.firma === firma) continue;
      caja.dataset.firma = firma;
      const F = r.filas, C = r.columnas, R = 0.42;
      const lugar = [];
      let vb, pres;
      if (r.distribucion === "hemiciclo") {
        const r0 = Math.max(1.4 * C / Math.PI, 1.4), paso = 1.32, r1 = r0 + paso * (F - 1), m = 0.6;
        for (const b of r.bancas) {
          const radio = r0 + paso * b.fila, ang = Math.PI * (1 - (b.columna + 0.5) / C);
          lugar.push([b, radio * Math.cos(ang), -radio * Math.sin(ang)]);
        }
        vb = [-(r1 + m), -(r1 + m), 2 * (r1 + m), r1 + m + 1.3];
        pres = { x: -Math.min(r0 * 0.6, 3), y: 0.35, w: Math.min(r0 * 1.2, 6) };
      } else {
        const paso = 1.2, arriba = 1.5;
        for (const b of r.bancas) lugar.push([b, b.columna * paso, arriba + b.fila * paso]);
        vb = [-0.6, -0.1, (C - 1) * paso + 1.2, arriba + (F - 1) * paso + 0.7];
        pres = { x: -0.5, y: 0, w: (C - 1) * paso + 1 };
      }
      const s = svg("svg", { viewBox: vb.map((n) => n.toFixed(2)).join(" "), role: "img",
                             "aria-label": "Plano de bancas: " + (a.presentes || 0) + " de " + (a.total || 0) + " presentes" });
      s.append(svg("rect", { x: pres.x, y: pres.y, width: pres.w, height: 0.8, rx: 0.15, class: "presidencia" }));
      const t = svg("text", { x: pres.x + pres.w / 2, y: pres.y + 0.52, "text-anchor": "middle", class: "presidencia-texto" });
      t.textContent = "PRESIDENCIA";
      s.append(t);
      for (const [b, x, y] of lugar) {
        const g = svg("g", { class: "banca" + (b.presente ? " presente" : "") + (b.habla ? " habla" : "") + (b.nombre ? "" : " vacia") });
        const c = color(b.bloque);
        if (b.habla) g.append(svg("circle", { cx: x, cy: y, r: R + 0.12, class: "aro" }));
        g.append(svg("circle", { cx: x, cy: y, r: R, fill: b.presente ? c : "transparent", stroke: c }));
        const n = svg("text", { x: x, y: y + 0.13, "text-anchor": "middle", class: "numero" + (b.presente ? " sobre" : "") });
        n.textContent = b.banca;
        const tt = svg("title", {});
        tt.textContent = (b.nombre || "Banca sin ocupar") + (b.bloque ? " · " + b.bloque : "") + " · Banca " + b.banca
          + (b.nombre ? (b.habla ? " · Habla" : b.presente ? (b.lugar === "remoto" ? " · Presente (remoto)" : " · Presente") : " · Ausente") : "");
        g.append(n, tt);
        s.append(g);
      }
      caja.textContent = "";
      caja.append(s);
    }
    const bl = a.bloques || [];
    si("bloques", bl.length > 0);
    lista('[data-vivo="bloques"]', bl, (b) => {
      const li = document.createElement("li");
      const m = document.createElement("i"); m.style.background = color(b.bloque);
      const n = document.createElement("b"); n.textContent = b.bloque;
      const q = document.createElement("span"); q.className = "mono tenue"; q.textContent = b.presentes + "/" + b.total;
      li.append(m, n, q);
      return li;
    });
  }
  function asamblea(e) {
    const a = e.asamblea || null;
    si("asamblea", !!a);
    if (!a) return;
    poner("asamblea-cuerpo", a.cuerpo || "La asamblea");
    poner("asamblea-presentes", String(a.presentes || 0));
    poner("asamblea-total", String(a.total || 0));
    const q = a.quorum || null;
    si("asamblea-quorum", !!q);
    poner("asamblea-quorum", q ? (q.hay ? "Hay quórum" : "Sin quórum") + " · se necesitan " + q.necesarios : "");
    for (const el of $$('[data-vivo="asamblea-quorum"]')) el.classList.toggle("en_vivo", !!(q && q.hay));
    plano(a);
    const colorI = colorDeBloque(a);
    lista('[data-vivo="integrantes"]', a.integrantes || [], (p) => {
      const li = document.createElement("li");
      li.className = "integrante" + (p.presente ? " presente" : "") + (p.habla ? " habla" : "");
      if (p.bloque) li.style.borderLeft = "3px solid " + colorI(p.bloque);
      const n = document.createElement("b"); n.textContent = p.nombre;
      const d = document.createElement("span"); d.className = "tenue";
      d.textContent = [p.banca ? "Banca " + p.banca : "", p.bloque].filter(Boolean).join(" · ");
      const est = document.createElement("span"); est.className = "ceja";
      est.textContent = p.habla ? "Habla" : p.presente ? (p.lugar === "remoto" ? "Presente · remoto" : "Presente") : "Ausente";
      li.append(n, d, est);
      return li;
    });
    const pt = a.punto || null;
    si("punto", !!pt);
    poner("punto-titulo", pt ? (pt.n ? "Punto " + pt.n + " · " : "") + pt.titulo : "");
    poner("punto-expediente", pt && pt.expediente ? "Expte. " + pt.expediente : "");
    const vs = a.votaciones || [];
    si("votaciones", vs.length > 0);
    lista('[data-vivo="votaciones"]', vs, (v) => {
      const li = document.createElement("li");
      li.className = "votacion-oficial " + (v.aprobado ? "aprobado" : "rechazado");
      const t = document.createElement("b"); t.textContent = v.asunto;
      const r = document.createElement("span"); r.className = "mono";
      r.textContent = v.afirmativos + " a favor · " + v.negativos + " en contra · " + v.abstenciones + " abstenciones";
      const est = document.createElement("span"); est.className = "estado" + (v.aprobado ? " en_vivo" : "");
      est.textContent = v.aprobado ? "Aprobado" : "Rechazado";
      li.append(t, r, est);
      return li;
    });
  }
  // Numera las secciones que se ven («01 /», «02 /»…): una que no aplica no deja hueco.
  function numerar() {
    let n = 0;
    for (const el of $$("[data-num]")) {
      const sec = el.closest("section");
      const visible = !sec || (!sec.hidden && !sec.closest("[hidden]"));
      if (visible) el.textContent = dos(++n);
    }
    // Las letras de las tarjetas ([data-letra]): A, B, C… solo entre las que se ven.
    for (const grupo of $$(".columnas")) {
      let i = 0;
      for (const el of grupo.querySelectorAll("[data-letra]")) {
        const t = el.closest(".tarjeta");
        if (!t || !t.hidden) el.textContent = String.fromCharCode(65 + i++);
      }
    }
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
    // Las preguntas del público (solo las moderadas: la que se responde, las elegidas y las
    // respondidas). Sin preguntas, el bloque no aparece.
    const preguntas = e.preguntas || [];
    si("preguntas", preguntas.length > 0);
    si("preguntas-o-participar", preguntas.length > 0 || !!e.publicoUrl);
    poner("n-preguntas", String(preguntas.length));
    for (const ul of $$('[data-vivo="preguntas"]')) {
      const firma = JSON.stringify(preguntas.map((p) => [p.texto, p.nombre, p.estado, p.votos]));
      if (ul.dataset.firma === firma) continue;
      ul.dataset.firma = firma;
      ul.textContent = "";
      for (const p of preguntas) {
        const li = document.createElement("li");
        li.className = "pregunta " + p.estado;
        const et = document.createElement("span");
        et.className = "ceja";
        et.textContent = p.estado === "al_aire" ? "Respondiendo ahora" : p.estado === "respondida" ? "Respondida" : "En la cola";
        const t = document.createElement("p");
        t.textContent = p.texto;
        li.append(et, t);
        if (p.nombre) { const n = document.createElement("span"); n.className = "tenue"; n.textContent = "— " + p.nombre; li.appendChild(n); }
        ul.appendChild(li);
      }
    }
    si("participar", !!e.publicoUrl);
    datosDelSistema(e);
    asamblea(e); href("publico", e.publicoUrl || ""); si("votacion", !!e.votacionAbierta);
    pintarVivos(e.vivos || []);
    relojes();
    numerar();
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
      const r = await fetch("/api/v1/evento?v=" + idMirada, { cache: "no-store" });
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
