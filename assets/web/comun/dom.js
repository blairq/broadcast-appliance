// ============================================================================
// Lo chico que usan todas las páginas, en un solo lugar (antes había una copia
// en cada una, y las de mmss no daban lo mismo).
//
//   $(id)                   document.getElementById
//   el(tag, clase, texto)   un elemento nuevo
//   mmss(seg, signo)        «3:20», «1:02:05»; con signo, «+0:42» (pasado) o «−0:42»;
//                           sin número, «—»
// ============================================================================
(() => {
  "use strict";
  window.$ = (id) => document.getElementById(id);
  window.el = (tag, cls, texto) => {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    if (texto !== undefined) e.textContent = texto;
    return e;
  };
  window.mmss = (seg, signo) => {
    if (seg === null || seg === undefined || Number.isNaN(seg)) return "—";
    const a = Math.abs(Math.round(seg));
    const h = Math.floor(a / 3600), m = Math.floor((a % 3600) / 60), s = a % 60;
    const t2 = (h ? h + ":" + String(m).padStart(2, "0") : String(m)) + ":" + String(s).padStart(2, "0");
    return signo ? (seg > 0 ? "+" : seg < 0 ? "−" : "") + t2 : t2;
  };
})();
