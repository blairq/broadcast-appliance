// ============================================================================
// El carrusel. Ver index.html para el por qué de los dos marcos.
//
// LA LISTA SE RELEE SOLA. El televisor apunta a esta página una vez y no se lo
// toca más: si el operador cambia un paso, la lista cambia en el gateway y acá
// se nota en la próxima consulta. Se compara por `version` para no rearmar
// nada cuando no cambió (rearmar haría parpadear la pantalla).
//
// SI LA RED SE CAE, SIGUE. Lo que ya está cargado se queda rotando con la
// última lista que se tenga. Una pantalla de sala que se pone en negro porque
// el servidor tosió es peor que una que muestra algo de hace un minuto.
// ============================================================================
(() => {
  "use strict";

  const $ = (id) => document.getElementById(id);
  const q = new URLSearchParams(location.search);
  const id = q.get("id") || "";
  // Cada cuánto se pregunta si la lista cambió.
  const RELEER_MS = 10000;
  // Tiene que coincidir con la transición del CSS.
  const CRUCE_MS = 600;

  const st = {
    pasos: [],
    version: -1,
    i: -1,
    frente: "a",      // cuál de los dos marcos se está viendo
    timer: null,
    inicioPaso: 0,
    duracionPaso: 0,
  };

  function avisar(t) {
    $("aviso").textContent = t;
    $("aviso").classList.remove("oculto");
  }
  function alAire() { $("aviso").classList.add("oculto"); }

  // --- La lista -------------------------------------------------------------

  async function leerLista() {
    try {
      const r = await fetch("../carrusel.json?id=" + encodeURIComponent(id), { cache: "no-store" });
      if (!r.ok) {
        if (st.pasos.length === 0) avisar("No encuentro ese carrusel");
        return;
      }
      const c = await r.json();
      if (!c || !Array.isArray(c.pasos) || c.pasos.length === 0) return;
      document.body.classList.toggle("corte", c.transicion === "corte");
      if (c.version === st.version) return;   // sin cambios: no se toca nada
      const primeraVez = st.pasos.length === 0;
      st.pasos = c.pasos;
      st.version = c.version;
      document.title = c.nombre || "Carrusel";
      if (primeraVez) {
        st.i = -1;
        siguiente();
      } else if (st.i >= st.pasos.length) {
        // La lista se acortó y estábamos más allá del final.
        st.i = st.pasos.length - 1;
      }
      // Si ya estaba andando, el cambio se ve en el próximo paso: cortar el
      // que está a la vista sería un salto raro para quien esté leyéndolo.
    } catch (e) {
      // Sin red: se sigue con lo que hay.
    }
  }

  // --- La rotación -----------------------------------------------------------

  function siguiente() {
    if (st.pasos.length === 0) return;
    st.i = (st.i + 1) % st.pasos.length;
    const paso = st.pasos[st.i];
    const entra = st.frente === "a" ? $("b") : $("a");
    const sale = st.frente === "a" ? $("a") : $("b");

    const arrancar = () => {
      entra.classList.add("visible");
      sale.classList.remove("visible");
      st.frente = st.frente === "a" ? "b" : "a";
      alAire();
      st.inicioPaso = Date.now();
      st.duracionPaso = (paso.segundos || 15) * 1000;
      clearTimeout(st.timer);
      st.timer = setTimeout(siguiente, st.duracionPaso);
    };

    // Se espera a que CARGUE antes de cruzar: si no, se vería el blanco de una
    // página a medio cargar en medio del salón.
    let listo = false;
    const alCargar = () => {
      if (listo) return;
      listo = true;
      entra.removeEventListener("load", alCargar);
      arrancar();
    };
    entra.addEventListener("load", alCargar);
    // Y un tope: una página que no carga no puede colgar el carrusel entero.
    setTimeout(alCargar, 6000);

    // Si es la misma URL que ya estaba, no se recarga (un reloj no tiene por
    // qué reiniciarse cada vuelta): se cruza y listo.
    if (entra.src === paso.url) alCargar();
    else entra.src = paso.url;
  }

  // La línea de abajo: cuánto falta del paso.
  function pintarBarra() {
    if (st.duracionPaso > 0) {
      const f = Math.min(1, (Date.now() - st.inicioPaso) / st.duracionPaso);
      $("progreso").style.width = (f * 100).toFixed(1) + "%";
    }
    requestAnimationFrame(pintarBarra);
  }

  if (!id) {
    avisar("Falta el carrusel. Esta pantalla se abre desde la app.");
    return;
  }
  // El latido: lo que convierte «le mandamos la orden» en «se está viendo».
  if (window.BALatido) {
    window.BALatido.empezar("carrusel", () => {
      const paso = st.pasos[st.i];
      if (!paso) return "sin pasos todavía";
      return "paso " + (st.i + 1) + "/" + st.pasos.length + ": " + (paso.titulo || paso.url);
    });
  }
  leerLista();
  setInterval(leerLista, RELEER_MS);
  requestAnimationFrame(pintarBarra);
})();
