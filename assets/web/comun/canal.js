// ============================================================================
// El canal web /ws, del lado de la página. UNO SOLO para todas: el invitado, la
// pantalla, la sesión remota, la consola de la presidencia, el registro y su
// atención, el control web, la botonera y las superficies (la pantalla de sala,
// el carrusel, el teleprompter, los rótulos, el tablero). El contrato está en
// docs/arquitectura/06-canal-web.md: mensajes de TEXTO con un objeto JSON y su `type`.
//
// QUE RESUELVE. Antes había ocho clientes escritos a mano, cada uno con su
// reconexión, su latido y su manera de entrar, y dos formatos (texto y binario
// con largo). Una página nueva copiaba el que tenía más cerca, con sus agujeros.
//
//   const c = BAWs.abrir({
//     rol: "sesion",                 // lo que va después de /ws? (nada secreto)
//     auth: () => ({ i, t, pin }),   // el primer mensaje (BAWs le agrega v y type);
//                                    // null = todavía no hay con qué entrar
//     alEntrar: (m) => {...},        // la sala dijo {type:"auth", ok:true, …}
//     alMensaje: (m) => {...},       // cada mensaje (menos pong y auth)
//     alRechazo: (m) => {...},       // auth ok:false que no se arregla esperando: el
//                                    // canal se queda quieto hasta c.reabrir()
//     alEspera: (m) => {...},        // auth ok:false «esperar»/«clave_mal» (opcional):
//                                    // vuelve a probar solo, con espera creciente
//     alCaer: () => {...},           // se cortó (opcional; vuelve solo)
//     alEstado: (e) => {...},        // "conectando" | "conectado" | "caido" | "cerrado"
//   });
//   c.mandar(m)   c.listo()   c.cerrar()   c.reabrir()
//
// SI SE CAE, VUELVE SOLA. Con espera creciente (1 s, 2 s… hasta 30 s): un
// televisor puede quedar días prendido y no tiene sentido que martille la red.
// Al volver la red o la pestaña al frente prueba enseguida.
//
// UNA CLAVE RECHAZADA SE VUELVE A PROBAR. La sala dice «clave_mal» si se cambió
// de evento (la página ya no vale) o si recién arrancó y todavía no tiene las
// claves; «esperar» si todavía no tiene las credenciales de la app. En los dos
// casos se vuelve a probar con espera creciente (2 s… hasta 1 min): si era lo
// segundo, la página vuelve sola, sin recargarla.
//
// UN CANAL MUDO NO ES UN CANAL VIVO. Cada 15 s se manda un «ping»; si el
// anterior no tuvo respuesta en 40 s, se da por caído y se reconecta (un
// televisor que perdió la red sin enterarse se quedaría esperando para
// siempre). Se mide desde el ping y no desde lo último que llegó: una pestaña
// escondida late una vez por minuto y no por eso está muerta.
//
// LO QUE VALE PARA TODAS («tema», «recargar») lo atiende esto: cada mensaje se
// avisa también como evento `ba-canal` en document (comun/tema.js lo escucha).
// ============================================================================
(() => {
  "use strict";

  const ESPERA_MAX_MS = 30000;
  const REINTENTO_MIN_MS = 2000;
  const REINTENTO_MAX_MS = 60000;
  const PING_MS = 15000;
  const MUDO_MS = 40000;
  const SE_ESPERA = new Set(["esperar", "clave_mal"]);

  const BAWs = (window.BAWs = {
    /// Cuántos canales hay abiertos y adentro (comun/tema.js no pregunta si hay uno).
    abiertos: 0,
    abrir(o) {
      const st = { ws: null, listo: false, cerrado: false, estado: "", intentos: 0, timer: 0, ping: 0,
                   oido: 0, pingMs: 0, reintento: REINTENTO_MIN_MS, proxima: 0 };
      const estado = (e) => {
        if (st.estado === e) return;
        st.estado = e;
        if (o.alEstado) o.alEstado(e);
      };
      const mandar = (m) => {
        if (!st.listo || !st.ws || st.ws.readyState !== WebSocket.OPEN) return false;
        st.ws.send(JSON.stringify(m));
        return true;
      };

      const caido = (ws) => {
        if (st.ws !== ws) return; // uno viejo que avisa tarde
        st.ws = null;
        clearInterval(st.ping);
        if (st.listo) { st.listo = false; BAWs.abiertos--; }
        estado(st.cerrado ? "cerrado" : "caido");
        if (o.alCaer) o.alCaer();
        if (st.cerrado) return;
        let ms = st.proxima;
        st.proxima = 0;
        if (!ms) {
          st.intentos = Math.min(st.intentos + 1, 6);
          ms = Math.min(1000 * 2 ** (st.intentos - 1), ESPERA_MAX_MS);
        }
        clearTimeout(st.timer);
        st.timer = setTimeout(conectar, ms);
      };

      const latir = (ws) => {
        clearInterval(st.ping);
        st.pingMs = 0;
        st.ping = setInterval(() => {
          if (st.ws !== ws) return clearInterval(st.ping);
          if (st.pingMs && st.oido < st.pingMs) {
            // El ping anterior sigue sin respuesta: a los 40 s, caído (close() puede
            // tardar en avisar con la red muerta: se da por caído ya).
            if (Date.now() - st.pingMs > MUDO_MS) { try { ws.close(); } catch (e) {} caido(ws); }
            return;
          }
          st.pingMs = Date.now();
          ws.send('{"type":"ping"}');
        }, PING_MS);
      };

      const entrada = (ws, m) => {
        if (m.ok) {
          st.listo = true;
          BAWs.abiertos++;
          st.intentos = 0;
          st.reintento = REINTENTO_MIN_MS;
          latir(ws);
          estado("conectado");
          if (o.alEntrar) o.alEntrar(m);
          return;
        }
        if (m.error_codigo === "version") { location.reload(); return; } // la página quedó vieja
        if (SE_ESPERA.has(m.error_codigo)) {
          st.proxima = st.reintento;
          st.reintento = Math.min(st.reintento * 2, REINTENTO_MAX_MS);
          if (o.alEspera) o.alEspera(m);
        } else {
          st.cerrado = true;
          if (o.alRechazo) o.alRechazo(m);
        }
        try { ws.close(); } catch (e) {}
      };

      const conectar = () => {
        clearTimeout(st.timer);
        if (st.cerrado || st.ws) return;
        const auth = o.auth ? o.auth() : {};
        if (!auth) return; // la página todavía no sabe con qué entrar (reabrir() cuando sepa)
        const proto = location.protocol === "https:" ? "wss:" : "ws:";
        let ws;
        try { ws = new WebSocket(proto + "//" + location.host + "/ws" + (o.rol ? "?" + o.rol : "")); }
        catch (e) { st.ws = {}; caido(st.ws); return; }
        st.ws = ws;
        estado("conectando");
        ws.onopen = () => ws.send(JSON.stringify(Object.assign({}, auth, { v: 1, type: "auth" })));
        ws.onmessage = (ev) => {
          if (st.ws !== ws) return;
          st.oido = Date.now();
          let m;
          try { m = JSON.parse(ev.data); } catch (e) { return; }
          if (!m || typeof m !== "object" || m.type === "pong") return;
          if (m.type === "auth" && !st.listo) return entrada(ws, m);
          if (m.type === "recargar" && !o.alRecargar) { location.reload(); return; }
          document.dispatchEvent(new CustomEvent("ba-canal", { detail: m }));
          if (m.type === "recargar") o.alRecargar();
          else if (o.alMensaje) o.alMensaje(m);
        };
        ws.onclose = () => caido(ws);
        ws.onerror = () => { try { ws.close(); } catch (e) {} };
      };

      // Volvió la red o la pestaña al frente: probar ya, sin esperar la espera creciente.
      const ya = () => {
        if (st.cerrado) return;
        if (st.listo) { mandar({ type: "ping" }); return; }
        if (!st.ws) { st.intentos = 0; conectar(); }
      };
      addEventListener("online", ya);
      document.addEventListener("visibilitychange", () => { if (!document.hidden) ya(); });

      conectar();
      return {
        mandar,
        listo: () => st.listo,
        estado: () => st.estado,
        cerrar() {
          st.cerrado = true;
          clearTimeout(st.timer);
          const ws = st.ws;
          try { if (ws && ws.close) ws.close(); } catch (e) {}
          if (ws) caido(ws);
          else estado("cerrado");
        },
        /// Después de un rechazo (o de cerrar): otra vez, con lo que diga `auth` ahora.
        reabrir() {
          st.cerrado = false;
          st.intentos = 0;
          st.reintento = REINTENTO_MIN_MS;
          if (!st.ws) conectar();
        },
      };
    },
  });

  // ==========================================================================
  // Las SUPERFICIES: la pantalla, el carrusel, el teleprompter, los rótulos, el
  // tablero. Mandarle algo a un televisor y que el comando salga bien NO quiere
  // decir que se esté viendo; con el canal, la página DICE cómo está (late cada
  // 5 s con qué muestra) y el operador le CAMBIA lo que muestra sin volver a
  // castear. Si se cae, lo que está mostrando se queda ahí: una pantalla de sala
  // en negro porque el servidor tosió es peor que una que muestra algo de hace
  // un minuto.
  //
  //   BACanal.abrir({
  //     tipo: "carrusel",
  //     que: () => "paso 2/3",        // qué estoy mostrando (para el operador)
  //     onMostrar: (url) => {...},    // el operador me cambió el contenido
  //     onRecargar: () => location.reload(),
  //     onAviso: (m) => {...},        // cualquier otro mensaje del servidor
  //     onEstado: (conectado) => {...}, // el canal se abrió o se cayó (opcional)
  //     extra: { p: "atril" },        // más datos para la sala (opcional)
  //     idClave: "prompter-control",  // con qué se recuerda el id (opcional; si no, el tipo)
  //     clave: "…",                   // la de las superficies reservadas (rótulos, corrección)
  //     onClaveMal: () => {...},      // la sala no la aceptó (el canal vuelve a probar solo)
  //     onClaveBien: () => {...},     // después de un «clave mal», la sala la aceptó (opcional)
  //   });
  // ==========================================================================
  const LATIDO_MS = 5000;

  /// Un id estable entre recargas: si no, cada recarga aparecería como una
  /// superficie nueva y la lista del operador se llenaría de fantasmas.
  function miId(tipo) {
    const clave = "ba-superficie-" + tipo;
    const nuevo = () => tipo + "-" + Math.random().toString(36).slice(2, 10);
    try {
      let v = localStorage.getItem(clave);
      if (!v) { v = nuevo(); localStorage.setItem(clave, v); }
      return v;
    } catch (e) {
      // Sin almacenamiento (modo privado, televisor viejo): id nuevo en cada
      // carga. Vale menos, pero la página sigue avisando que está viva.
      return nuevo();
    }
  }

  window.BACanal = {
    abrir(op) {
      const tipo = op.tipo || "pantalla";
      // `idClave`: dos páginas del mismo tipo en el mismo aparato (el control y la
      // vista del teleprompter) necesitan ids distintos.
      const id = miId(op.idClave || tipo);
      let latido = 0, claveMal = false, dentro = false;
      const late = () => {
        let que = "";
        try { que = String(op.que ? op.que() : ""); } catch (e) { que = ""; }
        c.mandar({ type: "late", tipo, que });
      };
      const c = BAWs.abrir({
        rol: "superficie",
        auth: () => {
          const a = { tipo, id };
          for (const [k, v] of Object.entries(op.extra || {}))
            if (v !== undefined && v !== null && v !== "") a[k] = String(v);
          if (op.clave) a.k = op.clave;
          return a;
        },
        alEntrar: () => {
          if (claveMal) { claveMal = false; if (op.onClaveBien) op.onClaveBien(); }
          late();
          clearInterval(latido);
          latido = setInterval(late, LATIDO_MS);
          dentro = true;
          if (op.onEstado) op.onEstado(true);
        },
        alCaer: () => {
          clearInterval(latido);
          // Solo si estaba adentro: un intento que no llegó a entrar (una clave que no
          // sirve) no pisa lo que la página ya dijo.
          if (!dentro) return;
          dentro = false;
          if (op.onEstado) op.onEstado(false);
        },
        alEspera: (m) => {
          if (m.error_codigo !== "clave_mal") return;
          claveMal = true;
          if (op.onClaveMal) op.onClaveMal();
        },
        alRecargar: () => (op.onRecargar ? op.onRecargar() : location.reload()),
        alMensaje: (m) => {
          if (m.type === "mostrar" && m.url && op.onMostrar) {
            op.onMostrar(m.url);
            late(); // que el operador vea el cambio enseguida
          } else if (op.onAviso) {
            // Cualquier otro mensaje se le pasa tal cual a la página. El canal
            // no tiene que saber de tally, ni de lo que venga después: solo
            // entrega. Así una superficie nueva no obliga a tocar esto.
            op.onAviso(m);
          }
        },
      });
      // Al volver de estar escondida, avisa enseguida en vez de esperar.
      document.addEventListener("visibilitychange", () => { if (!document.hidden) late(); });
      return { late, mandar: c.mandar, conectado: c.listo, cerrar: c.cerrar };
    },
  };
})();
