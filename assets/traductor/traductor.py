"""El traductor del programa (agent/Traductor.cpp lo corre con el Python del bundle).

Traduce frases sueltas, sin red, con los modelos OPUS-MT empaquetados por Argos
Translate (CTranslate2 + SentencePiece o BPE). No usa la biblioteca
argostranslate: arrastra stanza y torch, y para una frase de subtítulo alcanza
con el modelo y su tokenizador.

Una línea JSON por pedido en stdin, una por respuesta en stdout:

  {"id": 1, "op": "traducir", "texto": "...", "de": "es", "a": "en",
   "pasos": [{"de": "es", "a": "en", "paquete": "/ruta/al/.argosmodel"}, ...],
   "glosario": [{"es": ["cuarto intermedio"], "en": "recess", ...}, ...]}
    -> {"id": 1, "texto": "...", "ms": 312, "pasos": ["es>en", "en>fr"]}
  {"op": "salir"}

PASOS. Si no hay paquete directo, la app manda la cadena (es>en, en>fr): la
salida de uno es la entrada del siguiente.

GLOSARIO. Antes de cada paso, los términos del idioma de origen se reemplazan
por su traducción al idioma de destino de ESE paso (medido: meter el término ya
traducido en la frase es lo que mejor respeta el modelo; un marcador como «[1]»
lo pierde). En una cadena, el término en inglés del primer paso se vuelve a
reemplazar por el del idioma final en el segundo.

Un error en una frase vuelve como {"id", "error"}; el proceso sigue.
"""
from __future__ import annotations

import json
import os
import re
import sys
import time
import traceback
import zipfile

import ctranslate2
import sentencepiece as spm

_salida = os.fdopen(os.dup(1), "w", encoding="utf-8", buffering=1)
os.dup2(2, 1)  # un print() de una biblioteca no rompe el protocolo


def responder(m: dict) -> None:
    _salida.write(json.dumps(m, ensure_ascii=False) + "\n")
    _salida.flush()


class Paquete:
    """Un .argosmodel: se descomprime una vez al lado del archivo y se carga."""

    def __init__(self, ruta: str, de: str, a: str) -> None:
        self.de, self.a = de, a
        dest = ruta + ".d"
        if not os.path.isdir(dest):
            tmp = dest + ".tmp"
            with zipfile.ZipFile(ruta) as z:
                z.extractall(tmp)
            os.replace(tmp, dest)
        raiz = next(os.path.join(dest, n) for n in os.listdir(dest) if os.path.isdir(os.path.join(dest, n)))
        self.traductor = ctranslate2.Translator(os.path.join(raiz, "model"), device="cpu", intra_threads=4)
        if os.path.exists(os.path.join(raiz, "sentencepiece.model")):
            self.sp = spm.SentencePieceProcessor(model_file=os.path.join(raiz, "sentencepiece.model"))
            self.bpe = None
        else:  # los más viejos: Moses + BPE (subword-nmt)
            from sacremoses import MosesDetokenizer, MosesPunctNormalizer, MosesTokenizer
            from subword_nmt.apply_bpe import BPE

            self.sp = None
            with open(os.path.join(raiz, "bpe.model"), encoding="utf-8") as f:
                self.bpe = BPE(f)
            self.tok, self.norm, self.detok = MosesTokenizer(lang=de), MosesPunctNormalizer(lang=de), MosesDetokenizer(lang=a)

    def traducir(self, texto: str) -> str:
        if self.sp is not None:
            piezas = self.sp.encode(texto, out_type=str)
        else:
            piezas = self.bpe.process_line(" ".join(self.tok.tokenize(self.norm.normalize(texto), escape=False))).split()
        r = self.traductor.translate_batch([piezas], beam_size=2, max_decoding_length=256)
        salida = r[0].hypotheses[0]
        if self.sp is not None:
            # Algunos paquetes traen piezas que el .model no conoce: se arma a mano.
            return "".join(salida).replace("▁", " ").strip()
        return self.detok.detokenize(" ".join(salida).replace("@@ ", "").split())


_paquetes: dict[str, Paquete] = {}


def paquete(p: dict) -> Paquete:
    if p["paquete"] not in _paquetes:
        _paquetes[p["paquete"]] = Paquete(p["paquete"], p["de"], p["a"])
    return _paquetes[p["paquete"]]


def _patron(termino: str) -> re.Pattern:
    # Palabra entera, sin importar mayúsculas.
    return re.compile(r"(?<!\w)" + re.escape(termino) + r"(?!\w)", re.IGNORECASE)


def latino(s: str) -> bool:
    return all(ord(c) < 0x250 for c in s if c.isalpha())


def aplicar_glosario(texto: str, glosario: list, de: str, a: str) -> str:
    """Reemplaza los términos en `de` por su traducción en `a` (los más largos primero).

    Solo si el término destino es de escritura latina: meter cirílico o chino en
    una frase en inglés rompe al modelo (medido: «<unk>», frases inventadas). Para
    ruso y chino vale el reemplazo del paso anterior (castellano -> inglés)."""
    if not latino(" ".join(str(e.get(a, "")) for e in glosario[:1])) and a in ("ru", "zh"):
        return texto
    pares = []
    for e in glosario:
        destino = e.get(a)
        if not destino:
            continue
        origen = e.get(de)
        for t in origen if isinstance(origen, list) else [origen] if origen else []:
            if t:
                pares.append((t, destino))
    for t, d in sorted(pares, key=lambda x: -len(x[0])):
        texto = _patron(t).sub(d, texto)
    return texto


def traducir(m: dict) -> dict:
    t0 = time.time()
    texto = (m.get("texto") or "").strip()
    if not texto:
        return {"texto": ""}
    glosario = m.get("glosario") or []
    final = m.get("a", "")
    for p in m.get("pasos") or []:
        # Al ir a un idioma intermedio, el término va directo en el idioma FINAL
        # si el modelo lo deja pasar tal cual; si no hay, en el intermedio.
        texto = aplicar_glosario(texto, glosario, p["de"], p["a"])
        texto = paquete(p).traducir(texto)
    if final and m.get("pasos") and m["pasos"][-1]["a"] != final:
        return {"error": "la cadena no termina en el idioma pedido"}
    return {"texto": texto, "ms": round((time.time() - t0) * 1000), "pasos": [f"{p['de']}>{p['a']}" for p in m.get("pasos") or []]}


def main() -> None:
    responder({"ev": "listo", "ctranslate2": ctranslate2.__version__})
    for linea in sys.stdin:
        if not linea.strip():
            continue
        try:
            m = json.loads(linea)
        except ValueError:
            continue
        if m.get("op") == "salir":
            break
        try:
            r = traducir(m) if m.get("op") == "traducir" else {"error": "op desconocida"}
        except Exception as e:  # noqa: BLE001 — una frase mala no tira el proceso
            traceback.print_exc(file=sys.stderr)
            r = {"error": f"{type(e).__name__}: {e}"}
        r["id"] = m.get("id")
        responder(r)


if __name__ == "__main__":
    main()
