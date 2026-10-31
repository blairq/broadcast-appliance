/* /control/escaleta/: la escaleta de la línea en una tablet. Las piezas son las de
   ../linea.js (conducción, miniaturas, estante, escaleta); esto solo las acomoda. */
(() => {
  "use strict";
  const { el } = BA.tj;
  const main = document.getElementById("escaleta");
  let partes = [];
  BA.linea.pagina({
    temas: ["linea"],
    armar(api) {
      main.replaceChildren();
      const arriba = el("div", "dos-columnas");
      const izq = el("div"), der = el("div");
      arriba.append(izq, der);
      main.append(arriba);
      partes = [BA.linea.conduccion(izq, api), BA.linea.pulsos(izq, api), BA.linea.miniaturas(der, api), BA.linea.propuesta(main)];
      const est = el("section");
      est.append(el("h2", "", t("Estante")));
      const e = BA.linea.estante(est, api);
      if (est.children.length > 1) main.append(est);
      const lista = el("section");
      lista.append(el("h2", "", t("Pasos")));
      main.append(lista);
      partes.push(e, BA.linea.escaleta(lista, api));
    },
    alEstado(tema, datos) {
      if (tema === "linea") for (const p of partes) p.actualizar(datos);
    },
  });
})();
