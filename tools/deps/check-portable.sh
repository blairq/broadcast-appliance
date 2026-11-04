#!/usr/bin/env bash
# ============================================================================
# tools/deps/check-portable.sh <bundle> — ¿el bundle corre en otra maquina?
#
# Recorre cada ELF del bundle (ejecutables, .so, plugins) y cada biblioteca que
# pide (DT_NEEDED). Cada una tiene que estar DENTRO del bundle o en la base del
# sistema (tools/deps/linux-baseline.txt). Informa ademas la version minima de
# glibc que exige el bundle (depende de donde se compilo).
#
# Como el cargador dinamico: una dependencia del bundle tiene que estar en un
# directorio del RUNPATH/RPATH del PROPIO archivo ($ORIGIN = su carpeta). El
# RUNPATH no se hereda: un plugin de GStreamer busca FFmpeg con el suyo, no con el de ba-app.
# (LD_LIBRARY_PATH no cuenta: el launcher lo borra.)
#
# El trabajo lo hace tools/deps/check-portable.py (sin un proceso por archivo; con
# BA_PORTABLE_CACHE, lo de cada ELF recordado por su contenido).
# Sale con 1 si algo depende de una biblioteca de terceros del sistema.
# ============================================================================
set -euo pipefail
exec python3 "$(cd "$(dirname "$0")" && pwd)/check-portable.py" "${1:?uso: check-portable.sh <carpeta del bundle>}"
