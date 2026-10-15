/* Las tarjetas del dashboard web. Cada una:
     { id, titulo, temas: [lo que mira], herramientas: [lo que llama], ancha?,
       crear(contenido, api), actualizar(tema, datos) }
   `api` es BA.control: llamar/hacer(herramienta, args), puede(herramienta), avisar.
   Los temas los empuja la app (/ws?control) solo cuando cambian. Sin estilos en
   línea (la CSP no los deja): clases de control.css y el.style desde acá. */
(() => {
  "use strict";
  const BA = (window.BA = window.BA || {});

  // --- Utilidades ------------------------------------------------------------------------
  const el = (tag, cls, texto) => {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    if (texto !== undefined) e.textContent = texto;
    return e;
  };
  const boton = (texto, accion, attrs = {}) => {
    const b = el("ba-boton", attrs.clase || "", texto);
    for (const [k, v] of Object.entries(attrs)) if (k !== "clase") v === true ? b.setAttribute(k, "") : v !== false && b.setAttribute(k, v);
    if (accion) b.addEventListener("accion", accion);
    return b;
  };
  const maxDb = (v) => (Array.isArray(v) ? Math.max(...v, -100) : typeof v === "number" ? v : -100);
  const ms = (v) => (v === null || v === undefined ? "—" : v >= 1000 ? (v / 1000).toFixed(2) + " s" : Math.round(v) + " ms");
  const vacio = (contenido, texto) => contenido.replaceChildren(el("p", "vacio", texto));
  // Reutilizar elementos por clave (no rehacer el DOM en cada empuje: el dedo pierde el control).
  const sincronizar = (cont, claves, crear, actualizar) => {
    const vivos = new Map([...cont.children].map((c) => [c.dataset.clave, c]));
    let anterior = null;
    for (const k of claves) {
      let c = vivos.get(k);
      if (!c) { c = crear(k); c.dataset.clave = k; }
      vivos.delete(k);
      actualizar(c, k);
      if (c.previousElementSibling !== anterior || c.parentNode !== cont) anterior ? anterior.after(c) : cont.prepend(c);
      anterior = c;
    }
    for (const [, c] of vivos) c.remove();
  };

  BA.tarjetas = [
    // --- Escenas: armar, cortar, auto y la T-bar ------------------------------------------
    {
      id: "escenas", titulo: "Escenas", temas: ["escenas"], ancha: true,
      herramientas: ["scene_preview", "scene_cut", "scene_take", "scene_switch"],
      crear(c, api) {
        this.estado = el("div", "fila estado-escenas");
        this.prog = el("ba-tally"); this.prog.setAttribute("estado", "aire");
        this.prev = el("ba-tally"); this.prev.setAttribute("estado", "armado");
        this.estado.append(this.prog, this.prev);
        this.grilla = el("div", "grilla-escenas");
        const acciones = el("div", "fila acciones-escenas");
        acciones.append(
          boton("CORTE", () => api.hacer("scene_cut", {}), { grande: true, variante: "aire", clase: "lleno" }),
          boton("AUTO", () => api.hacer("scene_take", {}), { grande: true, variante: "acento", clase: "lleno" }));
        this.tbar = el("ba-slider", "tbar");
        for (const [k, v] of [["etiqueta", "T-bar"], ["min", "0"], ["max", "1"], ["paso", "0.01"], ["valor", "0"]]) this.tbar.setAttribute(k, v);
        this.tbar.addEventListener("cambio", (e) => api.llamar("transicion_manual", { t: e.detail.valor }).catch(() => {}));
        this.tbar.addEventListener("final", () => { api.llamar("transicion_manual", { soltar: true }).catch(() => {}); this.tbar.valor = 0; });
        c.append(this.estado, this.grilla, acciones, this.tbar);
        this.api = api;
      },
      actualizar(_, d) {
        this.prog.setAttribute("etiqueta", "Programa · " + (d.current || "—"));
        this.prev.setAttribute("etiqueta", "Armada · " + (d.armada || "—"));
        const escenas = (d.scenes || []).filter((s) => !s.name.startsWith("__"));
        sincronizar(this.grilla, escenas.map((s) => s.name), (nombre) => {
          // Un toque arma (preview); mantener apretado la pone al aire directo.
          const b = boton(nombre, () => this.api.hacer("scene_preview", { name: nombre }));
          let reloj = 0;
          b.addEventListener("pointerdown", () => { reloj = setTimeout(() => { reloj = 0; this.api.hacer("scene_switch", { name: nombre }); }, 700); });
          b.addEventListener("pointerup", () => clearTimeout(reloj));
          b.addEventListener("pointerleave", () => clearTimeout(reloj));
          return b;
        }, (b, nombre) => {
          const s = escenas.find((x) => x.name === nombre);
          b.toggleAttribute("activo", !!(s.live || s.armada));
          b.setAttribute("variante", s.live ? "aire" : s.armada ? "armado" : "");
        });
      },
    },

    // --- PTZ: joystick, zoom y posiciones ------------------------------------------------
    {
      id: "ptz", titulo: "Cámara PTZ", temas: ["ptz"],
      herramientas: ["ptz_mover", "ptz_parar", "ptz_ir_posicion"],
      crear(c, api) {
        this.api = api;
        const cab = el("div", "fila");
        this.selector = el("select", "selector-camara");
        this.selector.addEventListener("change", () => { this.camara = this.selector.value; this.pintarPosiciones(); });
        this.tallyCam = el("ba-chip"); this.tallyCam.textContent = "—";
        cab.append(this.selector, this.tallyCam);
        const cuerpo = el("div", "ptz-cuerpo");
        this.joy = el("ba-joystick");
        let ultimo = 0;
        this.joy.addEventListener("mover", (e) => {
          if (!this.camara || Date.now() - ultimo < 90) return; // ~10 por segundo alcanza
          ultimo = Date.now();
          api.llamar("ptz_mover", { entrada: this.camara, pan: e.detail.x, tilt: e.detail.y, zoom: 0, duracion_ms: 250 }).catch(() => {});
        });
        this.joy.addEventListener("soltar", () => this.camara && api.llamar("ptz_parar", { entrada: this.camara }).catch(() => {}));
        const zoom = el("div", "columna zoom");
        const z = (dir) => {
          const b = boton(dir > 0 ? "Zoom +" : "Zoom −", null, { mantener: true });
          b.addEventListener("presionar", () => this.camara && api.llamar("ptz_mover", { entrada: this.camara, pan: 0, tilt: 0, zoom: dir, duracion_ms: 400 }).catch(() => {}));
          b.addEventListener("soltar", () => this.camara && api.llamar("ptz_parar", { entrada: this.camara }).catch(() => {}));
          return b;
        };
        zoom.append(z(1), z(-1));
        cuerpo.append(this.joy, zoom);
        this.posiciones = el("div", "posiciones");
        c.append(cab, cuerpo, el("h3", "", "Posiciones"), this.posiciones);
      },
      pintarPosiciones() {
        const cam = (this.camaras || []).find((x) => x.entrada === this.camara);
        this.tallyCam.setAttribute("estado", cam && cam.al_aire ? "aire" : "apagado");
        this.tallyCam.textContent = cam ? (cam.al_aire ? "al aire" : cam.estado || "lista") : "—";
        const pos = (cam && cam.posiciones ? cam.posiciones : []).filter((p) => !p.interna);
        if (!pos.length) { vacio(this.posiciones, "Sin posiciones guardadas."); return; }
        sincronizar(this.posiciones, pos.map((p) => p.nombre), (nombre) =>
          boton(nombre, () => this.api.hacer("ptz_ir_posicion", { entrada: this.camara, posicion: nombre }), { compacto: true }),
          (b, nombre) => { const p = pos.find((x) => x.nombre === nombre); b.title = p.comentario || ""; });
      },
      actualizar(_, d) {
        this.camaras = d.camaras || [];
        const nombres = this.camaras.map((x) => x.entrada);
        if (this.selector.options.length !== nombres.length || [...this.selector.options].some((o, i) => o.value !== nombres[i])) {
          this.selector.replaceChildren(...nombres.map((n) => new Option(n, n)));
        }
        if (!this.camara || !nombres.includes(this.camara)) this.camara = nombres[0] || "";
        this.selector.value = this.camara;
        this.pintarPosiciones();
      },
    },

    // --- Mezclador: un fader por canal, mute y loudness -----------------------------------
    {
      id: "audio", titulo: "Mezclador", temas: ["audio"], ancha: true,
      herramientas: ["audio_set"],
      crear(c, api) {
        this.api = api;
        this.loud = el("div", "fila loudness");
        this.canales = el("div", "canales");
        c.append(this.loud, this.canales);
      },
      actualizar(_, d) {
        const l = d.loudness || {};
        this.loud.replaceChildren(
          ...[["M", l.m], ["S", l.s], ["I", l.i], ["TP", l.tp]].map(([k, v]) => {
            const ch = el("ba-chip"); ch.setAttribute("estado", l.fuera_de_objetivo ? "aviso" : "ok");
            ch.textContent = k + " " + (v > -100 ? v.toFixed(1) : "—");
            return ch;
          }),
          el("span", "suave", l.objetivo ? "objetivo " + l.objetivo.lufs + " LUFS" : ""));
        const entradas = d.entradas || [];
        sincronizar(this.canales, entradas.map((e) => e.nombre), (nombre) => {
          const canal = el("div", "canal");
          const f = el("ba-fader");
          f.setAttribute("etiqueta", nombre);
          f.addEventListener("final", (e) => this.api.hacer("audio_set", { name: nombre, volumeDb: e.detail.valor }).catch(() => {}));
          f.addEventListener("cambio", () => { f.dataset.tocando = Date.now(); });
          const mute = boton("M", () => this.api.hacer("audio_set", { name: nombre, muted: !mute.hasAttribute("activo") }), { compacto: true });
          canal.append(f, mute);
          canal.fader = f; canal.mute = mute;
          return canal;
        }, (canal, nombre) => {
          const e = entradas.find((x) => x.nombre === nombre);
          // Mientras el dedo lo mueve (o recién), no se pisa con lo que dice la app.
          if (!canal.fader.dataset.tocando || Date.now() - canal.fader.dataset.tocando > 800) canal.fader.valor = e.volumen_db;
          canal.fader.nivel = { rms: maxDb(e.rms_db), pico: maxDb(e.pico_db) };
          canal.mute.toggleAttribute("activo", !!e.mute);
          canal.mute.setAttribute("variante", e.mute ? "aire" : "");
        });
      },
    },

    // --- Salidas: transmitir y grabar ---------------------------------------------------
    {
      id: "salidas", titulo: "Salidas", temas: ["salidas", "estado"],
      herramientas: ["stream_start", "stream_stop", "record_start", "record_stop"],
      crear(c, api) {
        this.destinos = el("div", "columna");
        const tx = el("div", "fila");
        tx.append(boton("Transmitir", () => api.hacer("stream_start", {}), { variante: "aire", confirmar: "¿Salir al aire?", clase: "lleno" }),
                  boton("Cortar", () => api.hacer("stream_stop", {}), { confirmar: "¿Cortar la transmisión?", clase: "lleno" }));
        this.grab = el("div", "fila grabacion");
        this.chipGrab = el("ba-chip");
        this.grab.append(this.chipGrab, el("span", "espacio"),
          boton("⏺ Grabar", () => api.hacer("record_start", {}), { variante: "aire", compacto: true }),
          boton("■ Parar", () => api.hacer("record_stop", {}), { compacto: true, confirmar: "¿Parar la grabación?" }));
        c.append(this.destinos, tx, this.grab);
      },
      actualizar(tema, d) {
        if (tema === "estado") {
          this.chipGrab.setAttribute("estado", d.recording ? "aire" : "apagado");
          this.chipGrab.textContent = d.recording ? "Grabando " + (d.recordTime || "") : "Sin grabar";
          return;
        }
        const ds = d.destinos || [];
        if (!ds.length) { vacio(this.destinos, "Sin destinos configurados."); return; }
        sincronizar(this.destinos, ds.map((x) => x.id), () => {
          const f = el("div", "fila destino");
          f.chip = el("ba-chip"); f.kbps = el("span", "numero grande-num"); f.det = el("span", "suave");
          f.append(f.chip, el("span", "espacio"), f.kbps, f.det);
          return f;
        }, (f, id) => {
          const x = ds.find((y) => y.id === id);
          f.chip.setAttribute("estado", x.activo ? (x.reconectando ? "aviso" : "aire") : "apagado");
          f.chip.textContent = id;
          f.kbps.textContent = x.activo ? String(x.kbps) : "—";
          f.det.textContent = x.activo ? "kbps" + (x.perdidos_red ? " · " + x.perdidos_red + " perdidos" : "") : "";
        });
      },
    },

    // --- Gráficos: las capas del DSK ---------------------------------------------------
    {
      id: "graficos", titulo: "Capas (DSK)", temas: ["graficos"],
      herramientas: ["dsk_mostrar"],
      crear(c, api) { this.api = api; this.lista = el("div", "columna"); c.append(this.lista); },
      actualizar(_, d) {
        const capas = (Array.isArray(d) ? d : d.capas || []).filter((x) => x && (x.nombre || (x.contenido || []).length));
        if (!capas.length) { vacio(this.lista, "Sin capas definidas."); return; }
        sincronizar(this.lista, capas.map((x) => String(x.capa)), (k) => {
          const t = el("ba-toggle");
          t.addEventListener("cambio", (e) => this.api.hacer("dsk_mostrar", { capa: Number(k), visible: e.detail.valor }).catch(() => {}));
          return t;
        }, (t, k) => {
          const x = capas.find((y) => String(y.capa) === k);
          t.setAttribute("etiqueta", (x.nombre || "Capa " + k) + (x.fijo ? " (fija)" : ""));
          t.encendido = !!x.visible;
        });
      },
    },

    // --- Sala: lo que hacía la consola vieja (PIN, lobby, enlace y QR, admitir, la gente,
    //     el chat y los archivos compartidos) -------------------------------------------
    {
      id: "sala", titulo: "Sala de invitados", temas: ["sala"], ancha: true,
      herramientas: ["participant_admit", "participant_reject", "room_set", "room_pin", "chat_send"],
      crear(c, api) {
        this.api = api;
        // Arriba: PIN, lugares, lobby y sala cerrada.
        const cab = el("div", "fila sala-cabeza");
        this.pin = el("ba-chip");
        this.lugares = el("ba-chip");
        this.lobby = el("ba-toggle"); this.lobby.setAttribute("etiqueta", "Lobby");
        this.lobby.addEventListener("cambio", (e) => api.hacer("room_set", { lobby: e.detail.valor }).catch(() => {}));
        this.cerrada = el("ba-toggle"); this.cerrada.setAttribute("etiqueta", "Sala cerrada");
        this.cerrada.addEventListener("cambio", (e) => api.hacer("room_set", { locked: e.detail.valor }).catch(() => {}));
        cab.append(this.pin, boton("PIN nuevo", () => api.hacer("room_pin", {}).then(() => api.avisar("PIN nuevo")), { compacto: true, confirmar: "¿Cambiar el PIN?" }),
                   this.lugares, el("span", "espacio"), this.lobby, this.cerrada);
        // El enlace para los invitados (red local o internet, con o sin el PIN) y su QR.
        const enlace = el("div", "fila sala-enlace");
        this.conPin = el("ba-toggle"); this.conPin.setAttribute("etiqueta", "Con el PIN");
        const pedir = (target) => api.hacer("room_link", { target, withPin: this.conPin.encendido }).then((d) => this.mostrarEnlace(d)).catch(() => {});
        enlace.append(boton("Enlace (red local)", () => pedir("local"), { compacto: true }),
                      boton("Enlace (internet)", () => pedir("remote"), { compacto: true }), this.conPin);
        this.enlace = el("div", "enlace-qr oculto");
        // Las listas.
        this.esperan = el("div", "columna"); this.adentro = el("div", "columna");
        this.archivos = el("div", "columna");
        // El chat.
        this.chat = el("div", "chat");
        const escribir = el("div", "fila");
        this.campoChat = el("input"); this.campoChat.type = "text"; this.campoChat.maxLength = 1000; this.campoChat.placeholder = "Escribile a la sala…";
        this.campoChat.className = "lleno";
        const mandar = () => { const t = this.campoChat.value.trim(); if (!t) return; api.hacer("chat_send", { text: t }).then(() => { this.campoChat.value = ""; }).catch(() => {}); };
        this.campoChat.addEventListener("keydown", (e) => { if (e.key === "Enter") mandar(); });
        escribir.append(this.campoChat, boton("Enviar", mandar, { compacto: true, variante: "acento" }));
        const col1 = el("div", "columna"), col2 = el("div", "columna");
        col1.append(el("h3", "", "Esperan"), this.esperan, el("h3", "", "Adentro"), this.adentro);
        col2.append(el("h3", "", "Chat"), this.chat, escribir, el("h3", "", "Archivos compartidos"), this.archivos);
        const dos = el("div", "dos-columnas"); dos.append(col1, col2);
        c.append(cab, enlace, this.enlace, dos);
      },
      mostrarEnlace(d) {
        const url = (d && (d.url || d.texto)) || "";
        this.enlace.replaceChildren();
        if (!url) return;
        const fila = el("div", "fila");
        const campo = el("input"); campo.type = "text"; campo.readOnly = true; campo.value = url; campo.className = "lleno";
        fila.append(campo, boton("Copiar", () => navigator.clipboard?.writeText(url).then(() => this.api.avisar("Copiado")), { compacto: true }));
        this.enlace.append(fila);
        for (const img of (d && d.imagenes) || []) {
          const i = el("img", "qr"); i.alt = "QR"; i.src = "data:" + img.mime + ";base64," + img.data;
          this.enlace.append(i);
        }
        this.enlace.classList.remove("oculto");
      },
      actualizar(_, d) {
        this.pin.textContent = "PIN " + (d.pin || "—");
        this.lugares.setAttribute("estado", d.slotsFree > 0 ? "ok" : "aviso");
        this.lugares.textContent = (d.slotsFree ?? "—") + " de " + (d.maxGuests ?? "—") + " lugares libres";
        this.lobby.encendido = !!d.lobby;
        this.cerrada.encendido = !!d.locked;
        const ps = d.participants || [];
        const esperan = ps.filter((p) => p.state === "waiting");
        const adentro = ps.filter((p) => p.state === "admitted");
        const persona = (p, espera) => {
          const f = el("div", "fila persona");
          const nombre = el("div", "lleno");
          nombre.append(el("strong", "", p.name || p.id), el("span", "suave", " " + [p.slot ? "lugar " + p.slot : "", p.hand ? "✋ pide la palabra" : "", p.talk ? "🎙 hablando" : "", p.connected === false ? "desconectado" : ""].filter(Boolean).join(" · ")));
          f.append(nombre);
          if (espera) f.append(boton("Admitir", () => this.api.hacer("participant_admit", { participant: p.id }), { compacto: true, variante: "armado" }),
                               boton("✕", () => this.api.hacer("participant_reject", { participant: p.id }), { compacto: true }));
          else f.append(boton(p.talk ? "🎙 Callar" : "🎙 Hablar", () => this.api.hacer("participant_talk", { participant: p.id, on: !p.talk }), { compacto: true, activo: !!p.talk, variante: p.talk ? "aire" : "" }),
                        boton("Sacar", () => this.api.hacer("participant_kick", { participant: p.id }), { compacto: true, confirmar: "¿Sacarlo de la sala?" }));
          return f;
        };
        const todos = esperan.length > 1 ? [boton("Admitir a todos", () => this.api.hacer("participant_admit_all", {}), { compacto: true, variante: "armado" })] : [];
        esperan.length ? this.esperan.replaceChildren(...todos, ...esperan.map((p) => persona(p, true))) : vacio(this.esperan, "Nadie esperando.");
        adentro.length ? this.adentro.replaceChildren(...adentro.map((p) => persona(p, false))) : vacio(this.adentro, "Nadie adentro.");
        // El chat: los últimos 30, lo nuevo abajo.
        const msgs = (d.chat || []).slice(-30);
        if (msgs.length) {
          const abajo = this.chat.scrollTop + this.chat.clientHeight >= this.chat.scrollHeight - 8;
          this.chat.replaceChildren(...msgs.map((m) => { const x = el("div", "mensaje"); x.append(el("strong", "", (m.fromName || "Operador") + ": "), document.createTextNode(m.text || "")); return x; }));
          if (abajo) this.chat.scrollTop = this.chat.scrollHeight;
        } else vacio(this.chat, "Sin mensajes.");
        const shares = d.shares || [];
        shares.length ? this.archivos.replaceChildren(...shares.map((x) => {
          const f = el("div", "fila persona");
          f.append(el("span", "lleno", (x.name || x.id) + " · " + (x.participantName || "")),
                   boton("Tomar", () => this.api.hacer("share_take", { id: x.id }).then(() => this.api.avisar("Agregado a la escena")), { compacto: true, variante: "acento" }),
                   boton("Borrar", () => this.api.hacer("share_delete", { id: x.id }), { compacto: true, confirmar: "¿Borrarlo?" }));
          return f;
        })) : vacio(this.archivos, "Nadie compartió nada.");
      },
    },

    // --- Oradores: quién habla, siguiente, tiempo ---------------------------------------
    {
      id: "oradores", titulo: "Oradores", temas: ["oradores"],
      herramientas: ["speaker_next", "banca_terminar"],
      crear(c, api) {
        this.quien = el("div", "orador-actual");
        const acc = el("div", "fila");
        acc.append(boton("Siguiente ▶", () => api.hacer("speaker_next", {}), { variante: "acento" }),
                   boton("Terminar", () => api.hacer("banca_terminar", {}), { variante: "aire" }));
        // El modo oradores de la sala (lo que estaba en la consola vieja): prenderlo y el tiempo general.
        const modo = el("div", "fila");
        this.modo = el("ba-toggle"); this.modo.setAttribute("etiqueta", "Modo oradores");
        this.modo.addEventListener("cambio", (e) => api.hacer("speakers_config", { enabled: e.detail.valor }).catch(() => {}));
        this.tiempo = el("ba-slider");
        for (const [k, v] of [["etiqueta", "Tiempo general"], ["min", "30"], ["max", "900"], ["paso", "30"], ["valor", "180"], ["unidad", "s"]]) this.tiempo.setAttribute(k, v);
        this.tiempo.addEventListener("final", (e) => api.hacer("speakers_config", { defaultTime: e.detail.valor }).catch(() => {}));
        modo.append(this.modo);
        this.modoFila = modo;
        this.fila = el("ol", "fila-oradores");
        this.quien.append(el("strong", "", "Nadie tiene la palabra"));
        this.fila.append(el("li", "vacio", "Nadie pidió la palabra."));
        c.append(this.quien, acc, this.modoFila, this.tiempo, el("h3", "", "En la fila"), this.fila);
      },
      actualizar(_, d) {
        if (d.enabled !== undefined || d.speakerMode !== undefined) this.modo.encendido = !!(d.enabled ?? d.speakerMode);
        if (typeof d.defaultTime === "number") this.tiempo.valor = d.defaultTime;
        const actual = d.al_aire || d.onAir || d.actual || (d.speakersOnAir || [])[0];
        const nombre = actual ? (actual.nombre || actual.name || actual) : "Nadie tiene la palabra";
        this.quien.replaceChildren(el("strong", "", String(nombre)),
          el("span", "numero restante", d.restante_s !== undefined ? Math.floor(d.restante_s / 60) + ":" + String(Math.floor(d.restante_s % 60)).padStart(2, "0") : ""));
        const cola = d.cola || d.queue || d.order || [];
        cola.length ? this.fila.replaceChildren(...cola.map((p) => el("li", "", p.nombre || p.name || String(p)))) : this.fila.replaceChildren(el("li", "vacio", "Nadie pidió la palabra."));
      },
    },

    // --- Teleprompter: solo el control -------------------------------------------------
    {
      id: "prompter", titulo: "Teleprompter", temas: [],
      herramientas: ["prompter_control"],
      crear(c, api) {
        const fila1 = el("div", "fila");
        this.play = boton("▶ Arrancar", () => api.hacer("prompter_control", { corriendo: !this.corriendo }).then(() => this.leer()), { grande: true, variante: "acento", clase: "lleno" });
        fila1.append(this.play, boton("PARÁ AHÍ", () => api.hacer("prompter_control", { panico: true }), { grande: true, variante: "aire", clase: "lleno" }));
        this.ppm = el("ba-slider");
        for (const [k, v] of [["etiqueta", "Velocidad"], ["min", "60"], ["max", "260"], ["paso", "10"], ["valor", "140"], ["unidad", "ppm"]]) this.ppm.setAttribute(k, v);
        this.ppm.addEventListener("final", (e) => api.hacer("prompter_control", { ppm: e.detail.valor }).catch(() => {}));
        const avisos = el("div", "avisos");
        for (const t of ["Más despacio", "Más rápido", "Redondeá", "Mirá a cámara"])
          avisos.append(boton(t, () => api.hacer("prompter_control", { aviso: t }), { compacto: true }));
        avisos.append(boton("Borrar aviso", () => api.hacer("prompter_control", { aviso: "" }), { compacto: true }));
        c.append(fila1, this.ppm, el("h3", "", "Avisarle al orador"), avisos);
        this.api = api;
        this.leer = () => api.llamar("prompter_estado", {}).then((d) => {
          this.corriendo = !!(d && d.corriendo);
          this.play.textContent = this.corriendo ? "⏸ Pausa" : "▶ Arrancar";
          if (d && d.ppm) this.ppm.valor = d.ppm;
        }).catch(() => {});
        this.leer();
        this.reloj = setInterval(() => document.contains(c) ? this.leer() : clearInterval(this.reloj), 2000);
      },
    },

    // --- Latencias: el resumen de Latencias del pipeline -------------------------------
    {
      id: "latencias", titulo: "Latencias", temas: ["latencias"],
      herramientas: [],
      crear(c) { this.salidas = el("div", "columna"); this.lentos = el("div", "columna"); c.append(this.salidas, el("h3", "", "Lo que más tarda"), this.lentos); },
      actualizar(_, d) {
        const es = d.estudio || {};
        const s = (d.salidas || []).map((x) => {
          const f = el("div", "fila"); f.append(el("strong", "", x.titulo), el("span", "espacio"), el("span", "numero grande-num", ms(x.total_ms)));
          return f;
        });
        const est = el("div", "fila"); est.append(el("span", "suave", "Estudio"), el("span", "espacio"), el("span", "numero", ms(es.latencia_ms) + " (tope " + ms(es.tope_ms) + ")"));
        this.salidas.replaceChildren(est, ...s);
        const nodos = (d.nodos || []).filter((n) => n.activo !== false && typeof n.ms === "number").sort((a, b) => b.ms - a.ms).slice(0, 5);
        this.lentos.replaceChildren(...nodos.map((n) => {
          const f = el("div", "fila"); const ch = el("ba-chip"); ch.setAttribute("estado", n.estado === "tarde" ? "aire" : n.estado === "justo" ? "aviso" : "ok");
          ch.textContent = n.titulo; f.append(ch, el("span", "espacio"), el("span", "numero", ms(n.ms)));
          return f;
        }));
      },
    },

    // --- Estado del sistema -----------------------------------------------------------
    {
      id: "estado", titulo: "Estado", temas: ["estado"],
      herramientas: [],
      crear(c) { this.grilla = el("div", "grilla-estado"); c.append(this.grilla); },
      actualizar(_, d) {
        const dato = (k, v, mal) => { const x = el("div", "dato" + (mal ? " mal" : "")); x.append(el("span", "suave", k), el("strong", "numero", v)); return x; };
        this.grilla.replaceChildren(
          dato("CPU", Math.round(d.cpuPercent || 0) + " %", d.cpuPercent > 85),
          dato("Cuadros", (d.fps || 0).toFixed(0) + " fps"),
          dato("Perdidos", String(d.droppedFrames || 0), d.droppedFrames > 0),
          dato("Transmisión", d.streamKbps ? d.streamKbps + " kbps" : "—"),
          dato("Grabación", d.recording ? d.recordTime || "sí" : "—"),
          dato("Sala", d.room ? (d.room.inside || 0) + " adentro · " + (d.room.waiting || 0) + " esperan" : "—"),
          dato("Gateway", d.gatewayAlive ? "vivo" : "caído", !d.gatewayAlive),
          dato("Interfaz", d.latido && d.latido.ui_viva ? "viva" : "trabada", d.latido && !d.latido.ui_viva));
      },
    },
  ];
})();
