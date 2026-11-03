#!/usr/bin/env python3
"""Prueba del tablero web (/control/app/, Qt para WebAssembly) de punta a punta.

Con la app corriendo (abierta con `--args --test`, que aprueba sola lo que pide una persona)
y el tablero en el bundle (`make tablero-web` antes de `make app`):

  1. Crea un evento de cero («ZZ prueba tablero <hora>») y lo abre. Si lo abierto no es ese,
     corta sin tocar nada.
  2. Empareja un aparato con el perfil «todo» (como el control web: control_web_emparejar →
     POST /api/control/emparejar → control_web_resolver → token).
  3. Réplica por el WebSocket real (sin navegador): llega el esquema y los valores de Studio;
     abrir un hijo; el video del programa (cuántos cuadros, de qué tamaño y cuánto tardan desde
     que llegan a la app hasta acá: en la misma máquina, el transporte).
  4. Navegador sin ventana (chrome-headless-shell del proyecto, por CDP): carga /control/app/,
     mide cuánto tarda en estar listo, saca capturas, arma una escena por MCP y aprieta TAKE
     en la página: el programa tiene que cambiar. Lee las latencias que mide la página.
  5. Con --webrtc: abre también el programa por WebRTC (/cast/, buffer 0) en la misma página
     y saca capturas de los dos juntos (para comparar la hora quemada en el video).
  6. Quita el aparato, vuelve al evento de antes y borra el de prueba.

    tools/test/tablero-web.py [--capturas DIR] [--webrtc] [--cerrar]
"""
import argparse
import base64
import importlib.util
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

AQUI = os.path.dirname(os.path.abspath(__file__))
RAIZ = os.path.dirname(os.path.dirname(AQUI))
_spec = importlib.util.spec_from_file_location("control_web_mcp", os.path.join(AQUI, "control-web-mcp.py"))
_cw = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_cw)
conectar, Prueba, BA_MCP, Ws, emparejar, http = _cw.conectar, _cw.Prueba, _cw.BA_MCP, _cw.Ws, _cw.emparejar, _cw.http
CHROME = os.path.join(RAIZ, "extern", "chrome-headless", "mac-arm64", "chrome-headless-shell")


class Cdp:
    """Chrome por el protocolo de depuración (un WebSocket de texto, sin dependencias)."""

    def __init__(self, url_ws):
        u = urllib.parse.urlparse(url_ws)
        self.ws = Ws.__new__(Ws)
        crudo = socket.create_connection((u.hostname, u.port), timeout=30)
        self.ws.s = crudo
        clave = base64.b64encode(os.urandom(16)).decode()
        crudo.sendall((f"GET {u.path} HTTP/1.1\r\nHost: {u.netloc}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                       f"Sec-WebSocket-Key: {clave}\r\nSec-WebSocket-Version: 13\r\n\r\n").encode())
        cab = b""
        while b"\r\n\r\n" not in cab:
            cab += crudo.recv(1)
        self.n = 0

    def llamar(self, metodo, params=None, espera=30):
        self.n += 1
        n = self.n
        self.ws.enviar({"id": n, "method": metodo, "params": params or {}})
        fin = time.time() + espera
        while time.time() < fin:
            m = self.ws.recibir(max(0.1, fin - time.time()))
            if m.get("id") == n:
                return m.get("result", m)
        raise TimeoutError(metodo)

    def evaluar(self, expr):
        r = self.llamar("Runtime.evaluate", {"expression": expr, "returnByValue": True, "awaitPromise": True})
        return (r.get("result") or {}).get("value")

    def captura(self, ruta):
        r = self.llamar("Page.captureScreenshot", {"format": "png"})
        with open(ruta, "wb") as f:
            f.write(base64.b64decode(r["data"]))
        return ruta

    def clic(self, x, y):
        for tipo in ("mousePressed", "mouseReleased"):
            self.llamar("Input.dispatchMouseEvent", {"type": tipo, "x": x, "y": y, "button": "left", "clickCount": 1})


def replica_sin_navegador(base, token, t):
    """El protocolo de la réplica con un cliente WebSocket propio: esquema, valores, video."""
    ws = Ws(base)
    ws.enviar({"type": "auth", "token": token})
    t.chequear(ws.esperar("auth").get("ok") is True, "réplica: el aparato entra")
    ws.enviar({"type": "replica", "op": "hola"})
    lote = ws.esperar("replica", cond=lambda m: m.get("op") == "lote")
    ops = [x.get("op") for x in lote.get("m", [])]
    esq = next((x for x in lote["m"] if x.get("op") == "esquema" and x.get("o") == "Studio"), {})
    vals = next((x for x in lote["m"] if x.get("op") == "valores" and x.get("o") == "Studio"), {})
    props = [p[0] for p in esq.get("props", [])]
    t.chequear("raiz" in ops and len(props) > 50, f"réplica: esquema de Studio con {len(props)} propiedades")
    t.chequear("currentScene" in vals.get("v", {}), f"réplica: valores (escena «{vals.get('v', {}).get('currentScene')}»)")
    secretos = [p for p in props if any(s in p.lower() for s in ("token", "password", "streamkey"))]
    t.chequear(not secretos, f"réplica: sin propiedades secretas ({secretos})")
    v = vals.get("v", {})
    t.chequear(v.get("settings") is None and v.get("controlWeb") is None, "réplica: settings y controlWeb no salen")
    # Abrir un hijo (el dashboard: modelo con filas y propiedades).
    ws.enviar({"type": "replica", "op": "abrir", "o": "Studio.dashboard"})
    filas = ws.esperar("replica", cond=lambda m: any(x.get("op") == "filas" and x.get("o") == "Studio.dashboard"
                                                      for x in m.get("m", [])))
    fs = next(x for x in filas["m"] if x.get("op") == "filas")
    t.chequear(len(fs.get("filas", [])) > 0, f"réplica: el dashboard tiene {len(fs.get('filas', []))} tarjetas")
    # El video del programa: 4 segundos a 30 cps, 640 de ancho.
    ws.enviar({"type": "replica", "op": "video", "claves": [{"clave": "program", "ancho": 640, "fps": 30}]})
    cuadros, retardos, bytes_ = 0, [], 0
    fin = time.time() + 4
    w = h = 0
    while time.time() < fin:
        try:
            m = ws.recibir(1)
        except (TimeoutError, socket.timeout):
            continue
        if m.get("type") == "replica" and m.get("op") == "cuadro":
            cuadros += 1
            retardos.append(time.time() * 1000 - m["t"])
            bytes_ += len(m.get("jpg", "")) * 3 // 4
            w, h = m.get("w"), m.get("h")
    ws.enviar({"type": "replica", "op": "video", "claves": []})
    ws.cerrar()
    ok = cuadros > 20
    prom = sum(retardos) / len(retardos) if retardos else -1
    t.chequear(ok, f"réplica: {cuadros} cuadros del programa en 4 s ({w}x{h}, {bytes_ / max(1, cuadros) / 1024:.0f} KB "
                   f"por cuadro, {bytes_ * 8 / 4 / 1e6:.1f} Mbit/s); de la app a acá {prom:.0f} ms "
                   f"(mín {min(retardos or [0]):.0f}, máx {max(retardos or [0]):.0f})")
    return {"cuadros": cuadros, "retardo_ms": prom, "w": w, "h": h, "kb": bytes_ / max(1, cuadros) / 1024}


def al_aire(m):
    """La escena al aire (scene_list)."""
    sl = m.tool("scene_list")
    for k in ("current", "currentScene", "al_aire", "program", "programa", "onAir"):
        if isinstance(sl.get(k), str):
            return sl[k]
    return None


def navegador(base, token, t, m, capturas, webrtc):
    perfil = tempfile.mkdtemp(prefix="ba-tablero-chrome-")
    puerto = 9300 + os.getpid() % 500
    p = subprocess.Popen([CHROME, "--headless=new", f"--remote-debugging-port={puerto}", f"--user-data-dir={perfil}",
                          "--window-size=1600,1000", "--mute-audio", "--autoplay-policy=no-user-gesture-required",
                          "--ignore-certificate-errors", "about:blank"],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    res = {}
    try:
        for _ in range(50):
            try:
                with urllib.request.urlopen(f"http://127.0.0.1:{puerto}/json", timeout=1) as r:
                    pags = json.loads(r.read())
                if pags:
                    break
            except OSError:
                time.sleep(0.2)
        cdp = Cdp(next(x for x in pags if x.get("type") == "page")["webSocketDebuggerUrl"])
        cdp.llamar("Page.enable")
        cdp.llamar("Runtime.enable")
        url = base + "/control/app/?hud=1#t=" + token
        t0 = time.time()
        cdp.llamar("Page.navigate", {"url": url})
        listo = False
        while time.time() - t0 < 90:
            if cdp.evaluar("!!window.baTableroListo"):
                listo = True
                break
            time.sleep(0.25)
        res["carga_s"] = time.time() - t0
        t.chequear(listo, f"navegador: el tablero arranca y muestra el dashboard en {res['carga_s']:.1f} s (primera vez)")
        rec = cdp.evaluar("JSON.stringify(performance.getEntriesByType('resource').filter(e => /tablero|qtloader/.test(e.name))"
                          ".map(e => ({n: e.name.split('/').pop().split('?')[0], kb: Math.round(e.transferSize/1024), "
                          "ms: Math.round(e.duration)})))")
        res["recursos"] = json.loads(rec or "[]")
        print("  bajado:", res["recursos"])
        time.sleep(4)
        cdp.captura(os.path.join(capturas, "tablero-1-cargado.png"))
        # Segunda carga: de la caché del navegador.
        t1 = time.time()
        cdp.llamar("Page.navigate", {"url": base + "/control/app/?hud=1"})
        while time.time() - t1 < 60 and not cdp.evaluar("!!window.baTableroListo"):
            time.sleep(0.25)
        res["carga_cache_s"] = time.time() - t1
        t.chequear(cdp.evaluar("!!window.baTableroListo"), f"navegador: con la caché arranca en {res['carga_cache_s']:.1f} s")
        time.sleep(3)
        # Armar una escena por MCP y apretar TAKE en la página.
        sl = m.tool("scene_list")
        print("  scene_list:", {k: (v if not isinstance(v, list) else v[:3]) for k, v in sl.items() if k != "_texto"})
        lista = sl.get("scenes") or sl.get("escenas") or []
        if len(lista) < 2:  # un evento de cero trae una sola: una copia para tener qué armar
            m.tool("scene_copy", {"from": lista[0]["name"] if lista and isinstance(lista[0], dict) else "Principal",
                                  "to": "Segunda"})
            lista = m.tool("scene_list").get("scenes") or []
        escenas = [e if isinstance(e, str) else (e.get("name") or e.get("nombre")) for e in lista]
        antes = al_aire(m)
        otra = next((e for e in escenas if e and e != antes), None)
        if otra:
            r = m.tool("scene_preview", {"name": otra})
            time.sleep(1.5)
            cdp.captura(os.path.join(capturas, "tablero-2-armada.png"))
            cdp.clic(800, 150)  # el TAKE: columna del medio de la franja de arriba
            time.sleep(2.5)
            despues = al_aire(m)
            t.chequear(despues == otra, f"navegador: TAKE en la página pasa «{otra}» al aire (estaba «{antes}», quedó «{despues}»)")
            cdp.captura(os.path.join(capturas, "tablero-3-take.png"))
        else:
            t.chequear(False, f"no hay otra escena para armar ({escenas})")
        time.sleep(3)
        med = cdp.evaluar("JSON.stringify(window.baMedidas || {})")
        res["medidas"] = json.loads(med or "{}")
        for k, x in res["medidas"].items():
            print(f"  latencia «{k}» (de la app a la pantalla): prom {x.get('prom')} ms, último {x.get('ultimo')}, máx {x.get('max')}, "
                  f"{x.get('w')}x{x.get('h')}, n={x.get('n')}")
        t.chequear("program" in res["medidas"], "navegador: el programa se ve (y se mide)")
        if webrtc:
            cdp.evaluar("baModulo.baPedirWebrtc()")
            u = None
            for _ in range(40):
                u = cdp.evaluar("window.baWebrtc && window.baWebrtc.url")
                if u:
                    break
                time.sleep(0.25)
            res["webrtc_url"] = bool(u)
            if u:
                # El programa por WebRTC (/cast/) en la misma página, abajo a la izquierda.
                cdp.evaluar("(() => { const f = document.createElement('iframe'); f.id = 'rtc'; f.src = %s;"
                            " f.style = 'position:fixed;left:0;bottom:0;width:800px;height:450px;border:0;z-index:99';"
                            " document.body.appendChild(f); })()" % json.dumps(u + "&buffer=0&audio=0&hud=1"))
                time.sleep(8)
                for i in range(5):
                    cdp.captura(os.path.join(capturas, f"tablero-4-webrtc-{i}.png"))
                    time.sleep(0.7)
            t.chequear(bool(u), "navegador: el enlace WebRTC del programa")
    finally:
        p.terminate()
        try:
            p.wait(5)
        except subprocess.TimeoutExpired:
            p.kill()
        shutil.rmtree(perfil, ignore_errors=True)
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ba-mcp", default=BA_MCP)
    ap.add_argument("--capturas", default=tempfile.gettempdir())
    ap.add_argument("--webrtc", action="store_true")
    ap.add_argument("--cerrar", action="store_true")
    ap.add_argument("--json", default="", help="dónde dejar lo medido")
    a = ap.parse_args()
    os.makedirs(a.capturas, exist_ok=True)

    m, original = conectar(a.ba_mcp)
    prueba = "ZZ prueba tablero " + time.strftime("%H%M%S")
    r = m.tool("evento_nuevo", {"nombre": prueba, "desde": "cero", "abrir": True})
    if r.get("_isError"):
        print("no se pudo crear el evento de prueba:", r.get("_texto"))
        return 2
    m.cerrar()
    time.sleep(8)
    m, ab = conectar(a.ba_mcp)
    if ab != prueba:
        print(f"CORTO: lo abierto es «{ab}», no el evento de prueba")
        return 2
    time.sleep(4)
    t = Prueba(m, prueba)

    def chequear(bien, titulo):
        if not bien:
            t.fallas += 1
        print(f"[{'ok ' if bien else 'MAL'}] {titulo}", flush=True)

    t.chequear = chequear
    medido = {}
    try:
        t.guarda()
        r = m.tool("control_web_emparejar", {"perfil": ["*"]})
        base = r.get("url", "").split("/control/")[0]
        _, d = emparejar(m, base, "Tablero de prueba", ["*"])
        token = d.get("token", "")
        t.chequear(bool(token), "aparato emparejado con el perfil «todo»")
        try:
            with urllib.request.urlopen(urllib.request.Request(base + "/control/app/"), context=_cw.SIN_VERIFICAR,
                                        timeout=10) as r:
                st, csp = r.status, r.headers.get("Content-Security-Policy", "")
        except urllib.error.HTTPError as e:
            st, csp = e.code, ""
        t.chequear(st == 200 and "wasm-unsafe-eval" in csp, f"/control/app/ se sirve ({st}, CSP para wasm)")
        medido["replica"] = replica_sin_navegador(base, token, t)
        medido["navegador"] = navegador(base, token, t, m, a.capturas, a.webrtc)
    except Exception as e:  # lo que sea: igual se vuelve al evento de antes
        t.chequear(False, f"se cortó: {e!r}")
    finally:
        for ap_ in (m.tool("control_web_aparatos", {}).get("aparatos") or []):
            m.tool("control_web_quitar", {"aparato": ap_.get("id")})

    if a.json:
        with open(a.json, "w") as f:
            json.dump(medido, f, indent=2, ensure_ascii=False)
    r = m.tool("evento_cambiar", {"nombre": original})
    print("volver a «%s»: %s" % (original, r.get("_texto", "")[:100]))
    m.cerrar()
    time.sleep(8)
    m, ab = conectar(a.ba_mcp)
    if ab == original:
        t.m = m
        t.paso("borrar el evento de prueba", m.tool("evento_borrar", {"nombre": prueba}))
        pap = [p for p in m.tool("evento_recuperar").get("papelera", []) if p.get("nombre") == prueba]
        if pap:
            t.paso("sacarlo de la papelera", m.tool("evento_papelera_vaciar", {"id": pap[0]["id"]}))
    else:
        print(f"MAL: lo abierto es «{ab}» (el de prueba queda: borralo a mano)")
        t.fallas += 1
    if a.cerrar:
        m.tool("app_quit")
    m.cerrar()
    print("listo:", "todo bien" if t.fallas == 0 else f"{t.fallas} cosas MAL")
    return 0 if t.fallas == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
