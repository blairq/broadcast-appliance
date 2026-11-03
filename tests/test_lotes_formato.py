"""tools/i18n/lotes.py importar deja el .ts en el formato de lupdate: importar un lote sobre una
copia cambia SOLO las traducciones (antes ElementTree reescribía el archivo entero: DOCTYPE,
comillas, «/>», &apos; y sangría, más de cien mil líneas de diff por unas pocas traducciones)."""
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys

import pytest

RAIZ = pathlib.Path(__file__).resolve().parents[1]
LOTES = RAIZ / "tools" / "i18n" / "lotes.py"


def _lotes():
    import importlib.util
    spec = importlib.util.spec_from_file_location("lotes", LOTES)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def test_importar_cambia_solo_las_traducciones(tmp_path):
    if _lotes().lconvert() is None:
        pytest.skip("sin lconvert (make deps-qt-host)")
    original = RAIZ / "i18n" / "ba_fr.ts"
    texto = original.read_text(encoding="utf-8")
    # Una traducción terminada y simple, que se vuelve a dejar sin traducir en la copia.
    m = re.search(r"\n(\s*)<translation>([^<\n]{3,80})</translation>\n", texto)
    assert m, "no hay una traducción simple en ba_fr.ts"
    traduccion = m.group(2)
    copia = tmp_path / "ba_fr.ts"
    copia.write_text(texto[:m.start()] + f"\n{m.group(1)}<translation type=\"unfinished\"></translation>\n" + texto[m.end():],
                     encoding="utf-8")
    carpeta = tmp_path / "lotes"
    subprocess.run([sys.executable, str(LOTES), "exportar", str(copia), str(carpeta)], check=True)
    lote = next(carpeta.glob("lote-*.json"))
    d = json.loads(lote.read_text(encoding="utf-8"))
    assert len(d["textos"]) == 1
    # El texto de la traducción tal como lo guarda el XML (las entidades, como el .ts original).
    d["textos"][0]["traduccion"] = (traduccion.replace("&apos;", "'").replace("&quot;", '"')
                                    .replace("&lt;", "<").replace("&gt;", ">").replace("&amp;", "&"))
    lote.write_text(json.dumps(d, ensure_ascii=False), encoding="utf-8")
    subprocess.run([sys.executable, str(LOTES), "importar", str(copia), str(carpeta)], check=True)
    # Con la misma traducción de vuelta, el archivo queda idéntico al original (byte por byte).
    assert copia.read_text(encoding="utf-8") == texto
