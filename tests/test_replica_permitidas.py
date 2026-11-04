"""La lista permitida del tablero web (src/app/control/ReplicaPermitidas.inc) está al día con la
QML del dashboard: si una tarjeta llama algo nuevo de Studio, hay que regenerarla (y decir con
qué herramienta MCP se autoriza). tools/generar/replica-permitidas.py"""
import json
import os
import subprocess
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def test_lista_permitida_al_dia():
    r = subprocess.run([sys.executable, os.path.join(RAIZ, "tools", "generar", "replica-permitidas.py"), "--verificar"],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr


def test_herramientas_equivalentes_existen():
    """Cada cosa de la lista se autoriza con una herramienta MCP que EXISTE (docs/mcp.json, el
    registro sacado del código): si no, ControlWeb::autorizar la rechaza siempre («No existe la
    herramienta») y el botón del tablero web no anda aunque el perfil lo deje."""
    with open(os.path.join(RAIZ, "docs", "mcp.json"), encoding="utf-8") as f:
        registro = json.load(f)
    todas = {h for c in registro["categorias"] for h in c["herramientas"]}
    with open(os.path.join(RAIZ, "src", "app", "control", "ReplicaPermitidas.inc"), encoding="utf-8") as f:
        texto = f.read()
    lista = json.loads(texto[texto.index('R"json(') + 7:texto.rindex(')json"')])
    faltan = sorted({e["mcp"] for e in lista if e.get("mcp") and e["mcp"] not in todas})
    assert not faltan, f"herramientas de la lista permitida que no están en el registro MCP: {faltan}"
