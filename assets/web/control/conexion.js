/* La conexión de un aparato con la app (/ws?control), común al dashboard (/control/) y a
   las superficies de una página (/control/escaleta, /control/presentador, /control/camara):
   el emparejamiento, llamar herramientas (con la confirmación de lo que corta el aire),
   mirar temas, la salida que maneja el aparato, los avisos y los mensajes.

   El aparato es UNO aunque abra varias páginas: el token vive en este origen (todas las
   páginas de /control/ lo comparten) y el perfil lo decide la app. Las capas (emparejar,
   mensaje, confirmar, aviso) las arma esto: así cada página no repite su HTML.

     BA.conexion.arrancar({
       alListo(m),           // autenticado y con el catálogo de herramientas (m: el auth)
       alEstado(tema, d),    // la app empujó un tema que se mira
       alSalida(m),          // cambió la salida del aparato (o la lista de salidas)
       alAlarma(m),          // una alarma de la app (opcional)
     });
     BA.control.llamar(h, args) / hacer(h, args) / puede(h) / mirar([temas]) / avisar(t, error)

   Sin estilos en línea (la CSP no los deja): clases de control.css. */
(() => {
  "use strict";
  const BA = (window.BA = window.BA || {});
  const CLAVE_TOKEN = "ba-control-token";
  const el = (tag, cls, texto) => {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    if (texto !== undefined) e.textContent = texto;
    return e;
  };
  const leer = (k) => { try { return localStorage.getItem(k); } catch (_) { return null; } };
  const guardar = (k, v) => { try { v === null ? localStorage.removeItem(k) : localStorage.setItem(k, v); } catch (_) {} };

  // --- Las capas (se arman una vez, al primer uso) ------------------------------------------
  const capas = {};
  function capa(id) {
    if (capas[id]) return capas[id];
    const c = el("section", "cubre oculto");
    const caja = el("div", "caja panel");
    c.append(caja);
    const x = { raiz: c, caja };
    if (id === "emparejar") {
      x.titulo = el("h1", "", t("Emparejar este aparato"));
      x.texto = el("p", "suave", t("Poné un nombre para reconocerlo y pedí el emparejamiento. Una persona lo aprueba en la app."));
      x.nombre = el("input");
      x.nombre.type = "text";
      x.nombre.maxLength = 60;
      x.nombre.placeholder = t("Ej.: iPad de dirección");
      x.pedir = el("ba-boton", "", t("Pedir emparejamiento"));
      x.pedir.setAttribute("variante", "acento");
      x.pedir.setAttribute("grande", "");
      caja.append(x.titulo, x.texto, x.nombre, x.pedir);
    } else if (id === "mensaje") {
      x.titulo = el("h1");
      x.texto = el("p", "suave");
      x.olvidar = el("ba-boton", "oculto", t("Olvidar este aparato"));
      x.olvidar.addEventListener("accion", () => { guardar(CLAVE_TOKEN, null); location.reload(); });
      caja.append(x.titulo, x.texto, x.olvidar);
    } else if (id === "confirmar") {
      caja.classList.add("confirmar");
      x.texto = el("h2", "", t("Cambia el aire"));
      const fila = el("div", "fila");
      x.no = el("ba-boton", "lleno", t("Cancelar"));
      x.si = el("ba-boton", "lleno", t("Sí, hacelo"));
      x.si.setAttribute("variante", "aire");
      fila.append(x.no, x.si);
      caja.append(x.texto, fila);
    }
    document.body.append(c);
    capas[id] = x;
    return x;
  }

  let relojAviso = 0, avisoEl = null;
  function avisar(texto, error) {
    if (!avisoEl) { avisoEl = el("div", "aviso oculto"); document.body.append(avisoEl); }
    avisoEl.textContent = texto;
    avisoEl.classList.toggle("error", !!error);
    avisoEl.classList.remove("oculto");
    clearTimeout(relojAviso);
    relojAviso = setTimeout(() => avisoEl.classList.add("oculto"), error ? 4500 : 2500);
  }
  function mensaje(titulo, texto, conOlvidar) {
    const m = capa("mensaje");
    m.titulo.textContent = titulo;
    m.texto.textContent = texto;
    m.olvidar.classList.toggle("oculto", !conOlvidar);
    m.raiz.classList.remove("oculto");
  }
  const sinMensaje = () => { if (capas.mensaje) capas.mensaje.raiz.classList.add("oculto"); };

  function pedirConfirmacion(texto) {
    const c = capa("confirmar");
    return new Promise((resolver) => {
      c.texto.textContent = texto;
      c.raiz.classList.remove("oculto");
      const fin = (si) => {
        c.raiz.classList.add("oculto");
        c.si.removeEventListener("accion", siF);
        c.no.removeEventListener("accion", noF);
        resolver(si);
      };
      const siF = () => fin(true), noF = () => fin(false);
      c.si.addEventListener("accion", siF);
      c.no.addEventListener("accion", noF);
    });
  }

  // --- Emparejar -------------------------------------------------------------------------------
  function codigoDelFragmento() {
    const m = /(?:^#|&)p=([^&]+)/.exec(location.hash);
    return m ? decodeURIComponent(m[1]) : "";
  }
  function emparejar(codigo) {
    const e = capa("emparejar");
    e.raiz.classList.remove("oculto");
    e.pedir.addEventListener("accion", async () => {
      const nombre = e.nombre.value.trim() || t("Aparato sin nombre");
      try {
        const r = await fetch("/api/control/emparejar", { method: "POST", headers: { "Content-Type": "application/json" },
                                                          body: JSON.stringify({ codigo, nombre }) });
        const d = await r.json();
        if (!r.ok || !d.solicitud) throw new Error(t(d.error) || t("no se pudo pedir"));
        e.texto.textContent = t("Pedido. Aprobalo en la app (aparece un aviso) …");
        esperarAprobacion(d.solicitud);
      } catch (err) { avisar(BAi18n.llenar(t("No se pudo pedir el emparejamiento: %1"), err.message), true); }
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
          capa("emparejar").raiz.classList.add("oculto");
          conectar();
          return;
        }
        if (d.estado === "rechazada" || d.estado === "vencida" || d.error) {
          capa("emparejar").raiz.classList.add("oculto");
          mensaje(t("No se aprobó"), t(d.error) || t("La app rechazó el pedido (o se venció). Pedí otro QR."), false);
          return;
        }
      } catch (_) { /* sigue esperando */ }
    }
  }

  // --- La conexión ---------------------------------------------------------------------------
  let ws = null, autenticado = false, espera = 1000, siguienteId = 1, latido = 0, op = {};
  const pendientes = new Map(); // id → {resolver, rechazar, herramienta, args}
  const temasMirados = new Set();
  let herramientasNuevas = [], ultimoAuth = null;
  const C = (BA.control = BA.control || {});
  Object.assign(C, { herramientas: [], perfil: [], estado: {}, salida: "", salidas: [] });

  function estadoConexion(viva) {
    const c = document.getElementById("conexion");
    if (!c) return;
    c.setAttribute("estado", viva ? "ok" : "apagado");
    c.textContent = viva ? t("Conectado") : t("Reconectando…");
  }

  function conectar() {
    const token = leer(CLAVE_TOKEN);
    if (!token) {
      mensaje(t("Este aparato no está emparejado"), t("Abrí la página desde el QR de la app (Servicios → Control) para emparejarlo."), false);
      return;
    }
    ws = new WebSocket((location.protocol === "https:" ? "wss://" : "ws://") + location.host + "/ws?control");
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

  function salidaNueva(m) {
    C.salida = m.salida || "";
    C.salidas = m.salidas || [];
    if (op.alSalida) op.alSalida(m);
    dispatchEvent(new CustomEvent("ba-salida", { detail: { salida: C.salida, salidas: C.salidas } }));
  }

  function listasLasHerramientas() {
    C.herramientas = herramientasNuevas;
    herramientasNuevas = [];
    if (op.alListo) op.alListo(ultimoAuth);
    if (temasMirados.size) ws.send(JSON.stringify({ type: "mirar", temas: [...temasMirados] }));
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
        C.perfil = m.perfil || [];
        C.nombre = m.nombre || "";
        ultimoAuth = m;
        salidaNueva(m);
        herramientasNuevas = m.herramientas || [];
        if (!m.mas) listasLasHerramientas();
        break;
      case "herramientas": // las tandas que siguen al auth (el catálogo no entra en un mensaje)
        herramientasNuevas = herramientasNuevas.concat(m.herramientas || []);
        if (!m.mas) listasLasHerramientas();
        break;
      case "salida":
        if (m.error) avisar(t(m.error), true);
        salidaNueva(m);
        break;
      case "mirando":
        for (const r of m.rechazados || []) console.warn("tema rechazado", r.tema, r.error);
        if (op.alRechazo) for (const r of m.rechazados || []) op.alRechazo(r);
        break;
      case "estado":
        C.estado[m.tema] = m.datos;
        if (op.alEstado) op.alEstado(m.tema, m.datos);
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
      case "alarma":
        if (op.alAlarma) op.alAlarma(m);
        break;
      case "error":
        avisar(t(m.error) || t("error"), true);
        break;
    }
  }

  // --- Lo que usan las páginas --------------------------------------------------------------
  C.llamar = (herramienta, args = {}) => new Promise((resolver, rechazar) => {
    if (!ws || ws.readyState !== WebSocket.OPEN || !autenticado) { rechazar(new Error(t("sin conexión con la app"))); return; }
    const id = siguienteId++;
    pendientes.set(id, { resolver, rechazar, herramienta, args });
    ws.send(JSON.stringify({ type: "llamar", id, herramienta, args }));
  });
  // Lo mismo, avisando si falla (para los botones).
  C.hacer = (herramienta, args) =>
    C.llamar(herramienta, args).catch((e) => { if (e.message !== "cancelado") avisar(e.message, true); throw e; });
  C.puede = (herramienta) => C.herramientas.length === 0 || C.herramientas.some((h) => h.name === herramienta);
  C.avisar = avisar;
  C.mirar = (temas) => {
    let nuevos = false;
    for (const x of temas) if (!temasMirados.has(x)) { temasMirados.add(x); nuevos = true; }
    if (nuevos && autenticado) ws.send(JSON.stringify({ type: "mirar", temas: [...temasMirados] }));
  };
  // Lo que el aparato quiere recordar: va a SU registro en la app.
  C.preferencias = (datos) => { if (ws && ws.readyState === WebSocket.OPEN && autenticado) ws.send(JSON.stringify({ type: "preferencias", datos })); };
  // La salida que maneja: {id, nombre, color, por_omision} (la por omisión si no eligió).
  C.salidaActual = () => {
    const l = C.salidas;
    return l.find((x) => x.id === C.salida) || l.find((x) => x.por_omision) || l[0] || null;
  };
  // Pasar a manejar otra salida (id o nombre; la app contesta con {type: "salida"}).
  C.elegirSalida = (id) => {
    if (!ws || ws.readyState !== WebSocket.OPEN || !autenticado) return;
    const x = C.salidas.find((y) => y.id === id || y.nombre === id);
    ws.send(JSON.stringify({ type: "salida", salida: x && x.por_omision ? "" : (x ? x.id : id) }));
  };

  BA.conexion = {
    // Los textos en el idioma del evento, y a conectar (o a emparejar, si vino el QR).
    async arrancar(opciones) {
      op = opciones || {};
      await BAi18n.iniciar({ contexto: "web/control", preferir: "evento" });
      const codigo = codigoDelFragmento();
      if (codigo && !leer(CLAVE_TOKEN)) emparejar(codigo);
      else conectar();
    },
    avisar,
    mensaje,
  };
})();
