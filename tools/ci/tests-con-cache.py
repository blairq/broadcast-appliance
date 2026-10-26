#!/usr/bin/env python3
# ============================================================================
# tools/ci/tests-con-cache.py — un test que ya pasó con exactamente las mismas
# entradas no se vuelve a correr (como Bazel)
#
# La clave de cada test es el sha256 de TODO lo que puede cambiar su resultado:
#   - su ejecutable y las bibliotecas que carga, siguiendo la cadena entera
#     (macOS: otool -l con @rpath/@loader_path; Linux: ldd; Windows: la tabla de
#     importaciones de cada PE con llvm-objdump, como tools/deps/check-portable-win.sh
#     y con el mismo formato de caché «sha<TAB>dll dll...»). Lo del sistema
#     (/usr/lib, /System, las DLL de Wine) no: lo cubre la versión del sistema;
#   - lo que LEE: las rutas de su línea de comando y de su ENVIRONMENT, las rutas
#     que le pasa CMake por definiciones de compilación y las declaradas con
#     ba_test_entradas() (cmake/BaTestsCache.cmake → <build>/tests-entradas.json).
#     Un archivo entra por su contenido; una carpeta, entera (sin lo intermedio
#     del build: CMakeFiles, *.o, registros);
#   - su comando, sus propiedades de ctest, las variables de entorno que importan
#     (BA_*, GST_*, QT_*, DYLD_*, LD_*, WINE*, PYTHON*, LANG, LC_*), la versión de
#     ctest o de Wine, la del sistema y la de este script.
# Solo se guarda la clave de un test que pasó (0) o se salteó (77); un fallo
# nunca. Los contenidos se leen una vez: después, el memo de tools/ci/huellas.py
# (tamaño, mtime, ctime e inodo) dice si un archivo cambió sin releerlo.
#
# La caché vive en <build>/.cache-tests/ (en Docker, en el volumen ba-trabajo).
# TESTS_SIN_CACHE=1 (o SIN_CACHE=1) corre todo igual (y anota lo que pasa):
# es lo que hace `make publicar`. TESTS_SIN_LENTOS=1 deja afuera los de la
# etiqueta `lento`.
#
# Uso:
#   tests-con-cache.py --test-dir build [args de ctest...]      (make test)
#       Los filtros (-R, -E, -L, -LE) eligen los tests; el resto (-j,
#       --output-on-failure...) va al ctest que corre los que faltan.
#   tests-con-cache.py --test-dir build --explicar test_x
#       Qué entra en la clave de test_x y si está en la caché.
#   tests-con-cache.py wine-preparar --build build/win --herramienta <wine> \
#       --objdump <llvm-objdump> --ruta "$WINEPATH" --pendientes F  < lista de .exe
#   tests-con-cache.py wine-guardar --build build/win --resultados F
#       (los usa tools/ci/test-win.sh)
#
# Qué podría quedar en caché sin deber: un test que lee algo que no le llega por
# ninguno de esos caminos (una ruta armada a mano en el código, un módulo de
# Python importado de otra carpeta, la red, el hardware). Para eso está
# ba_test_entradas(); y antes de un release, TESTS_SIN_CACHE=1.
# ============================================================================
import argparse
import concurrent.futures
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import threading
import time
import xml.etree.ElementTree as ET

AQUI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, AQUI)
import huellas  # noqa: E402

PREFIJOS_ENTORNO = ("BA_", "GST_", "QT_", "QML", "DYLD_", "LD_LIBRARY_PATH", "LD_PRELOAD", "PYTHON",
                    "WINE", "LANG", "LC_", "TZ")
# No cambian lo que hace un test (son de este script o del build) o son su
# estado, no una entrada (el prefijo de Wine lo modifican los mismos tests).
ENTORNO_IGNORADO = {"BA_ACCEPT_MSVC_LICENSE", "WINEPREFIX"}
SISTEMA = ("/usr/", "/System/", "/bin/", "/sbin/", "/lib/", "/lib64/", "/etc/", "/dev/", "/proc/",
           "/Library/Apple/", "/Applications/Xcode")


def log(m):
    print("[tests-cache] " + m, flush=True)


def sin_cache():
    return any(os.environ.get(v, "") not in ("", "0") for v in ("TESTS_SIN_CACHE", "SIN_CACHE"))


def sin_lentos():
    return os.environ.get("TESTS_SIN_LENTOS", "") not in ("", "0")


def es_sistema(p):
    return p.startswith(SISTEMA)


def sha_texto(t):
    return hashlib.sha256(t.encode("utf-8", "surrogateescape")).hexdigest()


def trozos(v):
    """Las rutas de una lista tipo PATH: «;» (Windows) o «:», sin partir «Z:\\...»."""
    r = []
    for t in v.split(";"):
        if re.match(r"^[A-Za-z]:[\\/]", t):
            r.append(t)
        else:
            r.extend(t.split(":"))
    return r


def tipo_binario(p):
    try:
        with open(p, "rb") as f:
            m = f.read(4)
    except OSError:
        return None
    if m in (b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe", b"\xca\xfe\xba\xbe"):
        return "macho"
    if m == b"\x7fELF":
        return "elf"
    if m[:2] == b"MZ":
        return "pe"
    return None


class Cierre:
    """Las bibliotecas que carga un binario, siguiendo toda la cadena."""

    def __init__(self, memo, cache_dir, ruta_pe=(), objdump=None):
        self.memo = memo
        self.cache_dir = cache_dir
        self.ruta_pe = list(ruta_pe)
        self.objdump = objdump
        self.candado = threading.Lock()
        self.macho = self._leer_json("macho.json")
        self.pe = {}
        try:
            with open(os.path.join(cache_dir, "importaciones-pe.tsv")) as f:
                for linea in f:
                    h, _, dlls = linea.rstrip("\n").partition("\t")
                    if h:
                        self.pe[h] = dlls
        except OSError:
            pass
        self.cambio = False
        self.indices = {}  # carpeta -> {nombre en minúsculas: ruta}

    def _leer_json(self, nombre):
        try:
            with open(os.path.join(self.cache_dir, nombre)) as f:
                return json.load(f)
        except (OSError, ValueError):
            return {}

    def guardar(self):
        if not self.cambio:
            return
        os.makedirs(self.cache_dir, exist_ok=True)
        tmp = os.path.join(self.cache_dir, "macho.json.tmp.%d" % os.getpid())
        with open(tmp, "w") as f:
            json.dump(self.macho, f, separators=(",", ":"))
        os.replace(tmp, os.path.join(self.cache_dir, "macho.json"))
        tmp = os.path.join(self.cache_dir, "importaciones-pe.tsv.tmp.%d" % os.getpid())
        with open(tmp, "w") as f:
            for h in sorted(self.pe):
                f.write("%s\t%s\n" % (h, self.pe[h]))
        os.replace(tmp, os.path.join(self.cache_dir, "importaciones-pe.tsv"))

    def de(self, exe):
        t = tipo_binario(exe)
        if t == "macho":
            return self._macho(exe)
        if t == "elf":
            return self._elf(exe)
        if t == "pe":
            return self._pe(exe)
        return [exe]

    # --- macOS -------------------------------------------------------------
    def _info_macho(self, p):
        h = self.memo.archivo(p)
        if h is None:
            return [], []
        with self.candado:
            if h in self.macho:
                return self.macho[h]
        arch = ["-arch", platform.machine()] if platform.machine() in ("arm64", "x86_64") else []
        r = subprocess.run(["otool", "-l"] + arch + [p], capture_output=True, text=True)
        if r.returncode != 0:
            r = subprocess.run(["otool", "-l", p], capture_output=True, text=True)
        deps, rpaths, cmd = [], [], None
        for linea in r.stdout.splitlines():
            s = linea.strip()
            if s.startswith("cmd "):
                cmd = s[4:]
            elif s.startswith("name ") and cmd in ("LC_LOAD_DYLIB", "LC_LOAD_WEAK_DYLIB", "LC_REEXPORT_DYLIB",
                                                   "LC_LAZY_LOAD_DYLIB", "LC_LOAD_UPWARD_DYLIB"):
                deps.append(s[5:].rsplit(" (offset", 1)[0])
            elif s.startswith("path ") and cmd == "LC_RPATH":
                rpaths.append(s[5:].rsplit(" (offset", 1)[0])
        v = [deps, rpaths]
        with self.candado:
            self.macho[h] = v
            self.cambio = True
        return v

    def _macho(self, exe):
        exe_dir = os.path.dirname(os.path.abspath(exe))
        vistos, orden = set(), []
        pila = [(os.path.abspath(exe), [])]
        while pila:
            p, heredados = pila.pop()
            real = os.path.realpath(p)
            if real in vistos:
                continue
            vistos.add(real)
            orden.append(p)
            deps, rpaths = self._info_macho(real)
            loader = os.path.dirname(p)
            propios = [r.replace("@loader_path", loader).replace("@executable_path", exe_dir) for r in rpaths]
            todos = propios + heredados
            for d in deps:
                r = None
                if d.startswith("@executable_path/"):
                    r = exe_dir + d[len("@executable_path"):]
                elif d.startswith("@loader_path/"):
                    r = loader + d[len("@loader_path"):]
                elif d.startswith("@rpath/"):
                    for rp in todos:
                        c = rp + d[len("@rpath"):]
                        if os.path.exists(c):
                            r = c
                            break
                else:
                    r = d
                if r is None or es_sistema(r) or not os.path.exists(r):
                    continue
                pila.append((os.path.normpath(r), todos))
        return orden

    # --- Linux -------------------------------------------------------------
    def _elf(self, exe):
        r = subprocess.run(["ldd", exe], capture_output=True, text=True)
        orden = [exe]
        for linea in r.stdout.splitlines():
            m = re.search(r"=>\s*(/\S+)", linea) or re.match(r"\s*(/\S+)\s+\(", linea)
            if m and not es_sistema(m.group(1)):
                orden.append(m.group(1))
        return orden

    # --- Windows (bajo Wine) ----------------------------------------------
    def _indice(self, d):
        with self.candado:
            if d in self.indices:
                return self.indices[d]
        idx = {}
        try:
            for n in os.listdir(d):
                idx.setdefault(n.lower(), os.path.join(d, n))
        except OSError:
            pass
        with self.candado:
            self.indices[d] = idx
        return idx

    def _importa(self, p):
        h = self.memo.archivo(p)
        if h is None:
            return []
        with self.candado:
            if h in self.pe:
                return self.pe[h].split()
        r = subprocess.run([self.objdump, "-p", p], capture_output=True, text=True)
        dlls = " ".join(l.split("DLL Name:", 1)[1].strip() for l in r.stdout.splitlines() if "DLL Name:" in l)
        with self.candado:
            self.pe[h] = dlls
            self.cambio = True
        return dlls.split()

    def _pe(self, exe):
        # El orden de búsqueda de Windows para lo que se importa: la carpeta del
        # .exe (no la de la DLL que importa), el sistema, el PATH. Lo que no
        # está en ninguna de las nuestras es de Windows (de Wine): no entra.
        dirs = [os.path.dirname(os.path.abspath(exe))] + self.ruta_pe
        vistos, orden, pila = set(), [], [os.path.abspath(exe)]
        while pila:
            p = pila.pop()
            if p in vistos:
                continue
            vistos.add(p)
            orden.append(p)
            for dll in self._importa(p):
                k = dll.lower()
                if k.startswith(("api-ms-win-", "ext-ms-win-")):
                    continue
                for d in dirs:
                    c = self._indice(d).get(k)
                    if c:
                        pila.append(c)
                        break
        return orden


class Cache:
    def __init__(self, build, global_partes, ruta_pe=(), objdump=None):
        self.build = os.path.abspath(build)
        self.dir = os.path.join(self.build, ".cache-tests")
        os.makedirs(os.path.join(self.dir, "ok"), exist_ok=True)
        self.memo = huellas.Memo(os.path.join(self.dir, "huellas.json"))
        self.cierre = Cierre(self.memo, self.dir, ruta_pe, objdump)
        self.global_partes = global_partes
        self.raices = {self.build, os.path.dirname(self.build)}
        fuente = leer_cmakecache(self.build, "CMAKE_HOME_DIRECTORY")
        if fuente:
            self.raices.add(os.path.abspath(fuente))
        self.raices.add(os.path.expanduser("~"))
        self.raices.add("/")

    def guardar(self):
        self.memo.guardar()
        self.cierre.guardar()

    def entrada(self, partes, p):
        """Agrega lo que haya en la ruta p (si existe y no es del sistema)."""
        p = os.path.normpath(p)
        if es_sistema(p) or ("ruta " + p) in partes or ("binario " + p) in partes:
            return
        if p in self.raices:
            # La raíz del repo o del build entera: cambiaría con cualquier cosa (y
            # con los registros de los mismos tests). Lo que el test lee de ahí
            # se declara con ba_test_entradas().
            partes["ignorada " + p] = "raíz (declará lo que lee con ba_test_entradas)"
            return
        if os.path.isfile(p) and tipo_binario(p):
            for b in self.cierre.de(p):
                partes["binario " + b] = self.memo.archivo(b) or "falta"
            return
        partes["ruta " + p] = self.memo.ruta(p, filtro=True)

    def clave(self, comando, propiedades, entradas, entorno_extra=(), ignorar=(), extra=None):
        partes = dict(self.global_partes)
        partes.update(extra or {})
        partes["comando"] = json.dumps(comando)
        partes["propiedades"] = json.dumps(propiedades, sort_keys=True)
        if comando:
            exe = comando[0]
            if os.path.isabs(exe) and os.path.exists(exe):
                if tipo_binario(exe):
                    for b in self.cierre.de(exe):
                        partes["binario " + b] = self.memo.archivo(b) or "falta"
                else:
                    partes["ruta " + exe] = self.memo.ruta(exe)
            else:
                partes["comando falta"] = exe
        candidatas = []
        for a in comando[1:]:
            candidatas.append(a.split("=", 1)[1] if a.startswith("-") and "=" in a else a)
        for e in list(propiedades.get("ENVIRONMENT", [])) + list(entorno_extra):
            if "=" in e:
                candidatas.extend(trozos(e.split("=", 1)[1]))
        candidatas.extend(entradas)
        for c in candidatas:
            if c.startswith("Z:\\"):  # una ruta de Wine
                c = c[2:].replace("\\", "/")
            if any(c == i or c.startswith(i + "/") for i in ignorar):
                # Lo arma un test de preparación (FIXTURES_SETUP) y el test lo
                # modifica al correr: entra la clave de quien lo prepara.
                partes["producto de preparación " + c] = "no entra"
                continue
            if os.path.isabs(c) and os.path.exists(c):
                self.entrada(partes, c)
            elif c in entradas:
                partes["ruta " + c] = "falta"  # declarada y todavía no está
        for k in sorted(os.environ):
            if k.startswith(PREFIJOS_ENTORNO) and k not in ENTORNO_IGNORADO:
                v = os.environ[k]
                partes["entorno " + k] = v
                for c in trozos(v):
                    if os.path.isabs(c) and os.path.exists(c):
                        self.entrada(partes, c)
        return sha_texto(json.dumps(partes, sort_keys=True)), partes

    def esta(self, clave):
        return os.path.exists(os.path.join(self.dir, "ok", clave))

    def como(self, clave):
        """«pasó» o «se salteó» (77), según lo anotado."""
        try:
            with open(os.path.join(self.dir, "ok", clave)) as f:
                return "se salteó" if json.load(f).get("rc") == 77 else "pasó"
        except (OSError, ValueError):
            return "pasó"

    def anotar(self, clave, nombre, rc, segundos):
        p = os.path.join(self.dir, "ok", clave)
        with open(p + ".tmp", "w") as f:
            json.dump({"test": nombre, "rc": rc, "segundos": segundos,
                       "cuando": time.strftime("%Y-%m-%dT%H:%M:%S")}, f)
        os.replace(p + ".tmp", p)

    def podar(self, maximo=4000):
        d = os.path.join(self.dir, "ok")
        try:
            todos = [(os.path.getmtime(os.path.join(d, n)), n) for n in os.listdir(d)]
        except OSError:
            return
        if len(todos) > maximo:
            for _, n in sorted(todos)[:len(todos) - maximo]:
                try:
                    os.unlink(os.path.join(d, n))
                except OSError:
                    pass


def leer_cmakecache(build, var):
    try:
        with open(os.path.join(build, "CMakeCache.txt"), errors="replace") as f:
            for linea in f:
                if linea.startswith(var + ":"):
                    return linea.split("=", 1)[1].strip()
    except OSError:
        pass
    return None


def version_sistema():
    if sys.platform == "darwin":
        return "macOS %s %s" % (platform.mac_ver()[0], platform.machine())
    try:
        with open("/etc/os-release") as f:
            osr = sha_texto(f.read())[:16]
    except OSError:
        osr = "?"
    return "%s %s %s os-release:%s" % (platform.system(), platform.release(), platform.machine(), osr)


def partes_globales(herramienta):
    yo = hashlib.sha256()
    for f in (os.path.abspath(__file__), os.path.join(AQUI, "huellas.py")):
        with open(f, "rb") as fh:
            yo.update(fh.read())
    return {"script": yo.hexdigest(), "herramienta": herramienta, "sistema": version_sistema()}


def leer_entradas(arbol):
    try:
        with open(os.path.join(arbol, "tests-entradas.json")) as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def hilos():
    """Cuántos hilos para calcular las claves. TESTS_CACHE_HILOS lo fija. Bajo Rosetta (el
    contenedor x86 de Docker en una Mac) UNO: lanzar llvm-objdump desde varios hilos a la
    vez trabó el proceso entero (17 hilos esperando un candado de Rosetta, 47 minutos,
    sin hijos). Lo que se lee queda en la caché de importaciones: lento solo la primera vez."""
    v = os.environ.get("TESTS_CACHE_HILOS", "")
    if v.isdigit() and int(v) > 0:
        return int(v)
    if os.path.exists("/mnt/lima-rosetta") or os.path.exists("/proc/sys/fs/binfmt_misc/rosetta"):
        return 1
    return min(16, (os.cpu_count() or 4) * 2)


def calcular(cache, items, fn):
    """fn(item) -> (clave, partes), en paralelo (sha256 suelta el GIL), salvo bajo Rosetta."""
    n = hilos()
    if n == 1:
        return [fn(i) for i in items]
    with concurrent.futures.ThreadPoolExecutor(max_workers=n) as ex:
        return list(ex.map(fn, items))


# --- ctest (macOS, Linux) --------------------------------------------------
FILTROS = {"-R", "-E", "-L", "-LE", "-I", "--tests-regex", "--exclude-regex", "--label-regex",
           "--label-exclude", "-U", "--union"}


def modo_ctest(argv):
    ap = argparse.ArgumentParser(prog="tests-con-cache.py", add_help=False)
    ap.add_argument("--test-dir", required=True)
    ap.add_argument("--explicar")
    ap.add_argument("--ctest", default="ctest")
    a, resto = ap.parse_known_args(argv)
    if resto and resto[0] == "--":
        resto = resto[1:]
    build = os.path.abspath(a.test_dir)
    filtros, pasar = [], []
    i = 0
    while i < len(resto):
        x = resto[i]
        if x in FILTROS:
            filtros.append(x)
            if x not in ("-U", "--union") and i + 1 < len(resto):
                filtros.append(resto[i + 1])
                i += 1
        else:
            pasar.append(x)
        i += 1
    if sin_lentos():
        if "-LE" in filtros or "--label-exclude" in filtros:
            log("TESTS_SIN_LENTOS=1 con otro -LE: queda el tuyo")
        else:
            filtros += ["-LE", "lento"]
            log("TESTS_SIN_LENTOS=1: sin los de la etiqueta «lento»")

    def mostrar(f):
        r = subprocess.run([a.ctest, "--test-dir", build, "--show-only=json-v1"] + f, capture_output=True, text=True)
        if r.returncode != 0:
            sys.stderr.write(r.stderr)
            sys.exit(r.returncode)
        return json.loads(r.stdout).get("tests", [])

    tests = mostrar(filtros)
    todos = {t["name"]: t for t in (mostrar([]) if filtros else tests)}
    ver = subprocess.run([a.ctest, "--version"], capture_output=True, text=True).stdout.splitlines()
    cache = Cache(build, partes_globales(ver[0] if ver else "ctest"))
    entradas = leer_entradas(build)
    if entradas is None:
        log("no está %s/tests-entradas.json (reconfigurá con cmake): corren todos, sin caché" % build)

    def props_de(t):
        return {p["name"]: p["value"] for p in t.get("properties", []) if p["name"] != "LABELS"}

    # Fixtures: lo que arma un test de preparación (sus rutas dentro del build)
    # no es una entrada de nadie: cambia con cada corrida. Entra su clave.
    preparan = {}
    for t in todos.values():
        for fx in props_de(t).get("FIXTURES_SETUP", []):
            preparan.setdefault(fx, []).append(t)

    def productos(t):
        r = []
        for x in t.get("command", [])[1:]:
            x = x.split("=", 1)[1] if x.startswith("-") and "=" in x else x
            if os.path.isabs(x) and os.path.normpath(x).startswith(build + "/"):
                r.append(os.path.normpath(x))
        return r

    def una(t):
        props = props_de(t)
        if "command" not in t or entradas is None:
            return None, None
        decl = entradas.get(t["name"], {}).get("entradas", [])
        ignorar = productos(t) if props.get("FIXTURES_SETUP") else []
        extra = {}
        for fx in props.get("FIXTURES_REQUIRED", []):
            for s in preparan.get(fx, []):
                ignorar += productos(s)
                if "command" in s:
                    extra["preparación " + s["name"]] = cache.clave(
                        s["command"], props_de(s), entradas.get(s["name"], {}).get("entradas", []),
                        ignorar=productos(s))[0]
        return cache.clave(t["command"], props, decl, ignorar=ignorar, extra=extra)

    claves = calcular(cache, tests, una)
    if a.explicar:
        for t, (k, partes) in zip(tests, claves):
            if t["name"] == a.explicar:
                print("%s: clave %s (%s)" % (t["name"], k, "en la caché" if k and cache.esta(k) else "no está en la caché"))
                for n in sorted(partes or {}):
                    print("  %-60s %s" % (n, partes[n]))
                cache.guardar()
                return 0
        log("no hay un test %s (con esos filtros)" % a.explicar)
        return 1

    usar = not sin_cache()
    if not usar:
        log("TESTS_SIN_CACHE=1: corren todos (y se anotan los que pasan)")
    en_cache, correr = [], []
    for t, (k, _) in zip(tests, claves):
        (en_cache if usar and k and cache.esta(k) else correr).append(t["name"])
    clave_de = {t["name"]: k for t, (k, _) in zip(tests, claves)}
    for n in en_cache:
        print("      %s %s %s (caché)" % (n, "." * max(3, 40 - len(n)), cache.como(clave_de[n])))
    t0 = time.time()
    rc = 0
    resultados = {}
    if correr:
        junit = os.path.join(cache.dir, "junit-%d.xml" % os.getpid())
        regex = "^(" + "|".join(re.sub(r"([^A-Za-z0-9_-])", r"\\\1", n) for n in correr) + ")$"
        cmd = [a.ctest, "--test-dir", build, "-R", regex, "--output-junit", junit] + pasar
        rc = subprocess.call(cmd)
        try:
            for tc in ET.parse(junit).getroot().iter("testcase"):
                st = tc.get("status")
                sk = tc.find("skipped")
                if st == "run":
                    resultados[tc.get("name")] = (0, float(tc.get("time") or 0))
                elif st == "notrun" and sk is not None and "SKIP_RETURN_CODE" in (sk.get("message") or ""):
                    resultados[tc.get("name")] = (77, float(tc.get("time") or 0))
            os.unlink(junit)
        except (OSError, ET.ParseError):
            log("ctest no dejó el junit: no se anota nada")
    anotados = 0
    for n, (codigo, seg) in resultados.items():
        k = clave_de.get(n)
        if k:
            cache.anotar(k, n, codigo, seg)
            anotados += 1
    cache.guardar()
    cache.podar()
    log("%d tests: %d de la caché (ya pasaron con estas mismas entradas), %d corridos en %.0f s, %d anotados"
        % (len(tests), len(en_cache), len(correr), time.time() - t0, anotados))
    return rc


# --- Wine (tools/ci/test-win.sh) -------------------------------------------
def modo_wine_preparar(argv):
    ap = argparse.ArgumentParser(prog="tests-con-cache.py wine-preparar")
    ap.add_argument("--build", required=True)
    ap.add_argument("--herramienta", required=True)
    ap.add_argument("--objdump", required=True)
    ap.add_argument("--ruta", default="")
    ap.add_argument("--pendientes", required=True)
    a = ap.parse_args(argv)
    build = os.path.abspath(a.build)
    ruta = [d for d in re.split(r"[;]", a.ruta) if d]
    exes = [l.strip() for l in sys.stdin if l.strip()]
    entorno = ["%s=%s" % (k, os.environ[k]) for k in ("WINEPATH", "WINEDLLOVERRIDES", "BA_LSP_CLAP")
               if k in os.environ]
    cache = Cache(build, partes_globales("wine " + a.herramienta), ruta, a.objdump)
    arboles = {}

    def arbol_de(exe):
        d = os.path.dirname(os.path.abspath(exe))
        while d.startswith(build) and not os.path.exists(os.path.join(d, "tests-entradas.json")):
            d = os.path.dirname(d)
        if d not in arboles:
            arboles[d] = leer_entradas(d) or {}
        return arboles[d]

    def una(exe):
        info = arbol_de(exe).get(os.path.basename(exe)[:-4], {})
        k, _ = cache.clave([os.path.abspath(exe)], {}, info.get("entradas", []), entorno)
        return k, info.get("lento", False)

    res = calcular(cache, exes, una)
    usar = not sin_cache()
    if not usar:
        log("TESTS_SIN_CACHE=1: corren todos (y se anotan los que pasan)")
    pend, cache_n, lentos = [], 0, 0
    os.makedirs(os.path.join(build, "wine-logs"), exist_ok=True)
    with open(os.path.join(build, "wine-logs", ".claves"), "w") as f:
        for exe, (k, lento) in zip(exes, res):
            f.write("%s\t%s\n" % (os.path.abspath(exe), k))
            nombre = os.path.relpath(exe, build)
            if usar and cache.esta(k):
                cache_n += 1
                print("  %s: %s (caché)" % (nombre, cache.como(k)))
            elif lento and sin_lentos():
                lentos += 1
                print("  %s: salteado (lento, TESTS_SIN_LENTOS=1)" % nombre)
            else:
                pend.append(exe)
    with open(a.pendientes, "w") as f:
        f.write("".join(e + "\n" for e in pend))
    cache.guardar()
    log("Windows: %d tests, %d de la caché, %d lentos afuera, %d a correr" % (len(exes), cache_n, lentos, len(pend)))
    return 0


def modo_wine_guardar(argv):
    ap = argparse.ArgumentParser(prog="tests-con-cache.py wine-guardar")
    ap.add_argument("--build", required=True)
    ap.add_argument("--resultados", required=True)
    a = ap.parse_args(argv)
    build = os.path.abspath(a.build)
    claves = {}
    with open(os.path.join(build, "wine-logs", ".claves")) as f:
        for linea in f:
            exe, _, k = linea.rstrip("\n").partition("\t")
            claves[exe] = k
    cache = Cache(build, {})
    n = 0
    with open(a.resultados) as f:
        for linea in f:
            partes = linea.split(None, 4)  # rc segundos nombre log exe
            if len(partes) == 5 and partes[0] in ("0", "77"):
                k = claves.get(os.path.abspath(partes[4].strip()))
                if k:
                    cache.anotar(k, partes[2], int(partes[0]), float(partes[1]))
                    n += 1
    cache.podar()
    log("Windows: %d tests anotados en la caché" % n)
    return 0


def main(argv):
    if argv and argv[0] == "wine-preparar":
        return modo_wine_preparar(argv[1:])
    if argv and argv[0] == "wine-guardar":
        return modo_wine_guardar(argv[1:])
    return modo_ctest(argv)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
