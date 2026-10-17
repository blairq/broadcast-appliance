/* La tarjeta «Oradores» del dashboard: quién habla, siguiente, tiempo. Lo común, en comun.js. */
(() => {
  "use strict";
  const { el, boton, maxDb, ms, vacio, texto, attr, sincronizar } = BA.tj;
  BA.tarjetas.push(
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
      this.quienNombre = el("strong", "", "Nadie tiene la palabra");
      this.quienResta = el("span", "numero restante");
      this.quien.append(this.quienNombre, this.quienResta);
      this.fila.append(el("li", "vacio", "Nadie pidió la palabra."));
      c.append(this.quien, acc, this.modoFila, this.tiempo, el("h3", "", "En la fila"), this.fila);
    },
    actualizar(_, d) {
      if (d.enabled !== undefined || d.speakerMode !== undefined) this.modo.encendido = !!(d.enabled ?? d.speakerMode);
      if (typeof d.defaultTime === "number") this.tiempo.valor = d.defaultTime;
      const actual = d.al_aire || d.onAir || d.actual || (d.speakersOnAir || [])[0];
      const nombre = actual ? (actual.nombre || actual.name || actual) : "Nadie tiene la palabra";
      texto(this.quienNombre, String(nombre));
      texto(this.quienResta, d.restante_s !== undefined ? Math.floor(d.restante_s / 60) + ":" + String(Math.floor(d.restante_s % 60)).padStart(2, "0") : "");
      const cola = (d.cola || d.queue || d.order || []).map((p) => p.nombre || p.name || String(p));
      const firma = cola.join("\n");
      if (firma !== this.firmaCola) {
        this.firmaCola = firma;
        cola.length ? this.fila.replaceChildren(...cola.map((n) => el("li", "", n))) : this.fila.replaceChildren(el("li", "vacio", "Nadie pidió la palabra."));
      }
    },
  });
})();
