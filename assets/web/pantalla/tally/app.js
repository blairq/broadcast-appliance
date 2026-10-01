// El tally de una cámara: rojo al aire, verde armada, apagado si no.
//
// QUE MIRA. `?fuente=<nombre>` es la cámara (o la escena) de esta pantalla.
// El estado sale de /pantalla/tally.json, que publica la app en cada cambio.
// Coincide si el nombre es el de la escena al aire O el de una de sus fuentes:
// así una pantalla puede ser de una cámara («Cam 1») o de un plano entero
// («02 Invitado 1»), sin dos páginas distintas.
//
// NO PREGUNTA CADA MEDIO SEGUNDO. El canal de control avisa cuando algo
// cambió (mensaje `tally`) y recién ahí se relee. Se consulta igual cada 5 s
// por si se perdió un aviso: un tally que llega tarde es peor que no tenerlo,
// porque alguien se confía.
//
// SI SE CAE LA CONEXION NO DICE «LISTO»: dice que no sabe. Un tally apagado
// se lee como «no estoy al aire», y esa es justo la mentira que no se puede
// permitir.
(() => {
  "use strict";

  const $ = (id) => document.getElementById(id);
  const q = new URLSearchParams(location.search);
  const fuente = (q.get("fuente") || "").trim();

  const luz = $("luz");
  let ultimo = 0;
  let supoAlgunaVez = false;

  function pintar(clase, estado, quien) {
    luz.className = clase;
    $("estado").textContent = estado;
    $("quien").textContent = quien;
    document.title = estado + (quien ? " · " + quien : "");
  }

  function coincide(lista, escena) {
    if (!fuente) return false;
    if (escena && escena === fuente) return true;
    return Array.isArray(lista) && lista.indexOf(fuente) >= 0;
  }

  async function leer() {
    try {
      const r = await fetch("../tally.json", { cache: "no-store" });
      const t = await r.json();
      ultimo = Date.now();
      supoAlgunaVez = true;
      if (!fuente) {
        // Sin fuente asignada muestra el estado general, que sirve para una
        // pantalla de control en la cabina.
        pintar(t.aire ? "aire" : "listo", t.aire ? "AL AIRE" : "FUERA",
               (t.aire || "") + (t.armada ? "   →   " + t.armada : ""));
        return;
      }
      if (coincide(t.fuentesAire, t.aire)) pintar("aire", "AL AIRE", fuente);
      else if (coincide(t.fuentesArmadas, t.armada)) pintar("armada", "PREPARADA", fuente);
      else pintar("listo", "listo", fuente);
    } catch (e) {
      // Hasta que se sepa una vez, se dice que no se sabe. Después se aguanta
      // un rato antes de avisar: un hipo de red no tiene que apagar la sala.
      if (!supoAlgunaVez || Date.now() - ultimo > 12000)
        pintar("caido", "SIN SEÑAL", "no sé si estás al aire");
    }
  }

  window.BACanal.abrir({
    tipo: "tally",
    que: () => luz.className + (fuente ? " · " + fuente : ""),
    onMostrar: (url) => { location.href = url; },
    onAviso: (m) => { if (m.type === "tally") leer(); },
  });

  leer();
  setInterval(leer, 5000);
})();
