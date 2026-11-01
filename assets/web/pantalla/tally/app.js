// El tally de una cámara: rojo al aire, verde armada, apagado si no.
//
// QUE MIRA. `?fuente=<nombre>` es la cámara (o la escena) de esta pantalla.
// Coincide si el nombre es el de la escena al aire O el de una de sus fuentes:
// así una pantalla puede ser de una cámara («Cam 1») o de un plano entero
// («02 Invitado 1»), sin dos páginas distintas.
//
// NO PREGUNTA. El estado llega entero por el canal de control (mensaje `tally`):
// al conectarse y en cada cambio, que la app publica apenas pasa. Un tally que
// llega tarde es peor que no tenerlo, porque alguien se confía.
//
// SI SE CAE LA CONEXION NO DICE «LISTO»: dice que no sabe. Un tally apagado
// se lee como «no estoy al aire», y esa es justo la mentira que no se puede
// permitir. El canal se da por caído si se cierra o si se queda mudo (canal.js).
(async () => {
  "use strict";
  // Los textos, en el idioma del evento.
  await BAi18n.iniciar({ contexto: "web/pantalla", preferir: "evento" });

  const $ = (id) => document.getElementById(id);
  const q = new URLSearchParams(location.search);
  const fuente = (q.get("fuente") || "").trim();

  const luz = $("luz");
  let caida = null;

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

  function sinSenal() { pintar("caido", t("SIN SEÑAL"), t("no sé si estás al aire")); }

  function aplicar(tl) {
    if (!tl || typeof tl !== "object") return;
    if (!fuente) {
      // Sin fuente asignada muestra el estado general, que sirve para una
      // pantalla de control en la cabina.
      pintar(tl.aire ? "aire" : "listo", tl.aire ? t("AL AIRE") : t("FUERA"),
             (tl.aire || "") + (tl.armada ? "   →   " + tl.armada : ""));
      return;
    }
    if (coincide(tl.fuentesAire, tl.aire)) pintar("aire", t("AL AIRE"), fuente);
    else if (coincide(tl.fuentesArmadas, tl.armada)) pintar("armada", t("PREPARADA"), fuente);
    else pintar("listo", t("listo"), fuente);
  }

  sinSenal(); // hasta que se sepa una vez
  window.BACanal.abrir({
    tipo: "tally",
    que: () => luz.className + (fuente ? " · " + fuente : ""),
    onMostrar: (url) => { location.href = url; },
    onAviso: (m) => { if (m.type === "tally") aplicar(m.tally); },
    // Un hipo de red no tiene que apagar la sala: se aguanta un rato antes de avisar.
    onEstado: (on) => {
      clearTimeout(caida);
      if (!on) caida = setTimeout(sinSenal, 12000);
    },
  });
})();
