#!/usr/bin/env python3
"""Que herramientas nuevas hay, y frases candidatas para ellas.

EL PROBLEMA QUE RESUELVE. El MCP crece: se agrega una herramienta y el corpus
de entrenamiento no se entera. El modelo entrenado sigue contestando —no falla
nada— pero esa herramienta no existe para el: nunca la vio como respuesta. Es
una degradacion silenciosa, y las silenciosas son las que hay que preguntar.

QUE HACE:
  · lista lo NUEVO (herramientas del repertorio sin una sola frase),
  · lista lo HUERFANO (frases para herramientas que ya no existen),
  · avisa si cambio la FIRMA DEL PROMPT respecto del conjunto ya generado, que
    es cuando el modelo entrenado quedo viejo aunque las frases esten,
  · y con --generar propone frases candidatas para lo nuevo.

DE DONDE SALEN LAS CANDIDATAS, y por que son candidatas y no dato. Salen de las
ETIQUETAS del buscador (McpEtiquetas.cpp), que son lo mas parecido a «como lo
pediria alguien» que hay escrito en el proyecto: las escribio una persona,
herramienta por herramienta, para que «hay acople» llegue a audio_set. Pero son
bolsas de palabras, no frases dichas. Sirven para arrancar y NO reemplazan a
alguien escribiendo lo que se grita en una cabina.

Por eso van a un archivo aparte (datos/semillas-auto.json) marcadas con su
origen: se ven, se corrigen y se promueven a mano. Mezclarlas en silencio con
las escritas seria perder de vista que la mitad del corpus es de peor calidad.

  tools/corpus-descubrir.py            # que falta
  tools/corpus-descubrir.py --generar  # ademas, propone
"""
import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lib_prompt as lp  # noqa: E402

RAIZ = Path(__file__).resolve().parent.parent
MANO = RAIZ / "datos/semillas-reactivas.json"
AUTO = RAIZ / "datos/semillas-auto.json"
HUELLA = RAIZ / "datos/entrenamiento/huella.json"

# Palabras que aparecen en todas las etiquetas y no distinguen nada.
VACIAS = {"el", "la", "los", "las", "un", "una", "de", "del", "al", "a", "que",
          "y", "o", "en", "se", "lo", "le", "por", "para", "con", "sin", "su",
          "esta", "este", "esa", "ese", "no", "si", "mas", "ya", "hay", "es"}


def frases_desde_etiquetas(tags, titulo):
    """Trozos de 2 a 4 palabras de las etiquetas, como pedidos cortos.

    Se cortan en trozos y no se usa la etiqueta entera porque la etiqueta es
    una bolsa —«silenciar mutear bajar subir volumen»— y eso nadie lo dice.
    Un trozo de tres palabras se parece bastante a un pedido de cabina.
    """
    palabras = [w for w in re.findall(r"[a-záéíóúñ]+", tags.lower()) if w not in VACIAS and len(w) > 2]
    out, i = [], 0
    while i < len(palabras):
        trozo = palabras[i:i + 3]
        if len(trozo) >= 2:
            out.append(" ".join(trozo))
        i += 3
    if titulo:
        out.append(titulo.lower())
    # Sin repetidas y sin las larguisimas.
    vistas, limpio = set(), []
    for f in out:
        if f not in vistas and len(f) <= 48:
            vistas.add(f)
            limpio.append(f)
    return limpio[:6]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--generar", action="store_true", help="proponer frases para lo nuevo")
    args = ap.parse_args()

    inv = lp.inventario()
    repertorio = set(lp.herramientas_reactivas(inv))
    por_nombre = {h["nombre"]: h for h in inv["herramientas"]}

    mano = json.loads(MANO.read_text(encoding="utf-8"))["herramientas"]
    auto = json.loads(AUTO.read_text(encoding="utf-8"))["herramientas"] if AUTO.exists() else {}
    con_frases = set(mano) | set(auto)

    nuevas = sorted(repertorio - con_frases)
    huerfanas = sorted(con_frases - repertorio)

    print(f"repertorio reactivo: {len(repertorio)} herramientas")
    print(f"  con frases escritas a mano: {len(set(mano) & repertorio)}")
    print(f"  con frases propuestas:      {len(set(auto) & repertorio)}")
    print(f"  SIN NINGUNA FRASE:          {len(nuevas)}")
    if nuevas:
        for n in nuevas:
            print(f"     - {n}: {por_nombre[n]['titulo']}")
    if huerfanas:
        print(f"  frases para herramientas que YA NO EXISTEN: {', '.join(huerfanas)}")

    # La firma: aunque no falte ninguna frase, si cambio el prompt el modelo
    # entrenado quedo viejo.
    h = lp.huella(inv)
    if HUELLA.exists():
        vieja = json.loads(HUELLA.read_text(encoding="utf-8"))
        if vieja.get("prompt") != h["prompt"]:
            print("\n  OJO: el prompt CAMBIO desde que se genero el conjunto.")
            print(f"       antes {vieja.get('prompt')} ({vieja.get('cuantas')} herramientas)"
                  f" · ahora {h['prompt']} ({h['cuantas']})")
            print("       Hay que volver a generar el corpus Y a entrenar: el modelo que")
            print("       tengas fue entrenado mostrandole otras fichas.")
        else:
            print(f"\n  el prompt no cambio ({h['prompt']}): el modelo entrenado sigue valiendo")
    else:
        print(f"\n  todavia no hay conjunto generado (firma actual {h['prompt']})")

    if not args.generar:
        if nuevas:
            print("\n  para proponer frases: tools/corpus-descubrir.py --generar")
        return

    if not nuevas:
        print("\nno hay nada nuevo que proponer")
        return

    propuestas = dict(auto)
    for n in nuevas:
        hh = por_nombre[n]
        tags = (hh["etiquetas"]["propias"] or "") + " " + (hh["etiquetas"]["porPrefijo"] or "")
        fr = frases_desde_etiquetas(tags, hh["titulo"])
        if not fr:
            continue
        propuestas[n] = {"frases": fr, "tambien": [], "origen": "etiquetas", "revisado": False}

    doc = {
        "_comentario": [
            "PROPUESTAS, no dato. Las genero tools/corpus-descubrir.py a partir de las",
            "etiquetas del buscador: son bolsas de palabras, no frases dichas por nadie.",
            "",
            "Que hacer con esto: leerlas, corregirlas para que suenen a lo que se grita en",
            "una cabina, y MOVERLAS a datos/semillas-reactivas.json. Cuando una entrada",
            "pasa de aca para alla, se le pone `revisado` y deja de ser una propuesta.",
            "",
            "Entran igual al entrenamiento —es mejor que la herramienta no exista para el",
            "modelo— pero estan aca aparte para que se vea cuanto del corpus es de segunda.",
        ],
        "herramientas": propuestas,
    }
    AUTO.write_text(json.dumps(doc, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"\npropuestas para {len(nuevas)} herramientas -> {AUTO.relative_to(RAIZ)}")
    print("LEERLAS Y CORREGIRLAS: salen de bolsas de palabras, no de como habla alguien.")
    for n in nuevas[:4]:
        if n in propuestas:
            print(f"   {n}: {propuestas[n]['frases'][:3]}")


if __name__ == "__main__":
    main()
