#!/usr/bin/env python3
"""El examen contra el modelo entrenado, EN EL SERVIDOR (rapido, para iterar).

LA MEDICION QUE VALE ES LA OTRA. Esta corre en la 3090 y en bf16; el aparato
corre un GGUF cuantizado en la CPU de una notebook. Sirve para iterar sin
esperar veinte minutos por cada idea, pero el numero final se saca con
`tools/bateria-local.py` en la maquina de verdad. Cuantizar cambia el acierto:
medido sin entrenar, Q4 58% y Q8 62%.

RESPUESTAS RESTRINGIDAS, igual que en produccion. Alla una gramatica impide
escribir algo que no sea un nombre de herramienta; aca se hace lo mismo con
`prefix_allowed_tokens_fn`. Si se evaluara SIN restringir, el numero saldria
peor que el real y por un motivo que en produccion no existe.

ACIERTO CON CONJUNTO ACEPTABLE. Varias preguntas admiten mas de una respuesta
buena —ante un sintoma, medir antes de tocar vale— y contar solo una como
correcta mide mal. El corpus ya trae esa lista.

  tools/entrenar/evaluar.py --modelo modelos/reactivo-v1
  tools/entrenar/evaluar.py --modelo unsloth/Qwen3-1.7B     # la linea de base
"""
import argparse
import json
import sys
import time
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(RAIZ / "tools"))
EXAMEN = RAIZ / "datos/entrenamiento/examen.jsonl"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--modelo", required=True)
    ap.add_argument("--largo", type=int, default=4096)
    ap.add_argument("--ejemplos", type=int, default=0,
                    help="pares resueltos en el prefijo (para comparar con el aprendizaje en contexto)")
    args = ap.parse_args()

    from unsloth import FastLanguageModel  # noqa: E402
    import torch  # noqa: E402
    import lib_prompt as lp  # noqa: E402

    inv = lp.inventario()
    nombres = lp.herramientas_reactivas(inv)
    cabeza, cola = lp.partes(inv, args.ejemplos)

    modelo, tok = FastLanguageModel.from_pretrained(
        model_name=args.modelo, max_seq_length=args.largo, load_in_4bit=False, dtype=None)
    FastLanguageModel.for_inference(modelo)

    # El arbol de continuaciones validas: desde cada prefijo de tokens, que
    # tokens dejan llegar a un nombre que EXISTE. Es la gramatica, en HF.
    secuencias = [tok(n, add_special_tokens=False).input_ids for n in nombres]
    fin = tok.eos_token_id
    def permitidos(_, ids):
        generado = ids.tolist()[largo_prompt[0]:]
        opciones = set()
        for s in secuencias:
            if s[:len(generado)] == generado:
                opciones.add(s[len(generado)] if len(s) > len(generado) else fin)
        return sorted(opciones) or [fin]

    largo_prompt = [0]
    casos = [json.loads(l) for l in EXAMEN.read_text(encoding="utf-8").splitlines() if l.strip()]
    aciertos, fallos, t0 = 0, [], time.time()
    for c in casos:
        ent = tok(cabeza + c["frase"] + cola, return_tensors="pt").to(modelo.device)
        largo_prompt[0] = ent.input_ids.shape[1]
        with torch.no_grad():
            sal = modelo.generate(**ent, max_new_tokens=12, do_sample=False,
                                  prefix_allowed_tokens_fn=permitidos,
                                  pad_token_id=tok.pad_token_id or fin)
        elegida = tok.decode(sal[0][largo_prompt[0]:], skip_special_tokens=True).strip()
        if elegida in c["aceptables"]:
            aciertos += 1
        else:
            fallos.append((c["frase"], elegida, c["aceptables"]))

    print(f"\nmodelo: {args.modelo} · ejemplos en contexto: {args.ejemplos}")
    print(f"acierto: {aciertos}/{len(casos)}  ({100*aciertos//len(casos)}%)"
          f"  ·  {(time.time()-t0)/len(casos):.2f} s por caso (3090, bf16)")
    if fallos:
        print(f"\nfallos ({len(fallos)}):")
        for f, e, ac in fallos[:20]:
            print(f"  {f[:44]:46s} -> {e:22s} (esperaba {', '.join(ac)})")


if __name__ == "__main__":
    main()
