// ============================================================================
// El canal de control. UNO SOLO para todas las superficies web del aparato:
// la pantalla, el carrusel, el teleprompter, los rótulos, el tablero.
//
// QUE RESUELVE. Antes cada página se las arreglaba sola: unas preguntaban cada
// tanto, otras no decían nada. Eso dejaba dos agujeros. El primero: mandarle
// algo a un televisor y que el comando salga bien NO quiere decir que se esté
// viendo —pasó, y del lado del operador todo decía «mostrando» mientras la
// pantalla estaba en blanco—. El segundo: para cambiar lo que muestra había
// que volver a castear, o sea reiniciarle la página y que parpadee.
//
// Con el canal abierto, las dos cosas se arreglan: la página DICE cómo está, y
// el operador le CAMBIA lo que muestra sin tocar el aparato.
//
// SI SE CAE, LA PANTALLA SIGUE. Reconecta sola con espera creciente, y mientras
// tanto lo que está mostrando se queda ahí. Una pantalla de sala en negro
// porque el servidor tosió es peor que una que muestra algo de hace un minuto.
//
//   BACanal.abrir({
//     tipo: "carrusel",
//     que: () => "paso 2/3",        // qué estoy mostrando (para el operador)
//     onMostrar: (url) => {...},    // el operador me cambió el contenido
//     onRecargar: () => location.reload(),
//     onAviso: (m) => {...},        // cualquier otro mensaje del servidor
//     onEstado: (conectado) => {...}, // el canal se abrió o se cayó (opcional)
//     extra: { p: "atril" },        // más datos para la sala, en la dirección (opcional)
//     idClave: "prompter-control",  // con qué se recuerda el id (opcional; si no, el tipo)
//     clave: "…",                   // la de las superficies reservadas (rótulos, corrección):
//                                   // va en el primer mensaje, nunca en la dirección
//     onClaveMal: () => {...},      // la sala no la aceptó
//   });
//
// EL CANAL ES LA PRESENCIA Y EL AVISO. Lo que la página muestra le llega por acá
// cuando cambia (el tally, el texto del teleprompter): no hace falta preguntar
// cada tanto. Un canal mudo no es un canal vivo: cada 15 s se le manda un «ping»
// y, si en 40 s no llegó nada, se lo da por caído y se reconecta (un televisor
// que perdió la red sin enterarse se quedaría esperando para siempre).
// ============================================================================
(() => {
  "use strict";

  const LATIDO_MS = 5000;
  const ESPERA_MAX_MS = 30000;
  const PING_MS = 15000;
  const MUDO_MS = 40000;

  /// Un id estable entre recargas: si no, cada recarga aparecería como una
  /// superficie nueva y la lista del operador se llenaría de fantasmas.
  function miId(tipo) {
    const clave = "ba-superficie-" + tipo;
    try {
      let v = localStorage.getItem(clave);
      if (!v) {
        v = tipo + "-" + Math.random().toString(36).slice(2, 10);
        localStorage.setItem(clave, v);
      }
      return v;
    } catch (e) {
      // Sin almacenamiento (modo privado, televisor viejo): id nuevo en cada
      // carga. Vale menos, pero la página sigue avisando que está viva.
      return tipo + "-" + Math.random().toString(36).slice(2, 10);
    }
  }

  window.BACanal = {
    abrir(opciones) {
      const tipo = opciones.tipo || "pantalla";
      // `idClave`: dos páginas del mismo tipo en el mismo aparato (el control y la
      // vista del teleprompter) necesitan ids distintos.
      const id = miId(opciones.idClave || tipo);
      const st = { ws: null, intentos: 0, latido: null, ping: null, oido: 0, cerrado: false, conectado: false };
      const estado = (on) => {
        if (st.conectado === on) return;
        st.conectado = on;
        if (opciones.onEstado) opciones.onEstado(on);
      };

      const mandar = (m) => {
        if (st.ws && st.ws.readyState === WebSocket.OPEN) st.ws.send(window.BATrama.encode(m));
      };

      const late = () => {
        let que = "";
        try { que = String(opciones.que ? opciones.que() : ""); } catch (e) { que = ""; }
        mandar({ type: "late", tipo, que });
      };

      const conectar = () => {
        if (st.cerrado) return;
        const proto = location.protocol === "https:" ? "wss" : "ws";
        let url = `${proto}://${location.host}/ws?superficie=${encodeURIComponent(tipo)}` +
                  `&id=${encodeURIComponent(id)}`;
        for (const [k, v] of Object.entries(opciones.extra || {}))
          if (v !== undefined && v !== null && v !== "") url += `&${encodeURIComponent(k)}=${encodeURIComponent(v)}`;
        let ws;
        try { ws = new WebSocket(url); } catch (e) { reintentar(); return; }
        ws.binaryType = "arraybuffer";
        st.ws = ws;

        ws.onopen = () => {
          st.intentos = 0;
          st.oido = Date.now();
          estado(true);
          if (opciones.clave) mandar({ type: "clave", k: opciones.clave });
          late();
          clearInterval(st.latido);
          st.latido = setInterval(late, LATIDO_MS);
          clearInterval(st.ping);
          st.ping = setInterval(() => {
            if (Date.now() - st.oido > MUDO_MS) { try { ws.close(); } catch (e) {} return; }
            mandar({ type: "ping" });
          }, PING_MS);
        };

        ws.onmessage = (ev) => {
          st.oido = Date.now();
          const m = window.BATrama.decode(ev.data);
          if (!m || m.type === "pong") return;
          if (m.type === "mostrar" && m.url && opciones.onMostrar) {
            opciones.onMostrar(m.url);
            late(); // que el operador vea el cambio enseguida
          } else if (m.type === "clave_mal") {
            if (opciones.onClaveMal) opciones.onClaveMal();
          } else if (m.type === "recargar") {
            if (opciones.onRecargar) opciones.onRecargar();
            else location.reload();
          } else if (opciones.onAviso) {
            // Cualquier otro mensaje se le pasa tal cual a la página. El canal
            // no tiene que saber de tally, ni de lo que venga después: solo
            // entrega. Así una superficie nueva no obliga a tocar esto.
            opciones.onAviso(m);
          }
        };

        ws.onclose = () => {
          clearInterval(st.latido);
          clearInterval(st.ping);
          estado(false);
          reintentar();
        };
        ws.onerror = () => { try { ws.close(); } catch (e) {} };
      };

      const reintentar = () => {
        if (st.cerrado) return;
        // Espera creciente con tope: un televisor puede quedar días prendido y
        // no tiene sentido que martille la red.
        st.intentos = Math.min(st.intentos + 1, 6);
        setTimeout(conectar, Math.min(1000 * 2 ** st.intentos, ESPERA_MAX_MS));
      };

      // Al volver de estar escondida, avisa enseguida en vez de esperar.
      document.addEventListener("visibilitychange", () => { if (!document.hidden) late(); });
      conectar();
      return {
        late,
        mandar,
        conectado: () => st.conectado,
        cerrar() { st.cerrado = true; try { st.ws.close(); } catch (e) {} },
      };
    },
  };
})();
