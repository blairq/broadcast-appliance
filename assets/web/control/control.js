/* El dashboard web: arriba la multipantalla (multipantalla.js), abajo el tablero con las
   tarjetas (tarjetas/*.js) en una grilla que se acomoda al ancho (1 columna en el celular, 2
   en la tablet, 3 o 4 en la PC), el selector de salida y el tally de la barra. La conexión
   con la app (emparejar, llamar, mirar, confirmar lo que corta el aire) es la común de
   conexion.js.

   LA DISPOSICIÓN. Cada aparato ordena sus tarjetas, les cambia el ancho (de 1 a 4 columnas)
   y oculta las que no usa («✎ Disposición»: se arrastran por ⠿, o con ◀ ▶ y − +). Se guarda
   en el registro del aparato en la app (preferencias.disposicion): vuelve igual al refrescar,
   con el navegador borrado o desde otra pestaña del mismo aparato (que la recibe en el
   acto). «Volver a la de fábrica» la borra.
     disposicion = {orden: [id…], anchos: {id: 1…4}, ocultas: [id…], multipantalla: {contraida}}
   Sin estilos en línea (la CSP no los deja). */
(async () => {
  "use strict";
  const CLAVE_COPIA = "ba-control-disposicion"; // copia local: para pintar igual mientras conecta
  const leer = (k) => { try { return localStorage.getItem(k); } catch (_) { return null; } };
  const guardarLocal = (k, v) => { try { v === null ? localStorage.removeItem(k) : localStorage.setItem(k, v); } catch (_) {} };
  const { boton } = BA.tj;

  // Activa: queda a la vista. Terminada: se dice un rato y se va.
  let relojAlarma;
  function alarmaSistema(m) {
    const a = $("alarmaSistema");
    a.querySelector("strong").textContent = m.titulo || "";
    a.querySelector("span").textContent = m.texto || "";
    a.classList.toggle("terminada", !m.activa);
    a.classList.remove("oculto");
    clearTimeout(relojAlarma);
    if (!m.activa) relojAlarma = setTimeout(() => a.classList.add("oculto"), 8000);
  }

  // --- La salida que maneja el aparato ----------------------------------------------------
  // "" = la por omisión. Lo que llama va a esa (la app la pone en el contexto) y los temas
  // que miran las tarjetas son de esa: al cambiarla, la app vuelve a mandar lo de la nueva.
  function salidaNueva() {
    const lista = BA.control.salidas;
    const sel = $("salida");
    sel.classList.toggle("oculto", lista.length < 2);
    const actual = BA.control.salidaActual();
    sel.replaceChildren(...lista.map((x) => {
      const o = document.createElement("option");
      o.value = x.id;
      o.textContent = x.nombre || x.id;
      o.selected = actual && x.id === actual.id;
      return o;
    }));
    sel.style.borderColor = (actual && actual.color) || "";
    sel.style.boxShadow = actual && actual.color ? "inset 4px 0 0 " + actual.color : "";
    BA.multipantalla.salidas();
  }
  $("salida").addEventListener("change", (ev) => BA.control.elegirSalida(ev.target.value));

  // --- La disposición (las cuentas, en disposicion.js) ----------------------------------------
  const D = BA.disposicion;
  const normalizar = (p) => D.normalizar(p, BA.tarjetas);
  const desdePreferencias = (prefs) => D.desdePreferencias(prefs, BA.tarjetas);
  const porId = (id) => BA.tarjetas.find((d) => d.id === id);
  let disp = null; // la de este aparato (normalizada)

  function guardarDisposicion() {
    const prefs = Object.assign({}, BA.control.prefs || {});
    prefs.disposicion = disp;
    prefs.ocultas = disp.ocultas; // lo de antes, por si vuelve una página vieja
    BA.control.preferencias(prefs);
    guardarLocal(CLAVE_COPIA, JSON.stringify(disp));
  }
  function deFabrica() {
    disp = normalizar({});
    const prefs = Object.assign({}, BA.control.prefs || {});
    delete prefs.disposicion;
    delete prefs.ocultas;
    BA.control.preferencias(prefs);
    guardarLocal(CLAVE_COPIA, null);
  }

  // --- El tablero ---------------------------------------------------------------------------
  const Tablero = {
    vivas: new Map(), // id → la viva (hereda de la definición: sus métodos ven `this`)
    rechazados: new Set(), // temas que el perfil no deja mirar
    actualizar(tema, datos) {
      for (const [, x] of this.vivas)
        if (x.def.temas.includes(tema) && x.def.actualizar) {
          try { x.def.actualizar.call(x, tema, datos); } catch (e) { console.error(x.def.id, e); }
        }
    },
  };
  const sola = () => { const m = /(?:^#|&)tarjeta=([^&]+)/.exec(location.hash); return m ? decodeURIComponent(m[1]) : ""; };
  let listo = false;
  let editando = false;

  // ¿La tarjeta se puede usar con este perfil? Sin ninguna de sus herramientas (si declara) o
  // con un tema rechazado, no se muestra.
  const disponible = (def) =>
    !(def.herramientas && def.herramientas.length && !def.herramientas.some((h) => BA.control.puede(h))) &&
    !def.temas.some((tm) => Tablero.rechazados.has(tm));

  function armarTablero() {
    const tablero = $("tablero");
    tablero.replaceChildren();
    Tablero.vivas.clear();
    const unica = sola();
    document.body.classList.toggle("una-tarjeta", !!unica);
    document.body.classList.toggle("editando", editando && !unica);
    armarMultipantalla(unica);
    for (const id of disp.orden) {
      const def = porId(id);
      if (!def || !disponible(def)) continue;
      if (unica ? def.id !== unica : disp.ocultas.includes(def.id) && !editando) continue;
      const el2 = document.createElement("ba-tarjeta");
      el2.setAttribute("titulo", def.titulo);
      el2.className = "tarjeta-" + def.id + " ancho-" + disp.anchos[def.id] + (disp.ocultas.includes(def.id) ? " oculta" : "");
      el2.dataset.id = def.id;
      if (!unica) el2.setAttribute("ampliar", def.pagina || location.pathname + "#tarjeta=" + def.id);
      const contenido = document.createElement("div");
      contenido.className = "contenido-tarjeta";
      el2.append(contenido);
      if (editando && !unica) el2.append(herramientasEdicion(def.id));
      tablero.append(el2);
      // La viva hereda de la definición: los métodos propios de una tarjeta (p. ej.
      // pintarPosiciones de la PTZ) se ven desde crear y actualizar.
      const viva = Object.assign(Object.create(def), { def, el: el2, contenido, api: BA.control });
      Tablero.vivas.set(def.id, viva);
      try { def.crear.call(viva, contenido, BA.control); } catch (e) { console.error(def.id, e); }
      BA.control.mirar(def.temas);
      for (const x of def.temas) if (BA.control.estado[x] !== undefined) Tablero.actualizar(x, BA.control.estado[x]);
    }
    BA.control.mirar(["escenas"]); // el tally de la barra, siempre (también con una sola tarjeta)
    barraEdicion();
  }

  // La multipantalla: arriba de todo; con una sola tarjeta, solo con la de escenas (es su mesa).
  // Se arma una vez por conexión (editar el tablero no la toca: sus imágenes siguen).
  let multiArmada = "";
  function armarMultipantalla(unica) {
    const raiz = $("multipantalla");
    const sirve = BA.control.puede("preview_al_aire") || BA.control.puede("monitores_ver");
    const ver = sirve && (!unica || unica === "escenas");
    raiz.classList.toggle("oculto", !ver);
    if (!ver) { multiArmada = ""; BA.control.dejar(["monitores"]); return; }
    if (multiArmada === "si") return;
    multiArmada = "si";
    BA.multipantalla.crear(raiz, BA.control, {
      contraida: disp.multipantalla.contraida,
      alContraer(si) { disp.multipantalla.contraida = si; guardarDisposicion(); },
    });
  }

  // --- Editar la disposición -------------------------------------------------------------------
  function herramientasEdicion(id) {
    const h = el("div", "edicion-tarjeta");
    h.slot = "acciones";
    const asa = el("span", "asa", "⠿");
    asa.title = t("Arrastrá para moverla");
    asa.addEventListener("pointerdown", (e) => arrastrar(e, id));
    const cambiar = (hecho) => { if (!hecho) return; guardarDisposicion(); armarTablero(); };
    const mover = (d) => cambiar(D.mover(disp, id, d));
    const ancho = (d) => cambiar(D.ancho(disp, id, d));
    const oculta = disp.ocultas.includes(id);
    const ocultar = () => cambiar(D.alternarOculta(disp, id));
    const b = (txt, f, titulo) => { const x = boton(txt, f, { compacto: true }); x.title = titulo; return x; };
    const n = el("span", "numero ancho-actual", String(disp.anchos[id]));
    n.title = t("Columnas que ocupa");
    h.append(asa, b("◀", () => mover(-1), t("Antes")), b("▶", () => mover(1), t("Después")),
             b("−", () => ancho(-1), t("Más angosta")), n, b("+", () => ancho(1), t("Más ancha")),
             b(oculta ? "👁" : "✕", ocultar, oculta ? t("Mostrarla") : t("Ocultarla")));
    return h;
  }

  // Arrastrar con el dedo o el mouse (eventos de puntero: anda igual en una tablet): la
  // tarjeta se mueve en vivo delante o detrás de la que queda debajo; al soltar se guarda.
  function arrastrar(e, id) {
    e.preventDefault();
    const tablero = $("tablero");
    const propia = tablero.querySelector(`[data-id="${CSS.escape(id)}"]`);
    if (!propia) return;
    propia.classList.add("arrastrando");
    const mover = (ev) => {
      const debajo = document.elementFromPoint(ev.clientX, ev.clientY);
      const otra = debajo && debajo.closest && debajo.closest("#tablero > ba-tarjeta");
      if (!otra || otra === propia) return;
      const r = otra.getBoundingClientRect();
      // En la misma fila decide la mitad horizontal; si no, la vertical.
      const antes = Math.abs(ev.clientY - (r.top + r.height / 2)) < r.height / 2 && r.width < tablero.clientWidth * 0.9
        ? ev.clientX < r.left + r.width / 2 : ev.clientY < r.top + r.height / 2;
      if (antes ? otra.previousElementSibling !== propia : otra.nextElementSibling !== propia) (antes ? otra.before(propia) : otra.after(propia));
    };
    const soltar = () => {
      removeEventListener("pointermove", mover);
      removeEventListener("pointerup", soltar);
      removeEventListener("pointercancel", soltar);
      propia.classList.remove("arrastrando");
      if (D.reordenar(disp, [...tablero.children].map((c) => c.dataset.id))) guardarDisposicion();
      armarTablero();
    };
    addEventListener("pointermove", mover);
    addEventListener("pointerup", soltar);
    addEventListener("pointercancel", soltar);
  }

  function barraEdicion() {
    const b = $("barraEdicion");
    b.classList.toggle("oculto", !editando || !!sola());
    $("botonDisposicion").toggleAttribute("activo", editando);
  }
  function editar(si) {
    editando = si;
    armarTablero();
  }

  // El tally de la barra (y el borde de la pantalla): ¿hay algo al aire?
  function tallyGeneral(escenas) {
    const tl = $("tally");
    const alAire = escenas && escenas.current;
    tl.setAttribute("estado", alAire ? "aire" : "nada");
    tl.setAttribute("etiqueta", alAire ? BAi18n.llenar(t("Al aire · %1"), escenas.current) : t("Fuera del aire"));
  }

  // --- Arranque -------------------------------------------------------------------------------
  try { disp = normalizar(JSON.parse(leer(CLAVE_COPIA) || "null")); } catch (_) { disp = normalizar({}); }
  $("botonDisposicion").addEventListener("accion", () => editar(!editando));
  $("botonListo").addEventListener("accion", () => editar(false));
  $("botonDeFabrica").addEventListener("accion", () => { deFabrica(); armarTablero(); BA.control.avisar(t("Disposición de fábrica")); });
  $("botonCompleta").addEventListener("accion", () => {
    if (document.fullscreenElement) document.exitFullscreen(); else document.documentElement.requestFullscreen?.();
  });
  addEventListener("hashchange", () => { if (listo) armarTablero(); });

  // --- En desuso: el tablero de entrada es el de WebAssembly (/control/app/) --------------------
  // Un aparato con el perfil completo («todo») va derecho al tablero nuevo; este queda para los
  // perfiles acotados (el tablero nuevo necesita «todo») y, a pedido, con /control/?clasico (lo
  // que abre el tablero nuevo cuando no puede arrancar: así no rebotan entre las dos).
  const clasico = new URLSearchParams(location.search).has("clasico");
  const perfilCompleto = (p) => p === "*" || (Array.isArray(p) && p.includes("*"));
  function avisoDesuso(completo) {
    const a = $("avisoDesuso");
    a.querySelector("span").textContent = completo
      ? t("Este control web está en desuso: usá el tablero nuevo, el mismo de la app.")
      : t("Este control web está en desuso. El tablero nuevo necesita que en la app le den a este aparato el perfil «todo».");
    a.querySelector("a").classList.toggle("oculto", !completo);
    a.classList.remove("oculto");
  }

  await BA.conexion.arrancar({
    alListo(m) {
      const completo = perfilCompleto(BA.control.perfil);
      if (completo && !clasico && !sola()) { location.replace("/control/app/"); return; }
      avisoDesuso(completo);
      // Lo que este aparato recuerda lo guarda la app (en su registro); la copia local queda
      // solo para pintar rápido mientras se conecta.
      const prefs = (m && m.preferencias) || {};
      const tiene = prefs.disposicion || (Array.isArray(prefs.ocultas) && prefs.ocultas.length);
      if (tiene) disp = desdePreferencias(prefs);
      else if (leer(CLAVE_COPIA)) guardarDisposicion(); // lo que había en el navegador, a la app
      guardarLocal(CLAVE_COPIA, JSON.stringify(disp));
      $("aparato").textContent = m && m.nombre ? "· " + m.nombre : "";
      Tablero.rechazados.clear();
      multiArmada = ""; // el perfil pudo cambiar: de nuevo
      listo = true;
      armarTablero();
    },
    alEstado(tema, datos) {
      Tablero.actualizar(tema, datos);
      BA.multipantalla.actualizar(tema, datos);
      if (tema === "escenas") tallyGeneral(datos);
    },
    // Un tema fuera del perfil: su tarjeta no se muestra (no queda vacía para siempre).
    alRechazo(r) {
      if (!r || !r.tema || Tablero.rechazados.has(r.tema)) return;
      Tablero.rechazados.add(r.tema);
      BA.control.dejar([r.tema]);
      if (listo) armarTablero();
    },
    // Otra pestaña del mismo aparato cambió la disposición: igual acá.
    alPreferencias(prefs) {
      disp = desdePreferencias(prefs);
      BA.multipantalla.contraer(disp.multipantalla.contraida, true);
      guardarLocal(CLAVE_COPIA, JSON.stringify(disp));
      if (listo) armarTablero();
    },
    alSalida: salidaNueva,
    alAlarma: alarmaSistema,
  });
  $("tally").setAttribute("etiqueta", t("Fuera del aire"));
})();
