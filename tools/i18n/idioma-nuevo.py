#!/usr/bin/env python3
"""Sumar un idioma: el perfil y su catálogo, sin tocar código (docs/desarrollo/I18N.md).

  tools/i18n/idioma-nuevo.py pt-BR "Português (Brasil)" --locale pt_BR --plural frances --respaldo es
  tools/i18n/idioma-nuevo.py ar "العربية" --locale ar_EG --plural arabe --rtl --respaldo en

Hace:
  1. assets/idiomas/<código>.json, el perfil (nombre en su idioma, locale, dirección, regla de
     plurales, respaldos). La voz, el buscador y la tipografía se completan después a mano
     (ver las secciones del perfil en la guía).
  2. i18n/ba_<código>.ts con todos los textos sin traducir (make i18n-actualizar).
  3. Dice qué falta: traducir el .ts (tools/i18n/lotes.py, en lotes) y la carpeta
     assets/idiomas/<código>/ (prompts, ayuda, guías, tótem, plantillas), que mientras tanto
     sale de sus respaldos y al final del original.
"""
import argparse
import json
import pathlib
import subprocess
import sys

RAIZ = pathlib.Path(__file__).resolve().parents[2]
PERFILES = RAIZ / "assets" / "idiomas"
REGLAS = ("uno-otro", "frances", "sin-plural", "eslavo", "polaco", "checo", "arabe")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("codigo", help="BCP 47: pt-BR, fr, ar…")
    ap.add_argument("nombre", help="el nombre del idioma EN ese idioma")
    ap.add_argument("--locale", required=True, help="el de los formatos (números, fechas): pt_BR, ar_EG…")
    ap.add_argument("--plural", default="uno-otro", choices=REGLAS)
    ap.add_argument("--rtl", action="store_true", help="se escribe de derecha a izquierda")
    ap.add_argument("--respaldo", action="append", default=[], help="a qué idioma recurrir (se puede repetir)")
    a = ap.parse_args()

    perfil = PERFILES / f"{a.codigo}.json"
    if perfil.exists():
        print(f"[idioma] {perfil.relative_to(RAIZ)} ya existe: no lo piso")
    else:
        for r in a.respaldo:
            if not (PERFILES / f"{r}.json").exists():
                sys.exit(f"[idioma] el respaldo {r!r} no tiene perfil")
        datos = {"codigo": a.codigo, "nombre": a.nombre, "locale": a.locale,
                 "direccion": "rtl" if a.rtl else "ltr", "plural": a.plural, "respaldo": a.respaldo}
        perfil.write_text(json.dumps(datos, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(f"[idioma] {perfil.relative_to(RAIZ)}")
    r = subprocess.run([sys.executable, "-I", str(RAIZ / "tools" / "i18n" / "actualizar.py"), a.codigo])
    if r.returncode != 0:
        return r.returncode
    print(f"""
Lo que sigue:
  - Traducir i18n/ba_{a.codigo}.ts: tools/i18n/lotes.py exportar i18n/ba_{a.codigo}.ts <carpeta>
    (en lotes, en paralelo) y tools/i18n/lotes.py importar … Un glosario ayuda: i18n/glosario/.
  - La carpeta assets/idiomas/{a.codigo}/: prompts/, ayuda/, guias/, totem.json, plantillas/textos.json
    (y tools/generar/plantillas.py --idioma {a.codigo}). Mientras no estén, salen de los respaldos.
  - En el perfil: "voz" (voces y reconocimiento), "buscador" (palabras vacías) y "tipografia" si
    la escritura no está en las tipografías del programa.
  - .venv/bin/python -m pytest tests/test_i18n.py -q -rw  dice qué falta.""")
    return 0


if __name__ == "__main__":
    sys.exit(main())
