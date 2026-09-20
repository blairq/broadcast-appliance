/* ============================================================================
   Web/admin/admin.js — Consola web del operador

   Cliente de la API de control de B (ver src/gateway/AdminApi.cpp):
     - estado: EventSource /api/v1/events ("room" = snapshot completo,
       "chat" = un mensaje). El snapshot se re-renderiza entero: es chico y
       asi la vista nunca queda desincronizada.
     - acciones: POST/DELETE con la cabecera X-BA-Admin (defensa CSRF de la
       cookie de sesion).
   ========================================================================== */

(function () {
  "use strict";

  const $ = (id) => document.getElementById(id);
  let snap = null;
  let es = null;
  let pinVisible = false;
  let clockOffset = 0; // reloj de la sala - reloj local
  // Video en vivo de los participantes (relay de B, sin re-codificar).
  const live = window.BALive ? BALive.create({ endpoint: "/api/v1/view", headers: { "X-BA-Admin": "1" } }) : null;
  let liveIds = new Set();
  /// Miniatura en vivo si tiene medios; si no, nada.
  function liveThumb(p, place) {
    const box = el("div", "thumbv");
    if (live && p.connected && p.mediaUp) {
      box.append(live.video(p.id, place));
      liveIds.add(p.id);
    } else {
      box.classList.add("off");
      box.textContent = (p.name || "?").slice(0, 1).toUpperCase();
    }
    return box;
  }
  const knownLobby = new Set();

  // --- API ----------------------------------------------------------------
  async function api(method, path, body) {
    const res = await fetch(path, {
      method,
      credentials: "same-origin",
      headers: { "Content-Type": "application/json", "X-BA-Admin": "1" },
      body: body === undefined ? undefined : JSON.stringify(body),
    });
    if (res.status === 401) { showLogin(); throw new Error("auth"); }
    const data = await res.json().catch(() => ({}));
    if (!res.ok) {
      const msg = { full: "La sala está llena: sacá a alguien primero", not_found: "Ya no está", state: "No aplica en su estado", empty: "No hay nadie más en la fila" }[data.error];
      toast(msg || "Error: " + (data.error || res.status), "bad");
      throw new Error(data.error || String(res.status));
    }
    return data;
  }
  const act = (id, action, body) => api("POST", `/api/v1/participants/${encodeURIComponent(id)}/${action}`, body).catch(() => {});

  // --- Login --------------------------------------------------------------
  function showLogin() {
    if (es) { es.close(); es = null; }
    $("app").classList.add("hidden");
    $("login").classList.remove("hidden");
  }
  async function login() {
    $("login-error").textContent = "";
    const res = await fetch("/api/v1/login", {
      method: "POST",
      credentials: "same-origin",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ token: $("login-token").value.trim() }),
    });
    if (!res.ok) { $("login-error").textContent = "Token incorrecto."; return; }
    start();
  }

  // --- Eventos (SSE) ------------------------------------------------------
  function start() {
    $("login").classList.add("hidden");
    $("app").classList.remove("hidden");
    if (es) es.close();
    es = new EventSource("/api/v1/events", { withCredentials: true });
    es.addEventListener("room", (e) => render(JSON.parse(e.data)));
    es.addEventListener("chat", (e) => addChat(JSON.parse(e.data), true));
    es.onopen = () => $("live-dot").classList.remove("off");
    es.onerror = async () => {
      $("live-dot").classList.add("off");
      // EventSource reintenta solo; si fue por credenciales, al login.
      const r = await fetch("/api/v1/room", { credentials: "same-origin" }).catch(() => null);
      if (r && r.status === 401) showLogin();
    };
  }

  // --- Render -------------------------------------------------------------
  const since = (ms) => {
    const s = Math.max(0, Math.round((Date.now() - ms) / 1000));
    return s < 60 ? `${s} s` : `${Math.floor(s / 60)} min`;
  };
  function el(tag, cls, text) {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text !== undefined) e.textContent = text;
    return e;
  }
  function btn(text, cls, onClick, title) {
    const b = el("button", "chip " + (cls || ""), text);
    if (title) b.title = title;
    b.addEventListener("click", onClick);
    return b;
  }

  function render(s) {
    const firstRender = snap === null;
    snap = s;
    liveIds = new Set();
    if (s.serverNowMs) clockOffset = s.serverNowMs - Date.now();
    $("room-name").textContent = s.room;
    $("guest-url").textContent = s.guestUrl;
    $("guest-url").href = s.guestUrl;
    $("slots").textContent = `${s.maxGuests - s.slotsFree}/${s.maxGuests} lugares`;
    $("pin").textContent = pinVisible ? (s.pin || "—") : "••••••";
    $("sw-lobby").checked = s.lobby;
    $("sw-locked").checked = s.locked;

    const waiting = s.participants.filter((p) => p.state === "waiting");
    const inside = s.participants.filter((p) => p.state === "admitted").sort((a, b) => a.slot - b.slot);

    // Aviso de gente nueva en el lobby.
    for (const p of waiting) {
      if (!knownLobby.has(p.id) && !firstRender) toast(`${p.name} espera en el lobby`, "ok");
      knownLobby.add(p.id);
    }

    const lobby = $("lobby");
    lobby.replaceChildren();
    for (const p of waiting) {
      const li = el("li", "row" + (p.connected ? "" : " away") + (p.talk ? " talking" : ""));
      li.append(liveThumb(p, "lista"));
      const who = el("div", "who");
      who.append(el("strong", "", p.name), el("span", "muted", (p.connected ? "espera hace " : "desconectado · ") + since(p.joinedMs) +
                 (p.lobbySlot ? "" : " · sin lugar de preview")));
      if (p.info) who.append(el("span", "muted", p.info));
      if (p.contact) who.append(el("span", "muted", "☎ " + p.contact));
      const acts = el("div", "acts");
      acts.append(btn("Admitir", "go", () => act(p.id, "admit")),
                  btn(p.talk ? "Cortar" : "Hablar", p.talk ? "go" : "", () => act(p.id, "talk", { on: !p.talk }),
                      "Hablarle en privado (su video y su voz se ven y oyen en la app)"),
                  btn("Rechazar", "no", () => {
                    // El snapshot rearma la lista: se fija a quien antes de preguntar.
                    const id = p.id, name = p.name;
                    if (confirm(`¿Rechazar a ${name}? Sale del lobby sin entrar a la sala.`)) act(id, "reject");
                  }),
                  btn("💬", "", () => dm(p)));
      li.append(who, acts);
      lobby.append(li);
    }
    $("lobby-count").textContent = waiting.length;
    $("lobby-empty").classList.toggle("hidden", waiting.length > 0);
    $("btn-admit-all").classList.toggle("hidden", waiting.length < 2);

    const people = $("people");
    people.replaceChildren();
    for (const p of inside) {
      const li = el("li", "row person" + (p.connected ? "" : " away"));
      li.append(liveThumb(p, "lista"));
      const who = el("div", "who");
      const title = el("div", "line");
      title.append(el("span", "slot", p.slot ? String(p.slot) : "·"), el("strong", "", p.name));
      if (p.hand) title.append(el("span", "hand", "✋"));
      const badges = el("div", "badges");
      const badge = (on, a, b) => badges.append(el("span", "badge " + (on ? "on" : "off"), on ? a : b));
      badge(p.connected, "en línea", "reconectando…");
      badge(p.mediaUp, "medios ✓", "sin medios");
      badge(p.mic, "🎙️", "🎙️ off");
      badge(p.cam, "📷", "📷 off");
      if (p.screen) badges.append(el("span", "badge share", "🖥️ comparte pantalla"));
      who.append(title, badges);
      const acts = el("div", "acts");
      if (p.mic) acts.append(btn("🔇", "", () => act(p.id, "request", { what: "mute-mic" }), "Silenciar micrófono"));
      if (p.cam) acts.append(btn("📷✕", "", () => act(p.id, "request", { what: "mute-cam" }), "Pedir apagar cámara"));
      if (p.screen) acts.append(btn("🖥️✕", "", () => act(p.id, "request", { what: "stop-screen" }), "Dejar de mostrar su pantalla"));
      if (p.hand) acts.append(btn("Bajar ✋", "", () => act(p.id, "request", { what: "lower-hand" })));
      acts.append(btn("💬", "", () => dm(p), "Mensaje directo"),
                  btn("Sacar", "no", () => { if (confirm(`¿Sacar a ${p.name} de la sala?`)) act(p.id, "kick"); }));
      li.append(who, acts);
      people.append(li);
    }
    $("people-count").textContent = inside.length;
    $("people-empty").classList.toggle("hidden", inside.length > 0);

    const shares = $("shares");
    shares.replaceChildren();
    for (const f of [...s.shares].reverse()) {
      const li = el("li", "share");
      const thumb = el("div", "thumb");
      if (f.kind === "image") {
        const img = el("img");
        img.loading = "lazy";
        img.src = `/api/v1/shares/${f.id}/content`;
        thumb.append(img);
      } else {
        thumb.textContent = f.kind === "video" ? "🎬" : "🎵";
      }
      thumb.addEventListener("click", () => view(f));
      const info = el("div", "info");
      info.append(el("strong", "", f.name), el("span", "muted", `${f.participantName} · ${(f.bytes / 1048576).toFixed(1)} MB · hace ${since(f.atMs)}`));
      const acts = el("div", "acts");
      acts.append(btn("Ver", "", () => view(f)),
                  btn("Borrar", "no", () => {
                    const id = f.id, name = f.name;
                    if (!confirm(`¿Borrar el archivo «${name}»? Se elimina del equipo y no se puede recuperar.`)) return;
                    api("DELETE", `/api/v1/shares/${id}`).catch(() => {});
                  }));
      li.append(thumb, info, acts);
      shares.append(li);
    }
    $("shares-count").textContent = s.shares.length;
    $("shares-empty").classList.toggle("hidden", s.shares.length > 0);

    renderSpeakers(s);
    if (live) live.keep(liveIds);

    // Destinos del chat.
    const sel = $("chat-to"), cur = sel.value;
    sel.replaceChildren(new Option("A todos", "all"));
    for (const p of s.participants) sel.append(new Option(`A ${p.name}`, p.id));
    sel.value = [...sel.options].some((o) => o.value === cur) ? cur : "all";

    if (firstRender) {
      $("chat-log").replaceChildren();
      for (const m of s.chat) addChat(m, false);
    }
  }

  // --- Oradores -----------------------------------------------------------
  const fmt = (ms) => {
    const neg = ms < 0, t = Math.floor(Math.abs(ms) / 1000);
    return (neg ? "+" : "") + Math.floor(t / 60) + ":" + String(t % 60).padStart(2, "0");
  };
  const parseTime = (txt) => {
    // "m:ss", "m" (minutos) o "90s".
    const t = String(txt).trim();
    if (/^\d+:\d{1,2}$/.test(t)) { const [m, s] = t.split(":").map(Number); return m * 60 + s; }
    if (/^\d+s$/.test(t)) return parseInt(t, 10);
    if (/^\d+([.,]\d+)?$/.test(t)) return Math.round(parseFloat(t.replace(",", ".")) * 60);
    return 0;
  };
  // Tiempo que le queda (negativo = pasado) a quien esta al aire.
  const leftMs = (p) => p.allottedSec * 1000 - (Date.now() + clockOffset - p.onAirSinceMs);
  const setIfIdle = (input, v) => { if (document.activeElement !== input) input.value = v; };

  function renderSpeakers(s) {
    const cfg = s.speakers || {};
    $("panel-speakers").classList.toggle("spk-off", !cfg.enabled);
    $("sw-speakers").checked = !!cfg.enabled;
    setIfIdle($("spk-default"), fmt((cfg.defaultSec || 0) * 1000));
    setIfIdle($("spk-multi"), cfg.multi ? "1" : "0");
    setIfIdle($("spk-carousel"), cfg.carousel || "manual");
    setIfIdle($("spk-grace"), cfg.graceSec || 0);
    $("spk-grace-l").classList.toggle("hidden", cfg.carousel !== "time");
    const slotSel = $("spk-slot");
    if (slotSel.options.length !== s.maxGuests) {
      slotSel.replaceChildren();
      for (let i = 1; i <= s.maxGuests; i++) slotSel.append(new Option(String(i), String(i)));
    }
    setIfIdle(slotSel, String(cfg.defaultSlot || 1));

    const byId = new Map(s.participants.map((p) => [p.id, p]));
    const order = (s.order || []).map((id) => byId.get(id)).filter(Boolean);
    const strip = $("onair-strip"), row = $("spk-row");
    strip.replaceChildren();
    row.replaceChildren();
    const stamp = Math.floor(Date.now() / 5000); // refresca las fotos cada ~5 s
    let queuedPos = 0;
    order.forEach((p, i) => {
      if (p.speaker === "onair") {
        const oc = el("div", "onair-card");
        oc.dataset.clock = p.id;
        const who = el("div", "who");
        who.append(el("span", "muted", "AL AIRE" + (p.slot ? " · slot " + p.slot : "")), el("strong", "", p.name));
        oc.append(who, el("div", "clock"), btn("Sacar del aire", "no", () => act(p.id, "onair", { on: false })));
        strip.append(oc);
      }
      const card = el("div", "spk " + p.speaker + (p.connected ? "" : " away"));
      card.dataset.clock = p.id;
      const face = el("div", "face", p.hasAvatar ? "" : (p.name || "?").slice(0, 1).toUpperCase());
      if (p.hasAvatar) face.style.backgroundImage = `url("/api/v1/participants/${encodeURIComponent(p.id)}/avatar?t=${stamp}")`;
      if (live && p.connected && p.mediaUp) {
        face.textContent = "";
        face.append(live.video(p.id, "fila"));
        liveIds.add(p.id);
      }
      const ord = el("span", "ord", p.speaker === "queued" ? String(++queuedPos) : p.speaker === "onair" ? "●" : "✓");
      const st = el("span", "st", { queued: "en fila", onair: "al aire", done: "habló" }[p.speaker] || p.speaker);
      if (!p.connected) st.textContent = "desconectado";
      card.append(face, ord, st, el("div", "name", p.name));
      if (p.info) card.append(el("div", "info", p.info));
      if (p.contact) { const ct = el("div", "contact", "☎ " + p.contact); ct.title = p.contact; card.append(ct); }
      const clock = el("div", "clock");
      card.append(clock);
      const acts = el("div", "acts");
      acts.append(btn("◀", "", () => act(p.id, "move", { index: i - 1 }), "Adelantar en la fila"),
                  btn("▶", "", () => act(p.id, "move", { index: i + 1 }), "Atrasar en la fila"));
      if (p.speaker === "onair") acts.append(btn("Sacar", "no", () => act(p.id, "onair", { on: false })));
      else acts.append(btn("Al aire", "go", () => act(p.id, "onair", { on: true })));
      if (p.speaker === "done") acts.append(btn("↺", "", () => act(p.id, "requeue"), "Volver a la fila (al final)"));
      acts.append(btn("✎", "", () => editProfile(p), "Editar datos y tiempo"));
      card.append(acts);
      row.append(card);
    });
    $("spk-empty").classList.toggle("hidden", order.length > 0);
    tickClocks();
  }

  // Los relojes corren sin re-renderizar todo (4 veces por segundo).
  function tickClocks() {
    if (!snap) return;
    const byId = new Map(snap.participants.map((p) => [p.id, p]));
    for (const node of document.querySelectorAll("[data-clock]")) {
      const p = byId.get(node.dataset.clock);
      if (!p) continue;
      const clock = node.querySelector(".clock");
      node.classList.remove("warn", "over");
      if (p.speaker === "onair") {
        const left = leftMs(p);
        clock.textContent = left >= 0 ? fmt(Math.ceil(left / 1000) * 1000) : fmt(left);
        if (left < 0) node.classList.add("over");
        else if (left <= 10000) node.classList.add("warn");
      } else if (p.speaker === "done") {
        clock.textContent = "usó " + fmt(p.usedMs || 0);
      } else {
        clock.textContent = fmt(p.allottedSec * 1000);
      }
    }
  }
  setInterval(tickClocks, 250);

  let editing = null;
  function editProfile(p) {
    editing = p.id;
    $("prof-name").value = p.name || "";
    $("prof-info").value = p.info || "";
    $("prof-contact").value = p.contact || "";
    $("prof-time").value = fmt(p.allottedSec * 1000);
    $("profdlg").showModal();
  }
  $("profdlg").addEventListener("close", async () => {
    if ($("profdlg").returnValue !== "save" || !editing) return;
    const id = editing;
    editing = null;
    await act(id, "profile", { name: $("prof-name").value, info: $("prof-info").value, contact: $("prof-contact").value });
    const sec = parseTime($("prof-time").value);
    if (sec > 0) await act(id, "time", { sec });
  });

  const spkCfg = (body) => api("POST", "/api/v1/speakers", body).catch(() => {});
  $("sw-speakers").addEventListener("change", (e) => spkCfg({ enabled: e.target.checked }));
  $("spk-default").addEventListener("change", (e) => {
    const sec = parseTime(e.target.value);
    if (sec > 0) spkCfg({ defaultSec: sec }); else toast("Tiempo inválido (usá m:ss o minutos)", "bad");
  });
  $("spk-multi").addEventListener("change", (e) => spkCfg({ multi: e.target.value === "1" }));
  $("spk-slot").addEventListener("change", (e) => spkCfg({ defaultSlot: parseInt(e.target.value, 10) }));
  $("spk-carousel").addEventListener("change", (e) => spkCfg({ carousel: e.target.value }));
  $("spk-grace").addEventListener("change", (e) => spkCfg({ graceSec: parseInt(e.target.value, 10) || 0 }));
  $("btn-spk-next").addEventListener("click", () =>
    api("POST", "/api/v1/speakers/next").catch(() => {}));

  function view(f) {
    const body = $("viewer-body");
    body.replaceChildren();
    const url = `/api/v1/shares/${f.id}/content`;
    const media = f.kind === "image" ? el("img") : el(f.kind === "video" ? "video" : "audio");
    media.src = url;
    if (f.kind !== "image") media.controls = true;
    body.append(el("h3", "", f.name), media);
    $("viewer").showModal();
  }

  function dm(p) {
    $("chat-to").value = p.id;
    $("chat-input").focus();
  }

  function addChat(m, live) {
    const who = m.fromId === "host" ? "Vos (operador)" : m.fromName;
    const to = m.to === "all" ? "" : m.to === "host" ? " → operador" : " → " + nameOf(m.to);
    const row = el("div", "msg" + (m.fromId === "host" ? " mine" : ""));
    row.append(el("span", "who", who + to), el("span", "txt", m.text));
    $("chat-log").append(row);
    $("chat-log").scrollTop = $("chat-log").scrollHeight;
    if (live && m.fromId !== "host") toast(`${m.fromName}: ${m.text}`);
  }
  const nameOf = (id) => ((snap && snap.participants.find((p) => p.id === id)) || { name: id }).name;

  function toast(text, kind) {
    const t = el("div", "toast " + (kind || ""), text);
    $("toasts").append(t);
    setTimeout(() => t.classList.add("out"), 3500);
    setTimeout(() => t.remove(), 4000);
  }

  // --- Controles ----------------------------------------------------------
  $("btn-login").addEventListener("click", login);
  $("login-token").addEventListener("keydown", (e) => { if (e.key === "Enter") login(); });
  $("sw-lobby").addEventListener("change", (e) => api("POST", "/api/v1/room", { lobby: e.target.checked }).catch(() => {}));
  $("sw-locked").addEventListener("change", (e) => api("POST", "/api/v1/room", { locked: e.target.checked }).catch(() => {}));
  $("btn-pin-show").addEventListener("click", () => {
    pinVisible = !pinVisible;
    $("btn-pin-show").textContent = pinVisible ? "Ocultar" : "Ver";
    if (snap) render(snap);
  });
  $("btn-pin-new").addEventListener("click", async () => {
    if (!confirm("¿Generar un PIN nuevo? El anterior deja de servir para entrar.")) return;
    const r = await api("POST", "/api/v1/pin", {}).catch(() => null);
    if (r) { pinVisible = true; toast("PIN nuevo: " + r.pin, "ok"); }
  });
  $("btn-copy").addEventListener("click", () => {
    if (!snap) return;
    navigator.clipboard.writeText(snap.guestUrl).then(() => toast("Enlace copiado", "ok"), () => {});
  });
  $("btn-admit-all").addEventListener("click", async () => {
    for (const p of snap.participants.filter((x) => x.state === "waiting")) {
      try { await api("POST", `/api/v1/participants/${p.id}/admit`); } catch (e) { break; }
    }
  });
  $("chat-form").addEventListener("submit", (e) => {
    e.preventDefault();
    const text = $("chat-input").value.trim();
    if (!text) return;
    api("POST", "/api/v1/chat", { text, to: $("chat-to").value }).then(() => ($("chat-input").value = ""), () => {});
  });
  // --- Acceso directo (HTTPS + tunel WireGuard) ------------------------------
  async function refreshNetwork() {
    const r = await fetch("/api/v1/network", { credentials: "same-origin" }).catch(() => null);
    if (!r || !r.ok) return;
    const n = await r.json();
    const badges = $("net-badges");
    badges.replaceChildren();
    const tls = n.https || {}, wg = n.wireguard || {};
    badges.append(el("span", "badge " + (tls.enabled ? "on" : "off"), tls.enabled ? (tls.selfSigned ? "HTTPS (autofirmado)" : "HTTPS") : "sin HTTPS"));
    badges.append(el("span", "badge " + (wg.enabled && wg.up ? "on" : "off"), !wg.enabled ? "túnel apagado" : wg.up ? "túnel conectado" : "túnel sin conexión"));
    const urls = $("net-urls");
    urls.replaceChildren();
    for (const u of n.urls || []) {
      const li = el("li", "row");
      const a = el("a", "", u);
      a.href = u;
      a.target = "_blank";
      li.append(a, btn("Copiar", "", () => navigator.clipboard.writeText(u).then(() => toast("Copiado", "ok"))));
      urls.append(li);
    }
    $("net-wg").textContent = wg.enabled
      ? `Público: ${wg.publicAddress || "—"} · servidor ${wg.endpoint || "—"} · clave de este equipo ${wg.publicKey || "—"}`
      : `Clave pública de este equipo: ${wg.publicKey || "—"} · WebRTC UDP ${(n.rtcPorts || []).join("–")}`;
  }
  $("btn-wg").addEventListener("click", () => {
    $("wg-conf").classList.add("hidden");
    $("wgdlg").showModal();
  });
  $("btn-wg-gen").addEventListener("click", async () => {
    const r = await api("POST", "/api/v1/network/server-config", { endpoint: $("wg-endpoint").value.trim(), interface: $("wg-iface").value.trim() }).catch(() => null);
    if (!r) return;
    $("wg-conf").textContent = r.config;
    $("wg-conf").classList.remove("hidden");
    toast("Clave pública del servidor: " + r.serverPublicKey, "ok");
  });
  $("btn-wg-copy").addEventListener("click", () => navigator.clipboard.writeText($("wg-conf").textContent).then(() => toast("Copiado", "ok")));
  setInterval(refreshNetwork, 5000);
  refreshNetwork();

  // --- QR de ingreso al lobby ---------------------------------------------
  $("btn-qr").addEventListener("click", (e) => {
    e.stopPropagation();
    $("qr-menu").classList.toggle("hidden");
  });
  document.addEventListener("click", () => $("qr-menu").classList.add("hidden"));
  for (const b of $("qr-menu").querySelectorAll("button[data-qr]")) {
    b.addEventListener("click", () => showQr(b.dataset.qr));
  }
  let qrTarget = "local";
  $("qr-pin").addEventListener("change", () => showQr(qrTarget));
  $("btn-board").addEventListener("click", (e) => {
    e.stopPropagation();
    $("board-menu").classList.toggle("hidden");
  });
  document.addEventListener("click", () => $("board-menu").classList.add("hidden"));
  for (const b of $("board-menu").querySelectorAll("button[data-board]")) {
    b.addEventListener("click", () => showQr(b.dataset.board, true));
  }
  async function showQr(target, board) {
    $("qr-menu").classList.add("hidden");
    $("board-menu").classList.add("hidden");
    $("qr-pin").parentElement.classList.toggle("hidden", !!board);
    $("qr-title").textContent = board ? "Tablero de oradores (solo lectura) — " + (target === "remote" ? "por internet" : "red local")
      : target === "remote" ? "QR remoto — por internet (Cloudflare)" : "QR local — misma red";
    const canvas = $("qr-canvas"), box = canvas.parentElement;
    canvas.classList.add("hidden");
    box.classList.add("empty");
    $("qr-msg").textContent = "Generando…";
    $("qr-url").textContent = "";
    if (!$("qrdlg").open) $("qrdlg").showModal();
    if (!board) qrTarget = target;
    const withPin = !board && $("qr-pin").checked;
    $("qr-note").textContent = board ? "Abrilo en la tablet: muestra la fila, las caras y los tiempos. La clave cambia en cada arranque."
      : withPin ? "Incluye el PIN: quien tenga este QR puede pedir entrar (igual pasa por el lobby)." : "El PIN no va en el QR: pasalo aparte.";
    const path = board ? "/api/v1/board-link?target=" + target : "/api/v1/qr?target=" + target + (withPin ? "&pin=1" : "");
    const r = await fetch(path, { credentials: "same-origin" }).then((x) => x.json()).catch(() => null);
    if (!r || !r.ok) {
      $("qr-msg").textContent = (r && r.message) || "La sala no responde.";
      return;
    }
    const rows = r.modules, n = rows.length + 8; // zona silenciosa de 4 modulos
    const cell = Math.floor(canvas.width / n), off = Math.floor((canvas.width - cell * n) / 2) + 4 * cell;
    const ctx = canvas.getContext("2d");
    ctx.fillStyle = "#fff";
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    ctx.fillStyle = "#000";
    rows.forEach((row, y) => { for (let x = 0; x < row.length; x++) if (row[x] === "1") ctx.fillRect(off + x * cell, off + y * cell, cell, cell); });
    canvas.classList.remove("hidden");
    box.classList.remove("empty");
    $("qr-msg").textContent = r.warning || "";
    $("qr-url").textContent = r.url;
    $("qr-url").href = r.url;
  }
  $("btn-qr-copy").addEventListener("click", () => navigator.clipboard.writeText($("qr-url").textContent).then(() => toast("Enlace copiado", "ok")));

  // Los "hace N s" se refrescan solos.
  setInterval(() => { if (snap) render(snap); }, 15000);

  // Arranque: si hay sesion va directo, si no, login.
  fetch("/api/v1/room", { credentials: "same-origin" })
    .then((r) => (r.status === 401 ? showLogin() : start()))
    .catch(showLogin);
})();
