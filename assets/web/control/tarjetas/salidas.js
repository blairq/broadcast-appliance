/* La tarjeta «Salidas» del dashboard: transmitir y grabar. Lo común, en comun.js. */
(() => {
  "use strict";
  const { el, boton, maxDb, ms, vacio, texto, attr, sincronizar } = BA.tj;
  BA.tarjetas.push(
  {
    id: "salidas", get titulo() { return t("Salidas"); }, temas: ["salidas", "estado"],
    herramientas: ["stream_start", "stream_stop", "record_start", "record_stop"],
    crear(c, api) {
      this.destinos = el("div", "columna");
      const tx = el("div", "fila");
      tx.append(boton(t("Transmitir"), () => api.hacer("stream_start", {}), { variante: "aire", confirmar: t("¿Salir al aire?"), clase: "lleno" }),
                boton(t("Cortar"), () => api.hacer("stream_stop", {}), { confirmar: t("¿Cortar la transmisión?"), clase: "lleno" }));
      this.grab = el("div", "fila grabacion");
      this.chipGrab = el("ba-chip");
      this.grab.append(this.chipGrab, el("span", "espacio"),
        boton(t("⏺ Grabar"), () => api.hacer("record_start", {}), { variante: "aire", compacto: true }),
        boton(t("■ Parar"), () => api.hacer("record_stop", {}), { compacto: true, confirmar: t("¿Parar la grabación?") }));
      c.append(this.destinos, tx, this.grab);
    },
    actualizar(tema, d) {
      if (tema === "estado") {
        this.chipGrab.setAttribute("estado", d.recording ? "aire" : "apagado");
        this.chipGrab.textContent = d.recording ? BAi18n.llenar(t("Grabando %1"), d.recordTime || "") : t("Sin grabar");
        return;
      }
      // El tema «salidas» junta dos herramientas: {stream_salud, record_disco}.
      const ds = ((d && d.stream_salud) || d || {}).destinos || [];
      if (!ds.length) { vacio(this.destinos, t("Sin destinos configurados.")); return; }
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
        f.det.textContent = x.activo ? "kbps" + (x.perdidos_red ? " · " + BAi18n.llenar(t("%1 perdidos"), x.perdidos_red) : "") : "";
      });
    },
  });
})();
