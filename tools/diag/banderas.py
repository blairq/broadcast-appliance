"""Las banderas de compile_commands.json sin lo que no es del archivo: el lanzador (ccache y sus
clave=valor) y el header precompilado (cmake/BaPch.cmake).

Quien vuelve a compilar un archivo o un header con otras herramientas (test_headers_solos,
clang-tidy en lint-includes) lo hace SIN el PCH: con él, un header que no incluye lo que usa
compilaría igual (el PCH ya lo trajo), y el .pch de un clang no lo lee otro.

  sin_lanzador(args)  ["ccache", "sloppiness=…", "c++", …] -> ["c++", …]
  sin_pch(args)       sin -Xclang -include-pch/-include <…cmake_pch…>, -include <…cmake_pch…>,
                      -Winvalid-pch, -Xclang -fno-pch-timestamp, -fpch-instantiate-templates,
                      /Yu…, /Fp…, /FI…cmake_pch… (clang-cl)
"""
from __future__ import annotations

import pathlib
import re

LANZADORES = ("ccache", "sccache")
_CLAVE_VALOR = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*=")


def _es_pch(ruta: str) -> bool:
    return "cmake_pch" in pathlib.PurePath(ruta.replace("\\", "/")).name


def sin_lanzador(args: list[str]) -> list[str]:
    """Saca ccache/sccache del principio y las clave=valor que les siguen (ccache 4.8+)."""
    out = list(args)
    if out and pathlib.PurePath(out[0].replace("\\", "/")).name in LANZADORES:
        out = out[1:]
        while out and _CLAVE_VALOR.match(out[0]) and not out[0].startswith("-"):
            out = out[1:]
    return out


def sin_pch(args: list[str]) -> list[str]:
    out: list[str] = []
    i = 0
    while i < len(args):
        a = args[i]
        # -Xclang -include-pch -Xclang X / -Xclang -include -Xclang X (CMake con clang).
        if (a == "-Xclang" and i + 3 < len(args) and args[i + 1] in ("-include-pch", "-include")
                and args[i + 2] == "-Xclang" and _es_pch(args[i + 3])):
            i += 4
            continue
        if a == "-Xclang" and i + 1 < len(args) and args[i + 1] in ("-fno-pch-timestamp", "-fpch-instantiate-templates"):
            i += 2
            continue
        # -include X (gcc), -include-pch X.
        if a in ("-include", "-include-pch") and i + 1 < len(args) and _es_pch(args[i + 1]):
            i += 2
            continue
        if a.startswith("-include") and _es_pch(a[len("-include"):]):
            i += 1
            continue
        if a in ("-Winvalid-pch", "-fpch-instantiate-templates", "-fpch-preprocess"):
            i += 1
            continue
        # clang-cl / MSVC: /Yu"x", /Fp"x.pch", /FI"x".
        if re.match(r"^[/-](Yu|Yc)", a):
            i += 1
            continue
        if re.match(r"^[/-](Fp|FI)", a) and _es_pch(a[3:]):
            i += 1
            continue
        out.append(a)
        i += 1
    return out
