#!/usr/bin/env python3
# ============================================================================
# tools/deps/check-portable.py <bundle> — ¿el bundle corre en otra maquina? (Linux)
#
# Recorre cada ELF del bundle (ejecutables, .so, plugins) y cada biblioteca que
# pide (DT_NEEDED). Cada una tiene que estar DENTRO del bundle o en la base del
# sistema (tools/deps/linux-baseline.txt). Informa ademas la version minima de
# glibc que exige el bundle (depende de donde se compilo).
#
# Lo mismo que hacía check-portable.sh (que ahora lo llama), sin lanzar tres procesos
# por archivo (file, readelf dos veces, objdump: bajo Rosetta, 3 minutos para los
# ~1000 ELF del bundle). La tabla dinámica se lee acá (tools/ci/tests-con-cache.py) y lo
# de cada archivo (sus NEEDED, su RUNPATH/RPATH y la glibc que pide, que sale de
# objdump -T) se recuerda por el sha256 de su contenido en BA_PORTABLE_CACHE (JSON); el
# sha256, por tamaño/fechas/inodo (tools/ci/huellas.py, memo al lado). Con el bundle
# cambiado en un binario, solo ese se vuelve a mirar. Lo que se resuelve (si la
# biblioteca está en una carpeta del RUNPATH) se mira siempre: depende de lo que hay.
#
# Sale con 1 si algo depende de una biblioteca de terceros del sistema.
# ============================================================================
import importlib.util
import json
import os
import re
import stat
import subprocess
import sys

AQUI = os.path.dirname(os.path.abspath(__file__))
CI = os.path.join(os.path.dirname(AQUI), "ci")
sys.path.insert(0, CI)
import huellas  # noqa: E402

_spec = importlib.util.spec_from_file_location("tests_con_cache", os.path.join(CI, "tests-con-cache.py"))
_tcc = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_tcc)

# (Las carpetas de datos no son el programa: ahi hay agentes y plugins que bajo
# la persona, con sus propias dependencias.)
PODAR = {"Config", "Credenciales", "Agentes", "Cache", "Runtime", "Logs", "Recordings", "Shares", "Media", "Respaldos"}


def candidatos(bundle):
    for dirpath, dirnames, filenames in os.walk(bundle):
        if dirpath == bundle:
            dirnames[:] = [d for d in dirnames if d not in PODAR and not d.startswith(".actualizacion-")]
        for n in filenames:
            p = os.path.join(dirpath, n)
            try:
                st = os.lstat(p)
            except OSError:
                continue
            if not stat.S_ISREG(st.st_mode):
                continue  # find -type f: ni enlaces ni nada raro
            if st.st_mode & stat.S_IXUSR or n.endswith(".so") or ".so." in n or n.endswith(".clap"):
                yield p, st


def es_elf(p):
    try:
        with open(p, "rb") as f:
            return f.read(4) == b"\x7fELF"
    except OSError:
        return False


def glibc_de(p):
    r = subprocess.run(["objdump", "-T", p], capture_output=True, text=True)
    vs = re.findall(r"GLIBC_([0-9.]+)", r.stdout)
    return max(vs, key=version) if vs else ""


def version(v):
    return tuple(int(x) for x in re.findall(r"\d+", v))


def main(argv):
    if len(argv) != 2:
        print("uso: check-portable.py <carpeta del bundle>", file=sys.stderr)
        return 2
    bundle = argv[1].rstrip("/")
    permitidas = set()
    with open(os.path.join(AQUI, "linux-baseline.txt")) as f:
        for l in f:
            l = l.split("#", 1)[0].replace(" ", "").strip()
            if l:
                permitidas.add(l)
    cache_f = os.environ.get("BA_PORTABLE_CACHE")
    cache = {}
    if cache_f:
        try:
            with open(cache_f) as f:
                cache = json.load(f)
        except (OSError, ValueError):
            cache = {}
    memo = huellas.Memo(cache_f + ".huellas.json" if cache_f else None)
    malo, revisados, glibc_max, nuevos = False, 0, "2.0", 0
    for p, st in sorted(candidatos(bundle)):
        h = memo.archivo(p, os.stat(p))
        info = cache.get(h) if h else None
        if info is None:
            if not es_elf(p):
                info = None
            else:
                needed, rpath, runpath = _tcc.leer_dinamica_elf(p)
                # Como readelf | head -1: el RUNPATH o el RPATH, el que haya (no los dos a la vez).
                info = {"needed": needed, "runpath": runpath or rpath, "glibc": glibc_de(p)}
            if h:
                cache[h] = info or {"elf": False}
                nuevos += 1
        if not info or info.get("elf") is False:
            continue
        revisados += 1
        d = os.path.dirname(os.path.abspath(p))
        sub = lambda x: x.replace("${ORIGIN}", d).replace("$ORIGIN", d)
        for need in info["needed"]:
            if need in permitidas:
                continue
            if "/" in need:
                ok = os.path.exists(sub(need))
            else:
                ok = any(os.path.exists(os.path.join(sub(r), need)) for r in info["runpath"])
            if not ok:
                print("NO PORTABLE: %s necesita %s y no la encuentra en el bundle (su RUNPATH) ni es de la base del sistema"
                      % (os.path.relpath(p, bundle), need))
                malo = True
        v = info["glibc"]
        if v and version(v) >= version(glibc_max):
            glibc_max = v
    if cache_f:
        tmp = cache_f + ".tmp"
        with open(tmp, "w") as f:
            json.dump(cache, f, separators=(",", ":"))
        os.replace(tmp, cache_f)
        memo.guardar()
    print("ELF revisados: %d (%d leídos de nuevo); glibc minima: %s" % (revisados, nuevos, glibc_max))
    if malo:
        print("El bundle depende de bibliotecas del sistema que no son de la base (ver arriba).")
        return 1
    print("OK: el bundle solo usa la base del sistema (tools/deps/linux-baseline.txt).")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
