// Lo que se muestra viene en la URL. Nada de estado, nada que pedirle al
// servidor: si la red se cae, lo que ya está en pantalla sigue ahí.
(() => {
  "use strict";
  const q = new URLSearchParams(location.search);
  const tipo = q.get("tipo") || "mensaje";
  const texto = q.get("texto") || "";
  const pie = q.get("pie") || "";

  const $ = (id) => document.getElementById(id);

  if (tipo === "qr") {
    // El QR lo dibuja el servidor (ya tiene con qué): así esta página no
    // arrastra una biblioteca de QR y anda igual en un televisor viejo.
    $("qr-img").src = "qr.svg?texto=" + encodeURIComponent(texto);
    $("qr-img").alt = texto;
    $("qr").classList.remove("oculto");
    $("texto").remove();
  } else if (tipo === "reloj") {
    const pintar = () => {
      $("texto").textContent = new Date().toLocaleTimeString("es-AR", {
        hour: "2-digit", minute: "2-digit", second: "2-digit",
      });
    };
    pintar();
    setInterval(pintar, 500);
  } else {
    $("texto").textContent = texto;
  }

  if (pie) $("pie").textContent = pie;
  else $("pie").remove();

  document.title = texto ? texto.slice(0, 60) : "Pantalla";

  // Avisa que está viva: sin esto, una página que no carga se ve igual que
  // una que sí desde el lado del operador.
  if (window.BALatido) window.BALatido.empezar("pantalla", () => tipo + (texto ? ": " + texto : ""));
})();
