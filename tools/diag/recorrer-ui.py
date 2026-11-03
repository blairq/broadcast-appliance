#!/usr/bin/env python3
"""tools/diag/recorrer-ui.py — recorre toda la interfaz con el detector de acoples encendido

docs/arquitectura/detector-de-acoples.md. Con la app abierta en --test, enciende el «modo
sabotaje» (MCP diag_acoples: cada camino de la interfaz que no pasa por el modelo espera
--demora-ms de más y queda anotado), pasa por TODAS las páginas (el enum de ui_show_page,
leído de la app), sus secciones y sus paneles (ui_abrir_panel), y, con --invitados N, arranca
la prueba de sala con N invitados (prueba_sala_arrancar), espera a que entren y vuelve a
recorrer todo con la sala llena. Al final junta el informe de acoples ordenado por tiempo
total y dice en qué paso apareció cada uno.

NO CAMBIA NADA DEL EVENTO: solo muestra páginas y paneles (nada de agregar tarjetas, fuentes
ni escenas). La prueba de sala sí toca el cupo de la sala mientras dura (prueba_sala_parar lo
devuelve, también si este guion se corta con Ctrl+C): usarla en un evento de prueba.

Antes (y después: la app calienta la máquina, ver «No dejar la app corriendo»):
  ~/.claude/arbitro/bin/turno app tomar NOMBRE
  open dist/BroadcastAppliance.app --args --test --detectar-acoples
  (--detectar-acoples no hace falta: este guion lo enciende por MCP; con el flag ya cuenta
  desde el arranque)

Uso:
  tools/diag/recorrer-ui.py --salida ~/mediciones/acoples-1
  tools/diag/recorrer-ui.py --invitados 200 --minutos 10 --salida ~/mediciones/acoples-200
Opciones: --ba-mcp RUTA (el puente; de fábrica el del paquete de macOS en dist/), --demora-ms
(300), --pausa-s (lo que se queda en cada página, 2,5), --espera-sala-s (lo que espera a que
entren, 180), --vueltas-sala (cuántas veces recorre con la sala llena, 2).

Sale en --salida: informe.json (lo de diag_acoples, más `pasos`: en qué paso apareció cada
lugar), resumen.md (la tabla, para pegar) y pasos.jsonl (cada paso con lo nuevo).
"""
from __future__ import annotations

import argparse
import json
import pathlib
import signal
import subprocess
import sys
import threading
import time

RAIZ = pathlib.Path(__file__).resolve().parents[2]
PUENTE = RAIZ / "dist" / "BroadcastAppliance.app" / "Contents" / "Resources" / "bin" / "64bit" / "ba-mcp"

# Los paneles de ui_abrir_panel (McpUiTools.cpp, kPaneles; «escenas → efectos» pide una fuente).
PANELES = {
    "video": ["superresolucion", "entrada_virtual", "catalogo"],
    "historico": ["registro"],
    "guion": ["guion", "padron", "publico", "orden", "web", "documentos"],
}
# Las secciones de ui_show_page (su descripción): las que no dependen de lo que haya cargado.
SECCIONES = {
    "audio": ["estudio", "racks", "matriz", "mesa"],
    "asamblea": ["conferencia", "conferencia/sala", "conferencia/unidades", "conferencia/votacion",
                 "conferencia/conexion", "conferencia/avanzado", "recinto", "orden", "informes", "votacion",
                 "conexiones", "debate"],
    "personas": ["libreta", "registro"],
    "latencias": ["mapa", "sincronia"],
    "camaras": ["recinto"],
}


class Mcp:
    """Un cliente de ba-mcp por stdio (como tools/carga/medir-sala.py)."""

    def __init__(self, exe: str):
        self.p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                  text=True)
        self.n = 0
        self.mu = threading.Lock()
        self._enviar({"jsonrpc": "2.0", "id": 0, "method": "initialize",
                      "params": {"capabilities": {}, "clientInfo": {"name": "recorrer-ui", "version": "1"},
                                 "protocolVersion": "2024-11-05"}})
        self._esperar(0)

    def _enviar(self, o):
        self.p.stdin.write(json.dumps(o) + "\n")
        self.p.stdin.flush()

    def _esperar(self, i):
        while True:
            linea = self.p.stdout.readline()
            if not linea:
                raise RuntimeError("ba-mcp se cerró (¿la app está abierta?)")
            r = json.loads(linea)
            if r.get("id") == i:
                return r

    def pedir(self, metodo, params=None):
        with self.mu:
            self.n += 1
            i = self.n
            self._enviar({"jsonrpc": "2.0", "id": i, "method": metodo, "params": params or {}})
            r = self._esperar(i)
        if "error" in r:
            raise RuntimeError(f"{metodo}: {r['error']}")
        return r.get("result", {})

    def llamar(self, nombre, args=None):
        res = self.pedir("tools/call", {"name": nombre, "arguments": args or {}})
        if res.get("isError"):
            texto = " ".join(c.get("text", "") for c in res.get("content", []))
            raise RuntimeError(f"{nombre}: {texto}")
        if res.get("structuredContent"):
            return res["structuredContent"]
        for c in res.get("content", []):
            if c.get("type") == "text":
                try:
                    return json.loads(c["text"])
                except Exception:
                    return {"texto": c["text"]}
        return res

    def cerrar(self):
        try:
            self.p.kill()
        except Exception:
            pass


def clave(l: dict) -> str:
    return f"{l.get('clase')}|{l.get('lugar')}|{l.get('donde')}"


class Recorrido:
    def __init__(self, mcp: Mcp, a, salida: pathlib.Path):
        self.mcp, self.a, self.salida = mcp, a, salida
        self.vistos: dict[str, int] = {}     # clave -> veces (en el último informe)
        self.primer_paso: dict[str, str] = {}
        self.pasos: dict[str, list[str]] = {}  # clave -> pasos donde sumó veces
        self.diario = open(salida / "pasos.jsonl", "w", encoding="utf-8")

    def informe(self) -> dict:
        return self.mcp.llamar("diag_acoples", {})

    def anotar(self, paso: str) -> None:
        inf = self.informe()
        nuevos, sumaron = [], []
        for l in inf.get("lugares", []):
            k = clave(l)
            antes = self.vistos.get(k, 0)
            if l.get("veces", 0) > antes:
                sumaron.append(k)
                self.pasos.setdefault(k, []).append(paso)
                if antes == 0:
                    nuevos.append(k)
                    self.primer_paso.setdefault(k, paso)
            self.vistos[k] = l.get("veces", 0)
        self.diario.write(json.dumps({"paso": paso, "t": time.time(), "nuevos": nuevos, "sumaron": sumaron},
                                     ensure_ascii=False) + "\n")
        self.diario.flush()
        marca = f"  +{len(nuevos)} nuevos" if nuevos else ""
        print(f"  {paso}: {len(sumaron)} lugares{marca}", flush=True)

    def paso(self, paso: str, herramienta: str, args: dict) -> None:
        try:
            self.mcp.llamar(herramienta, args)
        except RuntimeError as e:
            print(f"  {paso}: {e}", flush=True)
            return
        time.sleep(self.a.pausa_s)
        self.anotar(paso)

    def todo(self, etiqueta: str, paginas: list[str]) -> None:
        print(f"== {etiqueta}: {len(paginas)} páginas", flush=True)
        for p in paginas:
            self.paso(f"{etiqueta}/{p}", "ui_show_page", {"page": p})
            for s in SECCIONES.get(p, []):
                self.paso(f"{etiqueta}/{p}/{s}", "ui_show_page", {"page": p, "seccion": s})
            for panel in PANELES.get(p, []):
                self.paso(f"{etiqueta}/{p}+{panel}", "ui_abrir_panel", {"page": p, "panel": panel})
            if p in PANELES:
                self.paso(f"{etiqueta}/{p}+cerrar", "ui_abrir_panel", {"page": p, "panel": "cerrar"})


def buscar(o, k):
    """Los valores de la clave `k` en cualquier nivel de `o`."""
    if isinstance(o, dict):
        for kk, v in o.items():
            if kk == k:
                yield v
            yield from buscar(v, k)
    elif isinstance(o, list):
        for v in o:
            yield from buscar(v, k)


def resumen_md(inf: dict, rec: Recorrido) -> str:
    filas = ["| total ms | veces | real ms (máx) | clase | lugar | dónde | primer paso | corría |",
             "|---:|---:|---:|---|---|---|---|---|"]
    for l in inf.get("lugares", []):
        k = clave(l)
        filas.append(f"| {l.get('total_ms', 0):.0f} | {l.get('veces', 0)} | {l.get('real_ms', 0):.1f} "
                     f"({l.get('real_max_ms', 0):.1f}) | {l.get('clase')} | `{l.get('lugar')}` | {l.get('donde')} | "
                     f"{rec.primer_paso.get(k, '(antes)')} | {str(l.get('corria', '')).replace('|', '/')[:60]} |")
    cab = (f"# Acoples de la interfaz\n\nDemora {inf.get('demora_ms')} ms por acople, {inf.get('total')} pasos, "
           f"{inf.get('dormido_ms', 0) / 1000:.1f} s dormidos a propósito ({inf.get('sin_dormir', 0)} sin dormir por el "
           f"tope de {inf.get('tope_ms')} ms cada {inf.get('ventana_ms')} ms).\n\n"
           "Cada fila es un camino de la interfaz que no pasa por el modelo. Lo que hay que hacer con cada clase, en "
           "docs/arquitectura/detector-de-acoples.md. La pila de la primera vez de cada uno está en informe.json.\n\n")
    return cab + "\n".join(filas) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--ba-mcp", default=str(PUENTE))
    ap.add_argument("--salida", required=True)
    ap.add_argument("--demora-ms", type=int, default=300)
    ap.add_argument("--pausa-s", type=float, default=2.5)
    ap.add_argument("--invitados", type=int, default=0, help="la prueba de sala con N invitados (0: sin sala)")
    ap.add_argument("--minutos", type=int, default=10, help="lo que dura la prueba de sala")
    ap.add_argument("--espera-sala-s", type=float, default=180)
    ap.add_argument("--vueltas-sala", type=int, default=2)
    a = ap.parse_args()

    salida = pathlib.Path(a.salida).expanduser()
    salida.mkdir(parents=True, exist_ok=True)
    mcp = Mcp(a.ba_mcp)
    sala_arrancada = False

    def parar_sala():
        nonlocal sala_arrancada
        if sala_arrancada:
            sala_arrancada = False
            try:
                mcp.llamar("prueba_sala_parar", {})
                print("prueba de sala terminada", flush=True)
            except RuntimeError as e:
                print(f"prueba_sala_parar: {e}", flush=True)

    def al_cortar(_s, _f):
        parar_sala()
        raise KeyboardInterrupt

    signal.signal(signal.SIGINT, al_cortar)
    try:
        herramientas = {t["name"]: t for t in mcp.pedir("tools/list").get("tools", [])}
        if "diag_acoples" not in herramientas:
            print("la app no tiene diag_acoples: abrila con --test", file=sys.stderr)
            return 2
        paginas = herramientas["ui_show_page"]["inputSchema"]["properties"]["page"]["enum"]
        mcp.llamar("diag_acoples", {"demora_ms": a.demora_ms, "reiniciar": True})
        rec = Recorrido(mcp, a, salida)
        rec.anotar("inicio")

        rec.todo("vacía", paginas)

        if a.invitados > 0:
            print(f"== prueba de sala: {a.invitados} invitados por {a.minutos} min", flush=True)
            mcp.llamar("prueba_sala_arrancar", {"participantes": a.invitados, "minutos": a.minutos})
            sala_arrancada = True
            # Mientras entran, la página de la sala y el tablero (lo que más se mueve).
            fin = time.time() + a.espera_sala_s
            conectados = 0
            while time.time() < fin:
                for p in ("sala", "dashboard"):
                    if p in paginas:
                        rec.paso(f"entrando/{p}", "ui_show_page", {"page": p})
                est = mcp.llamar("prueba_sala_estado", {})
                conectados = max([c for c in buscar(est, "conectados") if isinstance(c, (int, float))] or [0])
                print(f"  conectados: {conectados}/{a.invitados}", flush=True)
                if conectados >= a.invitados * 0.9:
                    break
            for v in range(a.vueltas_sala):
                rec.todo(f"sala{v + 1}", paginas)
            parar_sala()
            time.sleep(a.pausa_s)
            rec.anotar("sala terminada")

        inf = rec.informe()
        for l in inf.get("lugares", []):
            k = clave(l)
            l["primer_paso"] = rec.primer_paso.get(k, "(antes)")
            l["pasos"] = rec.pasos.get(k, [])
        (salida / "informe.json").write_text(json.dumps(inf, ensure_ascii=False, indent=1), encoding="utf-8")
        (salida / "resumen.md").write_text(resumen_md(inf, rec), encoding="utf-8")
        print(f"\n{len(inf.get('lugares', []))} acoples; los primeros:")
        for l in inf.get("lugares", [])[:15]:
            print(f"  {l.get('total_ms', 0):8.0f} ms  {l.get('veces', 0):5}×  [{l.get('clase')}] {l.get('lugar')}  "
                  f"({l.get('donde')})  — {l.get('primer_paso')}")
        print(f"\ninforme: {salida / 'informe.json'}\nresumen: {salida / 'resumen.md'}")
        return 0
    finally:
        parar_sala()
        try:
            mcp.llamar("diag_acoples", {"demora_ms": 0})
        except Exception:
            pass
        mcp.cerrar()


if __name__ == "__main__":
    sys.exit(main())
