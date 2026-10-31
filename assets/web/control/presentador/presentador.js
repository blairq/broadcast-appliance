/* /control/presentador/: cuánto le queda y qué viene, para leer de lejos. La cuenta la
   manda la app cada segundo (tema «linea»): acá no se cuenta nada. */
(() => {
  "use strict";
  const { texto } = BA.tj;
  const $ = (id) => document.getElementById(id);
  const mmss = BA.linea.mmss;
  BA.linea.pagina({
    temas: ["linea"],
    armar() {},
    alEstado(tema, datos) {
      if (tema !== "linea") return;
      const d = BA.linea.deLaSalida(datos);
      const a = d.salida.alAire, p = d.salida.proximo;
      texto($("que"), a ? a.nombre || a.paso : d.corrida === "terminada" ? t("Terminó") : t("Todavía no empezó"));
      const resta = a ? a.restanteSeg : null;
      // Cuenta para abajo; pasado, «+» y en rojo; el último minuto, en ámbar.
      texto($("cuanto"), resta === null ? "—" : mmss(-resta, resta < 0));
      $("cuanto").classList.toggle("pasado", resta !== null && resta < 0);
      $("cuanto").classList.toggle("ultimo", resta !== null && resta >= 0 && resta <= 60);
      let despues = "";
      if (p) {
        const v = BA.linea.pasosDe(d.ver).find((x) => x.id === p.paso);
        despues = v && v.enSeg !== undefined ? BAi18n.llenar(t("Después: %1 (en %2)"), p.nombre || p.paso, mmss(v.enSeg))
                                             : BAi18n.llenar(t("Después: %1"), p.nombre || p.paso);
      }
      texto($("despues"), despues);
    },
  });
})();
