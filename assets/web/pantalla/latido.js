// ============================================================================
// El latido de una superficie web.
//
// Que el aparato acepte la orden de mostrar algo NO quiere decir que la página
// se esté viendo: puede quedarse cargando para siempre y del lado del operador
// todo diría «mostrando». Esto es lo que convierte esa suposición en un dato:
// mientras la página está viva, avisa.
//
// Se incluye con <script src="/pantalla/latido.js"> y se arranca con
// BALatido.empezar("carrusel", () => "lo que estoy mostrando").
// ============================================================================
(() => {
  "use strict";
  const CADA_MS = 5000;

  // Un id estable entre recargas: si no, una página que se recarga sola
  // aparecería como una superficie nueva cada vez.
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
      // Sin almacenamiento (modo privado, televisor raro): igual late, con un
      // id nuevo en cada carga. Vale menos, pero sigue diciendo que está viva.
      return tipo + "-" + Math.random().toString(36).slice(2, 10);
    }
  }

  window.BALatido = {
    empezar(tipo, queMuestra) {
      const id = miId(tipo);
      const mandar = () => {
        let que = "";
        try { que = String(queMuestra ? queMuestra() : ""); } catch (e) { que = ""; }
        // `keepalive` para que el último latido salga aunque la página se esté
        // yendo. Los errores se ignoran a propósito: quedarse sin red no tiene
        // que romper lo que se está mostrando.
        fetch("/api/superficie", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ tipo, id, que }),
          keepalive: true,
        }).catch(() => {});
      };
      mandar();
      setInterval(mandar, CADA_MS);
      // Al volver de estar escondida, avisa enseguida en vez de esperar.
      document.addEventListener("visibilitychange", () => { if (!document.hidden) mandar(); });
    },
  };
})();
