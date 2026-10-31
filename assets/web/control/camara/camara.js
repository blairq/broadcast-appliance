/* /control/camara/?camara=…: los planos de UNA cámara (tema «linea-camaras», linea_camaras:
   la app ya los ordena y calcula cuánto falta). Acá solo se elige cuál y se dibuja. */
(() => {
  "use strict";
  const { el, texto, sincronizar } = BA.tj;
  const $ = (id) => document.getElementById(id);
  const mmss = BA.linea.mmss;
  let pedida = new URLSearchParams(location.search).get("camara") || "";
  let ultimo = null;

  // La cámara pedida entre las que hay: el nombre exacto, sin mayúsculas, o la que lleva
  // ese número («2» → «Cámara 2»). Sin pedido, la primera.
  function cual(nombres) {
    if (!pedida) return nombres[0] || "";
    const p = pedida.toLowerCase();
    return nombres.find((n) => n === pedida) || nombres.find((n) => n.toLowerCase() === p) ||
           nombres.find((n) => /^\d+$/.test(pedida) && (n.match(/\d+/g) || []).includes(pedida)) || pedida;
  }

  $("camara").addEventListener("change", (ev) => {
    pedida = ev.target.value;
    history.replaceState(null, "", location.pathname + "?camara=" + encodeURIComponent(pedida));
    if (ultimo) dibujar(ultimo);
  });

  function dibujar(datos) {
    const camaras = (datos && datos.camaras) || {};
    const nombres = Object.keys(camaras).sort();
    const cam = cual(nombres);
    const sel = $("camara");
    const firma = nombres.join("\n") + "\n" + cam;
    if (sel.dataset.firma !== firma) {
      sel.dataset.firma = firma;
      sel.replaceChildren(...(nombres.includes(cam) ? nombres : [cam, ...nombres]).map((n) => {
        const o = el("option", "", n);
        o.value = n;
        o.selected = n === cam;
        return o;
      }));
    }
    document.title = cam ? BAi18n.llenar(t("Cámara %1"), cam) : t("Cámara");
    const planos = camaras[cam] || [];
    const aire = planos.find((p) => p.estado === "alAire");
    const prox = planos.find((p) => p.estado !== "alAire");
    const tally = $("tally");
    tally.classList.toggle("aire", !!aire);
    tally.classList.toggle("armado", !aire && !!prox && prox.estado === "proximo");
    texto(tally, aire ? BAi18n.llenar(t("AL AIRE · %1"), aire.nombre || aire.paso)
                 : prox && prox.enSeg !== undefined ? BAi18n.llenar(t("Entrás en %1 · %2"), mmss(prox.enSeg), prox.nombre || prox.paso)
                 : t("Sin planos que vengan"));
    const vienen = planos.filter((p) => p.estado !== "alAire");
    sincronizar($("planos"), vienen.map((p) => p.salida + "/" + p.paso), () => {
      const f = el("div", "camara-plano");
      f.append(el("span", "numero en"), el("strong", "nombre"), el("span", "suave salida"), el("span", "ptz"));
      return f;
    }, (f, k) => {
      const p = vienen.find((y) => y.salida + "/" + y.paso === k);
      const [en, nombre, salida, ptz] = f.children;
      texto(en, p.enSeg !== undefined ? mmss(p.enSeg) : p.frente || "");
      texto(nombre, (p.nombre || p.paso) + (p.escena ? " · " + p.escena : ""));
      texto(salida, p.salida);
      texto(ptz, p.ptz ? "PTZ: " + (p.ptz.texto || JSON.stringify(p.ptz.datos)) : "");
    });
    if (!vienen.length && !$("planos").children.length) $("planos").append(el("p", "vacio", t("Nada más con esta cámara.")));
  }

  BA.linea.pagina({
    temas: ["linea-camaras"],
    armar() {},
    alEstado(tema, datos) {
      if (tema !== "linea-camaras") return;
      ultimo = datos;
      dibujar(datos);
    },
  });
})();
