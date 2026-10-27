/* ============================================================================
   Web/board/board.js — Tablero de oradores (solo lectura)

   Lee GET /api/v1/board (cabecera X-BA-Board) cada segundo y lo dibuja; los
   relojes corren localmente entre lecturas, corregidos con el reloj de la
   sala (serverNowMs). La clave viene en el fragmento del enlace (#k=...), se
   guarda en el navegador y se borra de la barra de direcciones.
   ========================================================================== */

(async function () {
  "use strict";

  const $ = (id) => document.getElementById(id);
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
  const live = window.BALive ? BALive.create({ endpoint: "/api/v1/board/view", headers: { "X-BA-Board": key } }) : null;
  let liveIds = new Set();
  let offset = 0; // reloj de la sala - reloj local
  let lastOk = 0;

  const fmt = (ms) => {
    const neg = ms < 0, t = Math.floor(Math.abs(ms) / 1000);
    const h = Math.floor(t / 3600), m = Math.floor((t % 3600) / 60), s = t % 60;
    const body = h > 0 ? h + ":" + String(m).padStart(2, "0") + ":" + String(s).padStart(2, "0") : m + ":" + String(s).padStart(2, "0");
    return (neg ? "+" : "") + body;
  };
  const now = () => Date.now() + offset;
  function el(tag, cls, text) {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text !== undefined) e.textContent = text;
    return e;
  }
  function face(p, cls, withLive) {
    const f = el("div", "face " + (cls || ""), p.hasAvatar ? "" : (p.name || "?").slice(0, 1).toUpperCase());
    if (p.hasAvatar) {
      const stamp = Math.floor(Date.now() / 5000);
      f.style.backgroundImage = `url("/api/v1/board/avatar/${encodeURIComponent(p.id)}?k=${encodeURIComponent(key)}&t=${stamp}")`;
    }
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
      const r = await fetch("/api/v1/board", { headers: { "X-BA-Board": key }, cache: "no-store" });
      if (r.status === 401) return blocked(t("La clave del tablero cambió (se reinició la sala). Escaneá el QR de nuevo."));
      if (!r.ok) throw new Error(String(r.status));
      board = await r.json();
      offset = board.serverNowMs - Date.now();
      lastOk = Date.now();
      $("blocked").classList.add("hidden");
      render();
    } catch (e) { /* sin conexion: el punto se pone rojo */ }
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
    $("dot").classList.toggle("off", Date.now() - lastOk > 3000);
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
  setInterval(poll, 1000);
  setInterval(tick, 250);
  poll();
})();
