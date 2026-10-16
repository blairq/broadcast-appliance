/* Lo común de las tarjetas del dashboard web (tarjetas/*.js, una por archivo). Cada una:
     { id, titulo, temas: [lo que mira], herramientas: [lo que llama], ancha?,
       crear(contenido, api), actualizar(tema, datos) }
   `api` es BA.control: llamar/hacer(herramienta, args), puede(herramienta), avisar.
   Los temas los empuja la app (/ws?control) solo cuando cambian. Sin estilos en
   línea (la CSP no los deja): clases de control.css y el.style desde acá. */
(() => {
  "use strict";
  const BA = (window.BA = window.BA || {});

  // --- Utilidades ------------------------------------------------------------------------
  const el = (tag, cls, texto) => {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    if (texto !== undefined) e.textContent = texto;
    return e;
  };
  const boton = (texto, accion, attrs = {}) => {
    const b = el("ba-boton", attrs.clase || "", texto);
    for (const [k, v] of Object.entries(attrs)) if (k !== "clase") v === true ? b.setAttribute(k, "") : v !== false && b.setAttribute(k, v);
    if (accion) b.addEventListener("accion", accion);
    return b;
  };
  const maxDb = (v) => (Array.isArray(v) ? Math.max(...v, -100) : typeof v === "number" ? v : -100);
  const ms = (v) => (v === null || v === undefined ? "—" : v >= 1000 ? (v / 1000).toFixed(2) + " s" : Math.round(v) + " ms");
  // Las actualizaciones NUNCA rehacen el DOM (la app empuja hasta 30 veces por segundo):
  // un componente nuevo nace sin su hoja de estilos un instante, cambia de tamaño y empuja
  // todo lo que tiene abajo. Se crea una vez y después solo se le cambia lo que cambió.
  const vacio = (contenido, texto) => {
    const c = contenido.firstElementChild;
    if (contenido.children.length === 1 && c.classList.contains("vacio") && !c.dataset.clave) { if (c.textContent !== texto) c.textContent = texto; return; }
    contenido.replaceChildren(el("p", "vacio", texto));
  };
  const texto = (e, t) => { if (e.textContent !== t) e.textContent = t; };
  const attr = (e, k, v) => { if (e.getAttribute(k) !== v) e.setAttribute(k, v); };
  // Reutilizar elementos por clave (no rehacer el DOM en cada empuje: el dedo pierde el control).
  const sincronizar = (cont, claves, crear, actualizar) => {
    const vivos = new Map([...cont.children].map((c) => [c.dataset.clave, c]));
    let anterior = null;
    for (const k of claves) {
      let c = vivos.get(k);
      if (!c) { c = crear(k); c.dataset.clave = k; }
      vivos.delete(k);
      actualizar(c, k);
      if (c.previousElementSibling !== anterior || c.parentNode !== cont) anterior ? anterior.after(c) : cont.prepend(c);
      anterior = c;
    }
    for (const [, c] of vivos) c.remove();
  };

  // Lo que usan las tarjetas (cada una en su archivo, tarjetas/<id>.js, en el orden de index.html).
  BA.tj = { el, boton, maxDb, ms, vacio, texto, attr, sincronizar };
  BA.tarjetas = [];
})();
