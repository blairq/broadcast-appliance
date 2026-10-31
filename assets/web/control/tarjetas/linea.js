/* La tarjeta «Línea» del dashboard: al aire, próximo, la cuenta y GO/HOLD de la salida que
   maneja el aparato. Ampliada (⤢) abre la escaleta entera (/control/escaleta/). Lo de la
   línea, en ../linea.js; lo común de las tarjetas, en comun.js. */
(() => {
  "use strict";
  BA.tarjetas.push({
    id: "linea", get titulo() { return t("Línea de tiempo"); }, temas: ["linea"], ancha: true,
    herramientas: ["linea_estado"],
    pagina: "/control/escaleta/",
    crear(c, api) {
      this.conduccion = BA.linea.conduccion(c, api);
    },
    actualizar(_, d) {
      this.conduccion.actualizar(d);
    },
  });
})();
