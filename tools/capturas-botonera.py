#!/usr/bin/env python3
"""Las miniaturas del selector de herramientas de la Botonera.

Una captura chica de la página de la app donde vive cada categoría del MCP
(assets/botonera-capturas/<categoría>.jpg, 320 px de ancho). Se sacan de la app
corriendo (con --test), por el MCP: ui_show_page + capture_ui.

    python3 tools/capturas-botonera.py [ruta/a/ba-mcp]

Si una categoría nueva no está en PAGINA, el selector la muestra sin captura.
"""
import json
import os
import re
import subprocess
import sys
import time

from PIL import Image

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DESTINO = os.path.join(RAIZ, "assets", "botonera-capturas")
MCP = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    RAIZ, "dist", "BroadcastAppliance.app", "Contents", "Resources", "bin", "64bit", "ba-mcp")

# categoría del MCP -> página (y sección) de ui_show_page
PAGINA = {
    "estado": "dashboard", "salidas": "dashboard", "operador": "dashboard",
    "escenas": "escenas", "fuentes": "escenas", "dsk": "escenas",
    "audio": "audio:mesa", "audio_rack": "audio:racks", "traduccion": "audio:estudio",
    "entradas": "entradas", "video": "video", "camaras": "camaras",
    "sala": "sala", "oradores": "sala",
    "cuentas": "youtube", "youtube": "youtube", "twitch": "twitch", "facebook": "facebook",
    "config": "configuracion", "interfaz": "configuracion",
    "asistente": "asistente", "alertas": "asistente", "ayuda": "asistente", "recetas": "asistente",
    "agentes": "agentes", "evento": "guion", "graficos": "graficos",
    "registro": "registro", "sistema": "registro",
    "prompter": "prompter", "pantallas": "pantallas", "ventanas": "pantallas",
    "qr": "qr", "padron": "personas", "rostros": "personas", "equipo": "personas",
    "preguntas": "publico", "medios": "medios", "imagenes": "medios", "laminas": "medios",
    "plugins": "plugins", "equipos": "equipos", "botonera": "botonera",
    "conferencia": "asamblea", "sesion": "asamblea:conexiones",
}


def llamar(nombre, args):
    p = subprocess.Popen([MCP], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    for m in ({"jsonrpc": "2.0", "id": 0, "method": "initialize",
               "params": {"capabilities": {}, "clientInfo": {"name": "capturas", "version": "1"},
                          "protocolVersion": "2024-11-05"}},
              {"jsonrpc": "2.0", "id": 1, "method": "tools/call", "params": {"name": nombre, "arguments": args}}):
        p.stdin.write(json.dumps(m) + "\n")
        p.stdin.flush()
    for linea in p.stdout:
        r = json.loads(linea)
        if r.get("id") == 1:
            p.kill()
            return " ".join(c.get("text", "") for c in r.get("result", {}).get("content", []))
    return ""


def main():
    os.makedirs(DESTINO, exist_ok=True)
    hechas = {}
    for cat, pagina in sorted(PAGINA.items(), key=lambda x: x[1]):
        if pagina not in hechas:
            page, _, sec = pagina.partition(":")
            llamar("ui_show_page", {"page": page, **({"seccion": sec} if sec else {})})
            time.sleep(2.0)
            llamar("alert_reconocer", {"todas": True})  # sin la franja roja de alarma en la foto
            time.sleep(0.8)
            m = re.search(r"Captura guardada en (.+?\.(?:jpg|png)) \(", llamar("capture_ui", {}))
            if not m:
                print(cat, "sin captura")
                continue
            im = Image.open(m.group(1)).convert("RGB")
            # Solo la página: sin el riel de la izquierda ni el zócalo del asistente.
            w, h = im.size
            im = im.crop((int(w * 0.057), int(h * 0.02), int(w * 0.995), int(h * 0.905)))
            im.thumbnail((320, 320))
            hechas[pagina] = im
        hechas[pagina].save(os.path.join(DESTINO, cat + ".jpg"), quality=78, optimize=True)
        print(cat, "<-", pagina)


if __name__ == "__main__":
    main()
