// ============================================================================
// El vigía del video congelado, en un solo lugar (la pantalla de sala y las
// vistas en vivo del tablero tenían cada una el suyo, con umbrales distintos).
//
// Que la conexión diga «connected» no quiere decir que se vea algo: puede estar
// llegando todo negro, o nada. Lo que importa son los CUADROS: si deja de haber
// cuadros nuevos, primero se pide uno completo (a los 2 s y a los 6 s: el que
// manda lo puede rearmar) y, a los 10 s, se da el video por muerto y se rearma.
//
//   const v = BAVigia.vigilar({
//     cuadros: () => n | null | Promise,  // cuántos van; null = no aplica (sin conexión)
//     alAndar: () => {...},     // llegó un cuadro nuevo (opcional)
//     alTrabado: () => {...},   // pedir un cuadro completo (opcional)
//     alMuerto: () => {...},    // 10 s sin cuadros: rearmar
//     cadaMs: 1000,             // cada cuánto se mira (opcional)
//   });
//   v.parar()
// ============================================================================
(() => {
  "use strict";
  const PEDIR_MS = [2000, 6000];
  const MUERTO_MS = 10000;

  window.BAVigia = {
    MUERTO_MS,
    vigilar(o) {
      let ultimo = -1, desde = 0, pedidos = 0, mirando = false;
      const mirar = async () => {
        if (mirando) return;
        mirando = true;
        let n = null;
        try { n = await o.cuadros(); } catch (e) { n = null; }
        mirando = false;
        const ahora = Date.now();
        if (typeof n !== "number") { desde = 0; pedidos = 0; return; }
        if (n !== ultimo) {
          if (n > 0 && o.alAndar) o.alAndar();
          ultimo = n; desde = ahora; pedidos = 0;
          return;
        }
        if (!desde) { desde = ahora; return; }
        const quieto = ahora - desde;
        if (quieto >= MUERTO_MS) {
          desde = 0; pedidos = 0; ultimo = -1;
          o.alMuerto();
        } else if (pedidos < PEDIR_MS.length && quieto >= PEDIR_MS[pedidos]) {
          pedidos++;
          if (o.alTrabado) o.alTrabado();
        }
      };
      const t = setInterval(mirar, o.cadaMs || 1000);
      return { parar: () => clearInterval(t) };
    },
  };
})();
