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
     <ba-tema>                                      el selector de tema

   BA.tema: lista, actual(), poner(id). Se recuerda por aparato (localStorage) y
   acepta ?tema=<id> en la URL.
   ============================================================================ */
(() => {
  "use strict";
  const BA = (window.BA = window.BA || {});

  // --- Temas ---------------------------------------------------------------------------
  const CLAVE_TEMA = "ba-tema";
  BA.tema = {
    lista: [
      { id: "neuro-oscuro", nombre: "Neumorfismo oscuro" },
      { id: "neuro-claro", nombre: "Neumorfismo claro" },
    ],
    actual() { return document.documentElement.dataset.tema || "neuro-oscuro"; },
    poner(id) {
      if (!BA.tema.lista.some((t) => t.id === id)) return;
      document.documentElement.dataset.tema = id;
      try { localStorage.setItem(CLAVE_TEMA, id); } catch (_) {}
      document.dispatchEvent(new CustomEvent("ba-tema", { detail: { tema: id } }));
    },
  };
  (() => {
    let id = new URLSearchParams(location.search).get("tema");
    if (!id) { try { id = localStorage.getItem(CLAVE_TEMA); } catch (_) {} }
    document.documentElement.dataset.tema = BA.tema.lista.some((t) => t.id === id) ? id : "neuro-oscuro";
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
  const BASE = `
    :host { font-family: var(--fuente); color: var(--texto); -webkit-tap-highlight-color: transparent; }
    :host([hidden]) { display: none !important; }
    .relieve { background: var(--superficie); border: 1px solid var(--borde); box-shadow: var(--relieve);
               backdrop-filter: var(--filtro); -webkit-backdrop-filter: var(--filtro); }
    .hundido { background: var(--superficie-hundida); box-shadow: var(--hundido); border: 1px solid var(--borde); }
    .hundido-suave { background: var(--superficie-hundida); box-shadow: var(--hundido-suave); border: 1px solid var(--borde); }
    :focus { outline: none; }
    .numero { font-family: var(--fuente-numeros); font-variant-numeric: tabular-nums; }
    .etiqueta { font-size: 12px; color: var(--texto-suave); letter-spacing: 0.02em; }
  `;

  class Base extends HTMLElement {
    constructor(css, html) {
      super();
      this.attachShadow({ mode: "open" }).innerHTML = `<style>${BASE}${css}</style>${html}`;
      this.$ = (s) => this.shadowRoot.querySelector(s);
    }
  }

  // --- Botón -------------------------------------------------------------------------------
  class BaBoton extends Base {
    static get observedAttributes() { return ["activo", "deshabilitado", "variante"]; }
    constructor() {
      super(`
        :host { display: inline-block; }
        button { all: unset; box-sizing: border-box; display: inline-flex; align-items: center; justify-content: center; gap: 8px;
                 min-height: 46px; min-width: 46px; padding: 0 20px; border-radius: var(--radio-chico); cursor: pointer;
                 font-weight: 600; font-size: 15px; color: var(--texto); user-select: none; -webkit-user-select: none;
                 transition: box-shadow .14s ease, transform .1s ease, color .14s; width: 100%; }
        :host([compacto]) button { min-height: 38px; min-width: 38px; padding: 0 14px; font-size: 13px; }
        :host([redondo]) button { border-radius: var(--radio-redondo); padding: 0; }
        :host([grande]) button { min-height: 64px; font-size: 18px; border-radius: var(--radio); }
        button:hover { box-shadow: var(--relieve-alto); }
        button:active, :host([activo]) button, button.apretado { box-shadow: var(--hundido); background: var(--superficie-hundida); transform: translateY(1px); }
        button:focus-visible { box-shadow: var(--relieve), 0 0 0 3px var(--foco); }
        :host([activo]) button { color: var(--acento); }
        :host([variante="acento"]) button { color: var(--acento); }
        :host([variante="aire"]) button { color: var(--aire); }
        :host([variante="armado"]) button { color: var(--armado); }
        :host([variante="peligro"]) button { color: var(--aire); }
        /* Activo con color de estado: el color lleno, para que se lea de lejos. */
        :host([activo][variante="aire"]) button { background: var(--aire); color: #fff; box-shadow: var(--hundido), 0 0 18px var(--aire); }
        :host([activo][variante="armado"]) button { background: var(--armado); color: #fff; box-shadow: var(--hundido), 0 0 14px var(--armado); }
        :host([activo][variante="acento"]) button { background: var(--acento); color: var(--acento-texto); }
        :host([deshabilitado]) button { opacity: .38; cursor: default; pointer-events: none; }
        button.armado-confirmar { color: var(--aire); animation: latir 0.8s ease-in-out infinite alternate; }
        @keyframes latir { from { box-shadow: var(--relieve); } to { box-shadow: var(--relieve), 0 0 0 3px var(--aire); } }
      `, `<button class="relieve" part="boton"><slot></slot></button>`);
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
      super(`
        :host { display: inline-flex; align-items: center; gap: 12px; cursor: pointer; user-select: none; min-height: 44px; }
        .pista { width: 58px; height: 32px; border-radius: var(--radio-redondo); position: relative; transition: background .2s; }
        .perilla { position: absolute; top: 4px; left: 4px; width: 24px; height: 24px; border-radius: 50%; transition: left .2s ease, background .2s; }
        :host([encendido]) .pista { background: var(--acento); box-shadow: var(--hundido-suave); }
        :host([encendido]) .perilla { left: 30px; }
        .pista:focus-visible { box-shadow: var(--hundido-suave), 0 0 0 3px var(--foco); }
      `, `<div class="pista hundido-suave" role="switch" tabindex="0"><div class="perilla relieve"></div></div><span class="etiqueta"></span>`);
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
      super(`
        :host { display: block; min-width: 160px; }
        .cabeza { display: flex; justify-content: space-between; margin-bottom: 10px; }
        .valor { font-size: 13px; color: var(--texto); }
        .pista { position: relative; height: 14px; border-radius: var(--radio-redondo); touch-action: none; cursor: pointer; }
        .lleno { position: absolute; left: 3px; top: 3px; bottom: 3px; border-radius: var(--radio-redondo); background: var(--acento); opacity: .85; }
        .perilla { position: absolute; top: 50%; width: 30px; height: 30px; margin: -15px 0 0 -15px; border-radius: 50%; }
        .perilla::after { content: ""; position: absolute; inset: 10px; border-radius: 50%; background: var(--acento); opacity: .9; }
        .pista:focus-visible .perilla { box-shadow: var(--relieve), 0 0 0 3px var(--foco); }
      `, `<div class="cabeza"><span class="etiqueta"></span><span class="valor numero"></span></div>
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
      super(`
        :host { display: block; width: 14px; height: 160px; }
        :host([orientacion="horizontal"]) { width: 160px; height: 14px; }
        .caja { position: relative; width: 100%; height: 100%; border-radius: 7px; overflow: hidden; }
        .barra { position: absolute; left: 3px; right: 3px; bottom: 3px; border-radius: 4px;
                 background: linear-gradient(to top, var(--vu-bajo) 0%, var(--vu-bajo) 70%, var(--vu-medio) 85%, var(--vu-alto) 100%);
                 background-size: 100% var(--alto-total, 154px); background-position: bottom; transition: height .06s linear; }
        :host([orientacion="horizontal"]) .barra { top: 3px; bottom: 3px; left: 3px; right: auto; height: auto !important;
                 background: linear-gradient(to right, var(--vu-bajo) 0%, var(--vu-bajo) 70%, var(--vu-medio) 85%, var(--vu-alto) 100%);
                 background-size: var(--ancho-total, 154px) 100%; background-position: left; transition: width .06s linear; }
        .pico { position: absolute; left: 3px; right: 3px; height: 2px; background: var(--texto); opacity: .85; }
        :host([orientacion="horizontal"]) .pico { top: 3px; bottom: 3px; width: 2px; height: auto; left: 0; right: auto; }
      `, `<div class="caja hundido-suave"><div class="barra"></div><div class="pico"></div></div>`);
      this._nivel = { rms: -100, pico: -100 };
    }
    connectedCallback() {
      const r = this.getBoundingClientRect();
      this.style.setProperty("--alto-total", `${Math.max(10, r.height - 6)}px`);
      this.style.setProperty("--ancho-total", `${Math.max(10, r.width - 6)}px`);
    }
    set nivel(n) {
      this._nivel = n || { rms: -100, pico: -100 };
      const h = this.getAttribute("orientacion") === "horizontal";
      const f = fracDb(this._nivel.rms), p = fracDb(this._nivel.pico);
      const barra = this.$(".barra"), pico = this.$(".pico");
      if (h) { barra.style.width = `calc(${f * 100}% - 6px)`; pico.style.left = `calc(${p * 100}% - 4px)`; }
      else { barra.style.height = `calc(${f * 100}% - 6px)`; pico.style.bottom = `calc(${p * 100}% - 4px)`; }
      pico.style.opacity = this._nivel.pico > -59 ? ".85" : "0";
    }
    get nivel() { return this._nivel; }
  }

  // --- Fader vertical (con su vúmetro) --------------------------------------------------------
  class BaFader extends ConValor {
    constructor() {
      super(`
        :host { display: inline-flex; flex-direction: column; align-items: center; gap: 10px; width: 78px; }
        .cuerpo { display: flex; gap: 10px; height: var(--alto-fader, 220px); }
        .pista { position: relative; width: 18px; height: 100%; border-radius: 9px; touch-action: none; cursor: pointer; }
        .cero { position: absolute; left: -8px; right: -8px; height: 1px; background: var(--texto-tenue); }
        .tapa { position: absolute; left: 50%; width: 44px; height: 26px; margin: -13px 0 0 -22px; border-radius: 8px; }
        .tapa::after { content: ""; position: absolute; left: 8px; right: 8px; top: 12px; height: 2px; background: var(--acento); border-radius: 1px; }
        .pista:focus-visible .tapa { box-shadow: var(--relieve), 0 0 0 3px var(--foco); }
        ba-vumetro { height: 100%; width: 12px; }
        .valor { font-size: 12px; min-height: 16px; }
        .etiqueta { max-width: 78px; text-align: center; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
      `, `<span class="valor numero"></span>
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
      super(`
        :host { display: inline-flex; flex-direction: column; align-items: center; gap: 8px; width: 88px; user-select: none; }
        .aro { position: relative; width: 72px; height: 72px; border-radius: 50%; touch-action: none; cursor: ns-resize; }
        svg { position: absolute; inset: -6px; width: 84px; height: 84px; pointer-events: none; }
        .base { fill: none; stroke: var(--superficie-hundida); stroke-width: 4; stroke-linecap: round; }
        .arco { fill: none; stroke: var(--acento); stroke-width: 4; stroke-linecap: round; }
        .boton { position: absolute; inset: 9px; border-radius: 50%; }
        .marca { position: absolute; left: 50%; top: 6px; width: 3px; height: 12px; margin-left: -1.5px; border-radius: 2px; background: var(--acento); transform-origin: 50% 21px; }
        .aro:focus-visible .boton { box-shadow: var(--relieve), 0 0 0 3px var(--foco); }
        .valor { font-size: 12px; }
      `, `<div class="aro hundido-suave" tabindex="0" role="slider">
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
      super(`
        :host { display: inline-block; width: var(--lado, 200px); height: var(--lado, 200px); }
        .base { position: relative; width: 100%; height: 100%; border-radius: 50%; touch-action: none; cursor: grab; }
        .cruz { position: absolute; inset: 18%; border-radius: 50%; border: 1px dashed var(--texto-tenue); opacity: .5; }
        .palanca { position: absolute; left: 50%; top: 50%; width: 38%; height: 38%; margin: -19% 0 0 -19%; border-radius: 50%; transition: transform .18s ease; }
        .palanca::after { content: ""; position: absolute; inset: 32%; border-radius: 50%; background: var(--acento); opacity: .85; }
        .base.agarrado { cursor: grabbing; }
        .base.agarrado .palanca { transition: none; }
      `, `<div class="base hundido"><div class="cruz"></div><div class="palanca relieve"></div></div>`);
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
      super(`
        :host { display: block; }
        .tarjeta { border-radius: var(--radio); padding: 16px 18px 18px; height: 100%; display: flex; flex-direction: column; }
        header { display: flex; align-items: center; gap: 10px; margin-bottom: 14px; min-height: 30px; }
        h2 { margin: 0; font-size: 13px; text-transform: uppercase; letter-spacing: 0.09em; color: var(--texto-suave); font-weight: 650; flex: 1; }
        .ampliar { all: unset; cursor: pointer; width: 32px; height: 32px; border-radius: 10px; display: grid; place-items: center; color: var(--texto-suave); font-size: 15px; }
        .ampliar:hover { color: var(--acento); box-shadow: var(--relieve-alto); }
        .ampliar:active { box-shadow: var(--hundido-suave); }
        .contenido { flex: 1; min-height: 0; }
      `, `<section class="tarjeta relieve"><header><h2></h2><slot name="acciones"></slot>
            <button class="ampliar relieve" title="A pantalla completa, en una pestaña nueva" hidden>⤢</button></header>
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
    }
  }

  // --- Chip y tally --------------------------------------------------------------------------
  class BaChip extends Base {
    constructor() {
      super(`
        :host { display: inline-flex; }
        span { display: inline-flex; align-items: center; gap: 7px; padding: 5px 12px; border-radius: var(--radio-redondo); font-size: 12px; font-weight: 600; color: var(--texto-suave); }
        i { width: 8px; height: 8px; border-radius: 50%; background: var(--texto-tenue); }
        :host([estado="ok"]) i { background: var(--ok); box-shadow: 0 0 8px var(--ok); }
        :host([estado="aviso"]) i { background: var(--aviso); box-shadow: 0 0 8px var(--aviso); }
        :host([estado="aire"]) i { background: var(--aire); box-shadow: 0 0 10px var(--aire); }
        :host([estado="armado"]) i { background: var(--armado); box-shadow: 0 0 8px var(--armado); }
        :host([estado="aire"]) span { color: var(--aire); }
      `, `<span class="hundido-suave"><i></i><slot></slot></span>`);
    }
  }

  class BaTally extends Base {
    static get observedAttributes() { return ["estado", "etiqueta"]; }
    constructor() {
      super(`
        :host { display: inline-flex; }
        span { padding: 6px 14px; border-radius: var(--radio-chico); font-weight: 800; font-size: 13px; letter-spacing: 0.08em; text-transform: uppercase; color: var(--texto-tenue); }
        :host([estado="aire"]) span { background: var(--aire); color: #fff; box-shadow: 0 0 18px var(--aire); }
        :host([estado="armado"]) span { background: var(--armado); color: #fff; box-shadow: 0 0 12px var(--armado); }
      `, `<span class="hundido-suave"></span>`);
    }
    attributeChangedCallback() {
      const e = this.getAttribute("estado") || "nada";
      this.$("span").textContent = this.getAttribute("etiqueta") || (e === "aire" ? "Al aire" : e === "armado" ? "Armado" : "—");
    }
  }

  // --- Selector de tema ----------------------------------------------------------------------
  class BaTema extends Base {
    constructor() {
      super(`
        :host { display: inline-flex; }
        select { font: inherit; font-size: 13px; color: var(--texto); padding: 8px 12px; border-radius: var(--radio-chico); min-height: 38px; cursor: pointer; }
      `, `<select class="hundido-suave" title="Tema"></select>`);
      const sel = this.$("select");
      for (const t of BA.tema.lista) sel.append(new Option(t.nombre, t.id));
      sel.value = BA.tema.actual();
      sel.addEventListener("change", () => BA.tema.poner(sel.value));
      document.addEventListener("ba-tema", (e) => { sel.value = e.detail.tema; });
    }
  }

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
  definir("ba-tema", BaTema);
})();
