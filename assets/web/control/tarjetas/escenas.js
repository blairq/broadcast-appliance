/* La tarjeta «Escenas» del dashboard: armar, cortar, auto y la T-bar. Lo común, en comun.js. */
(() => {
  "use strict";
  const { el, boton, maxDb, ms, vacio, texto, attr, sincronizar } = BA.tj;
  BA.tarjetas.push(
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
  });
})();
