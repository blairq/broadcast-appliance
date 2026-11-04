"""Las pruebas de Python que recorren src/, de a un área por vez (cmake/BaTestsPy.cmake).

Un área es un componente de tools/diag/niveles.txt (el de la ruta más larga que contiene al
archivo, como para los niveles) y, además, «resto»: lo de src/ que ningún componente cubre
(src/app/tests/, por ejemplo) y lo que está fuera de src/ (assets/web/).

Con BA_AREA=<componente> (o «resto») las pruebas marcadas `area` miran solo los archivos de
esa área, y de sus tablas de deuda solo las filas de esa área: la unión de todas las áreas es
exactamente el chequeo de siempre. Sin BA_AREA (pytest a mano, `make pytest`) miran todo.
Así la caché de tests (tools/ci/tests-con-cache.py) vuelve a correr solo el área que cambió.
"""
from __future__ import annotations

import functools
import os
import pathlib
import sys

RAIZ = pathlib.Path(__file__).resolve().parent.parent
SRC = RAIZ / "src"
RESTO = "resto"

sys.path.insert(0, str(RAIZ / "tools" / "diag"))
import niveles  # noqa: E402


def actual() -> str | None:
    """El área de esta corrida (BA_AREA), o None: todas."""
    return os.environ.get("BA_AREA") or None


@functools.lru_cache(maxsize=1)
def _mapa() -> niveles.Mapa:
    return niveles.Mapa.leer()


def de(ruta: pathlib.Path | str) -> str:
    """El área de un archivo: ruta absoluta o relativa a la raíz del repo."""
    p = pathlib.Path(ruta)
    if not p.is_absolute():
        p = RAIZ / p
    try:
        rel = p.relative_to(SRC).as_posix()
    except ValueError:
        return RESTO
    c = _mapa().componente_de(rel)
    return c.nombre if c is not None else RESTO


def mira(ruta: pathlib.Path | str) -> bool:
    """Si esta corrida mira ese archivo."""
    a = actual()
    return a is None or de(ruta) == a


def de_src(rel: str) -> bool:
    """mira() para una ruta relativa a src/ (las claves de las tablas de deuda)."""
    return mira(SRC / rel)


def deuda(tabla: dict, base: str = "", clave=lambda k: k) -> dict:
    """Las filas de una tabla de deuda que son de esta corrida. `base`: la carpeta (relativa a
    src/) a la que son relativas las claves; `clave`: cómo sacar el archivo de la clave."""
    if actual() is None:
        return dict(tabla)
    return {k: v for k, v in tabla.items() if de_src(base + clave(k))}


def nombres() -> list[str]:
    """Todas las áreas (para el registro en CMake y para revisar que no falte ninguna)."""
    return [c.nombre for c in _mapa().componentes] + [RESTO]
