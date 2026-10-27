/* La tarjeta «Gráficos» del dashboard: las capas del DSK. Lo común, en comun.js. */
(() => {
  "use strict";
  const { el, boton, maxDb, ms, vacio, texto, attr, sincronizar } = BA.tj;
  BA.tarjetas.push(
  {
    id: "graficos", get titulo() { return t("Capas (DSK)"); }, temas: ["graficos"],
    herramientas: ["dsk_mostrar"],
    crear(c, api) { this.api = api; this.lista = el("div", "columna"); c.append(this.lista); },
    actualizar(_, d) {
      // El tema «graficos» junta dos herramientas: {dsk_listar, grafico_en_escena}.
      const l = (d && d.dsk_listar) || d || {};
      const capas = (Array.isArray(l) ? l : l.capas || []).filter((x) => x && (x.nombre || (x.contenido || []).length));
      if (!capas.length) { vacio(this.lista, t("Sin capas definidas.")); return; }
      sincronizar(this.lista, capas.map((x) => String(x.capa)), (k) => {
        const tg = el("ba-toggle");
        tg.addEventListener("cambio", (e) => this.api.hacer("dsk_mostrar", { capa: Number(k), visible: e.detail.valor }).catch(() => {}));
        return tg;
      }, (tg, k) => {
        const x = capas.find((y) => String(y.capa) === k);
        tg.setAttribute("etiqueta", (x.nombre || BAi18n.llenar(t("Capa %1"), k)) + (x.fijo ? " " + t("(fija)") : ""));
        tg.encendido = !!x.visible;
      });
    },
  });
})();
