/* La tarjeta «Estado» del dashboard: CPU, cuadros, salidas, sala, gateway e interfaz. Lo común, en comun.js. */
(() => {
  "use strict";
  const { el, boton, maxDb, ms, vacio, texto, attr, sincronizar } = BA.tj;
  BA.tarjetas.push(
  {
    id: "estado", get titulo() { return t("Estado"); }, temas: ["estado"],
    herramientas: [],
    crear(c) {
      this.grilla = el("div", "grilla-estado");
      this.datos = {};
      // La clave de cada dato queda en español (la usa actualizar); la etiqueta, traducida.
      const etiqueta = { CPU: "CPU", Cuadros: t("Cuadros"), Perdidos: t("Perdidos"), "Transmisión": t("Transmisión"),
                         "Grabación": t("Grabación"), Conferencia: t("Conferencia"), Gateway: "Gateway", Interfaz: t("Interfaz") };
      for (const k of ["CPU", "Cuadros", "Perdidos", "Transmisión", "Grabación", "Conferencia", "Gateway", "Interfaz"]) {
        const x = el("div", "dato"); x.v = el("strong", "numero", "—");
        x.append(el("span", "suave", etiqueta[k]), x.v); this.grilla.append(x); this.datos[k] = x;
      }
      c.append(this.grilla);
    },
    actualizar(_, d) {
      const dato = (k, v, mal) => { const x = this.datos[k]; texto(x.v, v); if (x.classList.contains("mal") !== !!mal) x.classList.toggle("mal", !!mal); };
      dato("CPU", Math.round(d.cpuPercent || 0) + " %", d.cpuPercent > 85);
      dato("Cuadros", (d.fps || 0).toFixed(0) + " fps");
      dato("Perdidos", String(d.droppedFrames || 0), d.droppedFrames > 0);
      dato("Transmisión", d.streamKbps ? d.streamKbps + " kbps" : "—");
      dato("Grabación", d.recording ? d.recordTime || t("sí") : "—");
      dato("Conferencia", d.room ? BAi18n.llenar(t("%1 adentro · %2 esperan"), d.room.inside || 0, d.room.waiting || 0) : "—");
      dato("Gateway", d.gatewayAlive ? t("vivo") : t("caído"), !d.gatewayAlive);
      dato("Interfaz", d.latido && d.latido.ui_viva ? t("viva") : t("trabada"), d.latido && !d.latido.ui_viva);
    },
  });
})();
