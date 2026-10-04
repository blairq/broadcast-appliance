"""ffmpeg, ffprobe, yt-dlp y gst-launch del programa (vienen incluidos).

El programa los pone primeros en el PATH de cada plugin, así que
``subprocess.run(["ffmpeg", ...])`` ya usa los suyos. Esto es para quien quiera
la ruta exacta o correrlos sin pensar en eso::

    from ba_plugin import herramientas
    info = herramientas.probar("/ruta/video.mp4")          # ffprobe en JSON
    herramientas.correr("ffmpeg", "-i", src, "-c", "copy", dst)

No bajes ffmpeg ni instales nada: ya está.
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess

_VARS = {"ffmpeg": "BA_FFMPEG", "ffprobe": "BA_FFPROBE", "yt-dlp": "BA_YTDLP", "gst-launch-1.0": "BA_GST_LAUNCH"}


def ruta(nombre: str) -> str:
    """Ruta completa de una herramienta del programa («ffmpeg», «yt-dlp»…)."""
    var = _VARS.get(nombre)
    if var and os.environ.get(var) and os.path.isfile(os.environ[var]):
        return os.environ[var]
    carpeta = os.environ.get("BA_HERRAMIENTAS", "")
    for n in (nombre, nombre + ".exe"):
        p = os.path.join(carpeta, n)
        if carpeta and os.path.isfile(p):
            return p
    encontrada = shutil.which(nombre)
    if encontrada:
        return encontrada
    raise FileNotFoundError(f"{nombre}: no está en el programa (¿falta make deps-herramientas?)")


def correr(nombre: str, *args: str, tope: float | None = None, **kw) -> subprocess.CompletedProcess:
    """Corre la herramienta; devuelve el CompletedProcess (texto en stdout/stderr)."""
    return subprocess.run([ruta(nombre), *map(str, args)], capture_output=True, text=True, timeout=tope, **kw)


def probar(archivo: str) -> dict:
    """ffprobe de un archivo: {format, streams} en JSON."""
    r = correr("ffprobe", "-v", "error", "-print_format", "json", "-show_format", "-show_streams", archivo, tope=60)
    if r.returncode != 0:
        raise RuntimeError(r.stderr.strip() or f"ffprobe falló ({r.returncode})")
    return json.loads(r.stdout)
