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
//   });
// ============================================================================
(() => {
  "use strict";

  const LATIDO_MS = 5000;
  const ESPERA_MAX_MS = 30000;

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
      const id = miId(tipo);
      const st = { ws: null, intentos: 0, latido: null, cerrado: false };

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
        const url = `${proto}://${location.host}/ws?superficie=${encodeURIComponent(tipo)}` +
                    `&id=${encodeURIComponent(id)}`;
        let ws;
        try { ws = new WebSocket(url); } catch (e) { reintentar(); return; }
        ws.binaryType = "arraybuffer";
        st.ws = ws;

        ws.onopen = () => {
          st.intentos = 0;
          late();
          clearInterval(st.latido);
          st.latido = setInterval(late, LATIDO_MS);
        };

        ws.onmessage = (ev) => {
          const m = window.BATrama.decode(ev.data);
          if (!m) return;
          if (m.type === "mostrar" && m.url && opciones.onMostrar) {
            opciones.onMostrar(m.url);
            late(); // que el operador vea el cambio enseguida
          } else if (m.type === "recargar") {
            if (opciones.onRecargar) opciones.onRecargar();
            else location.reload();
          }
        };

        ws.onclose = () => {
          clearInterval(st.latido);
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
      return { late, cerrar() { st.cerrado = true; try { st.ws.close(); } catch (e) {} } };
    },
  };
})();
