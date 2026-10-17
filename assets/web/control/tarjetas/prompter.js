/* La tarjeta «Teleprompter» del dashboard: solo el control. Lo común, en comun.js. */
(() => {
  "use strict";
  const { el, boton, maxDb, ms, vacio, texto, attr, sincronizar } = BA.tj;
  BA.tarjetas.push(
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
  });
})();
