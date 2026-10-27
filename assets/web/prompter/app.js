// ============================================================================
// Teleprompter — la tablet del orador y el control del celular.
//
// COMO SE MUEVE EL TEXTO. El servidor no manda la posicion cuadro a cuadro:
// manda DESDE DONDE (`posicion`, en palabras), A QUE VELOCIDAD (`ppm`) y DESDE
// CUANDO (`desdeMs`). Cada vista calcula sola donde va, en su propio
// requestAnimationFrame. Asi un hipo de la red no se ve como un tiron en el
// renglon que alguien esta leyendo en vivo, y si la red se corta del todo, el
// texto sigue andando solo.
//
// EL RELOJ. `ahoraMs` del servidor viene en cada respuesta: con el se calcula
// el desfase contra el reloj del aparato, porque una tablet con la hora
// corrida arrancaria el texto en cualquier lado.
//
// LA POSICION VA EN PALABRAS, no en pixeles: la tablet y el celular tienen
// pantallas distintas y tienen que hablar del mismo lugar del texto.
// ============================================================================
(async () => {
  "use strict";
  // Los textos, en el idioma del evento.
  await BAi18n.iniciar({ contexto: "web/prompter", preferir: "evento" });

  const $ = (id) => document.getElementById(id);
  const esControl = location.hash.includes("control");
  const clave = new URLSearchParams(location.hash.replace(/^#/, "").replace("control&", "")).get("k") || "";

  // QUIEN SOY. El operador necesita ver, antes del discurso, si la tablet del
  // atril esta prendida. No hace falta un canal aparte: se manda la identidad
  // en el mismo pedido de estado que ya se hace cada medio segundo. El id se
  // guarda para que recargar la pagina no aparezca como un aparato nuevo, y el
  // nombre lo escribe quien la usa ("Atril 1") desde la propia pagina.
  const vista = esControl ? "control" : "orador";
  const guardado = (k, v) => {
    try {
      if (v !== undefined) localStorage.setItem(k, v);
      return localStorage.getItem(k) || "";
    } catch (e) {
      return ""; // modo privado o storage bloqueado: se anda igual, sin recordar
    }
  };
  let miId = guardado("ba-prompter-id");
  if (!miId) {
    miId = Math.random().toString(36).slice(2, 10) + Date.now().toString(36).slice(-4);
    guardado("ba-prompter-id", miId);
  }
  let miNombre = guardado("ba-prompter-nombre");

  const estado = {
    texto: "",
    titulo: "",
    palabras: 0,
    parrafos: [],
    posicion: 0,
    ppm: 140,
    corriendo: false,
    desdeMs: 0,
    tamano: 5,
    espejo: false,
    aviso: "",
    version: -1,
    desfaseMs: 0, // reloj del servidor menos el de este aparato
  };

  // La posicion AHORA: lo ultimo que dijo el servidor mas lo que corrio desde
  // entonces. Es la unica cuenta que importa en todo el archivo.
  function posicionAhora() {
    if (!estado.corriendo) return estado.posicion;
    const ahora = Date.now() + estado.desfaseMs;
    const minutos = (ahora - estado.desdeMs) / 60000;
    return Math.min(estado.palabras, estado.posicion + minutos * estado.ppm);
  }

  function mmss(seg) {
    const s = Math.max(0, Math.round(seg));
    return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0");
  }

  // --- Conversacion con el servidor ----------------------------------------
  async function leer() {
    const quien =
      "?vista=" + vista + "&id=" + encodeURIComponent(miId) +
      (miNombre ? "&nombre=" + encodeURIComponent(miNombre) : "");
    const r = await fetch("/api/prompter" + quien, { cache: "no-store" });
    const d = await r.json();
    aplicar(d);
    return d;
  }

  async function mandar(cmd) {
    const r = await fetch("/api/prompter", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(Object.assign({ clave }, cmd)),
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
    const textoNuevo = d.texto !== estado.texto;
    estado.desfaseMs = (d.ahoraMs || Date.now()) - Date.now();
    estado.texto = d.texto || "";
    estado.titulo = d.titulo || "";
    estado.palabras = d.palabras || 0;
    estado.parrafos = d.parrafos || [];
    estado.posicion = d.posicion || 0;
    estado.ppm = d.ppm || 140;
    estado.corriendo = !!d.corriendo;
    estado.desdeMs = d.desdeMs || 0;
    estado.tamano = d.tamano || 5;
    estado.espejo = !!d.espejo;
    estado.aviso = d.aviso || "";
    estado.duracionSeg = d.duracionSeg || 0;
    estado.version = d.version;
    if (textoNuevo) armarTexto();
    pintar();
  }

  // --- Vista del orador ------------------------------------------------------
  let spans = [];

  // El texto se parte en palabras una sola vez: mover 3000 spans por cuadro es
  // caro, pero crearlos en cada cuadro es imposible.
  function armarTexto() {
    const cont = $("texto");
    if (!cont) return;
    cont.textContent = "";
    spans = [];
    if (!estado.texto) {
      $("vacio").classList.remove("hidden");
      $("lienzo").classList.add("hidden");
      return;
    }
    $("vacio").classList.add("hidden");
    $("lienzo").classList.remove("hidden");

    // Se recorre el texto conservando los espacios y los saltos de linea tal
    // cual: el orador escribio los renglones donde queria respirar.
    const partes = estado.texto.split(/(\s+)/);
    for (const p of partes) {
      if (p === "") continue;
      if (/^\s+$/.test(p)) {
        cont.appendChild(document.createTextNode(p));
      } else {
        const s = document.createElement("span");
        s.textContent = p;
        cont.appendChild(s);
        spans.push(s);
      }
    }
  }

  let ultimoMarcado = -1;

  function dibujarOrador() {
    if (!spans.length) return;
    const pos = posicionAhora();
    const i = Math.min(spans.length - 1, Math.max(0, Math.floor(pos)));

    // Marcar lo leido es caro si se hace palabra por palabra en cada cuadro:
    // solo se tocan las que cambiaron desde el cuadro anterior.
    if (i !== ultimoMarcado) {
      const desde = Math.min(i, ultimoMarcado < 0 ? 0 : ultimoMarcado);
      const hasta = Math.max(i, ultimoMarcado < 0 ? spans.length - 1 : ultimoMarcado);
      for (let k = desde; k <= hasta && k < spans.length; k++) {
        spans[k].className = k < i ? "leido" : (k === i ? "aqui" : "");
      }
      ultimoMarcado = i;
    }

    // El desplazamiento: la palabra actual tiene que quedar en la guia.
    const lienzo = $("lienzo");
    const objetivoY = lienzo.clientHeight * 0.38;
    const palabra = spans[i];
    const y = palabra.offsetTop + palabra.offsetHeight / 2;
    $("texto").style.transform = "translateY(" + (objetivoY - y) + "px)";

    // Lo que falta, a la velocidad de ahora.
    const faltan = Math.max(0, estado.palabras - pos);
    $("reloj").textContent = estado.ppm > 0 ? mmss((faltan / estado.ppm) * 60) : "";
  }

  function pintarOrador() {
    document.body.classList.toggle("espejado", estado.espejo);
    document.documentElement.style.setProperty("--tamano", (3 + estado.tamano * 0.9).toFixed(1) + "vw");
    const av = $("aviso");
    av.textContent = estado.aviso;
    av.classList.toggle("hidden", !estado.aviso);
    ultimoMarcado = -1; // que el proximo cuadro repinte todo
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
    const faltan = Math.max(0, estado.palabras - pos);
    $("c-falta").textContent = estado.ppm > 0 ? BAi18n.llenar(t("faltan %1"), mmss((faltan / estado.ppm) * 60)) : "";
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

  // El nombre del aparato: se escribe una vez y viaja en cada pedido de
  // estado. No se manda aparte porque no hace falta: el proximo pedido (medio
  // segundo) ya lo lleva.
  function conectarNombre(campo) {
    if (!campo) return;
    campo.value = miNombre;
    campo.addEventListener("change", () => {
      miNombre = campo.value.trim().slice(0, 40);
      guardado("ba-prompter-nombre", miNombre);
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

  // Se consulta dos veces por segundo: alcanza, porque entre consulta y
  // consulta cada vista calcula la posicion sola.
  const refrescar = () => leer().catch(() => {});
  refrescar();
  setInterval(refrescar, 500);

  (function cuadro() {
    if (esControl) dibujarControl();
    else dibujarOrador();
    requestAnimationFrame(cuadro);
  })();
})();
