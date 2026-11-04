"""Todo header de core y de la app compila solo (es auto-contenido: incluye lo que usa).

Cada header se compila en un TU que no tiene más que `#include "<el header>"`, con las flags de
compile_commands.json de su target (las de su .cpp si lo tiene; si no, las de otro TU del mismo
componente o del mismo target) y `-fsyntax-only`. Lo que hoy no compila solo es deuda
(DEUDA_HEADERS) y SOLO PUEDE BAJAR: un header nuevo que no compila solo falla; uno de la deuda
que ya compila, también (sacalo de la lista).

Se corre por partes: un test de ctest por PARTE (unos 8 headers) de cada componente de
tools/diag/niveles.txt (cmake/BaNiveles.cmake; la parte de un header sale de su ruta:
niveles.parte_del_header), con sus headers y los headers del proyecto que estos alcanzan como
entradas: tocar un header corre solo las partes que lo ven. Y adentro de cada corrida hay una
caché propia (<build>/headers-solos/): un header se vuelve a compilar solo si cambió él, algo de
lo que incluye (la lista de -MD) o sus flags. «Cambió» es el contenido: con el mismo tamaño y
otra fecha (un checkout, un touch) se compara su huella.

  tests/test_headers_solos.py --build build --componente app/ui [--parte K --partes N] [--jobs N]
  tests/test_headers_solos.py --build build                todos los de core y app
  tests/test_headers_solos.py --build build --listar-fallas los que hoy no compilan solos, con el error

Solo en macOS: la deuda se midió con el clang de la Mac y las flags del build de la Mac (en
Windows y Linux los headers de plataforma son otros). Con pytest, en otro sistema o sin
build/compile_commands.json, se saltea con el motivo.

Qué NO se compila (no es deuda): los headers de una plataforma que no es esta (sufijo _win,
_linux, _posix en Windows…) y los que tienen su .cpp/.mm al lado pero ese .cpp no se compila en
este build (una función apagada: BA_HAVE_X=0): sus flags no existen acá.
"""
from __future__ import annotations

import concurrent.futures
import hashlib
import json
import os
import pathlib
import platform
import shlex
import subprocess
import sys

RAIZ = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(RAIZ / "tools" / "diag"))
import niveles  # noqa: E402

# Los headers (relativos a src/) que hoy no compilan solos. SOLO PUEDE BAJAR.
# Medido el 2026-11-04 sobre master b89e0247 (macOS, Apple clang, build Release).
DEUDA_HEADERS: list[str] = [
]

PLATAFORMA_AJENA = {"Darwin": ("_win", "_linux"), "Linux": ("_win", "_mac"), "Windows": ("_mac", "_linux", "_posix")}
VERSION_CACHE = "1"


def componentes_revisados(mapa: niveles.Mapa) -> list[niveles.Componente]:
    """Los de core y la app (niveles.PREFIJOS_HEADERS_SOLOS)."""
    return niveles.componentes_headers_solos(mapa)


# --- las flags --------------------------------------------------------------------------------
class Flags:
    """Lo de compile_commands.json que hace falta: por archivo fuente, sus comandos (uno por
    target que lo compila)."""

    def __init__(self, build: pathlib.Path):
        self.build = build
        datos = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
        self.por_archivo: dict[str, list[dict]] = {}
        for e in datos:
            f = str(pathlib.Path(e["directory"], e["file"]).resolve())
            self.por_archivo.setdefault(f, []).append(e)

    @staticmethod
    def _target(e: dict) -> str:
        out = e.get("output", "")
        for parte in pathlib.PurePosixPath(out).parts:
            if parte.endswith(".dir"):
                return parte[:-4]
        return ""

    def entrada(self, fuente: pathlib.Path, targets: list[str]) -> dict | None:
        """El comando de `fuente` en el primero de `targets` que la compila."""
        es = self.por_archivo.get(str(fuente.resolve()), [])
        for t in targets:
            for e in es:
                if self._target(e) == t:
                    return e
        return None

    def compilado(self, fuente: pathlib.Path) -> bool:
        return str(fuente.resolve()) in self.por_archivo


def targets_de(comp: niveles.Componente) -> list[str]:
    """El target cuyas flags valen para los headers del componente."""
    if comp.targets:
        return comp.targets
    return ["ba_core"] if comp.rutas[0].startswith("core/") else ["ba-app"]


def argumentos(e: dict) -> list[str]:
    args = e.get("arguments") or shlex.split(e["command"])
    out, saltar = [], False
    fuente = pathlib.Path(e["directory"], e["file"]).resolve()
    for i, a in enumerate(args):
        if saltar:
            saltar = False
            continue
        if i > 0 and pathlib.Path(a).name in ("ccache", "sccache"):
            continue
        if a in ("-o", "-MF", "-MT", "-MQ"):
            saltar = True
            continue
        if a in ("-c", "-MD", "-MMD"):
            continue
        try:
            if pathlib.Path(e["directory"], a).resolve() == fuente:
                continue
        except (OSError, ValueError):
            pass
        out.append(a)
    if out and pathlib.Path(out[0]).name in ("ccache", "sccache"):
        out = out[1:]
    return out


# --- un header -----------------------------------------------------------------------------
def _clave(args: list[str], lenguaje: str) -> str:
    return hashlib.sha256(json.dumps([VERSION_CACHE, args, lenguaje]).encode()).hexdigest()


_HUELLAS: dict[str, str | None] = {}


def _huella(p: str) -> str | None:
    """El sha1 del contenido (una vez por corrida y por archivo)."""
    if p not in _HUELLAS:
        try:
            _HUELLAS[p] = hashlib.sha1(pathlib.Path(p).read_bytes()).hexdigest()
        except OSError:
            _HUELLAS[p] = None
    return _HUELLAS[p]


def _estado(p: str) -> list | None:
    """[tamaño, fecha, huella del contenido]."""
    try:
        s = os.stat(p)
    except OSError:
        return None
    return [s.st_size, s.st_mtime_ns, _huella(p)]


def _sigue_igual(p: str, previo: list | None) -> bool:
    """Si `p` sigue como se anotó: misma fecha y tamaño (rápido) o, con otra fecha (un
    checkout, un touch), el mismo contenido. Al dar igual por contenido, anota la fecha nueva."""
    try:
        s = os.stat(p)
    except OSError:
        return previo is None
    if previo is None:
        return False
    if [s.st_size, s.st_mtime_ns] == previo[:2]:
        return True
    if len(previo) < 3 or s.st_size != previo[0] or _huella(p) != previo[2]:
        return False
    previo[1] = s.st_mtime_ns
    return True


def _deps(archivo_d: pathlib.Path, tu: pathlib.Path) -> dict[str, list | None]:
    txt = archivo_d.read_text(encoding="utf-8", errors="replace").replace("\\\n", " ")
    rutas = txt.split(":", 1)[1].split() if ":" in txt else []
    return {r: _estado(r) for r in rutas if pathlib.Path(r) != tu}


class Revisor:
    def __init__(self, build: pathlib.Path, mapa: niveles.Mapa | None = None):
        self.build = build
        self.mapa = mapa or niveles.Mapa.leer()
        self.flags = Flags(build)
        self.dir = build / "headers-solos"
        self.dir.mkdir(parents=True, exist_ok=True)

    def plan(self, comp: niveles.Componente, parte: int = 0, partes: int = 1
             ) -> tuple[list[tuple[str, dict, str]], dict[str, str]]:
        """([(header, entrada de compile_commands, lenguaje)], {header: por qué no se compila}),
        de los headers de la parte `parte` de `partes` (con 1, todos)."""
        archivos = niveles.archivos_del_componente(self.mapa, comp)
        headers = [a for a in archivos if a.endswith(niveles.EXT_HEADER)
                   and (partes <= 1 or niveles.parte_del_header(a, partes) == parte)]
        fuentes = [niveles.SRC / a for a in archivos if not a.endswith(niveles.EXT_HEADER)]
        targets = targets_de(comp)
        # El comando de respaldo: un TU del componente en su target; si no hay, uno del target.
        respaldo = next((e for f in fuentes if f.suffix in (".cpp", ".cc", ".cxx")
                         for e in [self.flags.entrada(f, targets)] if e), None)
        if respaldo is None:
            respaldo = next((e for es in self.flags.por_archivo.values() for e in es
                             if Flags._target(e) in targets and e["file"].endswith(".cpp")), None)
        ajenos = PLATAFORMA_AJENA.get(platform.system(), ())
        hacer, fuera = [], {}
        for h in headers:
            p = niveles.SRC / h
            if p.stem.endswith(ajenos):
                fuera[h] = "de otra plataforma"
                continue
            hermanos = [p.with_suffix(s) for s in (".cpp", ".mm", ".cc") if p.with_suffix(s).exists()]
            e, leng = None, "c++"
            for hno in hermanos:
                e = self.flags.entrada(hno, targets)
                if e:
                    leng = "objective-c++" if hno.suffix == ".mm" else "c++"
                    break
            if e is None and hermanos and not any(self.flags.compilado(x) for x in hermanos):
                fuera[h] = "su .cpp no se compila en este build"
                continue
            if e is None:
                e = respaldo
            if e is None:
                fuera[h] = f"no hay flags de {'/'.join(targets)} en compile_commands.json"
                continue
            hacer.append((h, e, leng))
        return hacer, fuera

    def compilar(self, h: str, e: dict, leng: str, cache: dict) -> tuple[bool, str]:
        args = argumentos(e)
        clave = _clave(args, leng)
        previo = cache.get(h)
        if previo and previo["clave"] == clave and all(_sigue_igual(r, est) for r, est in previo["deps"].items()):
            return previo["ok"], previo["error"]
        nombre = hashlib.sha1(h.encode()).hexdigest()[:16]
        tu = self.dir / f"{nombre}.cpp"
        d = self.dir / f"{nombre}.d"
        tu.write_text(f'#include "{niveles.SRC / h}"\n', encoding="utf-8")
        cmd = args + ["-fsyntax-only", "-w", "-MD", "-MF", str(d), "-x", leng, str(tu)]
        r = subprocess.run(cmd, cwd=e["directory"], capture_output=True, text=True, errors="replace")
        ok = r.returncode == 0
        error = "" if ok else "\n".join((r.stderr or r.stdout).splitlines()[:12])
        deps = _deps(d, tu) if d.exists() else {}
        # Sin la lista de dependencias (falló antes de leer nada) se recompila la próxima vez.
        if deps:
            deps[str(niveles.SRC / h)] = _estado(str(niveles.SRC / h))
            cache[h] = {"clave": clave, "deps": deps, "ok": ok, "error": error}
        else:
            cache.pop(h, None)
        return ok, error

    def revisar(self, comp: niveles.Componente, jobs: int, parte: int = 0, partes: int = 1
                ) -> tuple[dict[str, str], dict[str, str], int]:
        """({header: error} de los que no compilan solos, {header: por qué no se compiló},
        cuántos se revisaron). Una caché por parte: las partes corren a la vez."""
        hacer, fuera = self.plan(comp, parte, partes)
        archivo_cache = self.dir / (f"{comp.id}.json" if partes <= 1 else f"{comp.id}-{parte}de{partes}.json")
        try:
            cache = json.loads(archivo_cache.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            cache = {}
        fallas = {}
        with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, jobs)) as ex:
            futs = {ex.submit(self.compilar, h, e, leng, cache): h for h, e, leng in hacer}
            for f in concurrent.futures.as_completed(futs):
                ok, error = f.result()
                if not ok:
                    fallas[futs[f]] = error
        vigentes = {h for h, _, _ in hacer}
        cache = {h: v for h, v in cache.items() if h in vigentes}
        tmp = archivo_cache.with_suffix(".tmp")
        tmp.write_text(json.dumps(cache, ensure_ascii=False), encoding="utf-8")
        tmp.replace(archivo_cache)
        return dict(sorted(fallas.items())), fuera, len(hacer)


def chequear(revisor: Revisor, comp: niveles.Componente, jobs: int, parte: int = 0, partes: int = 1) -> list[str]:
    fallas, fuera, _ = revisor.revisar(comp, jobs, parte, partes)
    mios = {h for h in DEUDA_HEADERS if revisor.mapa.componente_de(h) is comp
            and (partes <= 1 or niveles.parte_del_header(h, partes) == parte)}
    errores = []
    nuevos = [h for h in fallas if h not in mios]
    if nuevos:
        errores.append(f"headers de {comp.nombre} que no compilan solos (incluí lo que usan; lo que va por "
                       "puntero/referencia, declarado adelante):")
        for h in nuevos:
            errores.append(f"  src/{h}\n" + "\n".join("      " + x for x in fallas[h].splitlines()))
    arreglados = sorted(h for h in mios if h not in fallas and h not in fuera)
    if arreglados:
        errores.append("ya compilan solos (sacalos de DEUDA_HEADERS en tests/test_headers_solos.py):")
        errores += [f"  {h}" for h in arreglados]
    return errores


def _build_por_defecto() -> pathlib.Path:
    return pathlib.Path(os.environ.get("BA_BUILD_DIR", RAIZ / "build"))


def _motivo_para_saltear(build: pathlib.Path) -> str:
    if platform.system() != "Darwin":
        return f"solo en macOS (la deuda se midió con el clang de la Mac; esto es {platform.system()})"
    if not (build / "compile_commands.json").is_file():
        return f"no hay {build}/compile_commands.json (configurá el build primero)"
    return ""


# --- pytest -----------------------------------------------------------------------------------
def test_headers_de_core_y_app_compilan_solos():
    import pytest
    build = _build_por_defecto()
    motivo = _motivo_para_saltear(build)
    if motivo:
        pytest.skip(motivo)
    rev = Revisor(build)
    errores = []
    for c in componentes_revisados(rev.mapa):
        errores += chequear(rev, c, os.cpu_count() or 2)
    assert not errores, "\n".join(errores)


def test_la_deuda_nombra_headers_que_existen():
    mapa = niveles.Mapa.leer()
    malos = [h for h in DEUDA_HEADERS if not (niveles.SRC / h).is_file()
             or mapa.componente_de(h) not in componentes_revisados(mapa)]
    assert not malos, f"DEUDA_HEADERS nombra headers que no existen o no son de core/app: {malos}"
    assert DEUDA_HEADERS == sorted(set(DEUDA_HEADERS)), "DEUDA_HEADERS: ordenada y sin repetidos"


def test_las_flags_sacan_salida_fuente_y_ccache():
    e = {"directory": "/b", "file": "/s/x.cpp",
         "command": "/usr/bin/ccache /usr/bin/c++ -DX=1 -I/s -O3 -o a/x.cpp.o -c /s/x.cpp -MD -MT a -MF a.d"}
    assert argumentos(e) == ["/usr/bin/c++", "-DX=1", "-I/s", "-O3"]


# --- por partes, desde ctest ------------------------------------------------------------------
def main(argv: list[str]) -> int:
    def valor(op, defecto=None):
        return argv[argv.index(op) + 1] if op in argv else defecto

    build = pathlib.Path(valor("--build", str(_build_por_defecto()))).resolve()
    motivo = _motivo_para_saltear(build)
    if motivo:
        print(f"salteado: {motivo}")
        return 77  # ctest: SKIP_RETURN_CODE
    jobs = int(valor("--jobs", str(os.cpu_count() or 2)))
    parte, partes = int(valor("--parte", "0")), int(valor("--partes", "1"))
    if not 0 <= parte < max(1, partes):
        print(f"--parte {parte} fuera de 0..{partes - 1}")
        return 1
    rev = Revisor(build)
    comps = componentes_revisados(rev.mapa)
    if "--componente" in argv:
        nombre = valor("--componente")
        comps = [c for c in comps if c.nombre == nombre]
        if not comps:
            print(f"{nombre}: no es un componente de core/app en tools/diag/niveles.txt")
            return 1
    if "--listar-fallas" in argv:
        total = 0
        for c in comps:
            fallas, fuera, n = rev.revisar(c, jobs, parte, partes)
            total += n
            for h, err in fallas.items():
                print(f"== src/{h}\n{err}\n")
            for h, por in fuera.items():
                print(f"-- no se compila: src/{h} ({por})")
        print(f"{total} headers revisados")
        return 0
    errores = []
    for c in comps:
        errores += chequear(rev, c, jobs, parte, partes)
    print("\n".join(errores) if errores else "bien")
    return 1 if errores else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
