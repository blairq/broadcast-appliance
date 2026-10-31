/* ============================================================================
   /comun/neuro/neuro.js — los componentes de las interfaces web de control

   Web Components sin dependencias. NINGUNO tiene colores ni sombras propios:
   usan los materiales del tema (temas.css), así un tema nuevo (vidrio, otro
   neumorfismo) cambia todo sin tocar esto. Pensados para el dedo (44 px o más,
   arrastrar) y también para el teclado (flechas, Enter, espacio).

     <ba-boton variante="acento|aire|armado|peligro" activo mantener confirmar="¿Cortar?">
       eventos: accion · presionar/soltar (con «mantener»)
     <ba-toggle encendido etiqueta="…">            evento: cambio {valor}
     <ba-slider min max paso valor etiqueta unidad>  eventos: cambio {valor} · final {valor}
     <ba-fader min="-60" max="12" valor etiqueta>    eventos: cambio · final; .nivel = {rms, pico} (dBFS)
     <ba-perilla min max valor defecto etiqueta unidad>  eventos: cambio · final (doble toque: el de fábrica)
     <ba-joystick>                                  eventos: mover {x, y} (−1…1, ~20/s) · soltar
     <ba-vumetro orientacion="vertical|horizontal"> .nivel = {rms, pico}
     <ba-tarjeta titulo ampliar="/control/#tarjeta=ptz">  ⤢ abre `ampliar` en una pestaña nueva
     <ba-chip estado="ok|aviso|aire|armado|apagado">
     <ba-tally estado="aire|armado|nada" etiqueta>
   BA.tema: actual(), poner(id). El tema lo elige la APP (Configuración o Servicios →
   Templates) para todas las páginas: el gateway lo publica en /comun/tema.json y
   /comun/tema.js (que la página carga antes que esto) lo lee al abrir, al volver a la
   pestaña y cada 15 s (cambia sin recargar). Se guarda una copia en el aparato para no
   pintar el de fábrica un instante al abrir.
   ?tema=<id> en la URL lo fuerza (para diseñar: muestra.html).
   ============================================================================ */
(() => {
  "use strict";
  const BA = (window.BA = window.BA || {});
  // Los textos propios de los componentes, en el idioma de la página (/comun/i18n.js).
  // Los componentes se arman antes de que cargue el idioma: se vuelven a traducir solos con
  // el evento `ba-idioma` que dispara BAi18n.iniciar() (y BAi18n.elegir()); BA.traducir()
  // queda para llamarlo a mano.
  const t = (s, op) => (window.BAi18n ? window.BAi18n.t(s, op) : s);

  // --- Temas ---------------------------------------------------------------------------
  const CLAVE_TEMA = "ba-tema";
  const valido = (id) => typeof id === "string" && /^[a-z0-9-]{1,40}$/.test(id);
  const forzado = new URLSearchParams(location.search).get("tema");
  BA.tema = {
    actual() { return document.documentElement.dataset.tema || "neuro-oscuro"; },
    poner(id) {
      if (!valido(id) || id === BA.tema.actual()) return;
      document.documentElement.dataset.tema = id;
      try { localStorage.setItem(CLAVE_TEMA, id); } catch (_) {}
      document.dispatchEvent(new CustomEvent("ba-tema", { detail: { tema: id } }));
    },
  };
  (() => {
    let id = forzado;
    if (!valido(id)) { try { id = localStorage.getItem(CLAVE_TEMA); } catch (_) {} }
    document.documentElement.dataset.tema = valido(id) ? id : "neuro-oscuro";
    if (valido(forzado)) return;
    // Quien lee /comun/tema.json es /comun/tema.js (el mismo pedido trae el tema del
    // evento): acá se escucha lo que trae. Si ya lo trajo antes de que cargara esto, está
    // en BA.temaDatos.
    const tomar = (d) => d && BA.tema.poner(d.tema);
    document.addEventListener("ba-tema-datos", (e) => tomar(e.detail));
    tomar(BA.temaDatos);
  })();

  // --- Utilidades ------------------------------------------------------------------------
  const limitar = (v, a, b) => Math.min(b, Math.max(a, v));
  const num = (el, nombre, defecto) => {
    const v = parseFloat(el.getAttribute(nombre));
    return Number.isFinite(v) ? v : defecto;
  };
  const redondear = (v, paso) => {
    if (!paso) return v;
    const r = Math.round(v / paso) * paso;
    return parseFloat(r.toFixed(6));
  };
  const avisar = (el, tipo, detalle) => el.dispatchEvent(new CustomEvent(tipo, { detail: detalle, bubbles: true, composed: true }));

  // Lo común de todos: el «material» como CSS dentro del Shadow DOM.
  // El estilo de TODOS los componentes, en un archivo al lado (componentes.css): cada uno lo
  // enlaza adentro de su Shadow DOM. Con la CSP estricta de las consolas (default-src 'self')
  // un <style> en línea no se aplica; un <link> a lo nuestro, sí.
  const HOJA = new URL("componentes.css", document.currentScript ? document.currentScript.src : location.href).href;

  class Base extends HTMLElement {
    constructor(html) {
      super();
      this.attachShadow({ mode: "open" }).innerHTML = `<link rel="stylesheet" href="${HOJA}">${html}`;
      this.$ = (s) => this.shadowRoot.querySelector(s);
    }
  }

  // --- Botón -------------------------------------------------------------------------------
  class BaBoton extends Base {
    static get observedAttributes() { return ["activo", "deshabilitado", "variante"]; }
    constructor() {
      super(`<button class="relieve" part="boton"><slot></slot></button>`);
      this.boton = this.$("button");
      this.armadoHasta = 0;
      this.boton.addEventListener("pointerdown", (e) => {
        if (this.hasAttribute("mantener")) { this.boton.classList.add("apretado"); this.boton.setPointerCapture(e.pointerId); avisar(this, "presionar"); }
      });
      const soltar = () => { if (this.hasAttribute("mantener") && this.boton.classList.contains("apretado")) { this.boton.classList.remove("apretado"); avisar(this, "soltar"); } };
      this.boton.addEventListener("pointerup", soltar);
      this.boton.addEventListener("pointercancel", soltar);
      this.boton.addEventListener("click", (e) => {
        e.stopPropagation();
        if (this.hasAttribute("deshabilitado")) return;
        const confirmar = this.getAttribute("confirmar");
        if (confirmar && Date.now() > this.armadoHasta) {
          // Dos toques para lo que corta el aire: el primero arma (3 s) y avisa qué va a pasar.
          this.armadoHasta = Date.now() + 3000;
          this.textoOriginal = this.textoOriginal ?? this.innerHTML;
          this.innerHTML = confirmar;
          this.boton.classList.add("armado-confirmar");
          clearTimeout(this.relojConfirmar);
          this.relojConfirmar = setTimeout(() => this.desarmar(), 3000);
          return;
        }
        this.desarmar();
        avisar(this, "accion");
      });
    }
    desarmar() {
      this.armadoHasta = 0;
      this.boton.classList.remove("armado-confirmar");
      if (this.textoOriginal !== undefined) { this.innerHTML = this.textoOriginal; this.textoOriginal = undefined; }
    }
    attributeChangedCallback() { this.boton.setAttribute("aria-pressed", this.hasAttribute("activo") ? "true" : "false"); }
  }

  // --- Interruptor ---------------------------------------------------------------------------
  class BaToggle extends Base {
    static get observedAttributes() { return ["encendido", "etiqueta"]; }
    constructor() {
      super(`<div class="pista hundido-suave" role="switch" tabindex="0"><div class="perilla relieve"></div></div><span class="etiqueta"></span>`);
      this.pista = this.$(".pista");
      const cambiar = () => { this.toggleAttribute("encendido"); avisar(this, "cambio", { valor: this.encendido }); };
      this.addEventListener("click", cambiar);
      this.pista.addEventListener("keydown", (e) => { if (e.key === " " || e.key === "Enter") { e.preventDefault(); cambiar(); } });
    }
    get encendido() { return this.hasAttribute("encendido"); }
    set encendido(v) { this.toggleAttribute("encendido", !!v); }
    attributeChangedCallback() {
      this.$(".etiqueta").textContent = this.getAttribute("etiqueta") || "";
      this.pista.setAttribute("aria-checked", this.encendido ? "true" : "false");
    }
  }

  // --- Base de los controles con valor (slider, fader, perilla) ----------------------------
  class ConValor extends Base {
    static get observedAttributes() { return ["valor", "min", "max", "etiqueta", "unidad"]; }
    get min() { return num(this, "min", 0); }
    get max() { return num(this, "max", 100); }
    get paso() { return num(this, "paso", 0); }
    get valor() { return num(this, "valor", this.min); }
    set valor(v) { this.setAttribute("valor", String(redondear(limitar(v, this.min, this.max), this.paso))); }
    fraccion() { return (this.valor - this.min) / Math.max(1e-9, this.max - this.min); }
    cambiarA(v, final) {
      const antes = this.valor;
      this.valor = v;
      if (this.valor !== antes) avisar(this, "cambio", { valor: this.valor });
      if (final) avisar(this, "final", { valor: this.valor });
    }
    teclas(el, eje) {
      el.addEventListener("keydown", (e) => {
        const paso = this.paso || (this.max - this.min) / 100;
        const mas = eje === "v" ? ["ArrowUp", "ArrowRight", "PageUp"] : ["ArrowRight", "ArrowUp", "PageUp"];
        const menos = eje === "v" ? ["ArrowDown", "ArrowLeft", "PageDown"] : ["ArrowLeft", "ArrowDown", "PageDown"];
        const k = e.key.startsWith("Page") ? 10 : 1;
        if (mas.includes(e.key)) { e.preventDefault(); this.cambiarA(this.valor + paso * k, true); }
        else if (menos.includes(e.key)) { e.preventDefault(); this.cambiarA(this.valor - paso * k, true); }
        else if (e.key === "Home") { e.preventDefault(); this.cambiarA(this.min, true); }
        else if (e.key === "End") { e.preventDefault(); this.cambiarA(this.max, true); }
      });
    }
    textoValor() {
      const v = this.valor, u = this.getAttribute("unidad") || "";
      const d = this.paso && this.paso < 1 ? Math.min(2, String(this.paso).split(".")[1]?.length || 1) : 0;
      return (u === "dB" && v > 0 ? "+" : "") + v.toFixed(d) + (u ? " " + u : "");
    }
  }

  // --- Slider horizontal ---------------------------------------------------------------------
  class BaSlider extends ConValor {
    constructor() {
      super(`<div class="cabeza"><span class="etiqueta"></span><span class="valor numero"></span></div>
          <div class="pista hundido-suave" tabindex="0" role="slider"><div class="lleno"></div><div class="perilla relieve"></div></div>`);
      const pista = this.$(".pista");
      const mover = (e, final) => {
        const r = pista.getBoundingClientRect();
        this.cambiarA(this.min + limitar((e.clientX - r.left) / r.width, 0, 1) * (this.max - this.min), final);
      };
      pista.addEventListener("pointerdown", (e) => { pista.setPointerCapture(e.pointerId); this.arrastrando = true; mover(e, false); });
      pista.addEventListener("pointermove", (e) => { if (this.arrastrando) mover(e, false); });
      pista.addEventListener("pointerup", (e) => { this.arrastrando = false; mover(e, true); });
      this.teclas(pista, "h");
    }
    attributeChangedCallback() {
      const f = this.fraccion();
      this.$(".lleno").style.width = `calc(${f * 100}% - 6px)`;
      this.$(".perilla").style.left = `${f * 100}%`;
      this.$(".etiqueta").textContent = this.getAttribute("etiqueta") || "";
      this.$(".valor").textContent = this.textoValor();
      this.$(".pista").setAttribute("aria-valuenow", this.valor);
    }
  }

  // --- Vúmetro -------------------------------------------------------------------------------
  // dBFS → fracción con una escala que da aire arriba (como las consolas): −60 abajo, 0 arriba.
  const fracDb = (db) => limitar((db + 60) / 60, 0, 1) ** 1.35;
  class BaVumetro extends Base {
    constructor() {
      super(`<div class="caja hundido-suave"><div class="barra"></div><div class="pico"></div></div>`);
      this._nivel = { rms: -100, pico: -100 };
    }
    set nivel(n) {
      this._nivel = n || { rms: -100, pico: -100 };
      const h = this.getAttribute("orientacion") === "horizontal";
      const f = fracDb(this._nivel.rms), p = fracDb(this._nivel.pico);
      const barra = this.$(".barra"), pico = this.$(".pico");
      // El gradiente ocupa todo; el nivel lo destapa (sin medir nada: anda aunque la hoja cargue después).
      const tapa = ((1 - f) * 100).toFixed(1);
      if (h) { barra.style.clipPath = `inset(0 ${tapa}% 0 0 round 4px)`; pico.style.left = `calc(${p * 100}% - 4px)`; }
      else { barra.style.clipPath = `inset(${tapa}% 0 0 0 round 4px)`; pico.style.bottom = `calc(${p * 100}% - 4px)`; }
      pico.style.opacity = this._nivel.pico > -59 ? ".85" : "0";
    }
    get nivel() { return this._nivel; }
  }

  // --- Fader vertical (con su vúmetro) --------------------------------------------------------
  class BaFader extends ConValor {
    constructor() {
      super(`<span class="valor numero"></span>
          <div class="cuerpo"><div class="pista hundido-suave" tabindex="0" role="slider" aria-orientation="vertical"><div class="cero"></div><div class="tapa relieve"></div></div><ba-vumetro></ba-vumetro></div>
          <span class="etiqueta"></span>`);
      const pista = this.$(".pista");
      const mover = (e, final) => {
        const r = pista.getBoundingClientRect();
        this.cambiarA(this.min + limitar(1 - (e.clientY - r.top) / r.height, 0, 1) * (this.max - this.min), final);
      };
      pista.addEventListener("pointerdown", (e) => {
        pista.setPointerCapture(e.pointerId);
        this.arrastrando = true;
        // Se agarra la tapa donde está (no salta al dedo) salvo que se toque lejos.
        this.desde = { y: e.clientY, v: this.valor };
      });
      pista.addEventListener("pointermove", (e) => {
        if (!this.arrastrando) return;
        const r = pista.getBoundingClientRect();
        const delta = -(e.clientY - this.desde.y) / r.height * (this.max - this.min);
        this.cambiarA(this.desde.v + delta, false);
      });
      pista.addEventListener("pointerup", () => { this.arrastrando = false; avisar(this, "final", { valor: this.valor }); });
      pista.addEventListener("dblclick", () => this.cambiarA(limitar(0, this.min, this.max), true)); // a 0 dB
      this.teclas(pista, "v");
    }
    get min() { return num(this, "min", -60); }
    get max() { return num(this, "max", 12); }
    get paso() { return num(this, "paso", 0.5); }
    set nivel(n) { this.$("ba-vumetro").nivel = n; }
    attributeChangedCallback() {
      const f = this.fraccion();
      this.$(".tapa").style.top = `${(1 - f) * 100}%`;
      const f0 = (0 - this.min) / (this.max - this.min);
      this.$(".cero").style.top = `${(1 - limitar(f0, 0, 1)) * 100}%`;
      if (!this.getAttribute("unidad")) this.setAttribute("unidad", "dB");
      this.$(".valor").textContent = this.valor <= this.min ? "−∞" : this.textoValor();
      this.$(".etiqueta").textContent = this.getAttribute("etiqueta") || "";
      this.$(".pista").setAttribute("aria-valuenow", this.valor);
    }
  }

  // --- Perilla -------------------------------------------------------------------------------
  class BaPerilla extends ConValor {
    constructor() {
      super(`<div class="aro hundido-suave" tabindex="0" role="slider">
            <svg viewBox="0 0 84 84"><path class="base"/><path class="arco"/></svg>
            <div class="boton relieve"><div class="marca"></div></div>
          </div><span class="valor numero"></span><span class="etiqueta"></span>`);
      const aro = this.$(".aro");
      aro.addEventListener("pointerdown", (e) => { aro.setPointerCapture(e.pointerId); this.desde = { y: e.clientY, v: this.valor }; });
      aro.addEventListener("pointermove", (e) => {
        if (!this.desde) return;
        // 200 px de arrastre = todo el recorrido (con Shift, fino).
        const rango = (this.max - this.min) * (e.shiftKey ? 0.2 : 1);
        this.cambiarA(this.desde.v - (e.clientY - this.desde.y) / 200 * rango, false);
      });
      aro.addEventListener("pointerup", () => { if (this.desde) { this.desde = null; avisar(this, "final", { valor: this.valor }); } });
      aro.addEventListener("wheel", (e) => {
        e.preventDefault();
        const paso = this.paso || (this.max - this.min) / 100;
        this.cambiarA(this.valor + (e.deltaY < 0 ? paso : -paso), true);
      }, { passive: false });
      aro.addEventListener("dblclick", () => this.cambiarA(num(this, "defecto", this.min), true));
      this.teclas(aro, "v");
    }
    attributeChangedCallback() {
      // De −135° a +135°.
      const a0 = -135, a1 = 135, f = this.fraccion(), a = a0 + f * (a1 - a0);
      const p = (g) => { const r = (g - 90) * Math.PI / 180; return `${42 + 38 * Math.cos(r)} ${42 + 38 * Math.sin(r)}`; };
      const arco = (desde, hasta) => `M ${p(desde)} A 38 38 0 ${hasta - desde > 180 ? 1 : 0} 1 ${p(hasta)}`;
      this.$(".base").setAttribute("d", arco(a0, a1));
      this.$(".arco").setAttribute("d", f > 0.001 ? arco(a0, a) : "");
      this.$(".marca").style.transform = `rotate(${a}deg)`;
      this.$(".valor").textContent = this.textoValor();
      this.$(".etiqueta").textContent = this.getAttribute("etiqueta") || "";
      this.$(".aro").setAttribute("aria-valuenow", this.valor);
    }
  }

  // --- Joystick (PTZ) ------------------------------------------------------------------------
  class BaJoystick extends Base {
    constructor() {
      super(`<div class="base hundido"><div class="cruz"></div><div class="palanca relieve"></div></div>`);
      const base = this.$(".base"), palanca = this.$(".palanca");
      this.pos = { x: 0, y: 0 };
      const mover = (e) => {
        const r = base.getBoundingClientRect(), radio = r.width / 2 * 0.62;
        let dx = e.clientX - (r.left + r.width / 2), dy = e.clientY - (r.top + r.height / 2);
        const d = Math.hypot(dx, dy);
        if (d > radio) { dx *= radio / d; dy *= radio / d; }
        palanca.style.transform = `translate(${dx}px, ${dy}px)`;
        this.pos = { x: dx / radio, y: -dy / radio };
      };
      base.addEventListener("pointerdown", (e) => {
        base.setPointerCapture(e.pointerId);
        base.classList.add("agarrado");
        mover(e);
        clearInterval(this.reloj);
        // Mientras se lo tiene agarrado, ~20 veces por segundo (aunque el dedo no se mueva).
        this.reloj = setInterval(() => avisar(this, "mover", { ...this.pos }), 50);
        avisar(this, "mover", { ...this.pos });
      });
      base.addEventListener("pointermove", (e) => { if (base.classList.contains("agarrado")) mover(e); });
      const soltar = () => {
        if (!base.classList.contains("agarrado")) return;
        base.classList.remove("agarrado");
        clearInterval(this.reloj);
        palanca.style.transform = "translate(0, 0)";
        this.pos = { x: 0, y: 0 };
        avisar(this, "soltar");
      };
      base.addEventListener("pointerup", soltar);
      base.addEventListener("pointercancel", soltar);
    }
  }

  // --- Tarjeta -------------------------------------------------------------------------------
  class BaTarjeta extends Base {
    static get observedAttributes() { return ["titulo", "ampliar"]; }
    constructor() {
      super(`<section class="tarjeta relieve"><header><h2></h2><slot name="acciones"></slot>
            <button class="ampliar relieve" hidden>⤢</button></header>
            <div class="contenido"><slot></slot></div></section>`);
      this.$(".ampliar").addEventListener("click", () => {
        const url = this.getAttribute("ampliar");
        avisar(this, "ampliar", { url });
        if (url) window.open(url, "_blank", "noopener");
      });
    }
    attributeChangedCallback() {
      this.$("h2").textContent = this.getAttribute("titulo") || "";
      this.$(".ampliar").hidden = !this.getAttribute("ampliar");
      this.traducir();
    }
    traducir() { this.$(".ampliar").title = t("A pantalla completa, en una pestaña nueva"); }
  }

  // --- Chip y tally --------------------------------------------------------------------------
  class BaChip extends Base {
    constructor() {
      super(`<span class="hundido-suave"><i></i><slot></slot></span>`);
    }
  }

  class BaTally extends Base {
    static get observedAttributes() { return ["estado", "etiqueta"]; }
    constructor() {
      super(`<span class="hundido-suave"></span>`);
    }
    attributeChangedCallback() { this.traducir(); }
    traducir() {
      const e = this.getAttribute("estado") || "nada";
      this.$("span").textContent = this.getAttribute("etiqueta") || (e === "aire" ? t("Al aire") : e === "armado" ? t("Armado") : "—");
    }
  }

  // Vuelve a poner los textos propios de los componentes que ya están en la página.
  BA.traducir = () => {
    for (const el of document.querySelectorAll("ba-tarjeta, ba-tally")) if (el.traducir) el.traducir();
  };
  window.addEventListener("ba-idioma", () => BA.traducir());

  const definir = (n, c) => { if (!customElements.get(n)) customElements.define(n, c); };
  definir("ba-boton", BaBoton);
  definir("ba-toggle", BaToggle);
  definir("ba-slider", BaSlider);
  definir("ba-vumetro", BaVumetro);
  definir("ba-fader", BaFader);
  definir("ba-perilla", BaPerilla);
  definir("ba-joystick", BaJoystick);
  definir("ba-tarjeta", BaTarjeta);
  definir("ba-chip", BaChip);
  definir("ba-tally", BaTally);
})();
