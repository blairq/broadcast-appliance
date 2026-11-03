"""La lista permitida del tablero web (src/app/control/ReplicaPermitidas.inc) está al día con la
QML del dashboard: si una tarjeta llama algo nuevo de Studio, hay que regenerarla (y decir con
qué herramienta MCP se autoriza). tools/generar/replica-permitidas.py"""
import os
import subprocess
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def test_lista_permitida_al_dia():
    r = subprocess.run([sys.executable, os.path.join(RAIZ, "tools", "generar", "replica-permitidas.py"), "--verificar"],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr
