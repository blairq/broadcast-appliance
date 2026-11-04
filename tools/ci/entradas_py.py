#!/usr/bin/env python3
# ============================================================================
# tools/ci/entradas_py.py — lo que lee cada prueba de Python de tests/ (cmake/BaTestsPy.cmake)
#
#   entradas_py.py --cmake <salida.cmake>
#
# Escribe, con rutas absolutas y solo si cambió:
#   BA_PY_AREAS                     los id de las áreas (tests/areas.py: los componentes de
#                                   tools/diag/niveles.txt y «resto»)
#   BA_PY_AREA_<id>_NOMBRE          el nombre del área (lo que va en BA_AREA)
#   BA_PY_AREA_<id>_ARCHIVOS        los archivos que miran las pruebas marcadas `area` en esa
#                                   área (.cpp .hpp .h .mm .qml .js de src/, con sus tests/; en
#                                   «resto», además, los .js de assets/web/)
#   BA_PY_ACOPLES_QML               la QML de la app, sus .hpp y el .cpp/.mm de los .hpp que
#                                   declaran Q_INVOKABLE (tests/test_acoples.py, lo que no es
#                                   por área: el cuerpo de una invocable que llama un binding)
#   BA_PY_REPUBLICADORES            los .cpp de la app que registran un republicador y las
#                                   rutas del gateway que clasifica tests/test_republicadores.py
#   BA_PY_FUENTES_GLOB              los patrones que, si aparece o se va un archivo, rehacen
#                                   esto (file(GLOB ... CONFIGURE_DEPENDS))
#
# Las listas que dependen del CONTENIDO (quién declara Q_INVOKABLE, quién registra un
# republicador) son de la última configuración. Alcanza: la caché solo guarda un test que
# pasó, y lo que lo puede hacer fallar es tocar un archivo que ya está en la lista (o el
# test, o un .hpp, que también están). Ver cmake/BaTestsPy.cmake.
# ============================================================================
import pathlib
import re
import sys

RAIZ = pathlib.Path(__file__).resolve().parents[2]
SRC = RAIZ / "src"
sys.path.insert(0, str(RAIZ / "tests"))
sys.path.insert(0, str(RAIZ / "tools" / "diag"))
import areas  # noqa: E402

EXT_AREA = (".cpp", ".hpp", ".h", ".mm", ".qml", ".js")
CARPETAS_FUERA = {"__pycache__", "node_modules"}


def _archivos(base: pathlib.Path, exts) -> list[pathlib.Path]:
    if not base.is_dir():
        return []
    return sorted(p for p in base.rglob("*")
                  if p.suffix in exts and p.is_file() and not (CARPETAS_FUERA & set(p.parts)))


def por_area() -> dict[str, list[pathlib.Path]]:
    out: dict[str, list[pathlib.Path]] = {n: [] for n in areas.nombres()}
    for p in _archivos(SRC, EXT_AREA):
        out[areas.de(p)].append(p)
    out[areas.RESTO] += _archivos(RAIZ / "assets" / "web", (".js",))
    return out


def acoples_qml() -> list[pathlib.Path]:
    app = SRC / "app"
    qml = _archivos(app / "qml", (".qml",))
    hpp = [p for p in _archivos(app, (".hpp",)) if "tests" not in p.relative_to(app).parts]
    cuerpos = []
    for h in hpp:
        if "Q_INVOKABLE" in h.read_text(encoding="utf-8", errors="replace"):
            cuerpos += [c for c in (h.with_suffix(".cpp"), h.with_suffix(".mm")) if c.is_file()]
    return qml + hpp + cuerpos


def republicadores() -> list[pathlib.Path]:
    import test_republicadores as t
    out = []
    for p in _archivos(SRC / "app", (".cpp",)):
        txt = p.read_text(encoding="utf-8", errors="replace")
        if "registrarRepublicador" in txt or "GuardadoEnEvento" in txt:
            out.append(p)
    out.append(SRC / "gateway" / "AdminApi.cpp")
    out += [SRC / "gateway" / a for a in sorted(t.BORDES)]
    return out


def _id(nombre: str) -> str:
    return re.sub(r"[^A-Za-z0-9_]", "_", nombre)


def _lista(xs) -> str:
    return "\n".join(f'    "{x.as_posix() if isinstance(x, pathlib.Path) else x}"' for x in xs)


def escribir_cmake(salida: pathlib.Path) -> None:
    partes = ["# Generado por tools/ci/entradas_py.py --cmake (no editar)."]
    ids = []
    for nombre, archivos in por_area().items():
        i = _id(nombre)
        ids.append(i)
        partes.append(f'set(BA_PY_AREA_{i}_NOMBRE "{nombre}")')
        partes.append(f"set(BA_PY_AREA_{i}_ARCHIVOS\n{_lista(archivos)})")
    partes.append(f"set(BA_PY_AREAS {' '.join(ids)})")
    partes.append(f"set(BA_PY_ACOPLES_QML\n{_lista(acoples_qml())})")
    partes.append(f"set(BA_PY_REPUBLICADORES\n{_lista(republicadores())})")
    globs = [f"{SRC.as_posix()}/*{e}" for e in EXT_AREA] + [f"{(RAIZ / 'assets' / 'web').as_posix()}/*.js"]
    partes.append(f"set(BA_PY_FUENTES_GLOB\n{_lista(globs)})")
    texto = "\n".join(partes) + "\n"
    if not salida.exists() or salida.read_text(encoding="utf-8") != texto:
        salida.parent.mkdir(parents=True, exist_ok=True)
        salida.write_text(texto, encoding="utf-8")


def main(argv: list[str]) -> int:
    if len(argv) == 2 and argv[0] == "--cmake":
        escribir_cmake(pathlib.Path(argv[1]))
        return 0
    for nombre, archivos in por_area().items():
        print(f"{nombre:<20} {len(archivos):>5} archivos")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
