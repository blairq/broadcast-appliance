#!/usr/bin/env python3
# ============================================================================
# tools/ci/pytest-con-cache.py — las pruebas de Python de tests/, de a un archivo, y uno
# que ya pasó con exactamente las mismas entradas no se vuelve a correr
#
# Lo mismo que tools/ci/tests-con-cache.py hace con ctest, para `pytest` a secas (lo que
# corre el CI y la receta de Docker después de `make test`). Por archivo de tests/:
#
#   - Lo que ya corre ctest no se repite: los test_py_* de cmake/BaTestsPy.cmake (con su
#     propia caché) cubren archivos enteros o una marca (`area`); de esos archivos queda
#     solo lo que ctest no corre (`-m "not area"`), o nada.
#   - Cada archivo corre en su propio proceso de pytest (en paralelo), con un gancho de
#     auditoría de Python (sys.addaudithook) que anota lo que LEE: los archivos que abre
#     (también los módulos que importa), las carpetas que lista (glob, os.walk, rglob) y
#     los procesos que lanza (el ejecutable, las rutas de sus argumentos y, si es un .py,
#     su carpeta entera; si es un binario, sus bibliotecas). Solo lo de este repositorio:
#     extern/ entra entero por sus centinelas (tools/ci/huellas.py), el venv por
#     requirements-dev.lock y la versión de Python.
#   - Si pasó (sin salteos: un salteo depende de lo que hay en la máquina), se anotan esas
#     entradas con el sha256 de su contenido. La próxima vez, si todas siguen iguales (y lo
#     global: este script, Python, el lock, extern/, los argumentos, el entorno BA_*...),
#     no corre: «pasó (caché)». Si no, dice qué cambió. Un fallo nunca se anota.
#
# Lo que podría quedar en caché sin deber: un archivo que la prueba solo mira con
# os.path.exists/stat (sin abrirlo ni listar su carpeta; Python no lo audita) o lo que lee
# un proceso hijo que no le llega por sus argumentos (eso se declara en el archivo de la
# prueba: ENTRADAS_DE_HIJOS = ["tests/x.py", "tools/y"]). Para lo demás, TESTS_SIN_CACHE=1
# (o SIN_CACHE=1, que pone `make publicar`): corre todo y no anota nada.
#
# La caché vive en <build>/.cache-pytest/ (en Docker, en el volumen ba-trabajo); los
# registros de cada archivo, en <build>/.cache-pytest/registros/.
#
# Uso:
#   pytest-con-cache.py [--build build] [--ctest] [--jobs N] [-- argumentos de pytest...]
#       --ctest: saca lo que ya corre ctest en <build> (los test_py_*).
#   pytest-con-cache.py --explicar [--build build] [--ctest]
#       Sin correr nada: qué archivos correrían y por qué.
# ============================================================================
import argparse
import hashlib
import importlib.util
import json
import os
import re
import subprocess
import sys
import time

AQUI = os.path.dirname(os.path.abspath(__file__))
RAIZ = os.path.dirname(os.path.dirname(AQUI))
sys.path.insert(0, AQUI)
import huellas  # noqa: E402

PREFIJOS_ENTORNO = ("BA_", "GST_", "QT_", "QML", "DYLD_", "LD_LIBRARY_PATH", "LD_PRELOAD", "PYTHON", "LANG",
                    "LC_", "TZ")
# Lo de la raíz que no es una entrada de ninguna prueba (o entra por otro lado).
FUERA = (".git", ".venv", "extern", "dist")


def log(m):
    print("[pytest-cache] " + m, flush=True)


def sin_cache():
    return any(os.environ.get(v, "") not in ("", "0") for v in ("TESTS_SIN_CACHE", "SIN_CACHE"))


def sha_texto(t):
    return hashlib.sha256(t.encode("utf-8", "surrogateescape")).hexdigest()


def _tcc():
    """tools/ci/tests-con-cache.py como módulo (el nombre tiene guiones): su Cierre (las
    bibliotecas de un binario) y la versión del sistema."""
    spec = importlib.util.spec_from_file_location("tests_con_cache", os.path.join(AQUI, "tests-con-cache.py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


# --- Dentro del proceso de cada archivo ---------------------------------------------------
def correr_uno(archivo, traza, args):
    """Corre pytest sobre `archivo` anotando lo que lee; escribe la traza en `traza`."""
    abiertos, listados, procesos = [], [], []
    getcwd, fspath = os.getcwd, os.fspath
    O_ACC, O_WR = getattr(os, "O_ACCMODE", 3), os.O_WRONLY

    def ruta(p):
        if isinstance(p, int):
            return None
        try:
            p = fspath(p)
        except TypeError:
            return None
        if isinstance(p, bytes):
            p = p.decode("utf-8", "surrogateescape")
        return p if os.path.isabs(p) else os.path.join(getcwd(), p)

    def gancho(ev, a):
        # Lo mínimo acá adentro (se llama en cada open de todo el proceso): anotar.
        if ev == "open":
            modo = a[1]
            if isinstance(modo, str):
                if "r" not in modo and "+" not in modo:
                    return  # solo escribe: no es una entrada
            elif isinstance(a[2], int) and (a[2] & O_ACC) == O_WR:
                return
            p = ruta(a[0])
            if p:
                abiertos.append(p)
        elif ev in ("os.listdir", "os.scandir"):
            p = ruta(a[0] if a[0] is not None else ".")
            if p:
                listados.append(p)
        elif ev == "subprocess.Popen":
            exe, argv, cwd = a[0], a[1], a[2]
            argv = [argv] if isinstance(argv, (str, bytes)) else list(argv or [])
            procesos.append([ruta(exe) if exe else None, [x if isinstance(x, str) else str(x) for x in argv],
                             cwd if isinstance(cwd, str) else getcwd()])

    salteos = []

    class Salteos:
        def pytest_runtest_logreport(self, report):
            if report.skipped:
                motivo = report.longrepr[2] if isinstance(report.longrepr, tuple) else str(report.longrepr)
                salteos.append("%s: %s" % (report.nodeid, motivo))

    import pytest
    # tools/ci no es parte de la prueba: que importlib no la liste (quedaría como entrada).
    sys.path[:] = [d for d in sys.path if os.path.abspath(d or ".") != AQUI]
    sys.addaudithook(gancho)
    rc = pytest.main([archivo, "-q", "-p", "no:cacheprovider"] + args, plugins=[Salteos()])
    # Lo que leen sus procesos hijos y no les llega por los argumentos, si el archivo lo
    # declara: ENTRADAS_DE_HIJOS = ["tests/x.py", "tools/y"] (relativas a la raíz).
    declaradas = []
    for m in list(sys.modules.values()):
        if os.path.abspath(getattr(m, "__file__", None) or "") == os.path.abspath(archivo):
            declaradas += [os.path.join(RAIZ, d) for d in getattr(m, "ENTRADAS_DE_HIJOS", [])]
    with open(traza, "w", encoding="utf-8") as f:
        json.dump({"rc": int(rc), "abiertos": sorted(set(abiertos)), "listados": sorted(set(listados)),
                   "procesos": procesos, "declaradas": declaradas, "salteos": salteos}, f)
    return int(rc)


# --- Las entradas ---------------------------------------------------------------------------
class Entradas:
    def __init__(self, cache_dir):
        self.memo = huellas.Memo(os.path.join(cache_dir, "huellas.json"))
        self.cache_dir = cache_dir
        self._cierre = None
        self.listas = {}

    def cierre(self):
        if self._cierre is None:
            tcc = _tcc()
            self._cierre = tcc.Cierre(self.memo, os.path.join(self.cache_dir, "binarios"))
            self._tcc = tcc
        return self._cierre

    def guardar(self):
        self.memo.guardar()
        if self._cierre is not None:
            self._cierre.guardar()

    @staticmethod
    def adentro(p):
        """¿Es de este repositorio (y no de lo que entra por otro lado)?"""
        p = os.path.normpath(p)
        if not p.startswith(RAIZ + os.sep):
            return False
        rel = os.path.relpath(p, RAIZ).split(os.sep)
        if rel[0] in FUERA or "__pycache__" in rel:
            return False
        if rel[0] == "build" and len(rel) > 1 and rel[1].startswith(".cache-"):
            return False
        return True

    def listado(self, d):
        """Qué hay en una carpeta (nombres y si son carpetas), no su contenido. Sin lo que deja
        Python al importar (__pycache__, .pyc) ni las cachés de pytest."""
        try:
            nombres = sorted(("%s%s" % (e.name, "/" if e.is_dir() else "")) for e in os.scandir(d)
                             if e.name not in ("__pycache__", ".pytest_cache") and not e.name.endswith(".pyc"))
        except OSError:
            return "falta"
        return sha_texto("\n".join(nombres))

    def valor(self, k):
        """El valor actual de una entrada («tipo ruta»)."""
        tipo, _, p = k.partition(" ")
        if tipo in ("archivo", "binario"):
            if os.path.isdir(p):
                return "carpeta"
            return self.memo.archivo(p) or "falta"
        if tipo == "listado":
            return self.listado(p)
        if tipo == "arbol":
            return self.memo.ruta(p, filtro=True)
        return "?"

    def de_traza(self, t):
        """{«tipo ruta»: valor} de lo que anotó el gancho."""
        partes = {}
        for p in t["abiertos"]:
            if "__pycache__" in p and p.endswith(".pyc"):
                try:
                    p = importlib.util.source_from_cache(p)
                except ValueError:
                    continue
            if self.adentro(p) and not os.path.isdir(p):
                partes["archivo " + os.path.normpath(p)] = None
        for d in t["listados"]:
            if self.adentro(d) or os.path.normpath(d) == RAIZ:
                partes["listado " + os.path.normpath(d)] = None
        for d in t.get("declaradas", []):
            d = os.path.normpath(d)
            partes[("arbol " if os.path.isdir(d) else "archivo ") + d] = None
        for exe, argv, cwd in t["procesos"]:
            exe = exe or (argv[0] if argv else None)
            cands = [exe] + argv[1:] if exe else argv[1:]
            for c in cands:
                if not isinstance(c, str):
                    continue
                c = c.split("=", 1)[1] if c.startswith("-") and "=" in c else c
                if not os.path.isabs(c):
                    c = os.path.join(cwd, c) if os.sep in c else None
                if not c or not os.path.exists(c) or not self.adentro(c):
                    continue
                c = os.path.normpath(c)
                if os.path.isdir(c):
                    partes["arbol " + c] = None
                elif c.endswith(".py"):
                    # Un script de Python: lo que importa de su carpeta no pasa por acá.
                    partes["arbol " + os.path.dirname(c)] = None
                elif self.cierre() is not None and self._tcc.tipo_binario(c):
                    for b in self._cierre.de(c):
                        if self.adentro(b):
                            partes["binario " + os.path.normpath(b)] = None
                else:
                    partes["archivo " + c] = None
        return {k: self.valor(k) for k in sorted(partes)}


def partes_globales(args, marca):
    yo = hashlib.sha256()
    for f in ("pytest-con-cache.py", "huellas.py", "tests-con-cache.py"):
        with open(os.path.join(AQUI, f), "rb") as fh:
            yo.update(fh.read())
    lock = os.path.join(RAIZ, "requirements-dev.lock")
    p = {"script": yo.hexdigest(), "python": sys.version.split()[0] + " " + sys.executable,
         "sistema": _tcc().version_sistema(), "argumentos": json.dumps(args + marca),
         "lock": huellas.sha256_archivo(lock) if os.path.exists(lock) else "falta",
         "extern": huellas.centinelas(os.path.join(RAIZ, "extern"))}
    for k in sorted(os.environ):
        if k.startswith(PREFIJOS_ENTORNO):
            p["entorno " + k] = os.environ[k]
    return p


# --- Lo que ya corre ctest ------------------------------------------------------------------
def de_ctest(build):
    """{tests/x.py: (marcas que corre ctest, [nombres de los tests])} de los test_py_*."""
    r = subprocess.run(["ctest", "--test-dir", build, "--show-only=json-v1"], capture_output=True, text=True)
    if r.returncode != 0:
        log("ctest --show-only falló: no se saca nada de lo que corre ctest")
        return {}
    cubre = {}
    for t in json.loads(r.stdout).get("tests", []):
        cmd = t.get("command") or []
        if "pytest" not in cmd:
            continue
        i = cmd.index("pytest")
        resto = cmd[i + 1:]
        marca = None
        if "-m" in resto:
            marca = resto[resto.index("-m") + 1]
        for a in resto:
            if a.endswith(".py") and os.path.isabs(a):
                rel = os.path.relpath(a, RAIZ)
                m, n = cubre.setdefault(rel, (set(), []))
                m.add(marca)
                n.append(t["name"])
    return cubre


def falta_de(marcas):
    """Lo que ctest NO corre de un archivo, como argumentos de pytest; None = nada."""
    if None in marcas or {"area", "not area"} <= marcas:
        return None
    if marcas == {"area"}:
        return ["-m", "not area"]
    if marcas == {"not area"}:
        return ["-m", "area"]
    return []  # otra marca: se corre entero (no se sabe complementar)


# --- Principal ------------------------------------------------------------------------------
def main(argv):
    if argv and argv[0] == "--uno":
        archivo, traza = argv[1], argv[3]
        resto = argv[5:] if len(argv) > 4 and argv[4] == "--" else []
        return correr_uno(archivo, traza, resto)

    ap = argparse.ArgumentParser(prog="pytest-con-cache.py")
    ap.add_argument("--build", default="build")
    ap.add_argument("--ctest", action="store_true")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--explicar", action="store_true")
    if "--" in argv:
        i = argv.index("--")
        a, args = ap.parse_args(argv[:i]), argv[i + 1:]
    else:
        a, args = ap.parse_args(argv), []
    build = os.path.abspath(a.build)
    cache_dir = os.path.join(build, ".cache-pytest")
    for d in ("ok", "registros", "trazas"):
        os.makedirs(os.path.join(cache_dir, d), exist_ok=True)
    archivos = sorted(os.path.join("tests", n) for n in os.listdir(os.path.join(RAIZ, "tests"))
                      if n.startswith("test_") and n.endswith(".py"))
    cubre = de_ctest(build) if a.ctest else {}
    usar = not sin_cache()
    if not usar:
        log("TESTS_SIN_CACHE=1: corren todos, sin anotar")
    ent = Entradas(cache_dir)
    pendientes, de_cache, por_ctest = [], 0, 0
    globales = {}
    for rel in archivos:
        marca = []
        if rel in cubre:
            marcas, nombres = cubre[rel]
            falta = falta_de(marcas)
            quien = ", ".join(sorted(nombres)[:2]) + (" y %d más" % (len(nombres) - 2) if len(nombres) > 2 else "")
            if falta is None:
                por_ctest += 1
                print("      %s %s lo corre ctest (%s)" % (rel, "." * max(3, 44 - len(rel)), quien))
                continue
            marca = falta
        g = globales.setdefault(json.dumps(marca), partes_globales(args, marca))
        anotado = None
        try:
            with open(os.path.join(cache_dir, "ok", os.path.basename(rel) + ".json"), encoding="utf-8") as f:
                anotado = json.load(f)
        except (OSError, ValueError):
            pass
        if not usar:
            pendientes.append((rel, marca, g, ["TESTS_SIN_CACHE=1"]))
            continue
        if anotado is None:
            pendientes.append((rel, marca, g, ["nunca pasó (o no hay registro de sus entradas)"]))
            continue
        cambios = ["cambió " + k for k in sorted(set(g) | set(anotado["globales"]))
                   if g.get(k) != anotado["globales"].get(k)]
        if not cambios:
            for k, v in anotado["entradas"].items():
                if ent.valor(k) != v:
                    cambios.append("cambió " + k.replace(RAIZ + "/", ""))
        if cambios:
            pendientes.append((rel, marca, g, cambios))
            continue
        de_cache += 1
        print("      %s %s pasó (caché: mismas %d entradas)" % (rel, "." * max(3, 44 - len(rel)), len(anotado["entradas"])))
    for rel, marca, _, por in pendientes:
        print("  corre %s%s: %s%s" % (rel, " " + " ".join(marca) if marca else "", por[0],
                                      " (y %d más)" % (len(por) - 1) if len(por) > 1 else ""))
    if a.explicar:
        ent.guardar()
        log("%d archivos: %d correrían, %d de la caché, %d los corre ctest" % (len(archivos), len(pendientes), de_cache, por_ctest))
        return 0

    # Los procesos, lanzados desde este hilo solo (lanzar desde varios hilos trabó Rosetta).
    t0 = time.time()
    corriendo, fallaron, anotados, rc_total = [], [], 0, 0
    cola = list(pendientes)
    while cola or corriendo:
        while cola and len(corriendo) < max(1, a.jobs):
            rel, marca, g, _ = cola.pop(0)
            nombre = os.path.basename(rel)
            traza = os.path.join(cache_dir, "trazas", nombre + ".json")
            registro = os.path.join(cache_dir, "registros", nombre + ".log")
            try:
                os.unlink(traza)
            except OSError:
                pass
            f = open(registro, "w", encoding="utf-8")
            p = subprocess.Popen([sys.executable, "-B", os.path.abspath(__file__), "--uno", os.path.join(RAIZ, rel),
                                  "--traza", traza, "--"] + args + marca, cwd=RAIZ, stdout=f, stderr=subprocess.STDOUT)
            corriendo.append((p, f, rel, marca, g, traza, registro, time.time()))
        time.sleep(0.2)
        for item in list(corriendo):
            p, f, rel, marca, g, traza, registro, ti = item
            rc = p.poll()
            if rc is None:
                continue
            corriendo.remove(item)
            f.close()
            seg = time.time() - ti
            with open(registro, encoding="utf-8", errors="replace") as fr:
                salida = fr.read()
            t = None
            try:
                with open(traza, encoding="utf-8") as ft:
                    t = json.load(ft)
            except (OSError, ValueError):
                pass
            ultima = salida.strip().splitlines()[-1] if salida.strip() else ""
            if rc in (0, 5):  # 5: nada que correr (todo lo de este archivo lo corre ctest)
                if t and usar and not t["salteos"]:
                    with open(os.path.join(cache_dir, "ok", os.path.basename(rel) + ".json.tmp"), "w", encoding="utf-8") as fo:
                        json.dump({"globales": g, "entradas": ent.de_traza(t), "rc": rc, "segundos": round(seg, 1),
                                   "cuando": time.strftime("%Y-%m-%dT%H:%M:%S")}, fo, indent=0, sort_keys=True)
                    os.replace(os.path.join(cache_dir, "ok", os.path.basename(rel) + ".json.tmp"),
                               os.path.join(cache_dir, "ok", os.path.basename(rel) + ".json"))
                    anotados += 1
                    print("  pasó %s (%.0f s): %s" % (rel, seg, ultima))
                else:
                    n = len(t["salteos"]) if t else 0
                    print("  pasó %s (%.0f s): %s%s" % (rel, seg, ultima, " — con %d salteos: no se anota (un salteo "
                                                         "depende de la máquina)" % n if n else ""))
            else:
                rc_total = 1
                fallaron.append(rel)
                print("---- FALLÓ %s (rc=%s, %.0f s; registro: %s)" % (rel, rc, seg, os.path.relpath(registro, RAIZ)))
                print(salida[-6000:])
    ent.guardar()
    log("%d archivos: %d de la caché (ya pasaron con estas mismas entradas), %d los corre ctest, %d corridos en %.0f s, "
        "%d anotados%s" % (len(archivos), de_cache, por_ctest, len(pendientes), time.time() - t0, anotados,
                           "; fallaron: " + " ".join(fallaron) if fallaron else ""))
    return rc_total


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
