#!/usr/bin/env python3
"""Prueba del dashboard web (/control/) de punta a punta, por MCP y por el WebSocket real.

Con la app corriendo (abierta con `--args --test`, que aprueba sola lo que pide una persona):

  1. Crea un evento de cero («ZZ prueba control <hora>») y lo abre. Si lo abierto no es
     ese, corta sin tocar nada.
  2. Empareja un aparato como lo haría el navegador: control_web_emparejar → POST
     /api/control/emparejar → control_web_resolver (aprobar) → GET ?solicitud= → token.
  3. Abre /ws?control con el token: auth, junta las tandas de herramientas, mira todos los
     temas y anota la forma de lo que llega; llama scene_list y room_link (con su QR);
     prueba un token malo y un tema fuera del perfil (con un segundo aparato, perfil chico).
  4. Quita los aparatos, vuelve al evento de antes y borra el de prueba.

    tools/test/control-web-mcp.py [--cerrar]
"""
import argparse
import base64
import importlib.util
import json
import os
import socket
import ssl
import struct
import sys
import time
import urllib.parse
import urllib.request

AQUI = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location("asamblea_mcp", os.path.join(AQUI, "asamblea-mcp.py"))
_am = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_am)
conectar, Prueba, BA_MCP = _am.conectar, _am.Prueba, _am.BA_MCP

SIN_VERIFICAR = ssl._create_unverified_context()  # el certificado propio de la sala


def http(url, metodo="GET", cuerpo=None):
    datos = json.dumps(cuerpo).encode() if cuerpo is not None else None
    req = urllib.request.Request(url, data=datos, method=metodo, headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, context=SIN_VERIFICAR, timeout=10) as r:
            return r.status, json.loads(r.read() or b"{}")
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read() or b"{}")


class Ws:
    """Un cliente WebSocket mínimo (texto, con máscara), sin dependencias."""

    def __init__(self, base):
        u = urllib.parse.urlparse(base)
        crudo = socket.create_connection((u.hostname, u.port or (443 if u.scheme == "https" else 80)), timeout=10)
        self.s = SIN_VERIFICAR.wrap_socket(crudo, server_hostname=u.hostname) if u.scheme == "https" else crudo
        clave = base64.b64encode(os.urandom(16)).decode()
        self.s.sendall((f"GET /ws?control=1 HTTP/1.1\r\nHost: {u.netloc}\r\nUpgrade: websocket\r\n"
                        f"Connection: Upgrade\r\nSec-WebSocket-Key: {clave}\r\nSec-WebSocket-Version: 13\r\n"
                        f"Origin: {base}\r\n\r\n").encode())
        cab = b""
        while b"\r\n\r\n" not in cab:
            cab += self.s.recv(1)
        if b" 101 " not in cab.split(b"\r\n")[0]:
            raise RuntimeError("sin upgrade: " + cab.split(b"\r\n")[0].decode())

    def enviar(self, obj):
        datos = json.dumps(obj).encode()
        mascara = os.urandom(4)
        n = len(datos)
        cab = bytes([0x81]) + (bytes([0x80 | n]) if n < 126 else bytes([0x80 | 126]) + struct.pack(">H", n)
                               if n < 65536 else bytes([0x80 | 127]) + struct.pack(">Q", n))
        self.s.sendall(cab + mascara + bytes(b ^ mascara[i % 4] for i, b in enumerate(datos)))

    def _leer(self, n):
        b = b""
        while len(b) < n:
            p = self.s.recv(n - len(b))
            if not p:
                raise ConnectionError("cerrado")
            b += p
        return b

    def recibir(self, espera=10):
        self.s.settimeout(espera)
        while True:
            a, b = self._leer(2)
            n = b & 0x7F
            if n == 126:
                n = struct.unpack(">H", self._leer(2))[0]
            elif n == 127:
                n = struct.unpack(">Q", self._leer(8))[0]
            datos = self._leer(n)
            op = a & 0x0F
            if op == 0x8:
                raise ConnectionError("el servidor cerró")
            if op == 0x1:
                return json.loads(datos)

    def esperar(self, tipo, espera=10, cond=lambda m: True):
        fin = time.time() + espera
        while time.time() < fin:
            m = self.recibir(max(0.1, fin - time.time()))
            if m.get("type") == tipo and cond(m):
                return m
        raise TimeoutError("no llegó " + tipo)

    def cerrar(self):
        try:
            self.s.close()
        except OSError:
            pass


def emparejar(m, base, nombre, perfil=None):
    r = m.tool("control_web_emparejar", {"perfil": perfil} if perfil else {})
    url = r.get("url", "")
    codigo = urllib.parse.unquote(url.split("#p=", 1)[1]) if "#p=" in url else ""
    st, d = http(base + "/api/control/emparejar", "POST", {"codigo": codigo, "nombre": nombre})
    sol = d.get("solicitud", "")
    r = m.tool("control_web_resolver", {"solicitud": sol, "aprobar": True, **({"perfil": perfil} if perfil else {})})
    st, d = http(base + "/api/control/emparejar?solicitud=" + urllib.parse.quote(sol))
    return url, d


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ba-mcp", default=BA_MCP)
    ap.add_argument("--cerrar", action="store_true")
    a = ap.parse_args()

    m, original = conectar(a.ba_mcp)
    prueba = "ZZ prueba control " + time.strftime("%H%M%S")
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
    ws = None
    try:
        t.guarda()
        r = m.tool("control_web_emparejar", {})
        base = r.get("url", "").split("/control/")[0]
        t.chequear(base.startswith("http"), f"URL para emparejar: {r.get('url', '')[:60]}…")
        url, d = emparejar(m, base, "Prueba de punta a punta")
        token = d.get("token", "")
        t.chequear(bool(token), f"token entregado (estado {d.get('estado')})")
        _, otra = http(base + "/api/control/emparejar?solicitud=x")
        t.chequear("token" not in otra, "una solicitud inventada no da token")

        ws = Ws(base)
        ws.enviar({"type": "auth", "token": token})
        au = ws.esperar("auth")
        t.chequear(au.get("ok") is True, f"auth ok: {au.get('nombre')} perfil={au.get('perfil')}")
        hs = list(au.get("herramientas", []))
        mas = au.get("mas")
        while mas:
            h = ws.esperar("herramientas")
            hs += h.get("herramientas", [])
            mas = h.get("mas")
        t.chequear(len(hs) > 100, f"catálogo entero en tandas: {len(hs)} herramientas")
        t.chequear(not any(h["name"].startswith("control_web_") for h in hs), "sin las de control_web")

        temas = ["escenas", "audio", "ptz", "salidas", "latencias", "estado", "sala", "oradores", "graficos"]
        ws.enviar({"type": "mirar", "temas": temas})
        mi = ws.esperar("mirando")
        t.chequear(not mi.get("rechazados"), f"mirando {mi.get('temas')} rechazados={mi.get('rechazados')}")
        vistos = {}
        fin = time.time() + 8
        while time.time() < fin and len(vistos) < len(temas):
            try:
                e = ws.recibir(1)
            except (TimeoutError, socket.timeout):
                continue
            if e.get("type") == "estado" and e["tema"] not in vistos:
                d = e.get("datos")
                vistos[e["tema"]] = list(d.keys())[:8] if isinstance(d, dict) else type(d).__name__
        for tm in temas:
            t.chequear(tm in vistos, f"estado «{tm}»: {vistos.get(tm)}")

        ws.enviar({"type": "llamar", "id": 1, "herramienta": "scene_list", "args": {}})
        r1 = ws.esperar("resultado", cond=lambda x: x.get("id") == 1)
        t.chequear(r1.get("ok") and isinstance(r1.get("datos"), dict), f"scene_list: {str(r1.get('datos'))[:80]}")
        ws.enviar({"type": "llamar", "id": 2, "herramienta": "room_link", "args": {"target": "local", "withPin": True}})
        r2 = ws.esperar("resultado", cond=lambda x: x.get("id") == 2, espera=15)
        d2 = r2.get("datos") or {}
        t.chequear(r2.get("ok") and str(d2.get("url", "")).startswith("http"), f"room_link url: {d2.get('url')}")
        imgs = d2.get("imagenes") or []
        t.chequear(bool(imgs) and imgs[0].get("mime") == "image/png" and len(imgs[0].get("data", "")) > 200,
                   f"room_link trae el QR ({len(imgs)} imagen)")
        ws.enviar({"type": "ping"})
        t.chequear(ws.esperar("pong", espera=5).get("type") == "pong", "ping → pong")
        ws.cerrar()
        ws = None

        malo = Ws(base)
        malo.enviar({"type": "auth", "token": "0" * 64})
        try:
            r = malo.esperar("auth", espera=5)
            t.chequear(r.get("ok") is False, f"token malo rechazado: {r.get('error')}")
        except ConnectionError:
            t.chequear(True, "token malo: el gateway corta")
        malo.cerrar()

        _, d = emparejar(m, base, "Perfil chico", ["escenas"])
        chico = Ws(base)
        chico.enviar({"type": "auth", "token": d.get("token", "")})
        au = chico.esperar("auth")
        nombres = [h["name"] for h in au.get("herramientas", [])]
        t.chequear(au.get("ok") and "mezclador_estado" not in nombres, f"perfil chico: {len(nombres)} herramientas")
        chico.enviar({"type": "mirar", "temas": ["escenas", "audio"]})
        mi = chico.esperar("mirando")
        t.chequear([x.get("tema") for x in mi.get("rechazados", [])] == ["audio"], f"tema fuera del perfil: {mi.get('rechazados')}")
        chico.enviar({"type": "llamar", "id": 9, "herramienta": "mezclador_estado", "args": {}})
        r9 = chico.esperar("resultado", cond=lambda x: x.get("id") == 9)
        t.chequear(not r9.get("ok"), f"herramienta fuera del perfil: {r9.get('error')}")
        chico.cerrar()
    finally:
        if ws:
            ws.cerrar()
        for ap_ in (m.tool("control_web_aparatos", {}).get("aparatos") or []):
            m.tool("control_web_quitar", {"aparato": ap_.get("id")})

    # --- Volver y limpiar (los aparatos son del evento: se van con él) ---------------------
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
