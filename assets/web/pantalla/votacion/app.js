// La votación proyectada. Consulta al gateway y dibuja lo que le manda.
(() => {
  "use strict";
  const q = new URLSearchParams(location.search);
  const clave = q.get("k") || "";
  // La dirección que va adentro del QR: la arma quien manda a mostrar esto,
  // porque es el único que sabe con qué IP lo van a alcanzar los teléfonos.
  const entrada = q.get("entrada") || "";

  const $ = (id) => document.getElementById(id);
  if (entrada) $("qr").src = "../qr.svg?texto=" + encodeURIComponent(entrada);
  else $("como").classList.add("oculto");

  let ultimo = "";

  function pintar(v) {
    const hay = v && v.hay === true;
    $("hay").classList.toggle("hidden", !hay);
    $("nada").classList.toggle("hidden", hay);
    if (!hay) { document.title = "Votación"; return; }

    $("estado").textContent = (v.modo === "formal" ? "Votación" : "Encuesta") +
                              (v.abierta ? " · abierta" : " · cerrada") +
                              (v.vinculante ? "" : " · consulta, no vinculante");
    $("estado").className = "estado" + (v.abierta ? " abierta" : "");
    // textContent y nunca innerHTML: la pregunta la escribió una persona.
    $("pregunta").textContent = v.pregunta || "";
    document.title = v.pregunta || "Votación";

    // `resultado` viene SOLO cuando el gateway considera que corresponde. Sin
    // él se muestran las opciones sin números: la gente tiene que saber qué
    // está votando aunque todavía no pueda ver cómo va.
    const res = v.resultado || null;
    const total = res ? Math.max(1, res.reduce((a, o) => a + o.votos, 0)) : 1;

    const cont = $("opciones");
    cont.textContent = "";
    (v.opciones || []).forEach((texto, i) => {
      const o = document.createElement("div");
      o.className = "op" + (res ? "" : " sinnumeros");
      const fila = document.createElement("div");
      fila.className = "fila";
      const n = document.createElement("span");
      n.className = "nombre";
      n.textContent = texto;
      const c = document.createElement("span");
      c.className = "n";
      c.textContent = res ? res[i].votos : "";
      fila.append(n, c);
      const riel = document.createElement("div");
      riel.className = "riel";
      const lleno = document.createElement("div");
      lleno.className = "lleno";
      lleno.style.width = res ? Math.round((res[i].votos / total) * 100) + "%" : "0";
      riel.appendChild(lleno);
      o.append(fila, riel);
      cont.appendChild(o);
    });

    // El quórum con sus dos números, que es como se lee desde la sala.
    $("cuenta").textContent = v.podian > 0
      ? "votaron " + v.votaron + " de " + v.podian
      : "votaron " + v.votaron;
    // El QR no hace falta una vez cerrada: ya no se puede votar.
    $("como").classList.toggle("oculto", !entrada || !v.abierta);
  }

  async function traer() {
    try {
      const r = await fetch("/api/votacion?k=" + encodeURIComponent(clave), { cache: "no-store" });
      const j = await r.json();
      const v = j.votacion;
      // Se redibuja solo si cambió: en una pantalla proyectada, repintar dos
      // veces por segundo hace parpadear las barras.
      const firma = JSON.stringify(v);
      if (firma === ultimo) return;
      ultimo = firma;
      pintar(v);
    } catch (_) {
      // Si la red se corta, lo que está en pantalla se queda: borrarlo sería
      // dejar la sala sin saber qué se está votando.
    }
  }

  traer();
  setInterval(traer, 1500);

  if (window.BACanal) {
    window.BACanal.abrir({
      tipo: "pantalla",
      que: () => "votacion",
      onMostrar: (url) => { location.href = url; },
    });
  }
})();
