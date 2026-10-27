/* La tarjeta «Estado» del dashboard: CPU, cuadros, salidas, sala, gateway e interfaz. Lo común, en comun.js. */
(() => {
  "use strict";
  const { el, boton, maxDb, ms, vacio, texto, attr, sincronizar } = BA.tj;
  BA.tarjetas.push(
  {
    id: "estado", titulo: "Estado", temas: ["estado"],
    herramientas: [],
    crear(c) {
      this.grilla = el("div", "grilla-estado");
      this.datos = {};
      for (const k of ["CPU", "Cuadros", "Perdidos", "Transmisión", "Grabación", "Conferencia", "Gateway", "Interfaz"]) {
        const x = el("div", "dato"); x.v = el("strong", "numero", "—");
        x.append(el("span", "suave", k), x.v); this.grilla.append(x); this.datos[k] = x;
      }
      c.append(this.grilla);
    },
    actualizar(_, d) {
      const dato = (k, v, mal) => { const x = this.datos[k]; texto(x.v, v); if (x.classList.contains("mal") !== !!mal) x.classList.toggle("mal", !!mal); };
      dato("CPU", Math.round(d.cpuPercent || 0) + " %", d.cpuPercent > 85);
      dato("Cuadros", (d.fps || 0).toFixed(0) + " fps");
      dato("Perdidos", String(d.droppedFrames || 0), d.droppedFrames > 0);
      dato("Transmisión", d.streamKbps ? d.streamKbps + " kbps" : "—");
      dato("Grabación", d.recording ? d.recordTime || "sí" : "—");
      dato("Conferencia", d.room ? (d.room.inside || 0) + " adentro · " + (d.room.waiting || 0) + " esperan" : "—");
      dato("Gateway", d.gatewayAlive ? "vivo" : "caído", !d.gatewayAlive);
      dato("Interfaz", d.latido && d.latido.ui_viva ? "viva" : "trabada", d.latido && !d.latido.ui_viva);
    },
  });
})();
