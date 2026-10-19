#!/usr/bin/env python3
"""El sistema de analisis de capas en el navegador.

Sirve la pagina (capas-web/index.html) y lo que guardo capas-guardar.py:

  /api/indice                          los analisis y las versiones de corpus y banco
  /api/analisis/<id>/<vista|curvas|meta>.json
  /api/corpus/<version>/resumen
  /api/corpus/<version>/frases?h=<herramienta>[&archivo=entrenar.jsonl]
  /api/corpus/<version>/anotaciones?h=<herramienta>
  /api/banco/<version>/casos?h=<herramienta>

Solo biblioteca estandar. Escucha en 127.0.0.1 (los datos son del corpus y del
banco: no se publican en la red sin pedirlo con --host).

  tools/entrenar/capas-web.py                       # http://127.0.0.1:8770
  tools/entrenar/capas-web.py --host server01.lan   # en la red (todas las direcciones del nombre)
"""
import argparse
import gzip
import json
import os
import re
import socket
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

BASE = Path(os.environ.get("BA_ANALISIS", "~/entrenamientos/analisis")).expanduser()
WEB = Path(__file__).resolve().parent / "capas-web"
ID = re.compile(r"^[0-9a-f]{4,64}$")
ARCHIVOS = {"entrenar.jsonl", "validar.jsonl", "prueba.jsonl", "charla-prueba.jsonl"}
TOPE = 2000  # filas por pedido


def leer(f):
    return json.loads(f.read_text(encoding="utf-8"))


def indice():
    def todos(carpeta, archivo):
        return sorted((leer(x / archivo) for x in (BASE / carpeta).glob("*") if (x / archivo).exists()),
                      key=lambda m: m.get("numero", 0) if carpeta != "analisis" else m["id"])
    analisis = todos("analisis", "meta.json")
    for m in analisis:  # lo liviano: la ficha entera se pide aparte
        m.pop("estados", None)
    corpus = [{k: v for k, v in r.items() if k != "por_herramienta"} for r in todos("corpus", "resumen.json")]
    banco = [{k: v for k, v in r.items() if k != "por_herramienta"} for r in todos("banco", "resumen.json")]
    return {"base": str(BASE), "analisis": analisis, "corpus": corpus, "banco": banco}


def frases(version, h, archivo):
    d = BASE / "corpus" / version
    out = []
    with gzip.open(d / f"{archivo}.gz", "rt", encoding="utf-8") as f:
        for linea in f:
            x = json.loads(linea)
            if h in (x.get("herramienta"), x.get("tipo")):
                out.append({k: x.get(k) for k in ("tipo", "herramienta", "frase", "plantilla", "args", "falta")
                            if x.get(k) is not None})
                if len(out) >= TOPE:
                    break
    return out


class Manejador(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def responder(self, cuerpo, tipo="application/json; charset=utf-8", codigo=200):
        if isinstance(cuerpo, (dict, list)):
            cuerpo = json.dumps(cuerpo, ensure_ascii=False).encode()
        self.send_response(codigo)
        self.send_header("Content-Type", tipo)
        self.send_header("Content-Length", str(len(cuerpo)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(cuerpo)

    def error(self, codigo, msg):
        self.responder({"error": msg}, codigo=codigo)

    def do_GET(self):
        u = urlparse(self.path)
        q = {k: v[0] for k, v in parse_qs(u.query).items()}
        partes = [p for p in u.path.split("/") if p]
        try:
            if not partes or partes == ["index.html"]:
                return self.responder((WEB / "index.html").read_bytes(), "text/html; charset=utf-8")
            if partes[0] != "api":
                return self.error(404, "no existe")
            if partes[1:] == ["indice"]:
                return self.responder(indice())
            if len(partes) == 4 and partes[1] == "analisis" and partes[2].isdigit() \
                    and partes[3] in ("vista.json", "curvas.json", "meta.json"):
                f = BASE / "analisis" / partes[2] / partes[3]
                return self.responder(f.read_bytes()) if f.exists() else self.error(404, "no esta ese analisis")
            if len(partes) == 4 and partes[1] in ("corpus", "banco") and ID.match(partes[2]):
                d = BASE / partes[1] / partes[2]
                if not d.exists():
                    return self.error(404, f"no esta esa version de {partes[1]}")
                h = q.get("h", "")
                if partes[3] == "resumen":
                    return self.responder((d / "resumen.json").read_bytes())
                if partes[1] == "corpus" and partes[3] == "frases":
                    archivo = q.get("archivo", "entrenar.jsonl")
                    if archivo not in ARCHIVOS:
                        return self.error(400, "archivo desconocido")
                    return self.responder(frases(partes[2], h, archivo))
                if partes[1] == "corpus" and partes[3] == "anotaciones":
                    f = d / "tc-anotaciones.json"
                    return self.responder(leer(f).get(h, []) if f.exists() else [])
                if partes[1] == "banco" and partes[3] == "casos":
                    r = leer(d / "resumen.json")
                    casos = [json.loads(l) for l in (d / r["archivo"]).read_text(encoding="utf-8").splitlines() if l.strip()]
                    return self.responder([x for x in casos if x.get("herramienta") == h or h in x.get("aceptables", [])][:TOPE])
            return self.error(404, "no existe")
        except (OSError, ValueError, KeyError) as e:
            return self.error(500, str(e))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--puerto", type=int, default=8770)
    ap.add_argument("--host", default="127.0.0.1")
    args = ap.parse_args()
    # un nombre (p. ej. server01.lan) puede resolver a varias direcciones, IPv4 e IPv6:
    # se escucha en cada una (ThreadingHTTPServer solo hace una familia por socket)
    dirs = []
    for fam, _, _, _, sa in socket.getaddrinfo(args.host, args.puerto, type=socket.SOCK_STREAM):
        if (fam, sa[0]) not in [(f, d) for f, d in dirs]:
            dirs.append((fam, sa[0]))
    servidores = []
    for fam, d in dirs:
        clase = type("Servidor", (ThreadingHTTPServer,), {"address_family": fam})
        servidores.append(clase((d, args.puerto), Manejador))
        print(f"analisis en {BASE} · http://{'[' + d + ']' if fam == socket.AF_INET6 else d}:{args.puerto}", flush=True)
    print(f"  (por nombre: http://{args.host}:{args.puerto})", flush=True)
    for srv in servidores[1:]:
        threading.Thread(target=srv.serve_forever, daemon=True).start()
    servidores[0].serve_forever()


if __name__ == "__main__":
    main()
