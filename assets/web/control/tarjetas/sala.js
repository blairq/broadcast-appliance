/* La tarjeta «Sala» del dashboard: PIN, lobby, enlace y QR, admitir, la gente, el chat y los archivos. Lo común, en comun.js. */
(() => {
  "use strict";
  const { el, boton, maxDb, ms, vacio, texto, attr, sincronizar } = BA.tj;
  BA.tarjetas.push(
  {
    id: "sala", get titulo() { return t("Conferencia"); }, temas: ["sala"], ancha: true,
    herramientas: ["participant_admit", "participant_reject", "room_set", "room_pin", "chat_send"],
    crear(c, api) {
      this.api = api;
      // Arriba: PIN, lugares, lobby y sala cerrada.
      const cab = el("div", "fila sala-cabeza");
      this.pin = el("ba-chip");
      this.lugares = el("ba-chip");
      this.lobby = el("ba-toggle"); this.lobby.setAttribute("etiqueta", t("Lobby"));
      this.lobby.addEventListener("cambio", (e) => api.hacer("room_set", { lobby: e.detail.valor }).catch(() => {}));
      this.cerrada = el("ba-toggle"); this.cerrada.setAttribute("etiqueta", t("Conferencia cerrada"));
      this.cerrada.addEventListener("cambio", (e) => api.hacer("room_set", { locked: e.detail.valor }).catch(() => {}));
      cab.append(this.pin, boton(t("PIN nuevo"), () => api.hacer("room_pin", {}).then(() => api.avisar(t("PIN nuevo"))), { compacto: true, confirmar: t("¿Cambiar el PIN?") }),
                 this.lugares, el("span", "espacio"), this.lobby, this.cerrada);
      // El enlace para los invitados (red local o internet, con o sin el PIN) y su QR.
      const enlace = el("div", "fila sala-enlace");
      this.conPin = el("ba-toggle"); this.conPin.setAttribute("etiqueta", t("Con el PIN"));
      const pedir = (target) => api.hacer("room_link", { target, withPin: this.conPin.encendido }).then((d) => this.mostrarEnlace(d)).catch(() => {});
      enlace.append(boton(t("Enlace (red local)"), () => pedir("local"), { compacto: true }),
                    boton(t("Enlace (internet)"), () => pedir("remote"), { compacto: true }), this.conPin);
      this.enlace = el("div", "enlace-qr oculto");
      // Las listas.
      this.esperan = el("div", "columna"); this.adentro = el("div", "columna");
      this.archivos = el("div", "columna");
      // El chat.
      this.chat = el("div", "chat");
      const escribir = el("div", "fila");
      this.campoChat = el("input"); this.campoChat.type = "text"; this.campoChat.maxLength = 1000; this.campoChat.placeholder = t("Escribile a la conferencia…");
      this.campoChat.className = "lleno";
      const mandar = () => { const tx = this.campoChat.value.trim(); if (!tx) return; api.hacer("chat_send", { text: tx }).then(() => { this.campoChat.value = ""; }).catch(() => {}); };
      this.campoChat.addEventListener("keydown", (e) => { if (e.key === "Enter") mandar(); });
      escribir.append(this.campoChat, boton(t("Enviar"), mandar, { compacto: true, variante: "acento" }));
      const col1 = el("div", "columna"), col2 = el("div", "columna");
      this.todos = boton(t("Admitir a todos"), () => api.hacer("participant_admit_all", {}), { compacto: true, variante: "armado", clase: "oculto" });
      col1.append(el("h3", "", t("Esperan")), this.todos, this.esperan, el("h3", "", t("Adentro")), this.adentro);
      col2.append(el("h3", "", t("Chat")), this.chat, escribir, el("h3", "", t("Archivos compartidos")), this.archivos);
      const dos = el("div", "dos-columnas"); dos.append(col1, col2);
      c.append(cab, enlace, this.enlace, dos);
    },
    mostrarEnlace(d) {
      const url = (d && (d.url || d.texto)) || "";
      this.enlace.replaceChildren();
      if (!url) return;
      const fila = el("div", "fila");
      const campo = el("input"); campo.type = "text"; campo.readOnly = true; campo.value = url; campo.className = "lleno";
      fila.append(campo, boton(t("Copiar"), () => navigator.clipboard?.writeText(url).then(() => this.api.avisar(t("Copiado"))), { compacto: true }));
      this.enlace.append(fila);
      for (const img of (d && d.imagenes) || []) {
        const i = el("img", "qr"); i.alt = "QR"; i.src = "data:" + img.mime + ";base64," + img.data;
        this.enlace.append(i);
      }
      this.enlace.classList.remove("oculto");
    },
    actualizar(_, d) {
      this.pin.textContent = BAi18n.llenar(t("PIN %1"), d.pin || "—");
      this.lugares.setAttribute("estado", d.slotsFree > 0 ? "ok" : "aviso");
      this.lugares.textContent = BAi18n.llenar(t("%1 de %2 lugares libres"), d.slotsFree ?? "—", d.maxGuests ?? "—");
      this.lobby.encendido = !!d.lobby;
      this.cerrada.encendido = !!d.locked;
      const ps = d.participants || [];
      const esperan = ps.filter((p) => p.state === "waiting");
      const adentro = ps.filter((p) => p.state === "admitted");
      // Una fila por persona, por su id: se crea una vez y se le cambia el texto y el estado.
      const persona = (espera) => (id) => {
        const f = el("div", "fila persona");
        f.nombre = el("strong"); f.det = el("span", "suave");
        const nombre = el("div", "lleno"); nombre.append(f.nombre, f.det);
        f.append(nombre);
        if (espera) f.append(boton(t("Admitir"), () => this.api.hacer("participant_admit", { participant: id }), { compacto: true, variante: "armado" }),
                             boton("✕", () => this.api.hacer("participant_reject", { participant: id }), { compacto: true }));
        else {
          f.hablar = boton(t("🎙 Hablar"), () => this.api.hacer("participant_talk", { participant: id, on: !f.p.talk }), { compacto: true });
          f.append(f.hablar, boton(t("Sacar"), () => this.api.hacer("participant_kick", { participant: id }), { compacto: true, confirmar: t("¿Sacarlo de la conferencia?") }));
        }
        return f;
      };
      const pintar = (lista) => (f, id) => {
        const p = lista.find((x) => x.id === id); f.p = p;
        texto(f.nombre, p.name || p.id);
        texto(f.det, " " + [p.slot ? BAi18n.llenar(t("lugar %1"), p.slot) : "", p.hand ? t("✋ pide la palabra") : "", p.talk ? t("🎙 hablando") : "", p.connected === false ? t("desconectado") : ""].filter(Boolean).join(" · "));
        if (f.hablar) {
          texto(f.hablar, p.talk ? t("🎙 Callar") : t("🎙 Hablar"));
          if (f.hablar.hasAttribute("activo") !== !!p.talk) f.hablar.toggleAttribute("activo", !!p.talk);
          attr(f.hablar, "variante", p.talk ? "aire" : "");
        }
      };
      this.todos.classList.toggle("oculto", esperan.length < 2);
      esperan.length ? sincronizar(this.esperan, esperan.map((p) => p.id), persona(true), pintar(esperan)) : vacio(this.esperan, t("Nadie esperando."));
      adentro.length ? sincronizar(this.adentro, adentro.map((p) => p.id), persona(false), pintar(adentro)) : vacio(this.adentro, t("Nadie adentro."));
      // El chat: los últimos 30, lo nuevo abajo.
      const msgs = (d.chat || []).slice(-30);
      const firma = JSON.stringify(msgs);
      if (firma === this.firmaChat) { /* igual que antes: no se toca */ }
      else if ((this.firmaChat = firma) && msgs.length) {
        const abajo = this.chat.scrollTop + this.chat.clientHeight >= this.chat.scrollHeight - 8;
        this.chat.replaceChildren(...msgs.map((m) => { const x = el("div", "mensaje"); x.append(el("strong", "", (m.fromName || t("Operador")) + ": "), document.createTextNode(m.text || "")); return x; }));
        if (abajo) this.chat.scrollTop = this.chat.scrollHeight;
      } else vacio(this.chat, t("Sin mensajes."));
      const shares = d.shares || [];
      shares.length ? sincronizar(this.archivos, shares.map((x) => x.id), (id) => {
        const f = el("div", "fila persona");
        f.nombre = el("span", "lleno");
        f.append(f.nombre,
                 boton(t("Tomar"), () => this.api.hacer("share_take", { id }).then(() => this.api.avisar(t("Agregado a la escena"))), { compacto: true, variante: "acento" }),
                 boton(t("Borrar"), () => this.api.hacer("share_delete", { id }), { compacto: true, confirmar: t("¿Borrarlo?") }));
        return f;
      }, (f, id) => { const x = shares.find((y) => y.id === id); texto(f.nombre, (x.name || x.id) + " · " + (x.participantName || "")); })
                    : vacio(this.archivos, t("Nadie compartió nada."));
    },
  });
})();
