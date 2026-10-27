/* El dashboard web: la conexión con la app (/ws?control), el emparejamiento, llamar
   herramientas (con confirmación de lo que corta el aire), mirar temas y armar el
   tablero con las tarjetas de tarjetas.js. Sin estilos en línea (la CSP no los deja). */
(async () => {
  "use strict";
  const $ = (id) => document.getElementById(id);
  const CLAVE_TOKEN = "ba-control-token";
  const CLAVE_OCULTAS = "ba-control-ocultas";

  // --- Avisos y paneles --------------------------------------------------------------
  let relojAviso = 0;
  function avisar(texto, error) {
    const a = $("aviso");
    a.textContent = texto;
    a.classList.toggle("error", !!error);
    a.classList.remove("oculto");
    clearTimeout(relojAviso);
    relojAviso = setTimeout(() => a.classList.add("oculto"), error ? 4500 : 2500);
  }
  function mensaje(titulo, texto, conOlvidar) {
    $("mensajeTitulo").textContent = titulo;
    $("mensajeTexto").textContent = texto;
    $("olvidar").classList.toggle("oculto", !conOlvidar);
    $("mensaje").classList.remove("oculto");
  }
  const sinMensaje = () => $("mensaje").classList.add("oculto");

  const leer = (k) => { try { return localStorage.getItem(k); } catch (_) { return null; } };
  const guardar = (k, v) => { try { v === null ? localStorage.removeItem(k) : localStorage.setItem(k, v); } catch (_) {} };

  // --- Emparejar ---------------------------------------------------------------------
  function codigoDelFragmento() {
    const m = /(?:^#|&)p=([^&]+)/.exec(location.hash);
    return m ? decodeURIComponent(m[1]) : "";
  }
  function emparejar(codigo) {
    $("emparejar").classList.remove("oculto");
    $("pedir").addEventListener("accion", async () => {
      const nombre = $("nombre").value.trim() || t("Aparato sin nombre");
      try {
        const r = await fetch("/api/control/emparejar", { method: "POST", headers: { "Content-Type": "application/json" },
                                                          body: JSON.stringify({ codigo, nombre }) });
        const d = await r.json();
        if (!r.ok || !d.solicitud) throw new Error(t(d.error) || t("no se pudo pedir"));
        $("emparejarTexto").textContent = t("Pedido. Aprobalo en la app (aparece un aviso) …");
        esperarAprobacion(d.solicitud);
      } catch (e) { avisar(BAi18n.llenar(t("No se pudo pedir el emparejamiento: %1"), e.message), true); }
    });
  }
  async function esperarAprobacion(solicitud) {
    for (;;) {
      await new Promise((r) => setTimeout(r, 1500));
      try {
        const r = await fetch("/api/control/emparejar?solicitud=" + encodeURIComponent(solicitud));
        const d = await r.json();
        if (d.token) {
          guardar(CLAVE_TOKEN, d.token);
          history.replaceState(null, "", location.pathname + location.search); // el código ya no sirve
          $("emparejar").classList.add("oculto");
          conectar();
          return;
        }
        if (d.estado === "rechazada" || d.estado === "vencida" || d.error) {
          $("emparejar").classList.add("oculto");
          mensaje(t("No se aprobó"), t(d.error) || t("La app rechazó el pedido (o se venció). Pedí otro QR."), false);
          return;
        }
      } catch (_) { /* sigue esperando */ }
    }
  }

  // --- La conexión -----------------------------------------------------------------------
  let ws = null, autenticado = false, espera = 1000, siguienteId = 1, latido = 0;
  const pendientes = new Map(); // id → {resolver, rechazar, herramienta, args}
  const temasMirados = new Set();
  BA.control = { herramientas: [], perfil: [], estado: {} };

  function conectar() {
    const token = leer(CLAVE_TOKEN);
    if (!token) {
      mensaje(t("Este aparato no está emparejado"), t("Abrí la página desde el QR de la app (Servicios → Control) para emparejarlo."), false);
      return;
    }
    const url = (location.protocol === "https:" ? "wss://" : "ws://") + location.host + "/ws?control";
    ws = new WebSocket(url);
    ws.onopen = () => {
      espera = 1000;
      ws.send(JSON.stringify({ type: "auth", token }));
      // El gateway corta a los 30 s sin nada: un ping cada 20.
      clearInterval(latido);
      latido = setInterval(() => { if (ws && ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify({ type: "ping" })); }, 20000);
    };
    ws.onmessage = (ev) => { let m; try { m = JSON.parse(ev.data); } catch (_) { return; } recibir(m); };
    ws.onclose = () => {
      clearInterval(latido);
      autenticado = false;
      estadoConexion(false);
      for (const [, p] of pendientes) p.rechazar(new Error(t("se cortó la conexión")));
      pendientes.clear();
      setTimeout(conectar, espera);
      espera = Math.min(15000, espera * 1.6);
    };
  }
  function estadoConexion(viva) {
    const c = $("conexion");
    c.setAttribute("estado", viva ? "ok" : "apagado");
    c.textContent = viva ? t("Conectado") : t("Reconectando…");
  }

  function recibir(m) {
    switch (m.type) {
      case "auth":
        if (!m.ok) {
          guardar(CLAVE_TOKEN, null);
          mensaje(t("Este aparato ya no está habilitado"), t(m.error) || t("Lo quitaron en la app. Emparejalo de nuevo desde el QR."), true);
          ws.onclose = null;
          ws.close();
          return;
        }
        autenticado = true;
        sinMensaje();
        estadoConexion(true);
        BA.control.perfil = m.perfil || [];
        // Lo que este aparato recuerda lo guarda la app (en su registro): manda eso, y la
        // copia local queda solo para pintar rápido mientras se conecta.
        if (m.preferencias && Array.isArray(m.preferencias.ocultas)) guardar(CLAVE_OCULTAS, JSON.stringify(m.preferencias.ocultas));
        else if (ocultas().size) ws.send(JSON.stringify({ type: "preferencias", datos: { ocultas: [...ocultas()] } })); // lo de antes, a la app
        $("aparato").textContent = m.nombre ? "· " + m.nombre : "";
        herramientasNuevas = m.herramientas || [];
        if (!m.mas) listasLasHerramientas();
        break;
      case "herramientas": // las tandas que siguen al auth (el catálogo no entra en un mensaje)
        herramientasNuevas = herramientasNuevas.concat(m.herramientas || []);
        if (!m.mas) listasLasHerramientas();
        break;
      case "mirando":
        for (const r of m.rechazados || []) console.warn("tema rechazado", r.tema, r.error);
        break;
      case "estado":
        BA.control.estado[m.tema] = m.datos;
        Tablero.actualizar(m.tema, m.datos);
        if (m.tema === "escenas") tallyGeneral(m.datos);
        break;
      case "resultado": {
        const p = pendientes.get(m.id);
        if (!p) return;
        pendientes.delete(m.id);
        if (m.ok) p.resolver(m.datos);
        else p.rechazar(new Error(t(m.error) || t("no se pudo")));
        break;
      }
      case "confirmar": {
        const p = pendientes.get(m.id);
        if (!p) return;
        pedirConfirmacion(m.texto || t("Esto cambia lo que está al aire.")).then((si) => {
          if (!si) { pendientes.delete(m.id); p.rechazar(new Error("cancelado")); return; }
          ws.send(JSON.stringify({ type: "llamar", id: m.id, herramienta: p.herramienta, args: p.args, confirmado: true }));
        });
        break;
      }
      case "error":
        avisar(t(m.error) || t("error"), true);
        break;
    }
  }

  // Con el catálogo entero (y cada vez que la app lo vuelve a mandar: cambió el perfil).
  let herramientasNuevas = [];
  function listasLasHerramientas() {
    BA.control.herramientas = herramientasNuevas;
    herramientasNuevas = [];
    armarTablero();
    if (temasMirados.size) ws.send(JSON.stringify({ type: "mirar", temas: [...temasMirados] }));
  }

  // Llamar una herramienta de la app: una promesa con lo que devuelve.
  BA.control.llamar = (herramienta, args = {}) => new Promise((resolver, rechazar) => {
    if (!ws || ws.readyState !== WebSocket.OPEN || !autenticado) { rechazar(new Error(t("sin conexión con la app"))); return; }
    const id = siguienteId++;
    pendientes.set(id, { resolver, rechazar, herramienta, args });
    ws.send(JSON.stringify({ type: "llamar", id, herramienta, args }));
  });
  // Lo mismo, avisando si falla (para los botones).
  BA.control.hacer = (herramienta, args) =>
    BA.control.llamar(herramienta, args).catch((e) => { if (e.message !== "cancelado") avisar(e.message, true); throw e; });
  BA.control.puede = (herramienta) => BA.control.herramientas.length === 0 || BA.control.herramientas.some((h) => h.name === herramienta);
  BA.control.avisar = avisar;

  function mirar(temas) {
    let nuevos = false;
    for (const t of temas) if (!temasMirados.has(t)) { temasMirados.add(t); nuevos = true; }
    if (nuevos && autenticado) ws.send(JSON.stringify({ type: "mirar", temas: [...temasMirados] }));
  }

  // --- Confirmar ------------------------------------------------------------------------
  function pedirConfirmacion(texto) {
    return new Promise((resolver) => {
      $("confirmarTexto").textContent = texto;
      $("confirmar").classList.remove("oculto");
      const fin = (si) => {
        $("confirmar").classList.add("oculto");
        $("confirmarSi").removeEventListener("accion", siF);
        $("confirmarNo").removeEventListener("accion", noF);
        resolver(si);
      };
      const siF = () => fin(true), noF = () => fin(false);
      $("confirmarSi").addEventListener("accion", siF);
      $("confirmarNo").addEventListener("accion", noF);
    });
  }

  // --- El tablero ---------------------------------------------------------------------------
  const Tablero = {
    vivas: new Map(), // id → {def, el, contenido}
    actualizar(tema, datos) {
      for (const [, t] of this.vivas)
        if (t.def.temas.includes(tema) && t.def.actualizar) {
          try { t.def.actualizar.call(t, tema, datos); } catch (e) { console.error(t.def.id, e); }
        }
    },
  };
  const ocultas = () => { try { return new Set(JSON.parse(leer(CLAVE_OCULTAS) || "[]")); } catch (_) { return new Set(); } };
  const sola = () => { const m = /(?:^#|&)tarjeta=([^&]+)/.exec(location.hash); return m ? decodeURIComponent(m[1]) : ""; };

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
      if (!unica) el.setAttribute("ampliar", location.pathname + "#tarjeta=" + def.id);
      const contenido = document.createElement("div");
      contenido.className = "contenido-tarjeta";
      el.append(contenido);
      tablero.append(el);
      const viva = { def, el, contenido, api: BA.control };
      Tablero.vivas.set(def.id, viva);
      try { def.crear.call(viva, contenido, BA.control); } catch (e) { console.error(def.id, e); }
      mirar(def.temas);
      for (const t of def.temas) if (BA.control.estado[t] !== undefined) Tablero.actualizar(t, BA.control.estado[t]);
    }
    mirar(["escenas"]); // el tally de la barra, siempre (también con una sola tarjeta)
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
        if (ws && ws.readyState === WebSocket.OPEN && autenticado)
          ws.send(JSON.stringify({ type: "preferencias", datos: { ocultas: [...f] } }));
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
  $("olvidar").addEventListener("accion", () => { guardar(CLAVE_TOKEN, null); location.reload(); });
  addEventListener("hashchange", () => { if (autenticado) armarTablero(); });

  // Los textos, en el idioma del evento (también los de las tarjetas, que se arman después).
  await BAi18n.iniciar({ contexto: "web/control", preferir: "evento" });
  $("tally").setAttribute("etiqueta", t("Fuera del aire"));
  BA.traducir();
  const codigo = codigoDelFragmento();
  if (codigo && !leer(CLAVE_TOKEN)) emparejar(codigo);
  else conectar();
})();
