/* La tarjeta «Mezclador» del dashboard: un fader por canal, mute y loudness. Lo común, en comun.js. */
(() => {
  "use strict";
  const { el, boton, maxDb, ms, vacio, texto, attr, sincronizar } = BA.tj;
  BA.tarjetas.push(
  {
    id: "audio", titulo: "Mezclador", temas: ["audio"], ancha: true,
    herramientas: ["audio_set"],
    crear(c, api) {
      this.api = api;
      this.loud = el("div", "fila loudness");
      this.chips = ["M", "S", "I", "TP"].map((k) => { const ch = el("ba-chip"); ch.dataset.k = k; this.loud.append(ch); return ch; });
      this.objetivo = el("span", "suave");
      this.loud.append(this.objetivo);
      this.canales = el("div", "canales");
      c.append(this.loud, this.canales);
    },
    actualizar(_, d) {
      const l = d.loudness || {};
      const vals = { M: l.m, S: l.s, I: l.i, TP: l.tp };
      for (const ch of this.chips) {
        const v = vals[ch.dataset.k];
        attr(ch, "estado", l.fuera_de_objetivo ? "aviso" : "ok");
        texto(ch, ch.dataset.k + " " + (v > -100 ? v.toFixed(1) : "—"));
      }
      texto(this.objetivo, l.objetivo ? "objetivo " + l.objetivo.lufs + " LUFS" : "");
      const entradas = d.entradas || [];
      sincronizar(this.canales, entradas.map((e) => e.nombre), (nombre) => {
        const canal = el("div", "canal");
        const f = el("ba-fader");
        f.setAttribute("etiqueta", nombre);
        f.addEventListener("final", (e) => this.api.hacer("audio_set", { name: nombre, volumeDb: e.detail.valor }).catch(() => {}));
        f.addEventListener("cambio", () => { f.dataset.tocando = Date.now(); });
        const mute = boton("M", () => this.api.hacer("audio_set", { name: nombre, muted: !mute.hasAttribute("activo") }), { compacto: true });
        canal.append(f, mute);
        canal.fader = f; canal.mute = mute;
        return canal;
      }, (canal, nombre) => {
        const e = entradas.find((x) => x.nombre === nombre);
        // Mientras el dedo lo mueve (o recién), no se pisa con lo que dice la app.
        if ((!canal.fader.dataset.tocando || Date.now() - canal.fader.dataset.tocando > 800) && canal.fader.valor !== e.volumen_db)
          canal.fader.valor = e.volumen_db;
        canal.fader.nivel = { rms: maxDb(e.rms_db), pico: maxDb(e.pico_db) };
        if (canal.mute.hasAttribute("activo") !== !!e.mute) canal.mute.toggleAttribute("activo", !!e.mute);
        attr(canal.mute, "variante", e.mute ? "aire" : "");
      });
    },
  });
})();
