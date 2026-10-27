// /comun/i18n.js — los textos de la web, en el idioma que corresponde
//
// Los mismos catálogos que la app (ba::i18n): los sirve el gateway en /comun/i18n/.
// El texto original es el español que está en la página; lo que no tiene traducción
// sale tal cual. Ver docs/planes/PLAN-I18N.md.
//
//   await BAi18n.iniciar({ contexto: "web/guest", evento: estado.idioma, preferir: "navegador" });
//   t("Entrar a la sala")                       // en el JS
//   t("%n invitado conectado", { n: 3 })         // plural
//   t("Salida", { desambiguacion: "audio" })
//   <button data-i18n="Entrar a la sala">Entrar a la sala</button>   // en el HTML:
//   <input data-i18n-placeholder="Tu nombre">                          // BAi18n.aplicar()
//
// Qué idioma: ?idioma= en la URL, si no el que eligió esta persona en la página
// (BAi18n.elegir, se recuerda en este navegador), si no según `preferir`:
//   "navegador" (invitados, público remoto): el del navegador si lo hay, si no el del evento;
//   "evento" (pantallas, presidencia): el del evento.
// tools/i18n/extraer.py encuentra los t("…") y data-i18n="…" para el .ts.
(function () {
  "use strict";
  const CLAVE_ELEGIDO = "ba.idioma";
  const estado = { contexto: "web/comun", evento: "", preferir: "evento", idioma: "es", perfil: null, cadena: [],
                  idiomas: [] };

  function guardado() {
    try { return localStorage.getItem(CLAVE_ELEGIDO) || ""; } catch (e) { return ""; }
  }

  function lengua(c) { return String(c || "").split("-")[0].toLowerCase(); }

  function buscar(codigo, conPseudo) {
    if (!codigo) return null;
    const c = String(codigo).toLowerCase();
    const ok = (p) => conPseudo || !p.pseudo;
    return estado.idiomas.find((p) => ok(p) && p.codigo.toLowerCase() === c) ||
           estado.idiomas.find((p) => ok(p) && p.codigo.toLowerCase() === lengua(c)) ||
           estado.idiomas.find((p) => ok(p) && lengua(p.codigo) === lengua(c)) || null;
  }

  function delNavegador() {
    for (const l of navigator.languages || [navigator.language]) {
      const p = buscar(l, false);
      if (p) return p.codigo;
    }
    return "";
  }

  // La misma numeración que ba::i18n::formaPlural (y que las <numerusform> del .ts).
  function formaPlural(regla, n) {
    const a = Math.abs(n), d = a % 10, c = a % 100;
    switch (regla) {
      case "sin-plural": return 0;
      case "frances": return a <= 1 ? 0 : 1;
      case "eslavo": return d === 1 && c !== 11 ? 0 : d >= 2 && d <= 4 && (c < 12 || c > 14) ? 1 : 2;
      case "polaco": return a === 1 ? 0 : d >= 2 && d <= 4 && (c < 12 || c > 14) ? 1 : 2;
      case "checo": return a === 1 ? 0 : a >= 2 && a <= 4 ? 1 : 2;
      case "arabe": return a === 0 ? 0 : a === 1 ? 1 : a === 2 ? 2 : c >= 3 && c <= 10 ? 3 : c >= 11 ? 4 : 5;
      default: return a === 1 ? 0 : 1;
    }
  }

  const LETRAS = { a: "ä", e: "ë", i: "ï", o: "ö", u: "ü", y: "ÿ", c: "ç", n: "ñ",
                   A: "Ä", E: "Ë", I: "Ï", O: "Ö", U: "Ü", Y: "Ÿ", C: "Ç", N: "Ñ" };
  // El de ba::i18n::pseudo: acentos, 40 % más largo, entre ⟦ ⟧; respeta %1, %n, <etiquetas> y &entidades;.
  function pseudo(s) {
    if (!s) return s;
    let out = "⟦", letras = 0;
    const partes = s.split(/(%L?(?:n|[1-9])|<[^>]*>|&[#\w]{1,8};)/);
    partes.forEach((p, i) => {
      if (i % 2 === 1) { out += p; return; }
      for (const ch of p) {
        if (/\p{L}/u.test(ch)) letras++;
        out += LETRAS[ch] || ch;
      }
    });
    return out + "·".repeat(Math.max(1, Math.floor((letras * 2 + 4) / 5))) + "⟧";
  }

  async function catalogo(codigo) {
    try {
      const r = await fetch("/comun/i18n/" + encodeURIComponent(codigo) + ".json", { cache: "no-cache" });
      return r.ok ? await r.json() : { mensajes: {} };
    } catch (e) {
      return { mensajes: {} };
    }
  }

  async function iniciar(op) {
    op = op || {};
    estado.contexto = op.contexto || estado.contexto;
    if (op.evento !== undefined) estado.evento = op.evento;
    if (op.preferir) estado.preferir = op.preferir;
    try {
      const r = await fetch("/comun/i18n/idiomas.json", { cache: "no-cache" });
      estado.idiomas = r.ok ? await r.json() : [];
    } catch (e) {
      estado.idiomas = [];
    }
    const url = new URLSearchParams(location.search).get("idioma");
    const evento = buscar(estado.evento, true);
    const navegador = buscar(delNavegador(), false);
    const pedido = buscar(url, true) || buscar(guardado(), true) ||
                   (estado.preferir === "navegador" ? navegador || evento : evento || navegador) ||
                   estado.idiomas.find((p) => p.fuente) || { codigo: "es", plural: "uno-otro", fuente: true };
    estado.idioma = pedido.codigo;
    estado.perfil = pedido;
    // El idioma y sus respaldos (hasta el original), como en el núcleo.
    estado.cadena = [];
    const pendientes = [pedido.codigo];
    while (pendientes.length && estado.cadena.length < 8) {
      const c = pendientes.shift();
      if (estado.cadena.some((x) => x.perfil.codigo === c)) continue;
      const perfil = buscar(c, true) || { codigo: c, plural: "uno-otro" };
      estado.cadena.push({ perfil, mensajes: perfil.pseudo ? {} : (await catalogo(perfil.codigo)).mensajes || {} });
      if (perfil.fuente) break;
      (perfil.respaldo || []).forEach((r) => pendientes.push(r));
    }
    document.documentElement.lang = pedido.codigo;
    document.documentElement.dir = pedido.rtl ? "rtl" : "ltr";
    aplicar(document);
    return pedido.codigo;
  }

  function t(texto, op) {
    op = op || {};
    const n = typeof op.n === "number" ? op.n : -1;
    const clave = op.desambiguacion ? texto + "\u0004" + op.desambiguacion : texto;
    const contextos = [op.contexto || estado.contexto, "web/comun"];
    let out = texto;
    buscando:
    for (const cat of estado.cadena) {
      if (cat.perfil.pseudo) { out = pseudo(texto); break; }
      for (const ctx of contextos) {
        const v = cat.mensajes[ctx] && cat.mensajes[ctx][clave];
        if (typeof v === "string") { out = v; break buscando; }
        if (Array.isArray(v) && v.length) {
          out = v[Math.min(v.length - 1, formaPlural(cat.perfil.plural, n < 0 ? 1 : n))];
          break buscando;
        }
      }
    }
    if (n >= 0) out = out.replace(/%L?n/g, String(n));
    return out;
  }

  // Llena %1, %2… en el orden en que los ponga la traducción.
  function llenar(plantilla, ...valores) {
    return plantilla.replace(/%([1-9])/g, (m, k) => (k - 1 < valores.length ? String(valores[k - 1]) : m));
  }

  const ATRIBUTOS = { "data-i18n-placeholder": "placeholder", "data-i18n-title": "title",
                      "data-i18n-aria-label": "aria-label" };
  function aplicar(raiz) {
    raiz = raiz || document;
    raiz.querySelectorAll("[data-i18n]").forEach((el) => { el.textContent = t(el.getAttribute("data-i18n")); });
    for (const [marca, attr] of Object.entries(ATRIBUTOS))
      raiz.querySelectorAll("[" + marca + "]").forEach((el) => el.setAttribute(attr, t(el.getAttribute(marca))));
  }

  // Lo que eligió esta persona en la página (un selector de idioma): se recuerda en este
  // navegador; vacío vuelve a la elección automática.
  async function elegir(codigo) {
    try {
      if (codigo) localStorage.setItem(CLAVE_ELEGIDO, codigo);
      else localStorage.removeItem(CLAVE_ELEGIDO);
    } catch (e) { /* sin almacenamiento: dura lo que la página */ }
    return iniciar({});
  }

  window.BAi18n = {
    iniciar, t, llenar, aplicar, elegir, pseudo, formaPlural,
    get idioma() { return estado.idioma; },
    get perfil() { return estado.perfil; },
    // Los que se pueden elegir en una página (sin el pseudo-idioma).
    get idiomas() { return estado.idiomas.filter((p) => !p.pseudo); },
  };
  window.t = t;
})();
