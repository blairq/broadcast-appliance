/* La tarjeta «Escenas» del dashboard: todas las escenas de la salida que maneja el aparato.
   Un toque la arma (PREVIEW, en la multipantalla de arriba); mantener apretado la pone al
   aire directo. El TAKE, el CORTE y la T-bar están en la multipantalla (multipantalla.js).
   Lo común, en comun.js. */
(() => {
  "use strict";
  const { el, boton, sincronizar } = BA.tj;
  BA.tarjetas.push(
  {
    id: "escenas", get titulo() { return t("Escenas"); }, temas: ["escenas"], ancha: true,
    herramientas: ["scene_preview", "scene_switch"],
    crear(c, api) {
      this.api = api;
      this.grilla = el("div", "grilla-escenas");
      c.append(this.grilla, el("p", "suave ayuda-escenas",
        t("Tocá una escena para armarla en PREVIEW; mantenela apretada para ponerla al aire directo.")));
    },
    actualizar(_, d) {
      const escenas = (d.scenes || []).filter((s) => !s.name.startsWith("__"));
      if (!escenas.length) { this.grilla.replaceChildren(el("p", "vacio", t("No hay escenas."))); return; }
      if (this.grilla.firstElementChild && !this.grilla.firstElementChild.dataset.clave) this.grilla.replaceChildren();
      sincronizar(this.grilla, escenas.map((s) => s.name), (nombre) => {
        // Un toque arma (preview); mantener apretado la pone al aire directo.
        let reloj = 0, directo = false;
        const b = boton(nombre, () => {
          if (directo) { directo = false; return; } // ya la puso al aire el apretón largo
          this.api.hacer("scene_preview", { name: nombre }).catch(() => {});
        });
        b.addEventListener("pointerdown", () => {
          directo = false;
          reloj = setTimeout(() => { reloj = 0; directo = true; this.api.hacer("scene_switch", { name: nombre }).catch(() => {}); }, 700);
        });
        const cortar = () => clearTimeout(reloj);
        b.addEventListener("pointerup", cortar);
        b.addEventListener("pointerleave", cortar);
        b.addEventListener("pointercancel", cortar);
        return b;
      }, (b, nombre) => {
        const s = escenas.find((x) => x.name === nombre);
        b.toggleAttribute("activo", !!(s.live || s.armada));
        b.setAttribute("variante", s.live ? "aire" : s.armada ? "armado" : "");
        b.title = s.live ? t("Al aire") : s.armada ? t("En PREVIEW: sale con el TAKE") : "";
      });
    },
  });
})();
