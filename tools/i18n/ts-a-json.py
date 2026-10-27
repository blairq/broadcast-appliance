#!/usr/bin/env python3
"""Los .ts de Qt (i18n/ba_<código>.ts) a los catálogos que lee todo el sistema.

Un solo formato para los tres consumidores: el núcleo (ba::i18n, sin Qt), la
interfaz (arranque/TraductorQt le pregunta a ba::i18n) y la web (/comun/i18n/).
Así no hace falta lrelease en ninguna compilación (Docker, Wine): solo Python.

Sale <salida>/<código>.json:
  {"idioma": "en", "total": 1234, "traducidos": 1200,
   "mensajes": {"<contexto>": {"<texto>": "<traducción>",
                               "<texto>\\u0004<desambiguación>": "...",
                               "<texto con %n>": ["forma 0", "forma 1"]}}}
Solo van las traducciones TERMINADAS: una sin terminar sale en el original (el
español), que es mejor que una a medias. Las obsoletas no cuentan.

Uso: tools/i18n/ts-a-json.py <salida> i18n/ba_en.ts [i18n/ba_pt-BR.ts ...]
"""
import json
import pathlib
import sys
import xml.etree.ElementTree as ET


def codigo_de(ts: pathlib.Path) -> str:
    nombre = ts.stem
    if not nombre.startswith("ba_"):
        raise SystemExit(f"{ts}: el nombre tiene que ser ba_<código>.ts")
    return nombre[3:]


def leer(ts: pathlib.Path) -> dict:
    raiz = ET.parse(ts).getroot()
    mensajes: dict = {}
    total = traducidos = 0
    for ctx in raiz.iter("context"):
        nombre = ctx.findtext("name") or ""
        for m in ctx.iter("message"):
            fuente = m.findtext("source")
            if fuente is None:
                continue
            t = m.find("translation")
            tipo = t.get("type", "") if t is not None else "unfinished"
            if tipo in ("obsolete", "vanished"):
                continue
            total += 1
            if t is None or tipo == "unfinished":
                continue
            clave = fuente
            desamb = m.findtext("comment")
            if desamb:
                clave += "\u0004" + desamb
            if m.get("numerus") == "yes":
                formas = [f.text or "" for f in t.iter("numerusform")]
                if not formas or not all(formas):
                    continue
                valor = formas
            else:
                valor = t.text or ""
                if not valor:
                    continue
            mensajes.setdefault(nombre, {})[clave] = valor
            traducidos += 1
    return {"idioma": codigo_de(ts), "total": total, "traducidos": traducidos, "mensajes": mensajes}


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    salida = pathlib.Path(sys.argv[1])
    salida.mkdir(parents=True, exist_ok=True)
    for arg in sys.argv[2:]:
        ts = pathlib.Path(arg)
        cat = leer(ts)
        destino = salida / f"{cat['idioma']}.json"
        texto = json.dumps(cat, ensure_ascii=False, sort_keys=True, indent=0) + "\n"
        # Sin reescribir lo que no cambió: el install incremental lo ve «Up-to-date».
        if not destino.exists() or destino.read_text(encoding="utf-8") != texto:
            destino.write_text(texto, encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
