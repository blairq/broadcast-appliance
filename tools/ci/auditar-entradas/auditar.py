#!/usr/bin/env python3
# ============================================================================
# tools/ci/auditar-entradas/auditar.py — ¿cada test declara todo lo que lee? (macOS)
#
# La caché de tests (tools/ci/tests-con-cache.py) no vuelve a correr un test cuyas
# entradas no cambiaron. Si un test lee un archivo del árbol que no está en su clave
# (ni por su comando, ni por una definición, ni declarado con ba_test_entradas() o
# ba_biblioteca_lee()), queda en caché aunque ese archivo cambie: no corre cuando
# debería. Esto lo encuentra midiendo:
#
#   auditar.py correr  --build build [-R regex] [-E regex] [--jobs N]
#       Corre cada test con ctest, uno por uno, con traza.c inyectada
#       (DYLD_INSERT_LIBRARIES): anota lo que abre para leer. Las trazas van a
#       <build>/auditar-entradas/<test>/. Cuenta como compilar: con turno.
#   auditar.py informe --build build
#       Por test, lo que ABRIÓ del árbol de fuentes o del build y no está en su
#       clave. No cuenta: lo intermedio (CMakeFiles, __pycache__, binarios, que
#       ya entran por el cierre de bibliotecas), lo que no existe, lo que el test
#       también escribió (registros, la caché de GStreamer, temporales: estado, no
#       entrada) y extern/ (lo que se carga con dlopen de ahí no entra: ver
#       cmake/BaTestsCache.cmake). Sale con 1 si encontró algo.
#
# Un stat o un listado de carpeta no es una lectura: los archivos nuevos los ve la
# reconfiguración (GLOB CONFIGURE_DEPENDS). Los hijos que son binarios del sistema
# (/bin/sh) pierden la traza (SIP).
# ============================================================================
import argparse
import concurrent.futures
import json
import os
import subprocess
import sys

AQUI = os.path.dirname(os.path.abspath(__file__))
RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(AQUI)))
INTERMEDIO = ("CMakeFiles", "Testing", ".cache-tests", "_deps", ".qt", "__pycache__", "auditar-entradas")
# Lo que un test lee de más A PROPÓSITO (la razón está en su CMake): no se informa.
LEE_DE_MAS = {
    # Lee todos los .cpp de la app buscando quién registra un republicador; en su clave
    # están los que registran (cmake/BaTestsPy.cmake): para que falle hay que tocar uno.
    "test_py_republicadores",
}
# Las carpetas de estado del programa corriendo desde el build (la caché de plugins de
# GStreamer, los registros, las credenciales de prueba): no son entradas.
ESTADO = {"Cache", "Logs", "Credenciales"}
MAGIAS_BINARIO = (b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe", b"\xca\xfe\xba\xbe", b"!<ar")


def tests_de(build, filtros):
    r = subprocess.run(["ctest", "--test-dir", build, "--show-only=json-v1"] + filtros,
                       capture_output=True, text=True, check=True)
    return json.loads(r.stdout)["tests"]


def dylib(build):
    salida = os.path.join(build, "auditar-entradas", "traza.dylib")
    fuente = os.path.join(AQUI, "traza.c")
    if not os.path.exists(salida) or os.path.getmtime(salida) < os.path.getmtime(fuente):
        os.makedirs(os.path.dirname(salida), exist_ok=True)
        subprocess.run(["clang", "-dynamiclib", "-O1", "-o", salida, fuente], check=True)
    return salida


def correr(a):
    if sys.platform != "darwin":
        sys.exit("auditar.py correr: solo en macOS (DYLD_INSERT_LIBRARIES)")
    build = os.path.abspath(a.build)
    lib = dylib(build)
    filtros = (["-R", a.R] if a.R else []) + (["-E", a.E] if a.E else [])
    base = os.path.join(build, "auditar-entradas")

    def uno(t):
        n = t["name"]
        d = os.path.join(base, n)
        os.makedirs(d, exist_ok=True)
        for f in os.listdir(d):
            os.unlink(os.path.join(d, f))
        env = dict(os.environ, BA_TRAZA_DIR=d, BA_TRAZA_PREFIJO=os.path.dirname(RAIZ), DYLD_INSERT_LIBRARIES=lib)
        r = subprocess.run(["ctest", "--test-dir", build, "-R", "^%s$" % n], env=env, capture_output=True, text=True)
        return n, "100% tests passed" in r.stdout

    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        for n, ok in ex.map(uno, tests_de(build, filtros)):
            print(("ok     " if ok else "FALLA  ") + n, flush=True)


def declaradas(t, entradas):
    decl = list(entradas.get(t["name"], {}).get("entradas", []))
    for x in t.get("command", []):
        decl.append(x.split("=", 1)[1] if x.startswith("-") and "=" in x else x)
    for p in t.get("properties", []):
        if p["name"] == "ENVIRONMENT":
            for e in p["value"]:
                if "=" in e:
                    decl.extend(e.split("=", 1)[1].split(":"))
    return [os.path.realpath(x) for x in decl if os.path.isabs(x)]


def es_binario(p):
    try:
        with open(p, "rb") as f:
            return f.read(4) in MAGIAS_BINARIO
    except OSError:
        return True


def informe(a):
    build = os.path.realpath(a.build)
    fuente = os.path.realpath(RAIZ)
    with open(os.path.join(build, "tests-entradas.json")) as f:
        entradas = json.load(f)
    base = os.path.join(build, "auditar-entradas")
    res = {}
    for t in tests_de(build, []):
        d = os.path.join(base, t["name"])
        if not os.path.isdir(d):
            continue
        decl = declaradas(t, entradas)
        if t["name"] in LEE_DE_MAS:
            continue
        leidos, escritos = set(), set()
        for f in os.listdir(d):
            with open(os.path.join(d, f), errors="replace") as fh:
                for l in fh:
                    if l[:2] in ("O ", "W "):
                        (leidos if l[0] == "O" else escritos).add(os.path.realpath(l[2:].rstrip("\n")))
        # Lo que el test (o lo que corre) escribe y relee es su estado, no una entrada:
        # registros, la caché de plugins de GStreamer, sus temporales.
        leidos -= escritos
        faltan = []
        for p in sorted(leidos):
            if not p.startswith(fuente + "/") or not os.path.isfile(p):
                continue
            rel = os.path.relpath(p, fuente)
            partes = rel.split("/")
            if partes[0] in (".git", ".venv", "extern") or any(x in partes for x in INTERMEDIO):
                continue
            if rel in ("CTestTestfile.cmake", "DartTestfile.txt") or partes[-1] in ("CTestTestfile.cmake", "CMakeCache.txt"):
                continue
            if p.startswith(build + "/") and (es_binario(p) or ESTADO & set(partes)):
                continue
            if any(p == x or p.startswith(x + "/") for x in decl):
                continue
            faltan.append(rel)
        if faltan:
            res[t["name"]] = faltan
    json.dump(res, sys.stdout, indent=1, ensure_ascii=False)
    print()
    return 1 if res else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("modo", choices=("correr", "informe"))
    ap.add_argument("--build", required=True)
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("-R")
    ap.add_argument("-E")
    a = ap.parse_args()
    return correr(a) if a.modo == "correr" else informe(a)


if __name__ == "__main__":
    sys.exit(main() or 0)
