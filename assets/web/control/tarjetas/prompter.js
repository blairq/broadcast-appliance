/* La tarjeta «Teleprompter» del dashboard: solo el control. Lo común, en comun.js. */
(() => {
  "use strict";
  const { el, boton, maxDb, ms, vacio, texto, attr, sincronizar } = BA.tj;
  BA.tarjetas.push(
  {
    id: "prompter", get titulo() { return t("Teleprompter"); }, temas: [],
    herramientas: ["prompter_control"],
    crear(c, api) {
      const fila1 = el("div", "fila");
      this.play = boton(t("▶ Arrancar"), () => api.hacer("prompter_control", { corriendo: !this.corriendo }).then(() => this.leer()), { grande: true, variante: "acento", clase: "lleno" });
      fila1.append(this.play, boton(t("PARÁ AHÍ"), () => api.hacer("prompter_control", { panico: true }), { grande: true, variante: "aire", clase: "lleno" }));
      this.ppm = el("ba-slider");
      for (const [k, v] of [["etiqueta", t("Velocidad")], ["min", "60"], ["max", "260"], ["paso", "10"], ["valor", "140"], ["unidad", "ppm"]]) this.ppm.setAttribute(k, v);
      this.ppm.addEventListener("final", (e) => api.hacer("prompter_control", { ppm: e.detail.valor }).catch(() => {}));
      const avisos = el("div", "avisos");
      // El aviso viaja tal cual se ve (es lo que lee el orador): en el idioma de esta página.
      for (const aviso of [t("Más despacio"), t("Más rápido"), t("Redondeá"), t("Mirá a cámara")])
        avisos.append(boton(aviso, () => api.hacer("prompter_control", { aviso }), { compacto: true }));
      avisos.append(boton(t("Borrar aviso"), () => api.hacer("prompter_control", { aviso: "" }), { compacto: true }));
      c.append(fila1, this.ppm, el("h3", "", t("Avisarle al orador")), avisos);
      this.api = api;
      this.leer = () => api.llamar("prompter_estado", {}).then((d) => {
        this.corriendo = !!(d && d.corriendo);
        this.play.textContent = this.corriendo ? t("⏸ Pausa") : t("▶ Arrancar");
        if (d && d.ppm) this.ppm.valor = d.ppm;
      }).catch(() => {});
      this.leer();
      this.reloj = setInterval(() => document.contains(c) ? this.leer() : clearInterval(this.reloj), 2000);
    },
  });
})();
