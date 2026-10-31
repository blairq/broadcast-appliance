/* El dashboard web: arma el tablero con las tarjetas (tarjetas/*.js), el menú de qué
   tarjetas mostrar, el selector de salida y el tally de la barra. La conexión con la app
   (emparejar, llamar, mirar, confirmar lo que corta el aire) es la común de conexion.js.
   Sin estilos en línea (la CSP no los deja). */
(async () => {
  "use strict";
  const $ = (id) => document.getElementById(id);
  const CLAVE_OCULTAS = "ba-control-ocultas";
  const leer = (k) => { try { return localStorage.getItem(k); } catch (_) { return null; } };
  const guardar = (k, v) => { try { v === null ? localStorage.removeItem(k) : localStorage.setItem(k, v); } catch (_) {} };

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
  }
  $("salida").addEventListener("change", (ev) => BA.control.elegirSalida(ev.target.value));

  // --- El tablero ---------------------------------------------------------------------------
  const Tablero = {
    vivas: new Map(), // id → {def, el, contenido}
    actualizar(tema, datos) {
      for (const [, x] of this.vivas)
        if (x.def.temas.includes(tema) && x.def.actualizar) {
          try { x.def.actualizar.call(x, tema, datos); } catch (e) { console.error(x.def.id, e); }
        }
    },
  };
  const ocultas = () => { try { return new Set(JSON.parse(leer(CLAVE_OCULTAS) || "[]")); } catch (_) { return new Set(); } };
  const sola = () => { const m = /(?:^#|&)tarjeta=([^&]+)/.exec(location.hash); return m ? decodeURIComponent(m[1]) : ""; };
  let listo = false;

  function armarTablero() {
    const tablero = $("tablero");
    tablero.replaceChildren();
    Tablero.vivas.clear();
    const unica = sola();
    document.body.classList.toggle("una-tarjeta", !!unica);
    const fuera = ocultas();
    for (const def of BA.tarjetas) {
      if (unica ? def.id !== unica : fuera.has(def.id)) continue;
      // Si el perfil del aparato no deja usar NINGUNA herramienta de la tarjeta, no se muestra.
      if (def.herramientas && !def.herramientas.some((h) => BA.control.puede(h))) continue;
      const el = document.createElement("ba-tarjeta");
      el.setAttribute("titulo", def.titulo);
      el.className = "tarjeta-" + def.id + (def.ancha ? " ancha" : "");
      if (!unica) el.setAttribute("ampliar", def.pagina || location.pathname + "#tarjeta=" + def.id);
      const contenido = document.createElement("div");
      contenido.className = "contenido-tarjeta";
      el.append(contenido);
      tablero.append(el);
      const viva = { def, el, contenido, api: BA.control };
      Tablero.vivas.set(def.id, viva);
      try { def.crear.call(viva, contenido, BA.control); } catch (e) { console.error(def.id, e); }
      BA.control.mirar(def.temas);
      for (const x of def.temas) if (BA.control.estado[x] !== undefined) Tablero.actualizar(x, BA.control.estado[x]);
    }
    BA.control.mirar(["escenas"]); // el tally de la barra, siempre (también con una sola tarjeta)
    menuTarjetas();
  }

  function menuTarjetas() {
    const m = $("menuTarjetas");
    m.replaceChildren();
    const fuera = ocultas();
    const h = document.createElement("h3");
    h.textContent = t("Tarjetas en este aparato");
    m.append(h);
    const lista = document.createElement("div");
    lista.className = "lista-tarjetas";
    m.append(lista);
    for (const def of BA.tarjetas) {
      const tg = document.createElement("ba-toggle");
      tg.setAttribute("etiqueta", def.titulo);
      if (!fuera.has(def.id)) tg.setAttribute("encendido", "");
      tg.addEventListener("cambio", (e) => {
        const f = ocultas();
        if (e.detail.valor) f.delete(def.id); else f.add(def.id);
        guardar(CLAVE_OCULTAS, JSON.stringify([...f]));
        BA.control.preferencias({ ocultas: [...f] });
        armarTablero();
      });
      lista.append(tg);
    }
  }

  // El tally de la barra (y el borde de la pantalla): ¿hay algo al aire?
  function tallyGeneral(escenas) {
    const tl = $("tally");
    const alAire = escenas && escenas.current;
    tl.setAttribute("estado", alAire ? "aire" : "nada");
    tl.setAttribute("etiqueta", alAire ? BAi18n.llenar(t("Al aire · %1"), escenas.current) : t("Fuera del aire"));
  }

  // --- Arranque -------------------------------------------------------------------------------
  $("botonTarjetas").addEventListener("accion", () => $("menuTarjetas").classList.toggle("oculto"));
  $("botonCompleta").addEventListener("accion", () => {
    if (document.fullscreenElement) document.exitFullscreen(); else document.documentElement.requestFullscreen?.();
  });
  addEventListener("hashchange", () => { if (listo) armarTablero(); });

  await BA.conexion.arrancar({
    alListo(m) {
      // Lo que este aparato recuerda lo guarda la app (en su registro): manda eso, y la
      // copia local queda solo para pintar rápido mientras se conecta.
      if (m.preferencias && Array.isArray(m.preferencias.ocultas)) guardar(CLAVE_OCULTAS, JSON.stringify(m.preferencias.ocultas));
      else if (ocultas().size) BA.control.preferencias({ ocultas: [...ocultas()] }); // lo de antes, a la app
      $("aparato").textContent = m.nombre ? "· " + m.nombre : "";
      listo = true;
      armarTablero();
    },
    alEstado(tema, datos) {
      Tablero.actualizar(tema, datos);
      if (tema === "escenas") tallyGeneral(datos);
    },
    alSalida: salidaNueva,
    alAlarma: alarmaSistema,
  });
  $("tally").setAttribute("etiqueta", t("Fuera del aire"));
})();
