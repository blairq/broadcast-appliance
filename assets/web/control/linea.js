/* La línea de tiempo en la web (PLAN-LINEA-DE-TIEMPO.md 3.5 D): lo común de la tarjeta
   «Línea» del dashboard y de las superficies /control/escaleta, /control/presentador y
   /control/camara. Solo dibuja: los tiempos los calcula la app (linea_estado, linea_ver,
   linea_camaras) y las acciones son las herramientas de siempre (linea_go, linea_hold…),
   que pasan por el perfil del aparato y la confirmación de lo que corta el aire.

   El tema «linea» trae {linea_estado, linea_ver} de la salida que maneja el aparato; el
   tema «linea-camaras» trae linea_camaras. Un aparato con el perfil «solo mirar»
   (linea:ver) no tiene las de vivo: los botones no aparecen.

   Usa BA.tj (tarjetas/comun.js) y BA.control (conexion.js). Sin estilos en línea. */
(() => {
  "use strict";
  const { el, boton, texto, sincronizar } = BA.tj;

  // «3:20», «1:02:05»; con signo, «+0:42» (pasado) o «−0:42».
  function mmss(seg, signo) {
    if (seg === null || seg === undefined || Number.isNaN(seg)) return "—";
    const a = Math.abs(Math.round(seg));
    const h = Math.floor(a / 3600), m = Math.floor((a % 3600) / 60), s = a % 60;
    const t2 = (h ? h + ":" + String(m).padStart(2, "0") : String(m)) + ":" + String(s).padStart(2, "0");
    return signo ? (seg > 0 ? "+" : seg < 0 ? "−" : "") + t2 : t2;
  }

  // Lo de la salida del aparato: el estado (de linea_estado) y la vista (linea_ver).
  function deLaSalida(datos) {
    const est = (datos && datos.linea_estado) || {};
    const ver = (datos && datos.linea_ver) || {};
    const id = ver.salida || est.salidaPorOmision;
    const s = (est.salidas || []).find((x) => x.salida === id) || (est.salidas || [])[0] || {};
    return { corrida: est.estado || "parada", retoma: est.retoma || {}, salida: s, ver, evento: est.evento };
  }

  // Los pasos de la vista, en orden, con su bloque.
  function pasosDe(ver) {
    const out = [];
    for (const b of ver.bloques || []) for (const p of b.pasos || []) out.push(Object.assign({ bloqueNombre: b.nombre, bloque: b.id }, p));
    return out;
  }

  const nombreEntra = (e) => ({ go: t("GO"), auto: t("solo"), hora: t("a la hora"), cuando: t("cuando"), ancla: t("con otra salida") })[e] || e || "";

  // --- La conducción: al aire, próximo, cuenta, over/under y los botones --------------------
  // `opciones.botones`: false = solo mirar (el presentador).
  function conduccion(c, api, opciones = {}) {
    const x = {};
    x.raiz = el("div", "linea-conduccion");
    const aire = el("div", "linea-aire");
    x.aireRotulo = el("span", "rotulo", t("Al aire"));
    x.aireNombre = el("strong", "linea-nombre", "—");
    x.cuenta = el("span", "numero linea-cuenta", "—");
    x.aireBloque = el("span", "suave");
    aire.append(x.aireRotulo, x.aireNombre, x.cuenta, x.aireBloque);
    const prox = el("div", "linea-proximo");
    x.proxNombre = el("strong", "linea-nombre", "—");
    x.proxCuando = el("span", "numero suave");
    prox.append(el("span", "rotulo", t("Próximo")), x.proxNombre, x.proxCuando);
    x.barra = el("div", "fila linea-barra");
    x.over = el("ba-chip");
    x.hold = el("ba-chip", "oculto", t("HOLD"));
    x.hold.setAttribute("estado", "aviso");
    x.hard = el("span", "suave numero");
    x.barra.append(x.over, x.hold, x.hard);
    x.raiz.append(aire, prox, x.barra);
    if (opciones.botones !== false) {
      const fila = el("div", "fila linea-botones");
      const b = (txt, h, args, attrs) => {
        if (!api.puede(h)) return null;
        const bt = boton(txt, () => api.hacer(h, args()).catch(() => {}), attrs);
        fila.append(bt);
        return bt;
      };
      b(t("◀ Anterior"), "linea_anterior", () => ({}), { clase: "lleno" });
      x.botonHold = b(t("HOLD"), "linea_hold", () => ({}), { clase: "lleno" });
      b(t("Saltar"), "linea_saltar", () => ({}), { clase: "lleno" });
      b(t("Siguiente ▶"), "linea_siguiente", () => ({}), { clase: "lleno" });
      b(t("GO"), "linea_go", () => ({}), { grande: true, variante: "aire", clase: "lleno linea-go" });
      if (fila.children.length) x.raiz.append(fila);
    }
    c.append(x.raiz);
    x.actualizar = (datos) => {
      const d = deLaSalida(datos);
      const s = d.salida, a = s.alAire, p = s.proximo;
      texto(x.aireNombre, a ? a.nombre || a.paso : d.corrida === "terminada" ? t("Terminó") : t("Nada al aire"));
      texto(x.cuenta, a ? mmss(-a.restanteSeg, a.restanteSeg < 0) : "");
      x.cuenta.classList.toggle("pasado", !!(a && a.restanteSeg < 0));
      texto(x.aireBloque, a && s.bloque ? BAi18n.llenar(t("bloque %1"), mmss(s.bloque.overUnderSeg, true)) : "");
      texto(x.proxNombre, p ? p.nombre || p.paso : "—");
      texto(x.proxCuando, !p ? "" : p.entraSoloEnSeg > 0 ? BAi18n.llenar(t("entra solo en %1"), mmss(p.entraSoloEnSeg))
                                 : p.automatico ? t("entra solo") : BAi18n.llenar(t("con %1"), nombreEntra(p.entra)));
      const ou = s.overUnderSeg || 0;
      x.over.setAttribute("estado", ou > 60 ? "aire" : ou > 0 ? "aviso" : "ok");
      texto(x.over, BAi18n.llenar(t("Evento %1"), mmss(ou, true)));
      x.hold.classList.toggle("oculto", !s.hold);
      if (x.botonHold) x.botonHold.toggleAttribute("activo", !!s.hold);
      const h = s.proximoHardStart;
      texto(x.hard, h ? BAi18n.llenar(t("%1 a las %2 (%3)"), h.paso, h.hora, mmss(h.gapSeg, true)) : "");
      x.raiz.classList.toggle("en-hold", !!s.hold);
    };
    return x;
  }

  // --- La escaleta compacta: los pasos por bloque ----------------------------------------------
  function escaleta(c, api) {
    const x = { raiz: el("div", "linea-escaleta") };
    c.append(x.raiz);
    x.actualizar = (datos) => {
      const d = deLaSalida(datos);
      const pasos = pasosDe(d.ver);
      if (!pasos.length) { x.raiz.replaceChildren(el("p", "vacio", t("La línea no tiene pasos."))); return; }
      sincronizar(x.raiz, pasos.map((p) => p.id), (id) => {
        const fila = el("div", "linea-paso");
        fila.append(el("span", "numero frente"), el("span", "nombre"), el("span", "suave entra"), el("span", "numero dur"),
                    el("span", "numero en"));
        // Tocar un paso que viene: ir a él (lo confirma la persona; corta el aire).
        if (api.puede("linea_ir"))
          fila.addEventListener("click", () => {
            const p = x.pasos.find((y) => y.id === id);
            if (p && (p.estado === "futuro" || p.estado === "proximo")) api.hacer("linea_ir", { paso: id }).catch(() => {});
          });
        return fila;
      }, (fila, id) => {
        const p = pasos.find((y) => y.id === id);
        fila.dataset.estado = p.estado;
        const [fr, nom, en, dur, falta] = fila.children;
        texto(fr, p.frente || "");
        texto(nom, (p.nombre || p.id) + (p.escena ? " · " + p.escena : ""));
        texto(en, nombreEntra(p.entra) + (p.hora ? " " + p.hora : ""));
        texto(dur, p.durSeg !== undefined ? mmss(p.durSeg) : "");
        texto(falta, p.enSeg !== undefined && p.estado !== "alAire" ? mmss(p.enSeg) : "");
        fila.classList.toggle("tarde", !!p.tarde);
      });
      x.pasos = pasos;
    };
    return x;
  }

  // --- El estante (adlibs) ----------------------------------------------------------------------
  function estante(c, api) {
    const x = { raiz: el("div", "linea-estante") };
    if (!api.puede("linea_adlib") && !api.puede("linea_insertar")) return { actualizar() {} };
    c.append(x.raiz);
    x.actualizar = (datos) => {
      const items = (deLaSalida(datos).ver.estante) || [];
      x.raiz.classList.toggle("oculto", !items.length);
      sincronizar(x.raiz, items.map((i) => i.id), (id) => boton(id, () => {
        const i = items.find((y) => y.id === id);
        // Un paso del estante va de próximo; una pieza entra ya.
        if (i && i.tipo === "paso") api.hacer("linea_insertar", { item: id }).catch(() => {});
        else api.hacer("linea_adlib", { pieza: id }).catch(() => {});
      }, { compacto: true }), (b, id) => {
        const i = items.find((y) => y.id === id);
        texto(b, i.boton || i.nombre || id);
      });
    };
    return x;
  }

  // --- Lo que propone el productor para llegar a horario -----------------------------------------
  // Solo se muestra (con cómo quedaría el fin de cada opción): se aplica desde la conducción.
  function propuesta(c) {
    const x = { raiz: el("section", "linea-propuesta oculto") };
    x.texto = el("p", "aviso");
    x.opciones = el("ul", "suave");
    x.raiz.append(el("h2", "", t("El productor propone")), x.texto, x.opciones);
    c.append(x.raiz);
    x.actualizar = (datos) => {
      const pr = deLaSalida(datos).salida.propuesta;
      x.raiz.classList.toggle("oculto", !pr);
      if (!pr) return;
      texto(x.texto, pr.texto || "");
      const ops = pr.opciones || [];
      sincronizar(x.opciones, ops.map((o) => o.id), () => el("li"), (li, id) => {
        const o = ops.find((y) => y.id === id);
        const fin = o.resultado && o.resultado.fin;
        texto(li, (o.texto || id) + (fin ? " · " + BAi18n.llenar(t("termina %1"), fin) : ""));
      });
    };
    return x;
  }

  // --- Los pulsos de la web («web:<botón>» en una condición de la línea) -------------------------
  // Un botón por cada uno que espera la línea: lo que pase (un GO, un adlib) lo dice la condición.
  function pulsos(c, api) {
    const x = { raiz: el("div", "fila linea-pulsos") };
    if (!api.puede("linea_pulso")) return { actualizar() {} };
    c.append(x.raiz);
    x.actualizar = (datos) => {
      const claves = ((deLaSalida(datos).ver.pulsos) || []).filter((k) => k.startsWith("web:"));
      x.raiz.classList.toggle("oculto", !claves.length);
      sincronizar(x.raiz, claves, (clave) => boton(clave.slice(4), () => api.hacer("linea_pulso", { clave }).catch(() => {}),
                                                   { compacto: true }), () => {});
    };
    return x;
  }

  // --- Las miniaturas de al aire y del próximo ---------------------------------------------------
  // Una foto de la escena (foto_fuente, 320 de ancho) cuando cambia y, la de al aire, cada 20 s.
  // Sin permiso de imágenes en el perfil, no hay miniaturas (la escaleta anda igual).
  function miniaturas(c, api) {
    const x = { raiz: el("div", "fila linea-miniaturas") };
    if (!api.puede("foto_fuente")) return { actualizar() {} };
    const caja = (rotulo) => {
      const k = el("figure", "linea-mini");
      const img = el("img");
      img.alt = "";
      const pie = el("figcaption", "suave");
      k.append(img, pie);
      x.raiz.append(k);
      return { k, img, pie, rotulo, escena: "", ms: 0 };
    };
    const aire = caja(t("Al aire")), prox = caja(t("Próximo"));
    c.append(x.raiz);
    const pedir = (m, escena) => {
      m.escena = escena;
      m.ms = Date.now();
      if (!escena) { m.img.removeAttribute("src"); return; }
      api.llamar("foto_fuente", { name: escena, maxWidth: 320 }).then((d) => {
        const im = d && d.imagenes && d.imagenes[0];
        if (im && m.escena === escena) m.img.src = "data:" + im.mime + ";base64," + im.data;
      }).catch(() => {});
    };
    x.actualizar = (datos) => {
      const d = deLaSalida(datos);
      const pasos = pasosDe(d.ver);
      const a = pasos.find((p) => p.estado === "alAire"), p = pasos.find((y) => y.estado === "proximo");
      for (const [m, paso] of [[aire, a], [prox, p]]) {
        const escena = (paso && paso.escena) || "";
        if (escena !== m.escena || (m === aire && escena && Date.now() - m.ms > 20000)) pedir(m, escena);
        const edad = m.escena ? Math.round((Date.now() - m.ms) / 1000) : 0;
        texto(m.pie, m.rotulo + (m.escena ? " · " + m.escena : "") + (edad >= 5 ? " · " + BAi18n.llenar(t("hace %1 s"), edad) : ""));
      }
    };
    return x;
  }

  // --- Una superficie de una página -------------------------------------------------------------
  // Conecta, aplica ?salida=… (el aparato pasa a manejar esa: lo que llama y lo que mira es
  // de esa), mira los `temas` y le pasa a `alEstado` lo que llega. `armar(api)` se llama una
  // vez con el catálogo (y de nuevo si cambia el perfil). La barra: #aparato y #salidaNombre.
  async function pagina({ temas, armar, alEstado }) {
    const q = new URLSearchParams(location.search);
    const pedida = q.get("salida") || "";
    let pidio = false;
    await BA.conexion.arrancar({
      alListo(m) {
        const ap = document.getElementById("aparato");
        if (ap) ap.textContent = m.nombre ? "· " + m.nombre : "";
        if (pedida && !pidio) {
          pidio = true;
          const actual = BA.control.salidaActual();
          if (!actual || (actual.id !== pedida && actual.nombre !== pedida)) BA.control.elegirSalida(pedida);
        }
        armar(BA.control);
        BA.control.mirar(temas);
        for (const x of temas) if (BA.control.estado[x] !== undefined) alEstado(x, BA.control.estado[x]);
      },
      alEstado,
      alSalida() {
        const s = BA.control.salidaActual(), e = document.getElementById("salidaNombre");
        if (!e) return;
        e.textContent = s && BA.control.salidas.length > 1 ? s.nombre || s.id : "";
        e.style.borderColor = (s && s.color) || "";
      },
      alRechazo(r) { BA.conexion.avisar(t(r.error) || r.error, true); },
    });
  }

  BA.linea = { mmss, deLaSalida, pasosDe, conduccion, escaleta, estante, propuesta, pulsos, miniaturas, pagina };
})();
