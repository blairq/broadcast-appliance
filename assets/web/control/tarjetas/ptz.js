/* La tarjeta «PTZ» del dashboard: joystick, zoom y posiciones. Lo común, en comun.js. */
(() => {
  "use strict";
  const { el, boton, maxDb, ms, vacio, texto, attr, sincronizar } = BA.tj;
  BA.tarjetas.push(
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
  });
})();
