#!/usr/bin/env python3
"""Lotes de traducción: sacar del .ts lo que falta traducir y volver a meterlo traducido.

Sirve para traducir en paralelo (varias personas o varios modelos) sin que todos editen el
mismo .ts: cada lote es un archivo JSON aparte.

  tools/i18n/lotes.py exportar i18n/ba_en.ts <carpeta> [--por 400]
      Un lote por cada ~400 textos sin terminar, agrupados por contexto:
      <carpeta>/lote-001.json = {"idioma": "en", "plural": "uno-otro", "formas": 2,
        "textos": [{"id": 17, "contexto": "SettingsPage", "fuente": "…",
                    "desambiguacion": "…", "comentario": "…", "plural": false,
                    "traduccion": ""  |  "traducciones": ["", ""]}]}
      Quien traduce completa "traduccion" (o "traducciones", una por forma del plural del
      idioma) y deja todo lo demás igual.

  tools/i18n/lotes.py exportar i18n/ba_fr.ts <carpeta> --referencia i18n/ba_en.ts
      Lo mismo, y cada texto lleva además "referencia": cómo quedó en ese otro idioma
      (sus formas, si es plural). Ayuda a quien traduce a un tercer idioma: el término
      técnico ya elegido, y qué es cada cosa cuando el original es corto.

  tools/i18n/lotes.py importar i18n/ba_en.ts <carpeta>
      Mete en el .ts lo traducido (y lo marca terminado); lo que quedó vacío sigue sin
      terminar. Revisa que la traducción conserve los %1, %n y las marcas fijas
      («[OPERADOR]»): si no, no la mete y lo dice.

Al importar, cada texto se busca por contexto + fuente + desambiguación: un lote sirve
aunque en el medio haya corrido lupdate (lo que ya no está se saltea y se cuenta).
"""
import argparse
import hashlib
import json
import pathlib
import re
import sys
import xml.etree.ElementTree as ET

RAIZ = pathlib.Path(__file__).resolve().parents[2]
MARCADOR = re.compile(r"%L?(?:[1-9]|n)")
MARCA_FIJA = re.compile(r"\[[A-ZÁÉÍÓÚÑ ]{3,}\]")
FORMAS = {"uno-otro": 2, "frances": 2, "sin-plural": 1, "eslavo": 3, "polaco": 3, "checo": 3, "arabe": 6}


def perfil(codigo: str) -> dict:
    p = RAIZ / "assets" / "idiomas" / f"{codigo}.json"
    return json.loads(p.read_text(encoding="utf-8")) if p.exists() else {}


def huella(ts: pathlib.Path) -> str:
    """De qué .ts salió el lote: las fuentes en orden (no las traducciones)."""
    raiz = ET.parse(ts).getroot()
    h = hashlib.sha256()
    for ctx in raiz.iter("context"):
        for m in ctx.iter("message"):
            h.update(((ctx.findtext("name") or "") + "\x00" + (m.findtext("source") or "") + "\x01").encode())
    return h.hexdigest()[:16]


def mensajes(raiz):
    i = 0
    for ctx in raiz.iter("context"):
        nombre = ctx.findtext("name") or ""
        for m in ctx.iter("message"):
            yield i, nombre, m
            i += 1


def traducidos(ts: pathlib.Path) -> dict:
    """{(contexto, fuente, desambiguación): traducción (o lista de formas)} de lo terminado en `ts`."""
    out = {}
    for _, ctx, m in mensajes(ET.parse(ts).getroot()):
        t = m.find("translation")
        if t is None or t.get("type") in ("unfinished", "obsolete", "vanished"):
            continue
        clave = (ctx, m.findtext("source") or "", m.findtext("comment") or "")
        out[clave] = [f.text or "" for f in t.iter("numerusform")] if m.get("numerus") == "yes" else (t.text or "")
    return out


def exportar(ts: pathlib.Path, carpeta: pathlib.Path, por: int, todos: bool = False,
             referencia: pathlib.Path | None = None) -> int:
    codigo = ts.stem[3:]
    ref = traducidos(referencia) if referencia else {}
    regla = perfil(codigo).get("plural", "uno-otro")
    raiz = ET.parse(ts).getroot()
    pendientes = []
    for i, ctx, m in mensajes(raiz):
        t = m.find("translation")
        if t is None or t.get("type") in ("obsolete", "vanished"):
            continue
        if t.get("type") != "unfinished" and not todos:
            continue
        e = {"id": i, "contexto": ctx, "fuente": m.findtext("source") or ""}
        if m.findtext("comment"):
            e["desambiguacion"] = m.findtext("comment")
        if m.findtext("extracomment"):
            e["comentario"] = m.findtext("extracomment")
        if ref:
            r = ref.get((ctx, e["fuente"], m.findtext("comment") or ""))
            if r:
                e["referencia"] = r
        terminada = t.get("type") != "unfinished"
        if m.get("numerus") == "yes":
            e["plural"] = True
            formas = [f.text or "" for f in t.iter("numerusform")] if terminada else []
            e["traducciones"] = formas if formas else [""] * FORMAS.get(regla, 2)
        else:
            e["plural"] = False
            e["traduccion"] = (t.text or "") if terminada else ""
        pendientes.append(e)
    carpeta.mkdir(parents=True, exist_ok=True)
    lotes, actual = [], []
    for e in pendientes:  # cortar en el cambio de contexto más cercano a `por` (o al doble, adentro de uno grande)
        if len(actual) >= por and (actual[-1]["contexto"] != e["contexto"] or len(actual) >= 2 * por):
            lotes.append(actual)
            actual = []
        actual.append(e)
    if actual:
        lotes.append(actual)
    h = huella(ts)
    for n, lote in enumerate(lotes, 1):
        (carpeta / f"lote-{n:03d}.json").write_text(json.dumps(
            {"idioma": codigo, "nombre": perfil(codigo).get("nombre", codigo), "plural": regla,
             "formas": FORMAS.get(regla, 2), "ts": h, "textos": lote}, ensure_ascii=False, indent=1) + "\n",
            encoding="utf-8")
    print(f"[lotes] {len(pendientes)} textos {'(con lo ya traducido)' if todos else 'sin traducir'} en {len(lotes)} lotes ({carpeta})")
    return 0


def _ok(fuente: str, trad: str) -> str:
    if sorted(set(MARCADOR.findall(trad)) - {"%n", "%Ln"}) != sorted(set(MARCADOR.findall(fuente)) - {"%n", "%Ln"}):
        return "los %1…%9 no coinciden"
    if sorted(MARCA_FIJA.findall(trad)) != sorted(MARCA_FIJA.findall(fuente)):
        return "falta una marca fija ([OPERADOR]…)"
    return ""


def importar(ts: pathlib.Path, carpeta: pathlib.Path) -> int:
    arbol = ET.parse(ts)
    raiz = arbol.getroot()
    # Por contexto + fuente + desambiguación (no por posición): un lote sirve aunque en el
    # medio haya corrido lupdate y aparecido textos nuevos; lo que ya no está se saltea.
    por_clave = {}
    for _, ctx, m in mensajes(raiz):
        por_clave.setdefault((ctx, m.findtext("source") or "", m.findtext("comment") or ""), []).append(m)
    puestos = rechazados = faltan = 0
    for archivo in sorted(carpeta.glob("lote-*.json")):
        lote = json.loads(archivo.read_text(encoding="utf-8"))
        for e in lote["textos"]:
            ms = por_clave.get((e["contexto"], e["fuente"], e.get("desambiguacion", "")))
            if not ms:
                faltan += 1
                continue
            if e.get("plural"):
                formas = e.get("traducciones") or []
                if not formas or not all(formas):
                    continue
                malo = next((_ok(e["fuente"], f) for f in formas if _ok(e["fuente"], f)), "")
            else:
                trad = e.get("traduccion", "")
                if not trad:
                    continue
                malo = _ok(e["fuente"], trad)
            if malo:
                rechazados += 1
                print(f"[lotes] {archivo.name} id {e['id']}: {malo}: {formas if e.get('plural') else trad!r}",
                      file=sys.stderr)
                continue
            for m in ms:
                t = m.find("translation")
                for hijo in list(t):
                    t.remove(hijo)
                if e.get("plural"):
                    t.text = None
                    for f in formas:
                        ET.SubElement(t, "numerusform").text = f
                else:
                    t.text = trad
                t.attrib.pop("type", None)
            puestos += 1
    arbol.write(ts, encoding="utf-8", xml_declaration=True)
    # El formato de lupdate (DOCTYPE, sangría): la próxima `make i18n-actualizar` lo deja igual.
    print(f"[lotes] {puestos} traducciones puestas, {rechazados} rechazadas, {faltan} que ya no están en el .ts")
    return 1 if rechazados else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("accion", choices=["exportar", "importar"])
    ap.add_argument("ts", type=pathlib.Path)
    ap.add_argument("carpeta", type=pathlib.Path)
    ap.add_argument("--por", type=int, default=400)
    ap.add_argument("--referencia", type=pathlib.Path, help="otro .ts: cada texto lleva cómo quedó en ese idioma")
    ap.add_argument("--todos", action="store_true",
                    help="exportar también lo ya traducido (con su traducción): para revisar o unificar términos")
    a = ap.parse_args()
    return exportar(a.ts, a.carpeta, a.por, a.todos, a.referencia) if a.accion == "exportar" else importar(a.ts, a.carpeta)


if __name__ == "__main__":
    sys.exit(main())
