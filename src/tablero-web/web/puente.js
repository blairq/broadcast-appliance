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

  // Los textos de la página. t() solo MARCA el texto (para tools/i18n/extraer.py, contexto
  // web/control): se traduce al mostrarlo (tr), porque los primeros avisos salen antes de que
  // esté cargado el idioma; cuando carga, el aviso que esté a la vista se vuelve a pintar.
  const t = (s) => s;
  const tr = (s) => (s && window.BAi18n ? BAi18n.t(s) : s || "");

  let avisoActual = null; // [titulo, texto, opciones]: para volver a pintarlo en su idioma
  function aviso(titulo, texto, opciones = {}) {
    avisoActual = [titulo, texto, opciones];
    $("aviso").classList.remove("oculto");
    $("aviso").classList.toggle("encima", !!opciones.encima);
    $("aviso-titulo").textContent = tr(titulo);
    $("aviso-texto").textContent = typeof texto === "function" ? texto() : tr(texto); // función: armado de partes
    $("girando").classList.toggle("oculto", !opciones.girando);
    // enlace: "emparejar" (a /control/, que empareja y vuelve acá) o "clasico" (el control web
    // en desuso, con ?clasico: /control/ manda a los de perfil «todo» acá y rebotarían).
    const enlace = $("aviso-enlace");
    enlace.classList.toggle("oculto", !opciones.enlace);
    enlace.href = opciones.enlace === "clasico" ? "/control/?clasico" : "/control/";
    enlace.textContent = opciones.enlace === "clasico" ? tr(t("Usar el control web clásico")) : tr(t("Emparejar en el control web"));
  }
  const sinAviso = () => { avisoActual = null; $("aviso").classList.add("oculto"); };

  // --- El idioma ---------------------------------------------------------------------------
  // El de la interfaz de la app (Configuración → Idioma; ?idioma= o lo elegido en este
  // navegador ganan), con los mismos catálogos que la app (/comun/i18n/, ver comun/i18n.js).
  // Se carga ANTES del módulo: el traductor de Qt (main.cpp) le pregunta a baPuente.traducir
  // desde el primer qsTr.
  const idiomaListo = (window.BAi18n
    ? BAi18n.iniciar({ contexto: "web/control", preferir: "app" }).catch((e) => console.warn("tablero: idioma", e))
    : Promise.resolve()).then(() => {
    $("reconectando").textContent = tr(t("Reconectando…"));
    if (avisoActual) aviso(...avisoActual);
  });
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
  // Escondida (otra pestaña al frente, el teléfono bloqueado): la app deja de mandar.
  document.addEventListener("visibilitychange", () => { if (modulo && pedido) modulo.baVisible(!document.hidden); });
  document.addEventListener("webkitfullscreenchange", avisarCompleta);

  window.baPuente = {
    // qsTr/tr del tablero (main.cpp, TraductorWeb): el contexto es el de lupdate (el nombre
    // del .qml o de la clase). "" = sin traducción (Qt deja el original).
    traducir(contexto, texto, desambiguacion, n) {
      if (!window.BAi18n) return "";
      const op = { contexto };
      if (desambiguacion) op.desambiguacion = desambiguacion;
      if (n >= 0) op.n = n;
      const r = BAi18n.t(texto, op);
      return r === texto ? "" : r;
    },
    // El idioma elegido: {codigo, rtl} (para QLocale y la dirección de la interfaz).
    idioma() {
      const p = window.BAi18n && BAi18n.perfil;
      return JSON.stringify(p ? { codigo: p.codigo, rtl: !!p.rtl } : { codigo: "es", rtl: false });
    },
    pantallaCompleta() {
      try {
        if (enCompleta()) {
          (document.exitFullscreen || document.webkitExitFullscreen).call(document);
          return "";
        }
        const pedir = docEl.requestFullscreen || docEl.webkitRequestFullscreen;
        if (!pedir) {
          return tr(t("En este aparato la pantalla completa se logra agregando la página a la pantalla de inicio (Compartir → Agregar a inicio) y abriéndola desde ahí."));
        }
        const r = pedir.call(docEl);
        if (r && r.catch) r.catch(() => {});
        return "";
      } catch (e) {
        return String(e && e.message ? e.message : e);
      }
    },
    // Una vista sola en una pestaña nueva (el mismo .wasm; el token sale del localStorage).
    abrirPestana(vista) {
      if (!/^[a-z0-9-]{1,32}$/.test(vista)) return;
      window.open(location.pathname + "?vista=" + vista, "ba-" + vista);
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
      else if (que === "error") aviso(t("No se puede mostrar el tablero"), detalle, { enlace: "clasico" });
    },
  };

  // --- La conexión ------------------------------------------------------------------------
  const token = leer(CLAVE_TOKEN);
  if (!token) {
    aviso(t("Este aparato no está emparejado"),
          t("Abrí el control web desde el QR de la app (Servicios → Control web), emparejalo y volvé acá."),
          { enlace: "emparejar" });
    return;
  }
  aviso(t("Cargando el tablero…"), "", { girando: true });
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
      // m.error viene de la app en español (marcado en web/comun): tr() lo traduce igual.
      const porQue = m.error || t("Lo quitaron en la app.");
      aviso(t("Este aparato ya no está habilitado"),
            () => tr(porQue) + " " + tr(t("Emparejalo de nuevo desde el QR de la app.")), { enlace: "emparejar" });
    },
    alCaer: () => {
      if (autenticado) seCayo = true;
      autenticado = false;
      if (seCayo) reconectando(true);
    },
  });

  // --- El módulo ---------------------------------------------------------------------------
  idiomaListo.then(() => qtLoad({
    locateFile: (f) => f + (version ? "?v=" + version : ""),
    qt: {
      containerElements: [$("pantalla")],
      // Qt 6.8 exporta el arranque como <target>_entry (ba_tablero_entry); antes era
      // createQtAppInstance. Sin él, qtLoad falla con «config.qt.entryFunction is required».
      entryFunction: window.ba_tablero_entry || window.createQtAppInstance,
      onExit: (e) => aviso(t("El tablero se cerró"), e && e.text ? e.text : "", { enlace: "clasico" }),
    },
  })).then((r) => {
    modulo = r.instance || r;
    window.baModulo = modulo;
    pedirModelo();
  }).catch((e) => {
    aviso(t("No se pudo cargar el tablero"), String(e && e.message ? e.message : e), { enlace: "clasico" });
  });
})();
