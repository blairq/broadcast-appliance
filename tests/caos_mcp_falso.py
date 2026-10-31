#!/usr/bin/env python3
"""A fake ba-mcp for tests/test_caos_banco.py: just enough of the app for the chaos bench.

It speaks MCP over stdio like the real bridge. Its "app" lives in a JSON state file
(CAOS_FALSO_ESTADO), because the bench opens a new bridge after every restart. On
stream_start it publishes with the VENDORED GStreamer (CAOS_FALSO_GST: gst-launch) to the
bench's local RTMP server; diag_falla codificador/sumidero of the transmission drops that
connection after `en_s` and publishes again a second later, as the real app reconnects.
Every tool call is appended to the state's `llamadas`, so the test can check what the
bench did to the events.
"""
from __future__ import annotations

import json
import os
import signal
import subprocess
import sys
import time

ESTADO = os.environ["CAOS_FALSO_ESTADO"]


def leer() -> dict:
    try:
        with open(ESTADO, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return {"abierto": "Evento del operador", "eventos": ["Evento del operador"], "llamadas": [], "papelera": [],
                "disparadas": [], "grabando": False}


def guardar(e: dict) -> None:
    tmp = ESTADO + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(e, f)
    os.replace(tmp, ESTADO)


def vive(pid) -> bool:
    try:
        os.kill(int(pid), 0)
        return True
    except (OSError, TypeError, ValueError):
        return False


def publicar(url: str) -> int:
    tubo = ("videotestsrc is-live=true pattern=ball ! video/x-raw,width=320,height=240,framerate=30/1 ! "
            "x264enc tune=zerolatency key-int-max=30 speed-preset=ultrafast ! h264parse ! flvmux streamable=true ! "
            f"rtmp2sink location={url}")
    p = subprocess.Popen([os.environ["CAOS_FALSO_GST"], "-q"] + tubo.split(), stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL, start_new_session=True)
    return p.pid


def cortar(pid) -> None:
    if vive(pid):
        os.kill(int(pid), signal.SIGKILL)


def herramienta(nombre: str, a: dict) -> dict:
    e = leer()
    e["llamadas"].append([nombre, a])
    r: dict = {}
    if nombre == "eventos_listar":
        r = {"abierto": e["abierto"], "eventos": e["eventos"]}
    elif nombre == "evento_nuevo":
        e["eventos"].append(a["nombre"])
        if a.get("abrir"):
            e["abierto"] = a["nombre"]
    elif nombre == "evento_cambiar":
        e["abierto"] = a["nombre"]
    elif nombre == "evento_borrar":
        e["eventos"].remove(a["nombre"])
        e["papelera"].append({"id": "p1", "nombre": a["nombre"]})
    elif nombre == "evento_recuperar":
        r = {"papelera": e["papelera"]}
    elif nombre == "evento_papelera_vaciar":
        e["papelera"] = [p for p in e["papelera"] if p["id"] != a.get("id")]
    elif nombre == "status":
        r = {"streaming": vive(e.get("gst")), "recording": e["grabando"]}
    elif nombre == "stream_start":
        cortar(e.get("gst"))
        e["url"] = a["server"] + "/" + a["key"]
        e["gst"] = publicar(e["url"])
    elif nombre == "stream_stop":
        cortar(e.get("gst"))
        e["gst"] = None
    elif nombre == "record_start":
        e["grabando"] = True
    elif nombre == "record_stop":
        e["grabando"] = False
    elif nombre == "diag_falla":
        if a.get("limpiar"):
            e["disparadas"] = []
        if a.get("pieza"):
            # La «app»: a los en_s se corta la conexión y al segundo vuelve (otra sesión).
            subprocess.Popen([sys.executable, "-I", __file__, "--romper", str(a.get("en_s", 0)), a["pieza"], a["salida"]],
                             env=os.environ, start_new_session=True)
        r = {"programadas": [], "disparadas": e["disparadas"]}
    elif nombre == "app_quit":
        cortar(e.get("gst"))
        e["gst"] = None
        e["salio"] = True
    guardar(e)
    return r


def romper(en_s: float, pieza: str, salida: str) -> None:
    time.sleep(en_s)
    e = leer()
    e["disparadas"].append({"pieza": pieza, "salida": salida})
    if salida == "transmision" and e.get("gst"):
        cortar(e["gst"])
        guardar(e)
        time.sleep(1.0)
        e = leer()
        e["gst"] = publicar(e["url"])
    guardar(e)


def main() -> int:
    if len(sys.argv) > 1 and sys.argv[1] == "--romper":
        romper(float(sys.argv[2]), sys.argv[3], sys.argv[4])
        return 0
    for linea in sys.stdin:
        m = json.loads(linea)
        if m.get("method") == "initialize":
            res = {"protocolVersion": "2024-11-05", "capabilities": {}}
        elif m.get("method") == "tools/call":
            d = herramienta(m["params"]["name"], m["params"].get("arguments") or {})
            res = {"content": [{"type": "text", "text": json.dumps(d)}], "structuredContent": d}
        else:
            continue
        print(json.dumps({"jsonrpc": "2.0", "id": m.get("id"), "result": res}), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
