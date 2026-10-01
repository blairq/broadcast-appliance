"""Correr un agente SIN CONEXION sobre un lote: lee su archivo, escribe su salida, nada mas.

Es el paso que en el bucle del corpus hace lo que la matematica no puede:
escribir frases, reescribir la que tira hacia una vecina, revisar el sentido
contra la ficha, juzgar a ciegas. Cada agente arranca en una carpeta temporal
con SOLO su lote y su consigna; puede leer y escribir ahi y nada mas (sin
MCP, sin ajustes del usuario, sin web). Asi no ve el examen, ni el banco, ni
el resto del corpus: no puede copiar lo que despues se usa para medir.

La salida es JSONL. Se valida que cada linea tenga los campos pedidos; si no,
se le devuelve el error y se reintenta una vez.

    import agente
    agente.correr_lotes([agente.Trabajo("consignas/afinar.md", lote, salida, ["herramienta","tipo","frase"])])

La CLI se elige con BA_AGENTE (por defecto «claude», la suscripcion de Claude
Code) y el modelo con BA_AGENTE_MODELO.
"""
import concurrent.futures
import json
import os
import shutil
import subprocess
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

CONSIGNAS = Path(__file__).resolve().parent / "consignas"


@dataclass
class Trabajo:
    consigna: str                 # nombre del .md en consignas/
    lote: Path                    # el archivo que lee el agente
    salida: Path                  # adonde queda su JSONL
    campos: list                  # campos obligatorios de cada linea
    datos: dict = field(default_factory=dict)  # reemplazos {CLAVE} en la consigna


def _cli(prompt, cwd, timeout):
    modelo = os.environ.get("BA_AGENTE_MODELO")
    cmd = ["claude", "-p", prompt, "--setting-sources", "", "--no-session-persistence", "--strict-mcp-config",
           "--allowedTools", "Read", "Write", "--permission-mode", "acceptEdits", "--output-format", "json"]
    if modelo:
        cmd += ["--model", modelo]
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=timeout)
    try:
        d = json.loads(r.stdout)
        return d.get("terminal_reason") == "completed", d.get("total_cost_usd", 0)
    except Exception:
        return False, 0


def _validar(p, campos):
    if not p.exists():
        return "no escribiste ./salida.jsonl"
    filas = []
    for i, l in enumerate(p.read_text(encoding="utf-8").splitlines(), 1):
        if not l.strip():
            continue
        try:
            r = json.loads(l)
        except Exception:
            return f"la linea {i} no es JSON valido"
        falta = [c for c in campos if c not in r]
        if falta:
            return f"a la linea {i} le falta {falta}"
        filas.append(r)
    return None if filas else "la salida esta vacia"


def correr(t: Trabajo, timeout=1800):
    texto = (CONSIGNAS / t.consigna).read_text(encoding="utf-8")
    for k, v in t.datos.items():
        texto = texto.replace("{" + k + "}", str(v))
    prompt = (texto + "\n\nACCESO: tu UNICA fuente es ./lote.txt. Escribi tu respuesta en ./salida.jsonl"
              " (una linea JSON por elemento). No uses scripts para generar el contenido: leelo y juzgalo vos."
              " Al terminar respondé solo cuantas lineas escribiste.")
    costo = 0.0
    with tempfile.TemporaryDirectory(prefix="agente-") as d:
        d = Path(d)
        shutil.copy(t.lote, d / "lote.txt")
        for intento in range(2):
            ok, c = _cli(prompt, d, timeout)
            costo += c
            err = _validar(d / "salida.jsonl", t.campos)
            if err is None:
                shutil.copy(d / "salida.jsonl", t.salida)
                return {"lote": str(t.lote), "ok": True, "costo": costo}
            prompt += f"\n\nOJO, el intento anterior fallo: {err}. Corregilo."
            (d / "salida.jsonl").unlink(missing_ok=True)
    return {"lote": str(t.lote), "ok": False, "error": err, "costo": costo}


def correr_lotes(trabajos, paralelo=4):
    with concurrent.futures.ThreadPoolExecutor(max_workers=paralelo) as ex:
        res = list(ex.map(correr, trabajos))
    for r in res:
        if not r["ok"]:
            print(f"  [agente] FALLO {r['lote']}: {r['error']}", flush=True)
    return res


def leer(salidas):
    filas = []
    for s in salidas:
        p = Path(s)
        if p.exists():
            filas += [json.loads(l) for l in p.read_text(encoding="utf-8").splitlines() if l.strip()]
    return filas
