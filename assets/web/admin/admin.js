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
      const msg = { full: "La sala está llena: sacá a alguien primero", not_found: "Ya no está", state: "No aplica en su estado" }[data.error];
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
      const who = el("div", "who");
      who.append(el("strong", "", p.name), el("span", "muted", (p.connected ? "espera hace " : "desconectado · ") + since(p.joinedMs) +
                 (p.lobbySlot ? "" : " · sin lugar de preview")));
      const acts = el("div", "acts");
      acts.append(btn("Admitir", "go", () => act(p.id, "admit")),
                  btn(p.talk ? "Cortar" : "Hablar", p.talk ? "go" : "", () => act(p.id, "talk", { on: !p.talk }),
                      "Hablarle en privado (su video y su voz se ven y oyen en la app)"),
                  btn("Rechazar", "no", () => act(p.id, "reject")),
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
      const who = el("div", "who");
      const title = el("div", "line");
      title.append(el("span", "slot", String(p.slot)), el("strong", "", p.name));
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
                  btn("Borrar", "no", () => api("DELETE", `/api/v1/shares/${f.id}`).catch(() => {})));
      li.append(thumb, info, acts);
      shares.append(li);
    }
    $("shares-count").textContent = s.shares.length;
    $("shares-empty").classList.toggle("hidden", s.shares.length > 0);

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

  // Los "hace N s" se refrescan solos.
  setInterval(() => { if (snap) render(snap); }, 15000);

  // Arranque: si hay sesion va directo, si no, login.
  fetch("/api/v1/room", { credentials: "same-origin" })
    .then((r) => (r.status === 401 ? showLogin() : start()))
    .catch(showLogin);
})();
