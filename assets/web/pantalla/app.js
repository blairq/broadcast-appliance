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
    // El estilo viaja en la MISMA dirección y se reenvía tal cual: esta
    // página no interpreta ni un color, así que agregar una forma de punto
    // nueva no la toca. El que sabe de estilos es el generador del núcleo.
    const estilo = [];
    for (const [k, v] of q) {
      if (k !== "texto" && k !== "tipo" && k !== "pie") {
        estilo.push(encodeURIComponent(k) + "=" + encodeURIComponent(v));
      }
    }
    $("qr-img").src = "qr.svg?texto=" + encodeURIComponent(texto) +
                      (estilo.length ? "&" + estilo.join("&") : "");
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
  // El título de una pantalla sin texto, en el idioma del evento. Lo que muestra no
  // espera al idioma: viene en la URL y tiene que verse aunque la red no ande.
  BAi18n.iniciar({ contexto: "web/pantalla", preferir: "evento" })
    .then(() => { if (!texto) document.title = t("Pantalla"); });

  // El canal: avisa que está viva y acepta que le cambien lo que muestra.
  if (window.BACanal) {
    window.BACanal.abrir({
      tipo: "pantalla",
      que: () => tipo + (texto ? ": " + texto : ""),
      onMostrar: (url) => { location.href = url; },
    });
  }
})();
