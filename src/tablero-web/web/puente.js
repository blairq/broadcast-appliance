// ============================================================================
// El puente entre la página y el tablero (Qt para WebAssembly).
//
// La conexión con la app es la del control web de siempre: el canal (/comun/canal.js) por
// /ws?control con el token que el aparato guardó al emparejarse en /control/ (QR o PIN, una
// sola vez; ver docs/funciones/TABLERO-WEB.md). Lo de la réplica ({type:"replica"}) va y
// viene entre el canal y el módulo:
//   app → Module.baRecibir(texto)        módulo → window.baPuente.enviar(texto)
// Al autenticarse se le avisa al módulo (Module.baConectado) y pide el modelo.
//
// Si el canal se cae y vuelve, se recarga la página: la app pudo haber reiniciado y los
// objetos de antes ya no son los mismos (el .wasm sale de la caché del navegador).
// ============================================================================
(() => {
  "use strict";
  const CLAVE_TOKEN = "ba-control-token";
  const leer = (k) => { try { return localStorage.getItem(k); } catch (_) { return null; } };
  const borrar = (k) => { try { localStorage.removeItem(k); } catch (_) {} };
  const version = document.documentElement.dataset.version || "";
  const $ = (id) => document.getElementById(id);

  let modulo = null;      // la instancia de emscripten, cuando terminó de arrancar
  let canal = null;
  let autenticado = false;
  let seCayo = false;
  let pedido = false;     // ya se le pidió el modelo a la app
  const cola = [];        // lo que llegó antes de que el módulo estuviera

  function aviso(titulo, texto, opciones = {}) {
    $("aviso").classList.remove("oculto");
    $("aviso").classList.toggle("encima", !!opciones.encima);
    $("aviso-titulo").textContent = titulo;
    $("aviso-texto").textContent = texto || "";
    $("girando").classList.toggle("oculto", !opciones.girando);
    $("aviso-enlace").classList.toggle("oculto", !opciones.enlace);
  }
  const sinAviso = () => $("aviso").classList.add("oculto");

  function pedirModelo() {
    if (pedido || !modulo || !autenticado) return;
    pedido = true;
    modulo.baConectado();
    for (const t of cola.splice(0)) modulo.baRecibir(t);
  }

  window.baPuente = {
    enviar(texto) {
      if (!canal) return;
      try { canal.mandar(JSON.parse(texto)); } catch (e) { console.warn("tablero: no se pudo mandar", e); }
    },
    estado(que, detalle) {
      if (que === "listo") { sinAviso(); window.baTableroListo = true; }
      else if (que === "error") aviso("No se puede mostrar el tablero", detalle, { enlace: true });
    },
  };

  // --- La conexión ------------------------------------------------------------------------
  // #t=<token>: lo guarda y lo saca de la barra (el fragmento no viaja al servidor). Es lo
  // mismo que emparejar: lo usan las pruebas, que emparejan por MCP.
  const frag = new URLSearchParams(location.hash.slice(1));
  if (/^[0-9a-f]{64}$/.test(frag.get("t") || "")) {
    try { localStorage.setItem(CLAVE_TOKEN, frag.get("t")); } catch (_) {}
    history.replaceState(null, "", location.pathname + location.search);
  }
  const token = leer(CLAVE_TOKEN);
  if (!token) {
    aviso("Este aparato no está emparejado",
          "Abrí el control web desde el QR de la app (Servicios → Control web), emparejalo y volvé acá.",
          { enlace: true });
    return;
  }
  aviso("Cargando el tablero…", "", { girando: true });
  canal = BAWs.abrir({
    rol: "control",
    auth: () => ({ token }),
    alEntrar: () => {
      if (seCayo) { location.reload(); return; }
      autenticado = true;
      pedirModelo();
    },
    alMensaje: (m) => {
      if (m.type !== "replica") return;
      const t = JSON.stringify(m);
      if (modulo && pedido) modulo.baRecibir(t);
      else cola.push(t);
    },
    alRechazo: (m) => {
      if (m.error_codigo !== "intentos") borrar(CLAVE_TOKEN);
      aviso("Este aparato ya no está habilitado", m.error || "Emparejalo de nuevo desde el QR de la app.", { enlace: true });
    },
    alCaer: () => {
      if (autenticado) seCayo = true;
      autenticado = false;
      aviso("Reconectando…", "Se cortó la conexión con la app.", { girando: true, encima: true });
    },
  });

  // --- El módulo ---------------------------------------------------------------------------
  qtLoad({
    locateFile: (f) => f + (version ? "?v=" + version : ""),
    qt: {
      containerElements: [$("pantalla")],
      entryFunction: window.createQtAppInstance,
      onExit: (e) => aviso("El tablero se cerró", e && e.text ? e.text : "", { enlace: true }),
    },
  }).then((r) => {
    modulo = r.instance || r;
    window.baModulo = modulo;
    pedirModelo();
  }).catch((e) => {
    aviso("No se pudo cargar el tablero", String(e && e.message ? e.message : e), { enlace: true });
  });
})();
