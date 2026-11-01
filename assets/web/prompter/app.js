// ============================================================================
// Teleprompter — la tablet del orador y el control del celular. Cómo se mueve
// el texto (desde dónde, a qué velocidad, desde cuándo, con el reloj de la
// sala): comun/prompter-vista.js, la misma vista que el panel del invitado.
//
// NO PREGUNTA. El estado llega por el canal de control (comun/canal.js) cuando
// cambia: entero (con el texto) al conectarse y cuando se carga un discurso, y
// sin el texto en lo demas. El mismo canal es lo que le dice al operador que
// esta tablet esta prendida.
// ============================================================================
(async () => {
  "use strict";
  // Los textos, en el idioma del evento.
  await BAi18n.iniciar({ contexto: "web/prompter", preferir: "evento" });

  const esControl = location.hash.includes("control");
  // CUAL TELEPROMPTER. Hay varios, cada uno con su texto: /prompter/ es el
  // principal y /prompter/<id>/ cada uno de los otros. La pagina es la misma.
  const partes = location.pathname.split("/").filter(Boolean); // ["prompter", "<id>"?]
  const prompterId = partes.length > 1 && partes[1] !== "index.html" ? partes[1] : "";
  const clave = new URLSearchParams(location.hash.replace(/^#/, "").replace("control&", "")).get("k") || "";

  // QUIEN SOY. El operador necesita ver, antes del discurso, si la tablet del
  // atril esta prendida: lo dice el canal (con la vista y el nombre, que lo
  // escribe quien la usa, "Atril 1", desde la propia pagina).
  const vista = esControl ? "control" : "orador";
  const guardado = (k, v) => {
    try {
      if (v !== undefined) localStorage.setItem(k, v);
      return localStorage.getItem(k) || "";
    } catch (e) {
      return ""; // modo privado o storage bloqueado: se anda igual, sin recordar
    }
  };
  // El nombre del aparato ("Atril 1") es de cada teleprompter: la misma tablet
  // puede ser el atril de uno y despues el de otro.
  const claveNombre = "ba-prompter-nombre" + (prompterId ? "-" + prompterId : "");
  let miNombre = guardado(claveNombre);

  // El texto, cómo corre y lo demás (comun/prompter-vista.js).
  const vista_ = BAPrompterVista.crear({ lienzo: $("lienzo"), texto: $("texto") });
  const estado = vista_.e;
  const posicionAhora = vista_.posicion;

  // --- Conversacion con el servidor ----------------------------------------
  // Una vez al abrir (por si el canal tarda): lo demas llega por el canal.
  async function leer() {
    const r = await fetch("/api/prompter" + (prompterId ? "?p=" + encodeURIComponent(prompterId) : ""),
                          { cache: "no-store" });
    const d = await r.json();
    aplicar(d);
    return d;
  }

  async function mandar(cmd) {
    const r = await fetch("/api/prompter", {
      method: "POST",
      headers: { "Content-Type": "application/json", "X-BA-Clave": clave },
      body: JSON.stringify(Object.assign({ prompter: prompterId }, cmd)),
    });
    if (r.status === 403) {
      if ($("c-error")) $("c-error").textContent =
        t("Este enlace no tiene la clave del control. Pedile al operador el enlace o el QR de nuevo.");
      return null;
    }
    const d = await r.json();
    aplicar(d);
    return d;
  }

  function aplicar(d) {
    if (!d || typeof d !== "object") return;
    if (vista_.aplicar(d)) {
      $("vacio").classList.toggle("hidden", !!estado.texto);
      $("lienzo").classList.toggle("hidden", !estado.texto);
    }
    pintar();
  }

  // --- Vista del orador ------------------------------------------------------
  function dibujarOrador() {
    if (!estado.texto) return;
    vista_.dibujar();
    // Lo que falta, a la velocidad de ahora.
    $("reloj").textContent = estado.ppm > 0 ? mmss(vista_.faltaSeg()) : "";
  }

  // Lo del presentador: cuánto le queda al momento al aire (con el reloj del servidor)
  // y qué viene. Pasado de tiempo, «+0:42». Se pinta aunque no haya discurso cargado.
  let presentadorPintado = "";
  function dibujarPresentador() {
    const p = estado.presentador;
    const caja = $("presentador");
    if (!caja) return;
    let ahora = "", queda = "", despues = "", pasado = false;
    if (p && p.alAire) {
      ahora = p.alAire.nombre || "";
      if (p.alAire.hastaMs) {
        const seg = (p.alAire.hastaMs - BAReloj.ahora()) / 1000;
        pasado = seg < 0;
        queda = (pasado ? "+" : "") + mmss(Math.abs(seg));
      }
      if (p.proximo && p.proximo.nombre) despues = BAi18n.llenar(t("Después: %1"), p.proximo.nombre);
    }
    const firma = ahora + "|" + queda + "|" + despues + "|" + pasado;
    if (firma === presentadorPintado) return;
    presentadorPintado = firma;
    caja.classList.toggle("hidden", !ahora);
    caja.classList.toggle("pasado", pasado);
    $("p-ahora").textContent = ahora;
    $("p-queda").textContent = queda;
    $("p-despues").textContent = despues;
  }

  function pintarOrador() {
    document.body.classList.toggle("espejado", estado.espejo);
    document.documentElement.style.setProperty("--tamano", (3 + estado.tamano * 0.9).toFixed(1) + "vw");
    const av = $("aviso");
    av.textContent = estado.aviso;
    av.classList.toggle("hidden", !estado.aviso);
  }

  // --- Control ---------------------------------------------------------------
  let arrastrando = false;

  function pintarControl() {
    $("c-titulo").textContent = estado.titulo || t("Sin discurso");
    $("c-estado").textContent = estado.palabras === 0
      ? t("sin texto")
      : (estado.corriendo ? t("corriendo") : t("parado"));
    $("c-ppm").textContent = estado.ppm;
    $("c-dura").textContent = estado.duracionSeg ? BAi18n.llenar(t("el discurso entero dura %1"), mmss(estado.duracionSeg)) : "";
    const play = $("c-play");
    play.textContent = estado.corriendo ? t("❚❚ Pausar") : t("▶ Arrancar");
    play.classList.toggle("corriendo", estado.corriendo);
  }

  function dibujarControl() {
    if (arrastrando || estado.palabras === 0) return;
    const pos = posicionAhora();
    $("c-barra").value = Math.round((pos / estado.palabras) * 1000);
    $("c-pos").textContent = BAi18n.llenar(t("palabra %1 de %2"), Math.floor(pos), estado.palabras);
    $("c-falta").textContent = estado.ppm > 0 ? BAi18n.llenar(t("faltan %1"), mmss(vista_.faltaSeg())) : "";
  }

  function pintar() {
    if (esControl) pintarControl();
    else pintarOrador();
  }

  /// En que parrafo esta la posicion actual (para saltar al anterior y al
  /// siguiente sin que el operador tenga que contarlos).
  function parrafoActual() {
    const pos = posicionAhora();
    let i = 0;
    for (let k = 0; k < estado.parrafos.length; k++)
      if (estado.parrafos[k] <= pos + 0.5) i = k;
    return i;
  }

  function conectarControl() {
    $("c-play").addEventListener("click", () => mandar({ corriendo: !estado.corriendo }));
    $("c-panico").addEventListener("click", () => mandar({ corriendo: false }));
    $("c-lento").addEventListener("click", () => mandar({ ppm: estado.ppm - 10 }));
    $("c-rapido").addEventListener("click", () => mandar({ ppm: estado.ppm + 10 }));
    $("c-inicio").addEventListener("click", () => mandar({ posicion: 0 }));
    $("c-atras").addEventListener("click", () => mandar({ parrafo: Math.max(0, parrafoActual() - 1) }));
    $("c-adelante").addEventListener("click", () => mandar({ parrafo: parrafoActual() + 1 }));
    // El aviso viaja tal cual lo lee el orador: en el idioma de esta página. La clave
    // (data-aviso) queda en el original.
    const AVISOS = { "Redondeá": t("Redondeá"), "Te escuchamos bajo": t("Te escuchamos bajo"),
                     "Más despacio": t("Más despacio"), "Mirá a cámara": t("Mirá a cámara") };
    for (const b of document.querySelectorAll(".aviso-btn"))
      b.addEventListener("click", () => mandar({ aviso: AVISOS[b.dataset.aviso] ?? b.dataset.aviso }));

    const barra = $("c-barra");
    // Mientras se arrastra no se pisa el valor con lo que dice el servidor.
    const tomar = () => { arrastrando = true; };
    const soltar = () => {
      arrastrando = false;
      mandar({ posicion: (barra.value / 1000) * estado.palabras });
    };
    barra.addEventListener("pointerdown", tomar);
    barra.addEventListener("touchstart", tomar, { passive: true });
    barra.addEventListener("change", soltar);
    barra.addEventListener("input", () => {
      arrastrando = true;
      $("c-pos").textContent = BAi18n.llenar(t("palabra %1 de %2"), Math.round((barra.value / 1000) * estado.palabras), estado.palabras);
    });
  }

  function conectarOrador() {
    // Lo de la tablet es COMO ve el texto, no como corre: se manda igual al
    // servidor para que el operador vea lo mismo, pero sin clave no pasa nada
    // malo si no llega (se aplica local igual).
    const local = (cambio) => {
      Object.assign(estado, cambio);
      pintarOrador();
      mandar(cambio).catch(() => {});
    };
    $("mas").addEventListener("click", () => local({ tamano: Math.min(10, estado.tamano + 1) }));
    $("menos").addEventListener("click", () => local({ tamano: Math.max(1, estado.tamano - 1) }));
    $("espejo").addEventListener("click", () => local({ espejo: !estado.espejo }));
    // Tocar el texto lo para: el reflejo de cualquiera cuando algo va mal.
    $("lienzo").addEventListener("click", () => mandar({ corriendo: !estado.corriendo }).catch(() => {}));
  }

  // El nombre del aparato: se escribe una vez, viaja al conectarse el canal y,
  // si se cambia, por el canal.
  let canal = null;
  function conectarNombre(campo) {
    if (!campo) return;
    campo.value = miNombre;
    campo.addEventListener("change", () => {
      miNombre = campo.value.trim().slice(0, 40);
      guardado(claveNombre, miNombre);
      if (canal) canal.mandar({ type: "nombre", nombre: miNombre });
    });
  }

  // --- Arranque ---------------------------------------------------------------
  if (esControl) {
    $("orador").classList.add("hidden");
    $("control").classList.remove("hidden");
    document.title = t("Control del teleprompter");
    conectarControl();
    conectarNombre($("c-nombre"));
  } else {
    conectarOrador();
    conectarNombre($("nombre"));
    // Que la tablet no se apague en medio de un discurso.
    if (navigator.wakeLock) {
      const pedir = () => navigator.wakeLock.request("screen").catch(() => {});
      pedir();
      document.addEventListener("visibilitychange", () => { if (!document.hidden) pedir(); });
    }
  }

  // Lo que cambia llega por el canal; entre aviso y aviso cada vista calcula
  // la posicion sola.
  leer().catch(() => {});
  canal = window.BACanal.abrir({
    tipo: "prompter",
    idClave: "prompter-" + vista + (prompterId ? "-" + prompterId : ""),
    extra: { p: prompterId, vista, nombre: miNombre },
    que: () => (estado.titulo || "") + (estado.corriendo ? " ▶" : ""),
    onAviso: (m) => { if (m.type === "prompter") aplicar(m.estado); },
  });

  (function cuadro() {
    if (esControl) dibujarControl();
    else {
      dibujarOrador();
      dibujarPresentador();
    }
    requestAnimationFrame(cuadro);
  })();
})();
