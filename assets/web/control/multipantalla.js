/* La multipantalla del tablero: arriba de todo, como la de un switcher. Dos pantallas lado a
   lado (una arriba de la otra en el celular): PROGRAMA (lo que sale al aire en la salida que
   maneja el aparato) y PREVIEW (la escena armada, la que sale con el TAKE), con sus nombres;
   el TAKE grande, el CORTE, la transición de ese take y la T-bar. Con varias salidas, cuál
   se maneja (lo mismo que el selector de la barra).

   Las imágenes son cuadros JPEG chicos que empuja la app (tema «monitores» →
   monitores_ver: ~4 por segundo, solo cuando cambian), de lo que la interfaz ya tiene: no se
   renderiza nada aparte. Contraída, o con la pestaña escondida, deja de mirarlas (la app deja
   de consultarlas). Todo lo que hace pasa por las herramientas de la app (scene_cut,
   preview_al_aire, transicion_manual), con su confirmación si hay aire.

     BA.multipantalla.crear(raiz, api, {contraida, alContraer(bool)})
     BA.multipantalla.actualizar(tema, datos)   // «escenas» y «monitores»
     BA.multipantalla.salidas()                 // cambió la salida o la lista
   Sin estilos en línea (la CSP no los deja): clases de control.css. */
(() => {
  "use strict";
  const BA = (window.BA = window.BA || {});
  const { boton, texto } = BA.tj;
  let x = null; // lo armado

  function monitor(clase, rotulo) {
    const f = el("figure", "monitor " + clase);
    const pantalla = el("div", "pantalla");
    const img = el("img");
    img.alt = "";
    img.decoding = "async";
    const nada = el("span", "sin-imagen");
    pantalla.append(img, nada);
    const pie = el("figcaption");
    const r = el("span", "rotulo-monitor", rotulo);
    const nombre = el("strong", "nombre-escena", "—");
    pie.append(r, nombre);
    f.append(pantalla, pie);
    return { f, img, nada, nombre, src: "", escena: "" };
  }

  function pintarMonitor(m, escena, imagen, vacio) {
    if (escena !== m.escena) { m.escena = escena; texto(m.nombre, escena || "—"); }
    const src = escena ? imagen || "" : "";
    if (src !== m.src) {
      m.src = src;
      if (src) m.img.src = src; else m.img.removeAttribute("src");
    }
    m.f.classList.toggle("con-imagen", !!src);
    texto(m.nada, escena ? (src ? "" : t("Esperando imagen…")) : vacio);
  }

  const mirando = () => x && !x.contraida && document.visibilityState !== "hidden" && x.api.puede("monitores_ver");
  function mirarMonitores() {
    if (!x) return;
    if (mirando()) x.api.mirar(["monitores"]); else x.api.dejar(["monitores"]);
  }

  function crear(raiz, api, op = {}) {
    x = { raiz, api, contraida: !!op.contraida, alContraer: op.alContraer || (() => {}) };
    raiz.replaceChildren();
    raiz.classList.add("multipantalla");

    const cab = el("div", "cabeza-multi");
    x.titulo = el("h2", "", t("Multipantalla"));
    x.salidas = el("div", "salidas-multi");
    x.contraer = boton("", () => contraer(!x.contraida), { compacto: true });
    cab.append(x.titulo, x.salidas, el("span", "espacio"), x.contraer);

    x.prog = monitor("programa", t("PROGRAMA"));
    x.prev = monitor("preview", t("PREVIEW"));
    x.monitores = el("div", "monitores");
    x.monitores.append(x.prog.f, x.prev.f);

    const acciones = el("div", "acciones-multi");
    x.take = boton(t("TAKE"), () => {
      const tr = x.transicion.value;
      api.hacer("preview_al_aire", tr ? { transicion: tr } : {}).catch(() => {});
    }, { grande: true, variante: "aire", clase: "boton-take" });
    x.corte = boton(t("CORTE"), () => api.hacer("scene_cut", {}).catch(() => {}), { grande: true, clase: "boton-corte" });
    x.transicion = el("select", "transicion-multi");
    x.transicion.title = t("La transición de este TAKE");
    for (const [v, n] of [["", t("Transición configurada")], ["cut", t("Corte")], ["fade", t("Fundido")],
                          ["swipe", t("Barrido")], ["slide", t("Deslizar")], ["fade_to_color", t("Fundido a color")]])
      x.transicion.append(new Option(n, v));
    x.tbar = el("ba-slider", "tbar-multi");
    for (const [k, v] of [["etiqueta", "T-bar"], ["min", "0"], ["max", "1"], ["paso", "0.01"], ["valor", "0"]]) x.tbar.setAttribute(k, v);
    x.tbar.addEventListener("cambio", (e) => api.llamar("transicion_manual", { t: e.detail.valor }).catch(() => {}));
    x.tbar.addEventListener("final", () => { api.llamar("transicion_manual", { t: 0, soltar: true }).catch(() => {}); x.tbar.valor = 0; });
    acciones.append(x.take, x.corte, x.transicion, x.tbar);
    // Sin permiso para cambiar el aire, los monitores solos (p. ej. la producción mirando).
    if (!api.puede("preview_al_aire")) x.take.classList.add("oculto");
    if (!api.puede("scene_cut")) x.corte.classList.add("oculto");
    if (!api.puede("preview_al_aire")) x.transicion.classList.add("oculto");
    if (!api.puede("transicion_manual")) x.tbar.classList.add("oculto");
    if (!api.puede("monitores_ver")) x.monitores.classList.add("sin-imagenes");

    raiz.append(cab, x.monitores, acciones);
    contraer(x.contraida, true);
    salidas();
    api.mirar(["escenas"]);
    for (const tema of ["escenas", "monitores"]) if (api.estado[tema] !== undefined) actualizar(tema, api.estado[tema]);
  }

  function contraer(si, primera) {
    if (!x) return;
    x.contraida = si;
    x.raiz.classList.toggle("contraida", si);
    x.contraer.textContent = si ? t("▾ Mostrar monitores") : t("▴ Ocultar monitores");
    if (!primera) x.alContraer(si);
    mirarMonitores();
  }

  // Lo que empuja la app. De «monitores» las imágenes; los nombres, del que llegue último
  // («escenas» va cada 500 ms, los cuadros cada 250: los dos dicen lo mismo).
  function actualizar(tema, d) {
    if (!x || !d) return;
    if (tema === "escenas") {
      x.alAire = d.current || "";
      x.armada = d.armada || "";
      pintarMonitor(x.prog, x.alAire, x.prog.escena === x.alAire ? x.prog.src : "", t("Nada al aire"));
      pintarMonitor(x.prev, x.armada, x.prev.escena === x.armada ? x.prev.src : "", t("Nada armado: elegí una escena"));
    } else if (tema === "monitores") {
      const p = d.programa || {}, v = d.preview || {};
      pintarMonitor(x.prog, p.escena || "", p.imagen || "", t("Nada al aire"));
      pintarMonitor(x.prev, v.escena || "", v.imagen || "", t("Nada armado: elegí una escena"));
    } else return;
    x.take.toggleAttribute("deshabilitado", !x.prev.escena);
    x.corte.toggleAttribute("deshabilitado", !x.prev.escena);
  }

  // Las salidas: un botón por cada una (con su color), si hay más de una.
  function salidas() {
    if (!x) return;
    const lista = x.api.salidas || [];
    const actual = x.api.salidaActual();
    x.salidas.classList.toggle("oculto", lista.length < 2);
    x.salidas.replaceChildren(...lista.map((s) => {
      const b = boton(s.nombre || s.id, () => x.api.elegirSalida(s.id), { compacto: true });
      const esta = actual && s.id === actual.id;
      b.toggleAttribute("activo", !!esta);
      if (esta) b.setAttribute("variante", "acento");
      if (s.color) b.style.boxShadow = "inset 0 -3px 0 " + s.color;
      b.title = t("Manejar esta salida");
      return b;
    }));
    // Lo de la salida anterior ya no vale: hasta que lleguen los de esta, sin imagen.
    if (x.salidaVista !== (actual && actual.id)) {
      x.salidaVista = actual && actual.id;
      for (const m of [x.prog, x.prev]) { m.src = "x"; pintarMonitor(m, m.escena, "", ""); }
    }
  }

  document.addEventListener("visibilitychange", mirarMonitores);
  // `callada`: sin avisar (lo pidió otra pestaña: ya está guardado).
  BA.multipantalla = { crear, actualizar, salidas, contraer: (si, callada) => contraer(si, callada) };
})();
