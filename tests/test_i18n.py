"""Repo invariants for internationalization (docs/planes/PLAN-I18N.md).

A language is files, never code: its profile (assets/idiomas/<code>.json) and its
catalog (i18n/ba_<code>.ts). These checks keep that true:

- every profile is well formed, there is exactly one source language, and every
  non-pseudo language has its .ts (and every .ts a profile);
- finished translations keep the placeholders (%1..%9, %n) and have as many plural
  forms as the language's rule;
- MCP tool names are never translatable text: a renamed tool breaks the trained
  models, the prompts, the control surfaces and saved events;
- no code branches on a specific language (``idioma == "en"``): what differs between
  languages goes in the profile;
- the tools (ts-a-json.py, extraer.py) work.

What is still being migrated (untranslated QML literals, unfinished translations)
only warns for now; it becomes a failure once phases I1-I9 close.
"""
from __future__ import annotations

import json
import re
import subprocess
import sys
import warnings
import xml.etree.ElementTree as ET
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parent.parent
PERFILES = REPO / "assets" / "idiomas"
TS_DIR = REPO / "i18n"
REGLAS = {"uno-otro": 2, "frances": 2, "sin-plural": 1, "eslavo": 3, "polaco": 3, "checo": 3, "arabe": 6}
MARCADOR = re.compile(r"%L?(?:[1-9]|n)")
# Marcas fijas que el programa pone sin traducir y el asistente reconoce: «[OPERADOR]».
MARCA_FIJA = re.compile(r"\[[A-ZÁÉÍÓÚÑ ]{3,}\]")


def perfiles() -> dict[str, dict]:
    return {p.stem: json.loads(p.read_text(encoding="utf-8")) for p in sorted(PERFILES.glob("*.json"))}


def mensajes(ts: Path):
    """(context, source, numerus, forms or [text], finished) for every live message."""
    raiz = ET.parse(ts).getroot()
    for ctx in raiz.iter("context"):
        nombre = ctx.findtext("name") or ""
        for m in ctx.iter("message"):
            t = m.find("translation")
            tipo = t.get("type", "") if t is not None else "unfinished"
            if tipo in ("obsolete", "vanished"):
                continue
            numerus = m.get("numerus") == "yes"
            formas = [f.text or "" for f in t.iter("numerusform")] if (t is not None and numerus) else \
                [(t.text or "") if t is not None else ""]
            yield nombre, m.findtext("source") or "", numerus, formas, tipo != "unfinished"


def test_profiles_are_well_formed() -> None:
    todos = perfiles()
    assert todos, "no language profiles in assets/idiomas"
    fuentes = [c for c, p in todos.items() if p.get("fuente")]
    assert len(fuentes) == 1, f"exactly one source language, found {fuentes}"
    for codigo, p in todos.items():
        assert p.get("codigo") == codigo, f"{codigo}.json says codigo={p.get('codigo')!r}"
        assert p.get("nombre"), f"{codigo}: missing nombre (its name in its own language)"
        assert p.get("locale"), f"{codigo}: missing locale"
        assert p.get("direccion", "ltr") in ("ltr", "rtl"), f"{codigo}: direccion is ltr or rtl"
        assert p.get("plural", "uno-otro") in REGLAS, f"{codigo}: unknown plural rule {p.get('plural')!r}"
        for r in p.get("respaldo", []):
            assert r in todos, f"{codigo}: fallback {r!r} has no profile"


def test_every_language_has_its_catalog() -> None:
    todos = perfiles()
    for codigo, p in todos.items():
        if p.get("pseudo"):
            continue
        assert (TS_DIR / f"ba_{codigo}.ts").is_file(), (
            f"{codigo} has no i18n/ba_{codigo}.ts: run `make i18n-actualizar`")
    for ts in TS_DIR.glob("ba_*.ts"):
        assert ts.stem[3:] in todos, f"{ts.name} has no profile in assets/idiomas"


@pytest.mark.parametrize("ts", sorted(TS_DIR.glob("ba_*.ts")), ids=lambda p: p.name)
def test_translations_keep_placeholders_and_plural_forms(ts: Path) -> None:
    perfil = perfiles().get(ts.stem[3:], {})
    formas_esperadas = REGLAS[perfil.get("plural", "uno-otro")]
    for ctx, fuente, numerus, formas, terminada in mensajes(ts):
        if not terminada:
            continue
        donde = f"{ts.name} [{ctx}] {fuente!r}"
        if numerus:
            assert len(formas) == formas_esperadas, f"{donde}: {len(formas)} plural forms, rule needs {formas_esperadas}"
        esperados = sorted(set(MARCADOR.findall(fuente)) - {"%n", "%Ln"})
        marcas = sorted(MARCA_FIJA.findall(fuente))
        for f in formas:
            assert sorted(set(MARCADOR.findall(f)) - {"%n", "%Ln"}) == esperados, f"{donde}: placeholders differ in {f!r}"
            assert sorted(MARCA_FIJA.findall(f)) == marcas, f"{donde}: fixed markers like [OPERADOR] must stay in {f!r}"


def test_source_language_has_every_plural() -> None:
    """The source language has no catalog except plurals: "%n invitado" needs its Spanish
    forms too, or the program says «5 invitado». Run `make i18n-actualizar` and fill them."""
    fuente = next(c for c, p in perfiles().items() if p.get("fuente"))
    sin = [f"[{ctx}] {fuente_txt!r}" for ctx, fuente_txt, numerus, _, terminada in mensajes(TS_DIR / f"ba_{fuente}.ts")
           if numerus and not terminada]
    assert not sin, f"plurals without their {fuente} forms in i18n/ba_{fuente}.ts:\n" + "\n".join(sin)


def test_tool_names_are_never_translatable() -> None:
    inventario = json.loads((REPO / "docs" / "mcp.json").read_text(encoding="utf-8"))
    nombres = {h for c in inventario["categorias"] for h in c["herramientas"]}
    assert nombres, "docs/mcp.json lists no tools"
    for ts in TS_DIR.glob("ba_*.ts"):
        for ctx, fuente, *_ in mensajes(ts):
            assert fuente not in nombres, (
                f"{ts.name} [{ctx}]: the MCP tool name {fuente!r} is marked for translation. "
                "Tool and parameter names are API: translate only descriptions.")


# `idioma == "en"`, `lang === 'pt-BR'`, `codigo != "es"`…: a code path for one language.
RAMA_POR_IDIOMA = re.compile(
    r"""\b(?:idioma\w*|lang\w*|locale|codigo)(?:\(\))?\s*[!=]==?\s*["'][a-z]{2}(?:[-_][A-Za-z]{2,4})?["']""")


# Still language-specific, each with the phase that removes it. A file listed here that no
# longer matches fails too: the list only shrinks.
PENDIENTES = {
    "src/app/qml/controls/PanelTraduccion.qml":
        "I7: live translation goes from Spanish (its models are es <-> X); the source becomes the event language",
}


def test_no_code_branches_on_a_language() -> None:
    malos, vistos = [], set()
    for base, exts in ((REPO / "src", {".cpp", ".hpp", ".mm", ".qml", ".js"}), (REPO / "assets" / "web", {".js"})):
        for f in base.rglob("*"):
            if f.suffix not in exts or "tests" in f.parts or not f.is_file():
                continue
            for n, linea in enumerate(f.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
                if RAMA_POR_IDIOMA.search(linea):
                    rel = f.relative_to(REPO).as_posix()
                    if rel in PENDIENTES:
                        vistos.add(rel)
                        continue
                    malos.append(f"{rel}:{n}: {linea.strip()}")
    assert not malos, "language-specific code (move it to the language profile):\n" + "\n".join(malos)
    resueltos = set(PENDIENTES) - vistos
    assert not resueltos, f"no longer language-specific, take them out of PENDIENTES: {sorted(resueltos)}"


def test_ts_to_json(tmp_path: Path) -> None:
    ts = tmp_path / "ba_zz.ts"
    ts.write_text("""<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS><TS version="2.1" language="zz">
<context><name>sala</name>
<message><source>Conferencia</source><translation>Conference</translation></message>
<message><source>Salida</source><comment>audio</comment><translation>Output</translation></message>
<message><source>Sin traducir</source><translation type="unfinished"></translation></message>
<message><source>Vieja</source><translation type="vanished">Old</translation></message>
<message numerus="yes"><source>%n invitado</source><translation>
<numerusform>%n guest</numerusform><numerusform>%n guests</numerusform></translation></message>
</context></TS>""", encoding="utf-8")
    subprocess.run([sys.executable, "-I", str(REPO / "tools" / "i18n" / "ts-a-json.py"), str(tmp_path / "out"), str(ts)],
                   check=True)
    cat = json.loads((tmp_path / "out" / "zz.json").read_text(encoding="utf-8"))
    assert cat["idioma"] == "zz" and cat["total"] == 4 and cat["traducidos"] == 3
    assert cat["mensajes"]["sala"] == {"Conferencia": "Conference", "Salida\u0004audio": "Output",
                                       "%n invitado": ["%n guest", "%n guests"]}


def test_web_extractor_runs(tmp_path: Path) -> None:
    salida = tmp_path / "web.cpp"
    subprocess.run([sys.executable, "-I", str(REPO / "tools" / "i18n" / "extraer.py"), str(salida)], check=True,
                   stdout=subprocess.DEVNULL)
    texto = salida.read_text(encoding="utf-8")
    assert "comun/i18n.js" not in texto, "the translator's own examples are not texts"


# --- What is still being migrated: warnings, not failures (yet) -------------------
QML_LITERAL = re.compile(r"""\b(?:text|title|placeholderText|ToolTip\.text)\s*:\s*["'][^"']*[A-Za-zÁÉÍÓÚáéíóúÑñ]""")


def test_progress_report() -> None:
    sueltos = 0
    for f in (REPO / "src").rglob("*.qml"):
        sueltos += len(QML_LITERAL.findall(f.read_text(encoding="utf-8", errors="replace")))
    if sueltos:
        warnings.warn(f"i18n: {sueltos} visible QML literals without qsTr (phase I1)")
    for ts in sorted(TS_DIR.glob("ba_*.ts")):
        if perfiles().get(ts.stem[3:], {}).get("fuente"):
            continue
        todos = list(mensajes(ts))
        faltan = sum(1 for *_, terminada in todos if not terminada)
        if faltan:
            warnings.warn(f"i18n: {ts.name}: {faltan} of {len(todos)} texts untranslated (phase I9)")
