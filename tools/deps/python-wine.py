#!/usr/bin/env python3
"""El Python de Windows (extern/python-win) corrido bajo Wine, para meson.

meson necesita correr el Python de destino para saber cómo es (cabeceras,
bibliotecas, dónde instalar). Bajo Wine contesta con rutas de Windows
(Z:\\trabajo\\...), que meson y clang en Linux no entienden: se traducen a
rutas de Linux (/trabajo/...). Lo usa tools/deps/gst-python-win.sh.

Entorno: BA_WINE (el wine) y BA_PYW (la carpeta de python-win).
"""
import os
import re
import subprocess
import sys

r = subprocess.run([os.environ["BA_WINE"], os.path.join(os.environ["BA_PYW"], "python.exe")] + sys.argv[1:],
                   stdout=subprocess.PIPE)
texto = r.stdout.decode("utf-8", "replace")
# Z:\algo\otro (con las barras escapadas o no, como en JSON): /algo/otro
ruta = re.compile(r"[Zz]:((?:\\+[^\\\"'\s,\]\}]*)+)")
texto = ruta.sub(lambda m: re.sub(r"\\+", "/", m.group(1)), texto)
sys.stdout.write(texto)
sys.exit(r.returncode)
