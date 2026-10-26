#!/usr/bin/env python3
# ============================================================================
# tools/ci/huellas.py — huellas por CONTENIDO de archivos y carpetas, y los
# «atajos» del build: un paso que no se vuelve a hacer si sus entradas no
# cambiaron
#
# Una huella es el sha256 del contenido (y de la ruta relativa, en una carpeta).
# Nunca de las fechas: una fecha cambia sin que cambie nada (un checkout, un
# `touch`, una copia) y un archivo puede cambiar sin que su fecha avance.
#
# Para no releer gigas en cada build, el sha256 de cada archivo se recuerda en un
# memo (JSON) junto con su tamaño, mtime, ctime e inodo: si los cuatro siguen
# iguales, el contenido es el mismo y no se relee. El ctime no lo puede fijar
# nadie desde afuera (cambia con cualquier escritura), así que el memo nunca
# devuelve un hash viejo para un archivo reescrito; a lo sumo relee de más. Es lo
# mismo que hace git con su índice y tools/deploy con BA_MANIFEST_CACHE.
#
# Lo usan tools/ci/tests-con-cache.py (la clave de cada test) y el Makefile:
#
#   huellas.py arbol <carpeta> [--memo F] [--excluir REL]...
#       Imprime la huella de una carpeta.
#
#   huellas.py atajo --nombre N --clave-en F [--memo M] [--entrada RUTA]...
#                    [--excluir REL]... [--centinelas DIR] [--texto T]... [--salida RUTA]...
#                    -- comando...
#       Corre el comando solo si cambió alguna entrada (o falta alguna salida, o
#       nunca corrió bien), y anota la clave cuando termina bien. Imprime qué
#       saltea y por qué, o qué cambió. SIN_CACHE=1 lo corre siempre.
#
# Solo biblioteca estándar (corre con el Python de la Mac y en el Ubuntu de Docker).
# ============================================================================
import hashlib
import json
import os
import stat
import subprocess
import sys
import threading

# Lo que no entra en la huella de una carpeta del BUILD (filtro=True; un bundle
# entra entero): lo que cambia solo (los registros de ctest, el prefijo de Wine
# que los tests modifican, las cachés) y lo intermedio del compilador, que no es
# lo que se prueba.
EXCLUIR_BUILD = {".git", "__pycache__", "CMakeFiles", "Testing", ".cache-tests", "wine-logs",
                 "wineprefix", ".ninja_deps", ".ninja_log"}
EXCLUIR_CARPETAS_SUFIJO = ("_autogen",)
EXCLUIR_ARCHIVOS_SUFIJO = (".o", ".obj", ".d", ".ilk", ".pyc")


def sha256_archivo(ruta):
    h = hashlib.sha256()
    with open(ruta, "rb") as f:
        while True:
            b = f.read(1 << 20)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


class Memo:
    """sha256 por archivo, recordado por (tamaño, mtime, ctime, inodo)."""

    def __init__(self, ruta=None):
        self.archivo_memo = ruta
        self.datos = {}
        self.cambio = False
        self.candado = threading.Lock()
        self.carpetas = {}  # huella de carpetas, solo en esta corrida
        if ruta and os.path.exists(ruta):
            try:
                with open(ruta) as f:
                    self.datos = json.load(f)
            except (OSError, ValueError):
                self.datos = {}
        self.usados = set()
        self.candados = {}

    def _candado(self, clave):
        # Uno por archivo o carpeta: varios hilos que piden lo mismo (los 15 tests
        # que leen GStreamer) esperan al primero en vez de leerlo 15 veces.
        with self.candado:
            c = self.candados.get(clave)
            if c is None:
                c = self.candados[clave] = threading.Lock()
            return c

    def archivo(self, ruta, st=None):
        """sha256 del archivo (sigue enlaces) o None si no existe."""
        with self._candado(("archivo", ruta)):
            return self._archivo(ruta, st)

    def _archivo(self, ruta, st=None):
        try:
            st = st or os.stat(ruta)
        except OSError:
            return None
        firma = [st.st_size, st.st_mtime_ns, st.st_ctime_ns, st.st_ino]
        with self.candado:
            self.usados.add(ruta)
            v = self.datos.get(ruta)
            if v and v[:4] == firma:
                return v[4]
        try:
            h = sha256_archivo(ruta)
        except OSError:
            return None
        with self.candado:
            self.datos[ruta] = firma + [h]
            self.cambio = True
        return h

    def arbol(self, raiz, excluir=(), filtro=False):
        """Huella de una carpeta: rutas relativas, contenido y destino de los enlaces."""
        clave = (raiz, tuple(sorted(excluir)), filtro)
        with self._candado(clave):
            return self._arbol(clave, raiz, excluir, filtro)

    def _arbol(self, clave, raiz, excluir, filtro):
        with self.candado:
            if clave in self.carpetas:
                return self.carpetas[clave]
        h = hashlib.sha256()
        for rel, tipo, valor in self.recorrer(raiz, excluir, filtro):
            h.update(("%s\0%s\0%s\n" % (rel, tipo, valor)).encode("utf-8", "surrogateescape"))
        r = h.hexdigest()
        with self.candado:
            self.carpetas[clave] = r
        return r

    def recorrer(self, raiz, excluir=(), filtro=False):
        excl = set(excluir)
        for dirpath, dirnames, filenames in os.walk(raiz):
            relbase = os.path.relpath(dirpath, raiz)
            relbase = "" if relbase == "." else relbase + "/"
            keep = []
            for d in sorted(dirnames):
                rel = relbase + d
                p = os.path.join(dirpath, d)
                if rel in excl or (filtro and (d in EXCLUIR_BUILD or d.endswith(EXCLUIR_CARPETAS_SUFIJO))):
                    continue
                if os.path.islink(p):
                    yield rel, "enlace", os.readlink(p)
                    continue
                keep.append(d)
            dirnames[:] = keep
            for fn in sorted(filenames):
                rel = relbase + fn
                if rel in excl or (filtro and (fn in EXCLUIR_BUILD or fn.endswith(EXCLUIR_ARCHIVOS_SUFIJO))):
                    continue
                p = os.path.join(dirpath, fn)
                try:
                    st = os.lstat(p)
                except OSError:
                    continue
                if stat.S_ISLNK(st.st_mode):
                    yield rel, "enlace", os.readlink(p)
                elif stat.S_ISREG(st.st_mode):
                    yield rel, "archivo", self.archivo(p, os.stat(p))

    def ruta(self, p, excluir=(), filtro=False):
        """Huella de lo que haya en p: archivo, carpeta o «falta»."""
        if os.path.isdir(p):
            return "carpeta:" + self.arbol(p, excluir, filtro)
        h = self.archivo(p)
        return ("archivo:" + h) if h else "falta"

    def guardar(self):
        if not self.archivo_memo or not self.cambio:
            return
        # Solo lo que se usó en esta corrida, más lo que sigue existiendo (otro
        # paso que comparte el memo lo va a querer): lo borrado se cae solo.
        datos = {k: v for k, v in self.datos.items() if k in self.usados or os.path.exists(k)}
        tmp = "%s.tmp.%d" % (self.archivo_memo, os.getpid())
        os.makedirs(os.path.dirname(os.path.abspath(self.archivo_memo)), exist_ok=True)
        with open(tmp, "w") as f:
            json.dump(datos, f, separators=(",", ":"))
        os.replace(tmp, self.archivo_memo)


def centinelas(raiz):
    """Lo que dejaron los `make deps-*` en extern/: qué hay (dos niveles) y los
    .version/.parches-*. Si alguien borra una dependencia o cambia de versión,
    cambia; leer todo extern/ (decenas de gigas) en cada build no tiene sentido."""
    h = hashlib.sha256()
    if not os.path.isdir(raiz):
        return "falta"
    for a in sorted(os.listdir(raiz)):
        if a.startswith(".") and a != ".downloads":
            continue
        pa = os.path.join(raiz, a)
        h.update(("%s\n" % a).encode())
        if not os.path.isdir(pa) or a == ".downloads":
            continue
        for b in sorted(os.listdir(pa)):
            pb = os.path.join(pa, b)
            h.update(("  %s\n" % b).encode())
            if b == ".version" or b.startswith(".parches-") or b.startswith(".version"):
                try:
                    with open(pb, "rb") as f:
                        h.update(f.read())
                except OSError:
                    pass
            elif os.path.isdir(pb) and not os.path.islink(pb):
                try:
                    for c in sorted(os.listdir(pb)):
                        if c.startswith(".version") or c.startswith(".parches-"):
                            with open(os.path.join(pb, c), "rb") as f:
                                h.update(c.encode() + b"\0" + f.read())
                except OSError:
                    pass
    return h.hexdigest()


def sin_cache():
    return os.environ.get("SIN_CACHE", "") not in ("", "0")


def atajo(args):
    import argparse
    ap = argparse.ArgumentParser(prog="huellas.py atajo")
    ap.add_argument("--nombre", required=True)
    ap.add_argument("--clave-en", required=True)
    ap.add_argument("--memo")
    ap.add_argument("--entrada", action="append", default=[])
    ap.add_argument("--excluir", action="append", default=[])
    ap.add_argument("--centinelas", action="append", default=[])
    ap.add_argument("--texto", action="append", default=[])
    ap.add_argument("--salida", action="append", default=[])
    if "--" not in args:
        ap.error("falta «-- comando»")
    i = args.index("--")
    a = ap.parse_args(args[:i])
    comando = args[i + 1:]
    memo = Memo(a.memo)

    def partes():
        p = {}
        for e in a.entrada:
            p["entrada " + e] = memo.ruta(e, a.excluir)
        for c in a.centinelas:
            p["centinelas " + c] = centinelas(c)
        for n, t in enumerate(a.texto):
            p["texto %d" % n] = hashlib.sha256(t.encode()).hexdigest()
        return p

    log = lambda m: print("[atajo] %s: %s" % (a.nombre, m), flush=True)
    ahora = partes()
    antes = None
    try:
        with open(a.clave_en) as f:
            antes = json.load(f)
    except (OSError, ValueError):
        pass
    faltan = [s for s in a.salida if not os.path.exists(s)]
    if sin_cache():
        motivo = "SIN_CACHE=1"
    elif antes is None:
        motivo = "no hay clave anterior (%s)" % a.clave_en
    elif faltan:
        motivo = "falta " + ", ".join(faltan)
    elif antes != ahora:
        cambios = sorted(k for k in set(antes) | set(ahora) if antes.get(k) != ahora.get(k))
        motivo = "cambió: " + ", ".join(cambios)
    else:
        memo.guardar()
        log("salteado: el contenido de sus entradas es el mismo que la última vez que anduvo (%s)"
            % ", ".join(sorted(ahora)))
        return 0
    log("corre (%s)" % motivo)
    try:
        os.unlink(a.clave_en)  # si se corta a la mitad, la próxima vez corre de nuevo
    except OSError:
        pass
    rc = subprocess.call(comando)
    if rc != 0:
        memo.guardar()
        return rc
    # La clave de DESPUÉS: el comando puede haber cambiado sus propias entradas
    # (deps-win llena extern/); lo que queda es lo que tiene que estar la próxima vez.
    memo.carpetas.clear()
    final = partes()
    os.makedirs(os.path.dirname(os.path.abspath(a.clave_en)), exist_ok=True)
    with open(a.clave_en + ".tmp", "w") as f:
        json.dump(final, f, indent=1, sort_keys=True)
    os.replace(a.clave_en + ".tmp", a.clave_en)
    memo.guardar()
    return 0


def main(argv):
    if len(argv) >= 2 and argv[1] == "arbol":
        import argparse
        ap = argparse.ArgumentParser(prog="huellas.py arbol")
        ap.add_argument("carpeta")
        ap.add_argument("--memo")
        ap.add_argument("--excluir", action="append", default=[])
        a = ap.parse_args(argv[2:])
        m = Memo(a.memo)
        print(m.arbol(a.carpeta, a.excluir))
        m.guardar()
        return 0
    if len(argv) >= 2 and argv[1] == "atajo":
        return atajo(argv[2:])
    print("uso: huellas.py arbol|atajo ... (ver la cabecera)", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
