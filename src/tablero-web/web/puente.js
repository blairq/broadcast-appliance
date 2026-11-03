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
// SI SE CORTA (se reinicia la app o el gateway, cambia el evento, se cae la red): un aviso chico
// «Reconectando…» arriba y el canal vuelve a probar solo, con espera creciente de 0,5 s a 5 s,
// para siempre. Cuando vuelve a entrar (con el mismo token, sin PIN) se recarga la página una
// vez: la app pudo haber reiniciado y los objetos de la réplica ya no son los mismos (el .wasm
// sale de la caché, es un segundo). Lo mismo si la app reinició sin que se cayera el gateway:
// lo dice un «auth» nuevo. Si el token ya no vale (lo quitaron en la app), se dice y se
// ofrece emparejar de nuevo.
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
  const reconectando = (si) => $("reconectando").classList.toggle("oculto", !si);
  const recargar = () => { reconectando(true); location.reload(); };

  function pedirModelo() {
    if (pedido || !modulo || !autenticado) return;
    pedido = true;
    modulo.baConectado();
    for (const t of cola.splice(0)) modulo.baRecibir(t);
  }

  // --- El video ---------------------------------------------------------------------------
  // Los cuadros llegan en JPEG. Los descomprime el navegador (createImageBitmap: fuera del
  // hilo de la página, con lo que tenga el aparato) y al módulo le llegan los píxeles. Si
  // mientras tanto llegan más de la misma vista, se queda solo con el último: nunca se
  // acumula atraso.
  const enCurso = new Map(), pendiente = new Map();
  let lienzo = null, ctx = null;
  const dec = (window.baMedidasDec = {});
  async function cuadro(m) {
    if (enCurso.get(m.clave)) { pendiente.set(m.clave, m); return; }
    enCurso.set(m.clave, true);
    const t0 = performance.now();
    try {
      const b = atob(m.jpg), bin = new Uint8Array(b.length);
      for (let i = 0; i < b.length; i++) bin[i] = b.charCodeAt(i);
      const bmp = await createImageBitmap(new Blob([bin], { type: "image/jpeg" }));
      const w = bmp.width, h = bmp.height;
      if (!lienzo) { lienzo = new OffscreenCanvas(w, h); ctx = lienzo.getContext("2d", { willReadFrequently: true }); }
      if (lienzo.width !== w || lienzo.height !== h) { lienzo.width = w; lienzo.height = h; }
      ctx.drawImage(bmp, 0, 0);
      bmp.close();
      const px = ctx.getImageData(0, 0, w, h).data;
      if (modulo) modulo.baCuadro(m.clave, px, w, h, m.t, m.s);
      const x = (dec[m.clave] = dec[m.clave] || { n: 0, suma: 0 });
      x.n++; x.suma += performance.now() - t0; x.prom = Math.round(x.suma / x.n * 10) / 10;
    } catch (e) {
      console.warn("tablero: cuadro", e);
    }
    enCurso.set(m.clave, false);
    const sig = pendiente.get(m.clave);
    if (sig) { pendiente.delete(m.clave); cuadro(sig); }
  }

  // Lo que el aparato recuerda en la app (su registro: la disposición del tablero), como la
  // superficie HTML: se manda el objeto entero, con lo de las otras páginas del aparato.
  let preferencias = {};
  const PREF_LOCAL = "ba-tablero-";

  // Pantalla completa: la Fullscreen API (con su prefijo en Safari). En iPhone no existe para
  // la página: lo que hay es agregarla a la pantalla de inicio (abre sin la barra).
  const docEl = document.documentElement;
  const enCompleta = () => !!(document.fullscreenElement || document.webkitFullscreenElement);
  const avisarCompleta = () => { if (modulo) modulo.baPantallaCompleta(enCompleta()); };
  document.addEventListener("fullscreenchange", avisarCompleta);
  document.addEventListener("webkitfullscreenchange", avisarCompleta);

  window.baPuente = {
    pantallaCompleta() {
      try {
        if (enCompleta()) {
          (document.exitFullscreen || document.webkitExitFullscreen).call(document);
          return "";
        }
        const pedir = docEl.requestFullscreen || docEl.webkitRequestFullscreen;
        if (!pedir) {
          return "En este aparato la pantalla completa se logra agregando la página a la pantalla de inicio "
            + "(Compartir → Agregar a inicio) y abriéndola desde ahí.";
        }
        const r = pedir.call(docEl);
        if (r && r.catch) r.catch(() => {});
        return "";
      } catch (e) {
        return String(e && e.message ? e.message : e);
      }
    },
    leerLocal(k) { try { return localStorage.getItem(PREF_LOCAL + k) || ""; } catch (_) { return ""; } },
    guardarLocal(k, v) { try { localStorage.setItem(PREF_LOCAL + k, v); } catch (_) {} },
    // La disposición del tablero de este aparato (en su registro de la app).
    tablero() { return preferencias && preferencias.tablero ? JSON.stringify(preferencias.tablero) : ""; },
    guardarTablero(texto) {
      try {
        preferencias = Object.assign({}, preferencias, { tablero: JSON.parse(texto) });
        if (canal && autenticado) canal.mandar({ type: "preferencias", datos: preferencias });
      } catch (e) { console.warn("tablero: no se pudo guardar la disposición", e); }
    },
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
    esperaMinMs: 500, esperaMaxMs: 5000, reintentoMinMs: 500, reintentoMaxMs: 5000,
    alEntrar: (m) => {
      if (seCayo) { recargar(); return; }
      autenticado = true;
      preferencias = (m && m.preferencias) || {};
      pedirModelo();
    },
    alEspera: () => { if (seCayo || autenticado) reconectando(true); },
    alMensaje: (m) => {
      // Un «auth» nuevo con la conexión viva: la app arrancó de nuevo (o cambió el perfil).
      if (m.type === "auth" && m.ok && pedido) { recargar(); return; }
      if (m.type !== "replica") return;
      if (m.op === "cuadro") { if (modulo && pedido) cuadro(m); return; }
      const t = JSON.stringify(m);
      if (modulo && pedido) modulo.baRecibir(t);
      else cola.push(t);
    },
    alRechazo: (m) => {
      reconectando(false);
      if (m.error_codigo !== "intentos") borrar(CLAVE_TOKEN);
      aviso("Este aparato ya no está habilitado",
            (m.error || "Lo quitaron en la app.") + " Emparejalo de nuevo desde el QR de la app.", { enlace: true });
    },
    alCaer: () => {
      if (autenticado) seCayo = true;
      autenticado = false;
      if (seCayo) reconectando(true);
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
