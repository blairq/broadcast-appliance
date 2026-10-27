/* La tarjeta «Latencias» del dashboard: el resumen de Latencias del pipeline. Lo común, en comun.js. */
(() => {
  "use strict";
  const { el, boton, maxDb, ms, vacio, texto, attr, sincronizar } = BA.tj;
  BA.tarjetas.push(
  {
    id: "latencias", get titulo() { return t("Latencias"); }, temas: ["latencias"],
    herramientas: [],
    crear(c) {
      this.salidas = el("div", "columna");
      const est = el("div", "fila"); this.estudio = el("span", "numero");
      est.append(el("span", "suave", t("Estudio")), el("span", "espacio"), this.estudio);
      this.lista = el("div", "columna");
      this.salidas.append(est, this.lista);
      this.lentos = el("div", "columna");
      c.append(this.salidas, el("h3", "", t("Lo que más tarda")), this.lentos);
    },
    actualizar(_, d) {
      const es = d.estudio || {};
      texto(this.estudio, BAi18n.llenar(t("%1 (tope %2)"), ms(es.latencia_ms), ms(es.tope_ms)));
      const salidas = d.salidas || [];
      sincronizar(this.lista, salidas.map((x) => x.titulo), () => {
        const f = el("div", "fila"); f.t = el("strong"); f.v = el("span", "numero grande-num");
        f.append(f.t, el("span", "espacio"), f.v); return f;
      }, (f, k) => { const x = salidas.find((y) => y.titulo === k); texto(f.t, k); texto(f.v, ms(x.total_ms)); });
      const nodos = (d.nodos || []).filter((n) => n.activo !== false && typeof n.ms === "number").sort((a, b) => b.ms - a.ms).slice(0, 5);
      sincronizar(this.lentos, nodos.map((n) => n.id || n.titulo), () => {
        const f = el("div", "fila"); f.ch = el("ba-chip"); f.v = el("span", "numero");
        f.append(f.ch, el("span", "espacio"), f.v); return f;
      }, (f, k) => {
        const n = nodos.find((y) => (y.id || y.titulo) === k);
        attr(f.ch, "estado", n.estado === "tarde" ? "aire" : n.estado === "justo" ? "aviso" : "ok");
        texto(f.ch, n.titulo); texto(f.v, ms(n.ms));
      });
    },
  });
})();
