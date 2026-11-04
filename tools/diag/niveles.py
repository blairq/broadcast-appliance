#!/usr/bin/env python3
"""Los niveles del diseño físico (docs/local/PLAN-DISENO-FISICO.md): quién incluye y quién
enlaza a quién.

El mapa (tools/diag/niveles.txt) reparte cada archivo de C/C++ de src/ en un COMPONENTE con
un NIVEL (0 ba_core → 1 ba_motor, ba_gateway_* → 2 áreas de la app → 3 ba_app_ui → 4 los
programas). La regla: un componente solo incluye y enlaza componentes de nivel más bajo; dos
del mismo nivel no se incluyen ni se enlazan entre sí. Hoy la app es un solo target, así que
el mapa reparte por CARPETA (src/app/<área>/); cuando una carpeta pase a ser una biblioteca
(`ba_biblioteca(... NIVEL n ...)`, Ola 2) su nivel sale de ahí y se revisa contra el mapa.

Lo que lee:
  - los `#include "…"` de cada archivo (resueltos como el compilador: al lado del archivo y
    después en las `raiz` del mapa; lo que no se encuentra en src/ es de terceros y no
    cuenta). Se cuentan todos, también los que están adentro de un #if de otra plataforma.
  - los `target_link_libraries(...)` y `ba_biblioteca(...)` de los CMakeLists.txt y cmake/.

Uso:
  tools/diag/niveles.py                    las violaciones de hoy, agrupadas
  tools/diag/niveles.py --componente X     solo las que salen de X (app/ui, core…)
  tools/diag/niveles.py --json             en JSON
  tools/diag/niveles.py --deuda            las tablas DEUDA_* de tests/test_niveles.py, recontadas
  tools/diag/niveles.py --componentes      los componentes con su nivel, rutas y cuántos archivos
  tools/diag/niveles.py --cmake SALIDA     escribe SALIDA (.cmake) con, por componente, sus
                                           archivos, sus headers y los headers del proyecto que
                                           alcanzan sus headers (para registrar los tests por
                                           área con sus entradas exactas: cmake/BaNiveles.cmake)

Es una biblioteca para los tests (tests/test_niveles.py, tests/test_headers_solos.py) y para
partir otros tests por área: `Mapa.leer()`, `mapa.componente_de(ruta)`, `Grafo(mapa)`.
"""
from __future__ import annotations

import fnmatch
import hashlib
import json
import pathlib
import re
import sys
from dataclasses import asdict, dataclass, field

RAIZ = pathlib.Path(__file__).resolve().parents[2]
SRC = RAIZ / "src"
MAPA = pathlib.Path(__file__).resolve().with_name("niveles.txt")

EXT_CODIGO = (".hpp", ".h", ".hh", ".hxx", ".inl", ".ipp", ".cpp", ".cc", ".cxx", ".c", ".mm", ".m")
EXT_HEADER = (".hpp", ".h", ".hh", ".hxx", ".inl", ".ipp")
# Lo que no es código de un componente: las pruebas incluyen lo que quieren.
CARPETAS_FUERA = {"tests"}

RE_INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*([<"])([^>"]+)[>"]', re.M)


def es_codigo(p: pathlib.Path) -> bool:
    return p.suffix in EXT_CODIGO and not (CARPETAS_FUERA & set(p.relative_to(SRC).parts[:-1]))


def _formato_nivel(n: float) -> str:
    return str(int(n)) if float(n).is_integer() else str(n)


# --- el mapa ----------------------------------------------------------------------------
@dataclass(eq=False)   # por identidad: uno por línea del mapa (y así entra en un set)
class Componente:
    nombre: str
    nivel: float
    rutas: list[str]                     # relativas a src/: «x/» carpeta, «x» archivo, «x*» patrón
    targets: list[str] = field(default_factory=list)
    linea: int = 0

    @property
    def id(self) -> str:
        """El nombre sin «/» ni «-» (para los nombres de tests y variables de CMake)."""
        return re.sub(r"[^A-Za-z0-9_]", "_", self.nombre)

    def carpetas(self) -> list[str]:
        """Las carpetas base de sus rutas (relativas a src/), sin repetir."""
        out = []
        for r in self.rutas:
            c = r if r.endswith("/") else r.rsplit("/", 1)[0] + "/"
            if c not in out:
                out.append(c)
        return out


class ErrorMapa(Exception):
    pass


class Mapa:
    def __init__(self, componentes: list[Componente], raices: dict[str, list[str]]):
        self.componentes = componentes
        self.por_nombre = {c.nombre: c for c in componentes}
        self.raices = raices
        self._cache: dict[str, Componente | None] = {}

    @classmethod
    def leer(cls, ruta: pathlib.Path = MAPA) -> "Mapa":
        return cls.desde_texto(ruta.read_text(encoding="utf-8"), str(ruta))

    @classmethod
    def desde_texto(cls, texto: str, nombre: str = "niveles.txt") -> "Mapa":
        comps: list[Componente] = []
        raices: dict[str, list[str]] = {}
        for n, linea in enumerate(texto.splitlines(), 1):
            linea = linea.split("#", 1)[0].strip()
            if not linea:
                continue
            t = linea.split()
            if t[0] == "componente":
                if len(t) < 4:
                    raise ErrorMapa(f"{nombre}:{n}: «componente <nombre> <nivel> <ruta>... [: <target>...]»")
                try:
                    nivel = float(t[2])
                except ValueError:
                    raise ErrorMapa(f"{nombre}:{n}: el nivel de {t[1]} no es un número: {t[2]}") from None
                resto = t[3:]
                targets: list[str] = []
                if ":" in resto:
                    i = resto.index(":")
                    resto, targets = resto[:i], resto[i + 1:]
                if not resto:
                    raise ErrorMapa(f"{nombre}:{n}: {t[1]} sin rutas")
                if t[1] in {c.nombre for c in comps}:
                    raise ErrorMapa(f"{nombre}:{n}: componente repetido: {t[1]}")
                comps.append(Componente(t[1], nivel, resto, targets, n))
            elif t[0] == "raiz":
                if len(t) < 3 or not t[1].endswith("/"):
                    raise ErrorMapa(f"{nombre}:{n}: «raiz <carpeta>/ <dir>...»")
                raices[t[1]] = t[2:]
            else:
                raise ErrorMapa(f"{nombre}:{n}: no entiendo «{t[0]}» (componente o raiz)")
        rutas: dict[str, str] = {}
        for c in comps:
            for r in c.rutas:
                if r in rutas:
                    raise ErrorMapa(f"{nombre}:{c.linea}: la ruta {r} ya es de {rutas[r]}")
                rutas[r] = c.nombre
        targets: dict[str, str] = {}
        for c in comps:
            for tg in c.targets:
                if tg in targets:
                    raise ErrorMapa(f"{nombre}:{c.linea}: el target {tg} ya es de {targets[tg]}")
                targets[tg] = c.nombre
        return cls(comps, raices)

    # Qué componente tiene un archivo: la ruta más larga que lo contiene.
    def componente_de(self, rel: str) -> Componente | None:
        """rel: relativo a src/ («app/ui/StudioViewModel.hpp»)."""
        if rel in self._cache:
            return self._cache[rel]
        mejor, largo = None, -1
        for c in self.componentes:
            for r in c.rutas:
                if r.endswith("/"):
                    ok = rel.startswith(r)
                elif "*" in r or "?" in r:
                    ok = fnmatch.fnmatchcase(rel, r)
                else:
                    ok = rel == r
                if ok and len(r) > largo:
                    mejor, largo = c, len(r)
        self._cache[rel] = mejor
        return mejor

    def raices_de(self, rel: str) -> list[str]:
        mejor, largo = ["core"], -1
        for pref, dirs in self.raices.items():
            if rel.startswith(pref) and len(pref) > largo:
                mejor, largo = dirs, len(pref)
        return mejor

    def nivel_de_target(self) -> dict[str, tuple[float, str]]:
        """{target: (nivel, componente)} del mapa."""
        return {t: (c.nivel, c.nombre) for c in self.componentes for t in c.targets}


# --- los includes -----------------------------------------------------------------------
@dataclass
class Include:
    archivo: str        # relativo a src/
    linea: int
    destino: str        # relativo a src/
    texto: str

    def __str__(self) -> str:
        return f"src/{self.archivo}:{self.linea}: {self.texto}"


@dataclass
class Violacion:
    origen: str         # componente
    destino: str        # componente (o target, en un enlace)
    tipo: str           # «arriba» | «mismo nivel»
    donde: str          # archivo:línea
    que: str            # la línea

    @property
    def clave(self) -> str:
        return f"{self.origen} -> {self.destino}"

    def __str__(self) -> str:
        return f"{self.donde}: [{self.tipo}] {self.clave} — {self.que}"


def archivos_de_codigo() -> list[str]:
    """Todos los archivos de C/C++ de src/ (relativos a src/, sin los de tests/)."""
    return sorted(p.relative_to(SRC).as_posix() for p in SRC.rglob("*") if p.is_file() and es_codigo(p))


class Grafo:
    """Los includes resueltos de src/. Lee los archivos a pedido (y una vez cada uno)."""

    def __init__(self, mapa: Mapa):
        self.mapa = mapa
        self._incs: dict[str, list[Include]] = {}

    def resolver(self, rel: str, nombre: str, comillas: bool) -> str | None:
        """Lo que encuentra el compilador para `#include "nombre"` en src/<rel> (None: no es
        de src/, o no existe)."""
        cands = [(SRC / rel).parent / nombre] if comillas else []
        cands += [SRC / r / nombre for r in self.mapa.raices_de(rel)]
        for c in cands:
            if c.is_file():
                try:
                    return c.resolve().relative_to(SRC.resolve()).as_posix()
                except ValueError:
                    return None
        return None

    def includes(self, rel: str) -> list[Include]:
        if rel not in self._incs:
            txt = (SRC / rel).read_text(encoding="utf-8", errors="replace")
            out = []
            for m in RE_INCLUDE.finditer(txt):
                dest = self.resolver(rel, m.group(2), m.group(1) == '"')
                if dest is None:
                    continue
                out.append(Include(rel, txt.count("\n", 0, m.start()) + 1, dest,
                                   m.group(0).strip()))
            self._incs[rel] = out
        return self._incs[rel]

    def cierre(self, archivos: list[str]) -> list[str]:
        """Los archivos de src/ que alcanzan, transitivamente, los includes de `archivos`
        (sin ellos mismos, salvo que se alcancen)."""
        vistos: set[str] = set()
        pila = list(archivos)
        while pila:
            a = pila.pop()
            for inc in self.includes(a):
                if inc.destino not in vistos:
                    vistos.add(inc.destino)
                    pila.append(inc.destino)
        return sorted(vistos)


def archivos_del_componente(mapa: Mapa, comp: Componente, todos: list[str] | None = None) -> list[str]:
    """Los archivos de código de src/ que caen en `comp` (relativos a src/)."""
    if todos is None:
        todos = []
        for c in comp.carpetas():
            base = SRC / c
            if base.is_dir():
                todos += [p.relative_to(SRC).as_posix() for p in base.rglob("*") if p.is_file() and es_codigo(p)]
    return sorted(a for a in set(todos) if mapa.componente_de(a) is comp)


def violaciones_includes(mapa: Mapa, grafo: Grafo, archivos: list[str]) -> list[Violacion]:
    out = []
    for a in archivos:
        co = mapa.componente_de(a)
        if co is None:
            continue
        for inc in grafo.includes(a):
            cd = mapa.componente_de(inc.destino)
            if cd is None or cd is co:
                continue
            if cd.nivel > co.nivel:
                tipo = "arriba"
            elif cd.nivel == co.nivel:
                tipo = "mismo nivel"
            else:
                continue
            out.append(Violacion(co.nombre, cd.nombre, tipo, f"src/{a}:{inc.linea}", inc.texto))
    return out


# tests/test_headers_solos.py revisa los headers de estos componentes (core y la app).
PREFIJOS_HEADERS_SOLOS = ("core/", "app/")


def componentes_headers_solos(mapa: Mapa) -> list[Componente]:
    return [c for c in mapa.componentes if all(r.startswith(PREFIJOS_HEADERS_SOLOS) for r in c.rutas)]


# Los headers de un componente se revisan en PARTES de unos pocos (un test de ctest cada una):
# en frío se reparten entre los núcleos (app/ui entera eran 67 s en un solo test) y la caché de
# tests vuelve a correr solo las partes cuyos headers (o lo que incluyen) cambiaron. La parte de
# un header sale de su ruta (no de su lugar en la lista): un header nuevo no mueve a los demás.
HEADERS_POR_PARTE = 8


def partes_headers_solos(headers: list[str]) -> int:
    """En cuántas partes se revisan los headers de un componente."""
    return max(1, -(-len(headers) // HEADERS_POR_PARTE))


def parte_del_header(header: str, partes: int) -> int:
    """La parte (0..partes-1) de un header (ruta relativa a src/), estable."""
    return int(hashlib.sha1(header.encode("utf-8")).hexdigest(), 16) % max(1, partes)


def sin_componente(mapa: Mapa, todos: list[str]) -> list[str]:
    return [a for a in todos if mapa.componente_de(a) is None]


# --- CMake: target_link_libraries y ba_biblioteca -----------------------------------------
@dataclass
class Llamada:
    archivo: str        # relativo a la raíz
    linea: int
    funcion: str
    args: list[str]


def archivos_cmake() -> list[pathlib.Path]:
    out = [RAIZ / "CMakeLists.txt"]
    out += sorted((RAIZ / "cmake").rglob("*.cmake")) + sorted((RAIZ / "cmake").rglob("CMakeLists.txt"))
    out += sorted(p for p in SRC.rglob("CMakeLists.txt"))
    return [p for p in out if p.is_file()]


def _sin_comentarios_cmake(txt: str) -> str:
    out = []
    for linea in txt.splitlines():
        en, i = None, 0
        while i < len(linea):
            ch = linea[i]
            if en:
                if ch == "\\":
                    i += 1
                elif ch == en:
                    en = None
            elif ch == '"':
                en = ch
            elif ch == "#":
                linea = linea[:i]
                break
            i += 1
        out.append(linea)
    return "\n".join(out)


def llamadas_cmake(funciones: tuple[str, ...], archivos: list[pathlib.Path] | None = None) -> list[Llamada]:
    """Las llamadas a `funciones` en los CMake del proyecto, con sus argumentos (tokens)."""
    out = []
    pat = re.compile(r"\b(%s)\s*\(" % "|".join(map(re.escape, funciones)), re.I)
    for p in archivos if archivos is not None else archivos_cmake():
        txt = _sin_comentarios_cmake(p.read_text(encoding="utf-8", errors="replace"))
        for m in pat.finditer(txt):
            if m.start() > 0 and (txt[m.start() - 1].isalnum() or txt[m.start() - 1] == "_"):
                continue
            prof, i = 1, m.end()
            while i < len(txt) and prof:
                if txt[i] == "(":
                    prof += 1
                elif txt[i] == ")":
                    prof -= 1
                i += 1
            cuerpo = txt[m.end():i - 1]
            args = re.findall(r'"(?:[^"\\]|\\.)*"|[^\s"]+', cuerpo)
            out.append(Llamada(p.relative_to(RAIZ).as_posix(), txt.count("\n", 0, m.start()) + 1,
                               m.group(1).lower(), [a.strip('"') for a in args]))
    return out


PALABRAS_ENLACE = {"PUBLIC", "PRIVATE", "INTERFACE", "LINK_PUBLIC", "LINK_PRIVATE", "LINK_INTERFACE_LIBRARIES",
                   "debug", "optimized", "general"}


@dataclass
class Biblioteca:
    nombre: str
    nivel: float
    depende: list[str]
    fuentes: list[str]   # relativas a src/ (las que caen en src/)
    donde: str


def bibliotecas_cmake(llamadas: list[Llamada]) -> list[Biblioteca]:
    """Las `ba_biblioteca(<nombre> NIVEL <n> DEPENDE ... FUENTES ...)` (Ola 2)."""
    out = []
    for ll in llamadas:
        if ll.funcion != "ba_biblioteca" or not ll.args:
            continue
        secciones: dict[str, list[str]] = {}
        actual = None
        for a in ll.args[1:]:
            if re.fullmatch(r"[A-Z][A-Z_]*", a):
                actual = a
                secciones.setdefault(a, [])
            elif actual:
                secciones[actual].append(a)
        nivel = secciones.get("NIVEL", [])
        if len(nivel) != 1:
            raise ErrorMapa(f"{ll.archivo}:{ll.linea}: ba_biblioteca({ll.args[0]}) sin NIVEL <n>")
        base = (RAIZ / ll.archivo).parent
        fuentes = []
        for f in secciones.get("FUENTES", []):
            if "$" in f:
                continue
            p = (base / f).resolve()
            try:
                fuentes.append(p.relative_to(SRC.resolve()).as_posix())
            except ValueError:
                pass
        out.append(Biblioteca(ll.args[0], float(nivel[0]), secciones.get("DEPENDE", []), fuentes,
                              f"{ll.archivo}:{ll.linea}"))
    return out


def niveles_de_targets(mapa: Mapa, bibliotecas: list[Biblioteca]) -> tuple[dict[str, tuple[float, str]], list[str]]:
    """({target: (nivel, componente)}, errores): los del mapa y los de ba_biblioteca, y que
    coincidan."""
    niveles = mapa.nivel_de_target()
    errores = []
    for b in bibliotecas:
        comps = {mapa.componente_de(f) for f in b.fuentes} - {None}
        for c in sorted(comps, key=lambda c: c.nombre):
            if c.nivel != b.nivel:
                errores.append(f"{b.donde}: ba_biblioteca({b.nombre}) es NIVEL {_formato_nivel(b.nivel)} y tiene "
                               f"fuentes de {c.nombre} (nivel {_formato_nivel(c.nivel)} en tools/diag/niveles.txt)")
        if b.nombre in niveles and niveles[b.nombre][0] != b.nivel:
            errores.append(f"{b.donde}: ba_biblioteca({b.nombre}) es NIVEL {_formato_nivel(b.nivel)} y el mapa "
                           f"le da {_formato_nivel(niveles[b.nombre][0])}")
        comp = sorted(comps, key=lambda c: c.nombre)[0].nombre if comps else b.nombre
        niveles.setdefault(b.nombre, (b.nivel, comp))
    return niveles, errores


def violaciones_enlaces(mapa: Mapa, llamadas: list[Llamada] | None = None) -> tuple[list[Violacion], list[str]]:
    """(violaciones de target_link_libraries/DEPENDE, errores del mapa contra ba_biblioteca)."""
    if llamadas is None:
        llamadas = llamadas_cmake(("target_link_libraries", "ba_biblioteca"))
    bibs = bibliotecas_cmake(llamadas)
    niveles, errores = niveles_de_targets(mapa, bibs)
    aristas = []
    for ll in llamadas:
        if ll.funcion == "target_link_libraries" and ll.args:
            aristas += [(ll.args[0], a, ll) for a in ll.args[1:] if a not in PALABRAS_ENLACE]
    for b in bibs:
        ll = next(x for x in llamadas if f"{x.archivo}:{x.linea}" == b.donde)
        aristas += [(b.nombre, d, ll) for d in b.depende]
    out = []
    for origen, dest, ll in aristas:
        if origen not in niveles or dest not in niveles:
            continue
        (no, co), (nd, cd) = niveles[origen], niveles[dest]
        if co == cd:
            continue
        if nd > no:
            tipo = "arriba"
        elif nd == no:
            tipo = "mismo nivel"
        else:
            continue
        out.append(Violacion(origen, dest, tipo, f"{ll.archivo}:{ll.linea}", f"{ll.funcion}({origen} … {dest})"))
    return out, errores


# --- tablas -------------------------------------------------------------------------------
def cuenta(violaciones: list[Violacion]) -> dict[str, int]:
    out: dict[str, int] = {}
    for v in violaciones:
        out[v.clave] = out.get(v.clave, 0) + 1
    return dict(sorted(out.items()))


def _imprimir_tabla(nombre: str, d: dict[str, int]) -> None:
    print(f"{nombre} = {{")
    for k, v in d.items():
        print(f'    "{k}": {v},')
    print("}")


# --- para CMake: los tests por área con sus entradas exactas ----------------------------------
def _cmake_lista(xs: list[str]) -> str:
    return "\n".join(f'    "{x}"' for x in xs)


def escribir_cmake(salida: pathlib.Path, mapa: Mapa) -> None:
    """Escribe un .cmake con, por componente: BA_NIVELES_<id>_NOMBRE, _NIVEL, _ARCHIVOS (su
    código), _HEADERS (sus headers) y _CIERRE (los headers del proyecto que alcanzan sus
    headers, sin ellos). Y BA_NIVELES_COMPONENTES (los id), BA_NIVELES_SIN_COMPONENTE (los
    archivos que el mapa no cubre: test_niveles_mapa falla) y BA_NIVELES_CMAKE (los CMake que
    lee test_niveles_enlaces). Y por componente de los headers solos: _PARTES y, por parte k,
    _P<k>_HEADERS y _P<k>_CIERRE. Rutas absolutas. Solo reescribe si cambió."""
    todos = archivos_de_codigo()
    grafo = Grafo(mapa)
    partes = ["# Generado por tools/diag/niveles.py --cmake (no editar)."]
    ids = []
    for c in mapa.componentes:
        ids.append(c.id)
        archivos = archivos_del_componente(mapa, c, todos)
        headers = [a for a in archivos if a.endswith(EXT_HEADER)]
        cierre = [a for a in grafo.cierre(headers) if a not in set(headers)]
        partes.append(f'set(BA_NIVELES_{c.id}_NOMBRE "{c.nombre}")')
        partes.append(f'set(BA_NIVELES_{c.id}_NIVEL "{_formato_nivel(c.nivel)}")')
        for var, lista in (("ARCHIVOS", archivos), ("HEADERS", headers), ("CIERRE", cierre)):
            partes.append(f"set(BA_NIVELES_{c.id}_{var}\n{_cmake_lista([str(SRC / a) for a in lista])})")
    partes.append(f"set(BA_NIVELES_COMPONENTES {' '.join(ids)})")
    solos = [c for c in componentes_headers_solos(mapa)
             if any(a.endswith(EXT_HEADER) for a in archivos_del_componente(mapa, c, todos))]
    partes.append(f"set(BA_NIVELES_HEADERS_SOLOS {' '.join(c.id for c in solos)})")
    # Por parte de los headers solos: sus headers y lo que estos alcanzan (sin ellos).
    for c in solos:
        hs = [a for a in archivos_del_componente(mapa, c, todos) if a.endswith(EXT_HEADER)]
        n = partes_headers_solos(hs)
        partes.append(f'set(BA_NIVELES_{c.id}_PARTES "{n}")')
        for k in range(n):
            mios = [h for h in hs if parte_del_header(h, n) == k]
            cierre = [a for a in grafo.cierre(mios) if a not in set(mios)]
            for var, lista in (("HEADERS", mios), ("CIERRE", cierre)):
                partes.append(f"set(BA_NIVELES_{c.id}_P{k}_{var}\n{_cmake_lista([str(SRC / a) for a in lista])})")
    partes.append(f"set(BA_NIVELES_SIN_COMPONENTE\n{_cmake_lista([str(SRC / a) for a in sin_componente(mapa, todos)])})")
    partes.append(f"set(BA_NIVELES_CMAKE\n{_cmake_lista([str(p) for p in archivos_cmake()])})")
    texto = "\n".join(partes) + "\n"
    if not salida.exists() or salida.read_text(encoding="utf-8") != texto:
        salida.parent.mkdir(parents=True, exist_ok=True)
        salida.write_text(texto, encoding="utf-8")


# --- main ---------------------------------------------------------------------------------
def main(argv: list[str]) -> int:
    mapa = Mapa.leer()
    if "--cmake" in argv:
        escribir_cmake(pathlib.Path(argv[argv.index("--cmake") + 1]), mapa)
        return 0
    todos = archivos_de_codigo()
    if "--componentes" in argv:
        for c in sorted(mapa.componentes, key=lambda c: (c.nivel, c.nombre)):
            n = len(archivos_del_componente(mapa, c, todos))
            print(f"{_formato_nivel(c.nivel):>4}  {c.nombre:<20} {n:>4} archivos  {' '.join(c.rutas)}"
                  + (f"  : {' '.join(c.targets)}" if c.targets else ""))
        return 0
    archivos = todos
    if "--componente" in argv:
        c = mapa.por_nombre[argv[argv.index("--componente") + 1]]
        archivos = archivos_del_componente(mapa, c, todos)
    grafo = Grafo(mapa)
    vi = violaciones_includes(mapa, grafo, archivos)
    ve, errores = violaciones_enlaces(mapa)
    sin = sin_componente(mapa, todos)
    if "--json" in argv:
        print(json.dumps({"includes": [asdict(v) for v in vi], "enlaces": [asdict(v) for v in ve],
                          "errores": errores, "sin_componente": sin}, ensure_ascii=False, indent=1))
        return 0
    if "--deuda" in argv:
        _imprimir_tabla("DEUDA_INCLUDES", cuenta(vi))
        _imprimir_tabla("DEUDA_ENLACES", cuenta(ve))
        return 0
    for titulo, vs in (("includes", vi), ("enlaces", ve)):
        print(f"== {titulo} hacia arriba o entre iguales ({len(vs)}) ==")
        por: dict[str, list[Violacion]] = {}
        for v in vs:
            por.setdefault(v.clave, []).append(v)
        for k in sorted(por):
            print(f"{k}: {len(por[k])} [{por[k][0].tipo}]")
            for v in por[k]:
                print(f"    {v.donde}: {v.que}")
    for e in errores:
        print("ERROR:", e)
    for a in sin:
        print(f"sin componente en tools/diag/niveles.txt: src/{a}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
