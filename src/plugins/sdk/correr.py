"""Arranca un plugin: ``python3 -I -u correr.py <carpeta-del-plugin>`` (lo hace la app).

Arma el sys.path —el SDK, las bibliotecas del plugin (``_libs``) y su carpeta—
y corre su archivo principal (``plugin.json`` -> ``"principal"``, de fábrica
``plugin.py``). Si el plugin no llamó ``plugin.correr()``, lo llama acá.
"""
import json
import os
import runpy
import sys

sdk = os.path.dirname(os.path.abspath(__file__))
carpeta = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else ".")
os.environ.setdefault("BA_PLUGIN_DIR", carpeta)
os.environ.setdefault("BA_PLUGIN_ID", os.path.basename(carpeta))
sys.path[:0] = [sdk, os.path.join(carpeta, "_libs"), carpeta]
os.chdir(carpeta)
try:
    with open(os.path.join(carpeta, "plugin.json"), encoding="utf-8") as f:
        principal = json.load(f).get("principal", "plugin.py")
except (OSError, ValueError):
    principal = "plugin.py"

import ba_plugin  # noqa: E402

runpy.run_path(os.path.join(carpeta, principal), run_name="__main__")
p = ba_plugin.Plugin._actual
if p is not None and not p._declarado:
    p.correr()
