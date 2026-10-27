// La botonera de la tablet (ver el comentario de index.html).
"use strict";

(async function () {
  const CLAVE_TOKEN = "ba-botonera-token";
  const CLAVE_DISP = "ba-botonera-dispositivo";
  const CLAVE_NOMBRE = "ba-botonera-nombre";
  const $ = (id) => document.getElementById(id);

  let ws = null;
  let estado = null;            // el último {type: "estado", ...} de la app
  let reintento = 1000;
  let latido = null;
  const celdas = new Map();     // "x,y" -> {el, def, actualizar}
  const enviarCada = new Map(); // throttling de faders: "x,y" -> {t, pendiente}

  function guardar(k, v) { try { if (v === null) localStorage.removeItem(k); else localStorage.setItem(k, v); } catch (e) { /* modo privado */ } }
  function leer(k) { try { return localStorage.getItem(k); } catch (e) { return null; } }

  // --- Avisos -------------------------------------------------------------------
  let avisoTimer = null;
  function aviso(texto, error) {
    const a = $("aviso");
    a.textContent = texto;
    a.classList.toggle("error", !!error);
    a.classList.remove("oculto");
    clearTimeout(avisoTimer);
    avisoTimer = setTimeout(() => a.classList.add("oculto"), error ? 4000 : 2500);
  }
  function mensaje(titulo, texto, conOlvidar) {
    $("mensajeTitulo").textContent = titulo;
    $("mensajeTexto").textContent = texto;
    $("olvidar").classList.toggle("oculto", !conOlvidar);
    $("mensaje").classList.remove("oculto");
  }
  function sinMensaje() { $("mensaje").classList.add("oculto"); }

  // --- Emparejamiento --------------------------------------------------------------
  function codigoDelFragmento() {
    const m = /(?:^#|&)p=([0-9a-f]{32})/.exec(location.hash);
    return m ? m[1] : "";
  }

  function mostrarEmparejar(codigo) {
    $("emparejar").classList.remove("oculto");
    $("nombre").value = leer(CLAVE_NOMBRE) || "";
    $("pedir").onclick = () => pedirEmparejamiento(codigo);
  }

  async function pedirEmparejamiento(codigo) {
    const nombre = ($("nombre").value || "").trim() || "Tablet";
    guardar(CLAVE_NOMBRE, nombre);
    $("pedir").disabled = true;
    let r;
    try {
      r = await fetch("/api/botonera/emparejar", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ codigo: codigo, nombre: nombre }),
      });
    } catch (e) {
      $("pedir").disabled = false;
      return aviso(t("No hay conexión con el aparato."), true);
    }
    const j = await r.json().catch(() => ({}));
    if (!r.ok || !j.solicitud) {
      $("pedir").disabled = false;
      return aviso(t(j.error) || t("No se pudo pedir el emparejamiento."), true);
    }
    // El código ya se usó: fuera de la barra de direcciones.
    history.replaceState(null, "", location.pathname);
    $("emparejar").classList.add("oculto");
    mensaje(t("Esperando la aprobación"), BAi18n.llenar(t("El operador tiene que aprobar «%1» en la app (página Botonera)."), nombre));
    esperarAprobacion(j.solicitud, Date.now());
  }

  async function esperarAprobacion(solicitud, desde) {
    if (Date.now() - desde > 5 * 60 * 1000) return mensaje(t("Venció"), t("Nadie aprobó el emparejamiento. Escaneá un QR nuevo."));
    let j = {};
    try {
      const r = await fetch("/api/botonera/emparejar?solicitud=" + encodeURIComponent(solicitud), { cache: "no-store" });
      j = await r.json();
    } catch (e) { /* se reintenta */ }
    if (j.estado === "aprobada" && j.token) {
      guardar(CLAVE_TOKEN, j.token);
      guardar(CLAVE_DISP, j.dispositivo || "");
      sinMensaje();
      return conectar();
    }
    if (j.estado === "rechazada") return mensaje(t("Rechazado"), t("El operador no aprobó esta tablet."));
    if (j.estado === "invalido") return mensaje(t("Código inválido"), (t(j.error) || t("El código venció o ya se usó.")) + " " + t("Escaneá un QR nuevo."));
    if (j.estado === "desconocida") return mensaje(t("Se perdió el pedido"), t("Escaneá un QR nuevo."));
    setTimeout(() => esperarAprobacion(solicitud, desde), 1000);
  }

  // --- Conexión ------------------------------------------------------------------
  function conectar() {
    const token = leer(CLAVE_TOKEN);
    if (!token) return mensaje(t("Tablet sin emparejar"), t("Escaneá el QR que muestra la app en la página Botonera → Emparejar tablet."));
    const url = (location.protocol === "https:" ? "wss://" : "ws://") + location.host + "/ws?botonera=1";
    try { ws = new WebSocket(url); } catch (e) { return reconectar(); }
    ws.onopen = () => {
      ws.send(JSON.stringify({ type: "auth", token: token }));
    };
    ws.onmessage = (ev) => {
      let m;
      try { m = JSON.parse(ev.data); } catch (e) { return; }
      recibir(m);
    };
    ws.onclose = () => {
      ws = null;
      $("conexion").className = "conexion apagada";
      clearInterval(latido);
      reconectar();
    };
  }
  let reconectando = null;
  function reconectar() {
    if (reconectando || !leer(CLAVE_TOKEN)) return;
    reconectando = setTimeout(() => { reconectando = null; conectar(); }, reintento);
    reintento = Math.min(reintento * 2, 10000);
  }
  function mandar(m) {
    if (ws && ws.readyState === 1) ws.send(JSON.stringify(m));
  }

  function recibir(m) {
    if (m.type === "auth") {
      if (!m.ok) {
        guardar(CLAVE_TOKEN, null);
        mensaje(t("Tablet sin emparejar"), t(m.error) || t("El token no vale."), false);
        if (ws) ws.close();
        return;
      }
      reintento = 1000;
      $("conexion").className = "conexion viva";
      sinMensaje();
      clearInterval(latido);
      latido = setInterval(() => mandar({ type: "ping" }), 10000);
      return;
    }
    if (m.type === "estado") {
      if (m.error) return mensaje(t("Sin perfil"), t(m.error), true);
      sinMensaje();
      dibujar(m);
      return;
    }
    if (m.type === "confirmar") return pedirConfirmacion(m);
    if (m.type === "aviso") return aviso(t(m.texto || ""), m.error);
  }

  // --- Dibujo ----------------------------------------------------------------------
  function dibujar(e) {
    const mismaPagina = estado && estado.pagina && e.pagina && estado.pagina.id === e.pagina.id &&
      estado.pagina.columnas === e.pagina.columnas && estado.pagina.filas === e.pagina.filas &&
      estado.perfil.id === e.perfil.id && JSON.stringify(estructura(estado)) === JSON.stringify(estructura(e));
    estado = e;
    $("perfil").textContent = e.perfil ? e.perfil.nombre : t("Botonera");
    $("pagina").textContent = e.pagina ? e.pagina.nombre : "";
    dibujarPaginas(e);
    if (!mismaPagina) armarGrilla(e);
    for (const c of e.celdas || []) {
      const k = c.x + "," + c.y;
      const it = celdas.get(k);
      if (it) it.actualizar(c);
    }
    for (const [k, it] of celdas) it.el.classList.toggle("esperando", e.confirmar === k);
  }
  function estructura(e) {
    return (e.celdas || []).map((c) => [c.x, c.y, c.ancho, c.alto, c.tipo]);
  }

  function dibujarPaginas(e) {
    const nav = $("paginas");
    nav.textContent = "";
    if (!e.paginas || e.paginas.length < 2) return;
    for (const p of e.paginas) {
      const b = document.createElement("button");
      b.type = "button";
      b.textContent = p.nombre;
      if (e.pagina && p.id === e.pagina.id) b.className = "actual";
      b.onclick = () => mandar({ type: "pagina", id: p.id });
      nav.appendChild(b);
    }
  }

  function armarGrilla(e) {
    const g = $("grilla");
    g.textContent = "";
    celdas.clear();
    g.style.gridTemplateColumns = "repeat(" + e.pagina.columnas + ", 1fr)";
    g.style.gridTemplateRows = "repeat(" + e.pagina.filas + ", 1fr)";
    for (const c of e.celdas || []) {
      const el = document.createElement("div");
      el.className = "celda " + c.tipo;
      el.style.gridColumn = (c.x + 1) + " / span " + c.ancho;
      el.style.gridRow = (c.y + 1) + " / span " + c.alto;
      g.appendChild(el);
      const fab = { boton: boton, fader: fader, perilla: perilla, medidor: medidor, texto: texto, reloj: reloj }[c.tipo] || texto;
      celdas.set(c.x + "," + c.y, fab(el, c));
    }
    ajustarTextos();
  }

  // --- Caras animadas ----------------------------------------------------------------
  function urlCuadro(id, n) { return "caras/" + encodeURIComponent(id) + "/" + String(n).padStart(4, "0") + ".jpg"; }
  const precargadas = new Set();
  function precargar(k) {
    if (precargadas.has(k.id)) return;
    precargadas.add(k.id);
    for (let i = 0; i < k.cuadros; ++i) { const im = new Image(); im.src = urlCuadro(k.id, i); }
  }
  // Un solo reloj para todas: cada cara muestra el cuadro de este momento.
  setInterval(() => {
    const ahora = Date.now();
    for (const im of document.querySelectorAll("img.cara[data-anim]")) {
      const [id, n, fps] = im.dataset.anim.split("|");
      const src = urlCuadro(id, Math.floor(ahora * Number(fps) / 1000) % Number(n));
      if (im.getAttribute("src") !== src) im.setAttribute("src", src);
    }
  }, 1000 / 16);

  function estilo(el, c) {
    // Sin color propio, el material del tema (temas.css); con color, el que manda la app.
    el.style.background = c.fondo || "";
    el.style.color = c.color || "";
    el.classList.toggle("con-fondo", !!c.fondo);
    el.classList.toggle("titilar", !!c.titilar);
    el.classList.toggle("activo", !!c.activo);
    // La cara (data: URL que viene con la celda), el borde que puede titilar solo,
    // la insignia de la esquina (turno, «P») y el atenuado (sin unidad, sin central).
    // Una cara propia (c.cara = {id, cuadros, fps}: foto o video corto) se trae
    // por URL en 288 px (el navegador la guarda: no cambia nunca); si no, la
    // data: URL chica de c.imagen.
    let cara = el.querySelector(":scope > img.cara");
    const propia = c.cara && /^[\w-]+$/.test(c.cara.id || "") ? c.cara : null;
    const src = propia ? urlCuadro(propia.id, 0) : c.imagen;
    if (src) {
      if (!cara) {
        cara = document.createElement("img");
        cara.className = "cara";
        cara.alt = "";
        el.prepend(cara);
      }
      if (!propia || (propia.cuadros || 1) <= 1) {
        delete cara.dataset.anim;
        if (cara.getAttribute("src") !== src) cara.src = src;
      } else {
        const anim = propia.id + "|" + propia.cuadros + "|" + (propia.fps || 8);
        if (cara.dataset.anim !== anim) {
          cara.dataset.anim = anim;
          precargar(propia);
          cara.src = src;
        }
      }
    } else if (cara) {
      cara.remove();
    }
    el.classList.toggle("con-cara", !!src);
    el.style.setProperty("--borde-celda", c.borde || "transparent");
    el.classList.toggle("con-borde", !!c.borde);
    el.classList.toggle("borde-titila", !!c.borde_titilar);
    el.classList.toggle("atenuada", !!c.atenuar);
    let ins = el.querySelector(":scope > .insignia");
    if (c.insignia) {
      if (!ins) {
        ins = document.createElement("span");
        ins.className = "insignia";
        el.appendChild(ins);
      }
      ins.textContent = c.insignia;
    } else if (ins) {
      ins.remove();
    }
    if (c.tamano_texto) {
      el.dataset.tamanoFijo = "1";
      el.style.fontSize = c.tamano_texto + "px";
    }
  }

  function boton(el, c) {
    const ic = document.createElement("span");
    ic.className = "icono-celda";
    const tx = document.createElement("span");
    el.append(ic, tx);
    let apretado = false;
    const soltar = () => {
      if (!apretado) return;
      apretado = false;
      el.classList.remove("apretada");
      mandar({ type: "soltar", x: c.x, y: c.y });
    };
    el.addEventListener("pointerdown", (ev) => {
      ev.preventDefault();
      el.setPointerCapture(ev.pointerId);
      apretado = true;
      el.classList.add("apretada");
      if (navigator.vibrate) navigator.vibrate(10);
      mandar({ type: "presionar", x: c.x, y: c.y });
    });
    el.addEventListener("pointerup", soltar);
    el.addEventListener("pointercancel", soltar);
    return {
      el: el,
      actualizar(n) {
        estilo(el, n);
        ic.textContent = n.icono || "";
        ic.classList.toggle("oculto", !n.icono);
        tx.textContent = n.texto || "";
      },
    };
  }

  /// Manda el valor de un fader como mucho cada 33 ms (el último gana).
  function mandarValor(c, v) {
    const k = c.x + "," + c.y;
    const e = enviarCada.get(k) || { t: 0, pendiente: null, timer: null };
    enviarCada.set(k, e);
    const ahora = performance.now();
    if (ahora - e.t >= 33) {
      e.t = ahora;
      mandar({ type: "valor", x: c.x, y: c.y, v: v });
    } else {
      e.pendiente = v;
      if (!e.timer) e.timer = setTimeout(() => {
        e.timer = null;
        e.t = performance.now();
        if (e.pendiente !== null) mandar({ type: "valor", x: c.x, y: c.y, v: e.pendiente });
        e.pendiente = null;
      }, 34);
    }
  }

  function fmt(v) {
    if (typeof v !== "number") return "";
    return Math.abs(v) >= 100 ? v.toFixed(0) : v.toFixed(1);
  }

  function fader(el, c) {
    const rot = document.createElement("div");
    rot.className = "rotulo";
    const riel = document.createElement("div");
    riel.className = "riel";
    const relleno = document.createElement("div");
    relleno.className = "relleno";
    const nivel = document.createElement("div");
    nivel.className = "nivel";
    const mango = document.createElement("div");
    mango.className = "mango";
    riel.append(relleno, nivel, mango);
    const val = document.createElement("div");
    val.className = "valor";
    el.append(rot, riel, val);
    let def = c;
    let local = null;        // mientras se arrastra, manda lo de la mano
    let arrastre = null;
    const frac = (v) => (def.max > def.min ? (v - def.min) / (def.max - def.min) : 0);
    const pintar = (v) => {
      const f = Math.max(0, Math.min(1, frac(v)));
      relleno.style.height = (f * 100) + "%";
      mango.style.bottom = (f * 100) + "%";
      val.textContent = fmt(v);
    };
    // Toma suave: no salta adonde se tocó, sigue desde donde estaba.
    riel.addEventListener("pointerdown", (ev) => {
      ev.preventDefault();
      riel.setPointerCapture(ev.pointerId);
      arrastre = { y: ev.clientY, v: typeof def.valor === "number" ? def.valor : def.min };
      local = arrastre.v;
    });
    riel.addEventListener("pointermove", (ev) => {
      if (!arrastre) return;
      const alto = riel.getBoundingClientRect().height || 1;
      let v = arrastre.v + ((arrastre.y - ev.clientY) / alto) * (def.max - def.min);
      const paso = def.paso || 1;
      v = Math.max(def.min, Math.min(def.max, Math.round(v / paso) * paso));
      if (v !== local) {
        local = v;
        pintar(v);
        mandarValor(def, v);
      }
    });
    const fin = () => { arrastre = null; setTimeout(() => { local = null; }, 300); };
    riel.addEventListener("pointerup", fin);
    riel.addEventListener("pointercancel", fin);
    return {
      el: el,
      actualizar(n) {
        def = n;
        estilo(el, n);
        rot.textContent = n.texto || "";
        if (local === null) pintar(typeof n.valor === "number" ? n.valor : n.min);
        if (typeof n.nivel === "number") {
          // Nivel en dBFS (-60..0) al lado del fader.
          const f = Math.max(0, Math.min(1, (n.nivel + 60) / 60));
          nivel.style.height = (f * 100) + "%";
          nivel.className = "nivel" + (n.nivel > -3 ? " pico" : n.nivel > -12 ? " alto" : "");
        } else {
          nivel.style.height = "0";
        }
      },
    };
  }

  function perilla(el, c) {
    const rot = document.createElement("div");
    rot.className = "rotulo";
    const disco = document.createElement("div");
    disco.className = "disco";
    const marca = document.createElement("div");
    marca.className = "marca";
    disco.appendChild(marca);
    const val = document.createElement("div");
    val.className = "valor";
    el.append(rot, disco, val);
    let def = c;
    let arrastre = null;
    const pintar = (v) => {
      const f = def.max > def.min ? Math.max(0, Math.min(1, (v - def.min) / (def.max - def.min))) : 0.5;
      marca.style.transform = "rotate(" + (-135 + f * 270) + "deg)";
      val.textContent = def.modo === "relativo" ? "" : fmt(v);
    };
    // Arrastre vertical: cada 12 px es un paso.
    el.addEventListener("pointerdown", (ev) => {
      ev.preventDefault();
      el.setPointerCapture(ev.pointerId);
      arrastre = { y: ev.clientY, pasos: 0 };
    });
    el.addEventListener("pointermove", (ev) => {
      if (!arrastre) return;
      const pasos = Math.trunc((arrastre.y - ev.clientY) / 12);
      const d = pasos - arrastre.pasos;
      if (d !== 0) {
        arrastre.pasos = pasos;
        mandar({ type: "girar", x: def.x, y: def.y, delta: d });
      }
    });
    const fin = (ev) => {
      // Un toque sin girar es un botón (si la perilla tiene acción al apretar).
      if (arrastre && arrastre.pasos === 0 && ev.type === "pointerup") {
        mandar({ type: "presionar", x: def.x, y: def.y });
        mandar({ type: "soltar", x: def.x, y: def.y });
      }
      arrastre = null;
    };
    el.addEventListener("pointerup", fin);
    el.addEventListener("pointercancel", fin);
    return {
      el: el,
      actualizar(n) {
        def = n;
        estilo(el, n);
        rot.textContent = n.texto || "";
        pintar(typeof n.valor === "number" ? n.valor : n.min);
      },
    };
  }

  function medidor(el, c) {
    const rot = document.createElement("div");
    rot.className = "rotulo";
    const riel = document.createElement("div");
    riel.className = "riel";
    const relleno = document.createElement("div");
    relleno.className = "relleno";
    const pico = document.createElement("div");
    pico.className = "pico";
    riel.append(relleno, pico);
    const val = document.createElement("div");
    val.className = "valor";
    el.append(rot, riel, val);
    let picoV = -Infinity;
    let picoT = 0;
    return {
      el: el,
      actualizar(n) {
        estilo(el, n);
        rot.textContent = n.texto || "";
        const v = typeof n.valor === "number" ? n.valor : n.min;
        const f = n.max > n.min ? Math.max(0, Math.min(1, (v - n.min) / (n.max - n.min))) : 0;
        relleno.style.height = (f * 100) + "%";
        // Pico retenido 1,5 s.
        const ahora = performance.now();
        if (v >= picoV || ahora - picoT > 1500) { picoV = v; picoT = ahora; }
        const fp = n.max > n.min ? Math.max(0, Math.min(1, (picoV - n.min) / (n.max - n.min))) : 0;
        pico.style.bottom = (fp * 100) + "%";
        val.textContent = fmt(v);
      },
    };
  }

  function texto(el, c) {
    const tx = document.createElement("span");
    el.appendChild(tx);
    return {
      el: el,
      actualizar(n) {
        estilo(el, n);
        tx.textContent = n.texto || "";
      },
    };
  }

  const relojes = new Set();
  function reloj(el, c) {
    const tx = document.createElement("span");
    el.appendChild(tx);
    const it = {
      el: el,
      rotulo: c.texto || "",
      actualizar(n) {
        estilo(el, n);
        it.rotulo = n.texto || "";
        it.tic();
      },
      tic() {
        const d = new Date();
        const h = String(d.getHours()).padStart(2, "0") + ":" + String(d.getMinutes()).padStart(2, "0") + ":" +
          String(d.getSeconds()).padStart(2, "0");
        tx.textContent = (it.rotulo ? it.rotulo + "\n" : "") + h;
      },
    };
    relojes.add(it);
    return it;
  }
  setInterval(() => {
    for (const r of relojes) if (r.el.isConnected) r.tic(); else relojes.delete(r);
  }, 1000);

  /// El tamaño del texto según el tamaño de la celda (sin medir cada texto).
  function ajustarTextos() {
    for (const [, it] of celdas) {
      if (it.el.dataset.tamanoFijo) continue;
      const r = it.el.getBoundingClientRect();
      it.el.style.fontSize = Math.max(11, Math.min(r.width / 7, r.height / 4, 34)) + "px";
    }
  }
  window.addEventListener("resize", ajustarTextos);

  // --- Confirmación de aire -----------------------------------------------------------
  // Mantener apretado 1 s: un toque al pasar no alcanza para cambiar el aire.
  let confirmacion = null;
  function pedirConfirmacion(m) {
    const [x, y] = String(m.celda || "").split(",").map(Number);
    confirmacion = { x: x, y: y, vence: Date.now() + (m.vence_ms || 5000) };
    $("confirmarTexto").textContent = m.texto || t("Cambia el aire: confirmá");
    $("confirmar").classList.remove("oculto");
    const barra = $("confirmarBarra").firstElementChild;
    barra.style.width = "0";
    const si = $("confirmarSi");
    let inicio = 0;
    let anim = null;
    const cancelarMantener = () => { inicio = 0; cancelAnimationFrame(anim); barra.style.width = "0"; };
    const paso = () => {
      if (!inicio) return;
      const f = Math.min(1, (performance.now() - inicio) / 1000);
      barra.style.width = (f * 100) + "%";
      if (f >= 1) {
        inicio = 0;
        cerrarConfirmacion();
        mandar({ type: "confirmar", x: confirmacion.x, y: confirmacion.y });
        return;
      }
      anim = requestAnimationFrame(paso);
    };
    si.onpointerdown = (ev) => { ev.preventDefault(); si.setPointerCapture(ev.pointerId); inicio = performance.now(); anim = requestAnimationFrame(paso); };
    si.onpointerup = cancelarMantener;
    si.onpointercancel = cancelarMantener;
    $("confirmarNo").onclick = () => { cerrarConfirmacion(); mandar({ type: "cancelar" }); };
    setTimeout(() => { if (confirmacion && Date.now() >= confirmacion.vence) cerrarConfirmacion(); }, (m.vence_ms || 5000) + 50);
  }
  function cerrarConfirmacion() {
    $("confirmar").classList.add("oculto");
  }

  // --- Arranque ------------------------------------------------------------------
  $("pantallaCompleta").onclick = () => {
    const d = document.documentElement;
    if (document.fullscreenElement) document.exitFullscreen();
    else if (d.requestFullscreen) d.requestFullscreen().catch(() => {});
  };
  $("olvidar").onclick = () => {
    guardar(CLAVE_TOKEN, null);
    guardar(CLAVE_DISP, null);
    if (ws) ws.close();
    mensaje(t("Tablet sin emparejar"), t("Escaneá el QR que muestra la app en la página Botonera → Emparejar tablet."));
  };
  // Que la pantalla no se apague (si el navegador lo permite).
  if (navigator.wakeLock) {
    const pedir = () => navigator.wakeLock.request("screen").catch(() => {});
    pedir();
    document.addEventListener("visibilitychange", () => { if (document.visibilityState === "visible") pedir(); });
  }

  // Los textos, en el idioma del evento.
  await BAi18n.iniciar({ contexto: "web/botonera", preferir: "evento" }); // neuro.js se traduce solo (ba-idioma)
  const codigo = codigoDelFragmento();
  if (codigo) mostrarEmparejar(codigo);
  else conectar();
})();
