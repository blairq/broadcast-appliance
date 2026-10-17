"""Recorrer un pedazo de un temporal (C++20): el temporal muere antes del recorrido.

    for (const auto& [k, v] : algo.value("x", json::object()).items())   // MAL
    for (const auto& t : llamar()["result"]["tools"])                     // MAL

`.items()`, `operator[]` y `.at()` devuelven referencias ADENTRO del objeto; si ese objeto
es lo que devolvió una llamada, se destruye al terminar la expresión del `for` y el recorrido
lee memoria liberada (ASan: heap-use-after-free). Se encontró así en Cuerpo, en las ranuras
del debate, en haySalida y en una prueba. Lo que devuelve copia (`.value(...)`, `.toArray()`)
sí vive todo el recorrido. El arreglo: ponerle nombre antes del `for` (o en su init:
`for (const json x = ...; const auto& [k, v] : x.items())`).
"""
import pathlib
import re

RAIZ = pathlib.Path(__file__).resolve().parent.parent
CABECERA = re.compile(r"\bfor\s*\(")


def cabeceras(texto):
    """El contenido de cada `for (...)` (con paréntesis balanceados) y su línea."""
    for m in CABECERA.finditer(texto):
        i, prof = m.end(), 1
        while i < len(texto) and prof:
            prof += {"(": 1, ")": -1}.get(texto[i], 0)
            i += 1
        yield texto.count("\n", 0, m.start()) + 1, texto[m.end():i - 1]


def rango(cab):
    """La expresión del rango de un for de rango (sin init-statement), o None."""
    if ";" in cab:  # for clásico, o con init (ahí el temporal tiene nombre)
        return None
    prof = 0
    for i, c in enumerate(cab):
        prof += {"(": 1, "[": 1, "{": 1, ")": -1, "]": -1, "}": -1}.get(c, 0)
        if c == ":" and prof == 0 and cab[i - 1:i + 1] != "::" and cab[i:i + 2] != "::":
            return cab[i + 1:].strip()
    return None


def peligroso(expr):
    # Una llamada en algún lado y, al final, algo que devuelve una referencia adentro.
    if "(" not in expr:
        return False
    if expr.endswith(".items()"):
        base = expr[: -len(".items()")]
    elif expr.endswith("]") or re.search(r"\.at\([^()]*\)$", expr):
        base = re.sub(r"(\[[^\]]*\]|\.at\([^()]*\))+$", "", expr)
    else:
        return False
    # La base es un temporal si termina en una llamada (no `(*p)` ni `x.y`).
    return base.endswith(")") and not re.fullmatch(r"\(\*?[A-Za-z_][A-Za-z0-9_]*\)", base)


def test_sin_recorrer_temporales():
    malos = []
    for f in sorted((RAIZ / "src").rglob("*.cpp")) + sorted((RAIZ / "src").rglob("*.hpp")):
        texto = f.read_text(encoding="utf-8", errors="replace")
        for linea, cab in cabeceras(texto):
            expr = rango(cab)
            if expr and peligroso(expr):
                malos.append(f"{f.relative_to(RAIZ)}:{linea}: {expr}")
    assert not malos, "recorre un pedazo de un temporal (ponerle nombre antes):\n" + "\n".join(malos)


def test_el_chequeo_ve_los_casos():
    assert peligroso('algo.value("x", json::object()).items()')
    assert peligroso('llamar(core, req(3))["result"]["tools"]')
    assert peligroso('f().at("x")')
    assert not peligroso('obj.items()')
    assert not peligroso('(*p)["paginas"]')
    assert not peligroso('snapshot().value("participants").toArray()')
    assert not peligroso('j.value("x", json::array())')
    assert rango('size_t i = 0; i < n; ++i') is None
    assert rango('const json x = f(); const auto& [k, v] : x.items()') is None
    assert rango('const auto& [k, v] : a::b(c).items()') == 'a::b(c).items()'
