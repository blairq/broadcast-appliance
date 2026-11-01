/* ============================================================================
   Web/board/board.js — Tablero de oradores (solo lectura)

   Lee GET /api/v1/board (cabecera X-BA-Clave) al abrir y después le llega
   cada cambio por el canal de control (comun/canal.js, con la clave en su
   primer mensaje); los relojes corren localmente, corregidos con el reloj de
   la sala (comun/reloj.js). La clave viene en el fragmento del enlace (#k=...),
   se guarda en el navegador y se borra de la barra de direcciones.
   ========================================================================== */

(async function () {
  "use strict";

  const llenar = (s, ...v) => BAi18n.llenar(s, ...v);
  const KEY = "ba.board.key";
  const hash = new URLSearchParams(location.hash.replace(/^#/, ""));
  let key = hash.get("k") || "";
  try {
    if (key) localStorage.setItem(KEY, key);
    else key = localStorage.getItem(KEY) || "";
  } catch (e) {}
  if (hash.has("k")) history.replaceState(null, "", location.pathname + location.search);

  let board = null;
  // Video en vivo de los oradores: relay de B (los mismos paquetes que manda
  // el invitado, sin re-codificar). Sin video, la foto.
  const live = window.BALive ? BALive.create({ endpoint: "/api/v1/board/view", headers: { "X-BA-Clave": key } }) : null;
  let liveIds = new Set();
  let lastOk = 0;
  let conectado = false; // el canal de control (lo que cambia llega por ahí)

  // «+0:42» pasado de tiempo (los ms, truncados a segundos).
  const fmt = (ms) => (ms < 0 ? "+" : "") + mmss(Math.floor(Math.abs(ms) / 1000));
  const now = BAReloj.ahora;
  // Las fotos con la clave en la cabecera (nunca en la dirección: queda en los registros):
  // se bajan con fetch y se muestran desde la memoria; una nueva cada 5 s como mucho.
  const avatares = new Map(); // id -> {stamp, url}
  function avatar(id, f) {
    const stamp = Math.floor(Date.now() / 5000);
    const a = avatares.get(id);
    if (a && a.url) f.style.backgroundImage = `url("${a.url}")`;
    if (a && a.stamp === stamp) return;
    avatares.set(id, { stamp, url: a ? a.url : "" });
    fetch("/api/v1/board/avatar/" + encodeURIComponent(id), { headers: { "X-BA-Clave": key }, cache: "no-store" })
      .then((r) => (r.ok ? r.blob() : null))
      .then((b) => {
        if (!b) return;
        const viejo = avatares.get(id);
        if (viejo && viejo.url) URL.revokeObjectURL(viejo.url);
        avatares.set(id, { stamp, url: URL.createObjectURL(b) });
        render();
      })
      .catch(() => {});
  }
  function face(p, cls, withLive) {
    const f = el("div", "face " + (cls || ""), p.hasAvatar ? "" : (p.name || "?").slice(0, 1).toUpperCase());
    if (p.hasAvatar) avatar(p.id, f);
    if (withLive && live && p.connected && p.mediaUp) {
      f.textContent = "";
      f.append(live.video(p.id, cls || "card"));
      liveIds.add(p.id);
    }
    return f;
  }

  async function poll() {
    if (!key) return blocked(t("Este enlace no trae la clave del tablero. Escaneá el QR desde la consola del operador."));
    try {
      const r = await fetch("/api/v1/board", { headers: { "X-BA-Clave": key }, cache: "no-store" });
      if (r.status === 401) return blocked(t("La clave del tablero cambió (se reinició la sala). Escaneá el QR de nuevo."));
      if (!r.ok) throw new Error(String(r.status));
      aplicar(await r.json());
    } catch (e) { /* sin conexion: el punto se pone rojo */ }
  }
  function aplicar(b) {
    board = b;
    BAReloj.tomar(board);
    lastOk = Date.now();
    $("blocked").classList.add("hidden");
    render();
  }
  function blocked(text) {
    $("blocked-text").textContent = text;
    $("blocked").classList.remove("hidden");
  }

  const carouselName = () => ({ manual: t("carrusel manual"), chain: t("entra el siguiente al sacar"), time: t("rotación por tiempo") });

  // Estructura: se rearma en cada lectura (1 s). Los relojes, cada 250 ms.
  function render() {
    const b = board;
    const cfg = b.speakers || {};
    liveIds = new Set();
    $("room").textContent = b.room || "—";
    $("meta").textContent = cfg.enabled
      ? `${carouselName()[cfg.carousel] || cfg.carousel}${cfg.carousel === "time" ? " (+" + cfg.graceSec + " s)" : ""} · ${cfg.multi ? t("varios al aire") : t("uno a la vez")} · ${llenar(t("%1 por orador"), fmt(cfg.defaultSec * 1000))}` +
        (b.lobby.length ? " · " + t("%n en el lobby", { n: b.lobby.length }) : "")
      : t("Modo oradores apagado");

    const onair = b.order.filter((p) => p.speaker === "onair");
    const queued = b.order.filter((p) => p.speaker === "queued");
    const done = b.order.filter((p) => p.speaker === "done");

    const oa = $("onair");
    oa.replaceChildren();
    for (const p of onair) {
      const h = el("div", "hero");
      h.dataset.id = p.id;
      const body = el("div", "body");
      body.append(el("div", "tag", t("AL AIRE")), el("div", "name", p.name));
      if (p.info) body.append(el("div", "info", p.info));
      body.append(el("div", "clock"), el("div", "sub", llenar(t("de %1"), fmt(p.allottedSec * 1000))));
      h.append(face(p, "hero", true), body);
      oa.append(h);
    }

    // Espera estimada: lo que le queda a los de al aire + los de adelante.
    let eta = onair.reduce((a, p) => a + Math.max(0, p.allottedSec * 1000 - (now() - p.onAirSinceMs)), 0);
    const q = $("queue");
    q.replaceChildren();
    let total = 0;
    queued.forEach((p, i) => {
      const c = el("div", "card" + (i === 0 ? " next" : "") + (p.connected ? "" : " away"));
      c.append(face(p, "card", true), el("span", "ord", String(i + 1)),
               el("span", "badge", !p.connected ? t("desconectado") : i === 0 ? t("PRÓXIMO") : t("en fila")),
               el("div", "name", p.name));
      if (p.info) c.append(el("div", "info", p.info));
      if (p.contact) c.append(el("div", "contact", "☎ " + p.contact));
      const tm = el("div", "times");
      const a = el("span", "", t("tiempo") + " ");
      a.append(el("strong", "", fmt(p.allottedSec * 1000)));
      tm.append(a, el("span", "", llenar(t("entra en ~%1"), fmt(eta))));
      c.append(tm);
      q.append(c);
      eta += p.allottedSec * 1000;
      total += p.allottedSec * 1000;
    });
    $("qcount").textContent = queued.length;
    $("qtotal").textContent = queued.length ? "· " + llenar(t("total %1"), fmt(total)) : "";
    $("qempty").classList.toggle("hidden", queued.length > 0);

    const d = $("done");
    d.replaceChildren();
    for (const p of done) {
      const r = el("div", "row");
      const over = p.usedMs - p.allottedSec * 1000;
      r.append(face(p), el("span", "name", p.name));
      if (p.contact) r.append(el("span", "contact", "☎ " + p.contact));
      r.append(el("span", "used" + (over > 0 ? " over" : ""), fmt(p.usedMs) + (over > 999 ? " " + llenar(t("(se pasó %1)"), fmt(over).replace("+", "")) : "")));
      d.append(r);
    }
    $("dcount").textContent = done.length;
    if (live) live.keep(liveIds);
    tick();
  }

  function tick() {
    $("now").textContent = new Date().toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", second: "2-digit" });
    $("dot").classList.toggle("off", !conectado);
    if (!board) return;
    for (const h of document.querySelectorAll(".hero")) {
      const p = board.order.find((x) => x.id === h.dataset.id);
      if (!p) continue;
      const left = p.allottedSec * 1000 - (now() - p.onAirSinceMs);
      h.classList.toggle("over", left < 0);
      h.classList.toggle("warn", left >= 0 && left <= 10000);
      h.querySelector(".clock").textContent = left >= 0 ? fmt(Math.ceil(left / 1000) * 1000) : fmt(left);
      h.querySelector(".tag").textContent = left < 0 ? t("AL AIRE · SE PASÓ") : t("AL AIRE");
    }
  }

  // Los textos, en el idioma del evento.
  await BAi18n.iniciar({ contexto: "web/board", preferir: "evento" });
  setInterval(tick, 250);
  poll();
  if (key)
    window.BACanal.abrir({
      tipo: "tablero",
      clave: key,
      que: () => (board && board.order ? board.order.length : 0) + "",
      onAviso: (m) => { if (m.type === "tablero") aplicar(m.board); },
      onClaveMal: () => blocked(t("La clave del tablero cambió (se reinició la sala). Escaneá el QR de nuevo.")),
      onEstado: (on) => { conectado = on; if (on) poll(); },
    });
})();
