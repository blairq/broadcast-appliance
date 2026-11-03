/* La disposición del tablero de un aparato (el orden de las tarjetas, cuántas columnas ocupa
   cada una, cuáles oculta y si la multipantalla está contraída): las cuentas, sin DOM, para
   que control.js las use y tests/test_web_control.py las pruebe.

     disposicion = {orden: [id…], anchos: {id: 1…4}, ocultas: [id…], multipantalla: {contraida}}

     BA.disposicion.normalizar(guardada, tarjetas)  lo guardado → una completa y válida
     BA.disposicion.desdePreferencias(prefs, tarjetas)  (o lo de antes: preferencias.ocultas)
     BA.disposicion.mover(d, id, ±1) / ancho(d, id, ±1) / alternarOculta(d, id) / reordenar(d, ids)
   Cada una devuelve si cambió algo. `tarjetas`: [{id, ancha?}], en su orden de fábrica. */
(() => {
  "use strict";
  const BA = (window.BA = window.BA || {});
  const ANCHO_MAX = 4;
  const anchoDeFabrica = (def) => (def && def.ancha ? 2 : 1);

  // Las que no conoce afuera, las nuevas al final (en su orden de fábrica), anchos de 1 a 4.
  function normalizar(p, tarjetas) {
    const d = p && typeof p === "object" ? p : {};
    const ids = tarjetas.map((x) => x.id);
    const orden = (Array.isArray(d.orden) ? d.orden : []).filter((id, i, a) => ids.includes(id) && a.indexOf(id) === i);
    for (const id of ids) if (!orden.includes(id)) orden.push(id);
    const anchos = {};
    for (const def of tarjetas) {
      const n = d.anchos && Number(d.anchos[def.id]);
      anchos[def.id] = n >= 1 && n <= ANCHO_MAX ? Math.round(n) : anchoDeFabrica(def);
    }
    const ocultas = (Array.isArray(d.ocultas) ? d.ocultas : []).filter((id, i, a) => ids.includes(id) && a.indexOf(id) === i);
    const mp = d.multipantalla && typeof d.multipantalla === "object" ? d.multipantalla : {};
    return { orden, anchos, ocultas, multipantalla: { contraida: !!mp.contraida } };
  }

  function desdePreferencias(prefs, tarjetas) {
    const p = prefs && typeof prefs === "object" ? prefs : {};
    if (p.disposicion) return normalizar(p.disposicion, tarjetas);
    return normalizar({ ocultas: Array.isArray(p.ocultas) ? p.ocultas : [] }, tarjetas); // la de antes
  }

  function mover(d, id, paso) {
    const i = d.orden.indexOf(id), j = i + paso;
    if (i < 0 || j < 0 || j >= d.orden.length) return false;
    d.orden.splice(i, 1);
    d.orden.splice(j, 0, id);
    return true;
  }

  function ancho(d, id, paso) {
    if (!(id in d.anchos)) return false;
    const n = Math.min(ANCHO_MAX, Math.max(1, d.anchos[id] + paso));
    if (n === d.anchos[id]) return false;
    d.anchos[id] = n;
    return true;
  }

  function alternarOculta(d, id) {
    if (!d.orden.includes(id)) return false;
    d.ocultas = d.ocultas.includes(id) ? d.ocultas.filter((x) => x !== id) : d.ocultas.concat(id);
    return true;
  }

  // `vistas`: las tarjetas en el orden en que quedaron en pantalla (después de arrastrar). Las
  // que no estaban a la vista (sin permiso) van detrás, en su orden de antes.
  function reordenar(d, vistas) {
    const nuevo = vistas.filter((id) => d.orden.includes(id));
    for (const id of d.orden) if (!nuevo.includes(id)) nuevo.push(id);
    const cambio = nuevo.some((id, i) => id !== d.orden[i]);
    d.orden = nuevo;
    return cambio;
  }

  BA.disposicion = { ANCHO_MAX, normalizar, desdePreferencias, mover, ancho, alternarOculta, reordenar };
})();
