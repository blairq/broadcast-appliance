"""JSON de un literal: con ba::jsonDeTexto (src/core/json/Json.hpp), no con `json::parse("…")` ni `"…"_json`.

Código nuevo que lee un texto fijo como JSON usa `ba::jsonDeTexto`, compilada una sola vez en
ba_core. Esta prueba cuenta, por archivo de src/ (con las pruebas), las llamadas a
`json::parse(` cuyo primer argumento es un literal (también en la línea de abajo, también
R"(…)") y los `"…"_json`, sin contar los comentarios, y falla si aparece uno que no está en
DEUDA. Lo que ya estaba queda anotado con su cantidad, que SOLO PUEDE BAJAR: al migrar un
archivo, se baja (o se borra) su número acá.

Medido el 2026-11-03: el `parse<const char*>` (~176 ms por TU) lo instancia el propio
<nlohmann/json.hpp> en cada TU que lo incluye, lo llame o no; migrar estos usos no acelera el
build. Lo que acelera es que menos TUs vean json.hpp (json/JsonFwd.hpp en los headers). Esta
vigilancia es para que el código nuevo no sume más.
"""
import pathlib
import re

RAIZ = pathlib.Path(__file__).resolve().parent.parent
LITERAL = re.compile(r'\bjson::parse\(\s*(?:u8)?R?"|"_json\b')

# Lo de antes (relativo a src/). SOLO PUEDE BAJAR.
DEUDA = {
    "app/tests/test_argumentos_locales.cpp": 9,
    "app/tests/test_capacidad_reglas.cpp": 3,
    "app/tests/test_linea.cpp": 13,
    "app/tests/test_linea_productor.cpp": 4,
    "app/tests/test_modelos_locales.cpp": 1,
    "app/tests/test_padron.cpp": 2,
    "app/tests/test_ref_escena.cpp": 2,
    "app/tests/test_registro_totems.cpp": 1,
    "gateway/tests/test_gateway_salidas.cpp": 5,
    "gateway/tests/test_sesion_borde.cpp": 4,
}


def sin_comentarios(t: str) -> str:
    """El código sin comentarios; los literales quedan (con su contenido)."""
    out, i, n = [], 0, len(t)
    while i < n:
        if t.startswith("//", i):
            j = t.find("\n", i)
            i = n if j < 0 else j
        elif t.startswith("/*", i):
            j = t.find("*/", i + 2)
            i = n if j < 0 else j + 2
            out.append(" ")
        elif t[i] == "R" and t.startswith('R"', i) and (i == 0 or not (t[i - 1].isalnum() or t[i - 1] == "_")):
            m = re.match(r'R"([^(\s]{0,16})\(', t[i:])
            if not m:
                out.append(t[i])
                i += 1
                continue
            cierre = ")" + m.group(1) + '"'
            j = t.find(cierre, i + m.end())
            fin = n if j < 0 else j + len(cierre)
            out.append(t[i:fin])
            i = fin
        elif t[i] in "\"'":
            q, j = t[i], i + 1
            while j < n and t[j] != q and t[j] != "\n":
                j += 2 if t[j] == "\\" else 1
            out.append(t[i:j + 1])
            i = j + 1
        else:
            out.append(t[i])
            i += 1
    return "".join(out)


def contar():
    cuenta = {}
    for f in sorted((RAIZ / "src").rglob("*")):
        if f.suffix not in (".cpp", ".hpp", ".h", ".mm") or not f.is_file():
            continue
        n = len(LITERAL.findall(sin_comentarios(f.read_text(encoding="utf-8", errors="replace"))))
        if n:
            cuenta[f.relative_to(RAIZ / "src").as_posix()] = n
    return cuenta


def test_sin_json_de_literales_nuevos():
    cuenta = contar()
    nuevos = {f: n for f, n in cuenta.items() if n > DEUDA.get(f, 0)}
    assert not nuevos, ("json::parse(\"…\") o \"…\"_json nuevos: usá ba::jsonDeTexto (json/Json.hpp); "
                        f"ver la cabecera de esta prueba: {nuevos}")


def test_la_deuda_solo_baja():
    cuenta = contar()
    sobra = {f: (n, cuenta.get(f, 0)) for f, n in DEUDA.items() if cuenta.get(f, 0) < n}
    assert not sobra, f"ya se migraron (bajá su número en DEUDA): {sobra}"
