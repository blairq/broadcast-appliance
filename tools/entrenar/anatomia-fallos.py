#!/usr/bin/env python3
"""Anatomia de cada error: ¿lo perdio por poco o estaba convencido?

LAS TRES PREGUNTAS QUE CONTESTA, que son distintas entre si:

1. ¿ES UN VOLADO? Si la probabilidad de la que eligio y la de la correcta son
   casi iguales, el error no es de conocimiento: los dos conceptos se pisan y
   cual sale depende del ruido. Eso NO se arregla con mas ejemplos de cada una
   por separado; se arregla ensenando el LIMITE (pares contrastivos) o
   aceptando que son la misma intencion y que el examen acepte las dos.
   Si en cambio la eligio con holgura, el modelo esta seguro y equivocado, que
   es otro problema: le falta la distincion, no le sobra ruido.

2. ¿SIRVE TOCAR LA TEMPERATURA? Contestado de antemano: NO, y conviene saber
   por que. Se decodifica por argmax (`do_sample=False`), y la temperatura
   divide todos los logits por igual: no cambia cual es el mayor. Es
   literalmente una operacion nula acá. Muestrear SI cambiaria algo, y para
   mal: convertiria los volados en volados de verdad. El margen que mide este
   script es lo que la temperatura tocaria si se muestreara, y por eso se
   informa: dice cuanto se perderia.

3. ¿EN QUE CAPA SE PIERDE? Con el logit lens —proyectar el estado oculto de
   cada capa por la cabeza de salida— se ve el ranking de la respuesta correcta
   capa por capa. Si la correcta viene arriba y cae al final, sobra algo de las
   ultimas capas. Si nunca estuvo arriba, el modelo no la considero nunca, y
   ahi el problema es el ejemplo y no la decodificacion.

Las probabilidades son sobre el NOMBRE ENTERO, no sobre el primer token: se
puntua cada nombre completo reusando el cache del prompt, que es exacto y
cuesta poco.

  tools/entrenar/anatomia-fallos.py --modelo ~/entrenamientos/reactivo-v7
"""
import argparse
import json
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(RAIZ / "tools"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
EXAMEN = RAIZ / "datos/entrenamiento/examen.jsonl"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--modelo", required=True)
    ap.add_argument("--largo", type=int, default=4096)
    ap.add_argument("--todos", action="store_true", help="tambien los que acierta")
    ap.add_argument("--json", help="guardar el detalle")
    args = ap.parse_args()

    from unsloth import FastLanguageModel  # noqa: E402
    import torch  # noqa: E402
    import lib_prompt as lp  # noqa: E402
    from vector_control_util import capas_del  # noqa: E402

    inv = lp.inventario()
    nombres = lp.nombres_posibles(inv)
    cabeza, cola = lp.partes(inv)
    modelo, tok = FastLanguageModel.from_pretrained(
        model_name=args.modelo, max_seq_length=args.largo, load_in_4bit=False, dtype=None)
    FastLanguageModel.for_inference(modelo)
    dev = modelo.device

    # La cabeza de salida y la norma final, para el logit lens.
    nucleo = modelo
    while not hasattr(nucleo, "lm_head") and hasattr(nucleo, "model"):
        nucleo = nucleo.model
    lm_head = nucleo.lm_head
    capas = capas_del(modelo)
    norma = None
    m = modelo
    for _ in range(6):
        if hasattr(m, "norm"):
            norma = m.norm
            break
        m = getattr(m, "model", getattr(m, "base_model", m))

    secuencias = {n: tok(n, add_special_tokens=False).input_ids for n in nombres}

    def puntuar(frase):
        """logprob de CADA nombre entero, reusando el cache del prompt."""
        ent = tok(cabeza + frase + cola, return_tensors="pt").to(dev)
        with torch.no_grad():
            sal = modelo(**ent, use_cache=True, output_hidden_states=True)
        cache, ultimo = sal.past_key_values, sal.logits[0, -1].float()
        ocultos = [h[0, -1, :] for h in sal.hidden_states[1:]]
        n_prompt = ent.input_ids.shape[1]
        base = torch.log_softmax(ultimo, -1)
        puntajes = {}
        # DE A UN TOKEN, y no de corrido: la ruta de inferencia rapida de
        # unsloth solo acepta q_len==1 cuando hay cache. Despues se recorta el
        # cache a lo que media el prompt, que es lo que deja reusarlo para el
        # nombre siguiente sin volver a leer los 2.650 tokens.
        for n, ids in secuencias.items():
            lp_ = base[ids[0]].item()
            for k in range(len(ids) - 1):
                t = torch.tensor([[ids[k]]], device=dev)
                with torch.no_grad():
                    s2 = modelo(input_ids=t, past_key_values=cache, use_cache=True)
                lp_ += torch.log_softmax(s2.logits[0, -1].float(), -1)[ids[k + 1]].item()
            cache.crop(n_prompt)
            puntajes[n] = lp_
        return puntajes, ocultos

    def lente(ocultos, ids_primeros):
        """Ranking del primer token de cada candidato, capa por capa."""
        out = []
        for h in ocultos:
            x = norma(h) if norma is not None else h
            with torch.no_grad():
                lg = lm_head(x.unsqueeze(0).to(lm_head.weight.dtype))[0].float()
            orden = torch.argsort(lg, descending=True)
            pos = {t: (orden == t).nonzero()[0, 0].item() for t in ids_primeros}
            out.append(pos)
        return out

    casos = [json.loads(l) for l in EXAMEN.read_text(encoding="utf-8").splitlines() if l.strip()]
    detalle, volados, seguros = [], 0, 0
    print(f"{len(casos)} casos · {len(nombres)} nombres\n")
    for c in casos:
        p, ocultos = puntuar(c["frase"])
        elegida = max(p, key=p.get)
        ok = elegida in c["aceptables"]
        if ok and not args.todos:
            continue
        mejor_ac = max((a for a in c["aceptables"] if a in p), key=lambda a: p[a], default=None)
        if mejor_ac is None:
            continue
        import math
        pe, pa = math.exp(p[elegida]), math.exp(p[mejor_ac])
        margen = pe / (pe + pa)  # 0,5 = volado perfecto
        # en que capa la correcta estuvo por delante de la elegida
        t_el, t_ac = secuencias[elegida][0], secuencias[mejor_ac][0]
        rangos = lente(ocultos, [t_el, t_ac])
        arriba = [i + 1 for i, r in enumerate(rangos) if r[t_ac] < r[t_el]]
        if not ok:
            volados += margen < 0.6
            seguros += margen > 0.9
            print(f"  {c['frase'][:38]:40s} {elegida:20s} vs {mejor_ac:20s}"
                  f" margen {margen:.2f}" +
                  ("  VOLADO" if margen < 0.6 else "  SEGURO Y ERRADO" if margen > 0.9 else "") +
                  (f"  · la correcta iba adelante hasta la capa {max(arriba)}" if arriba else
                   "  · la correcta NUNCA estuvo adelante"))
        detalle.append({"frase": c["frase"], "elegida": elegida, "correcta": mejor_ac,
                        "acierto": ok, "margen": margen,
                        "capas_con_la_correcta_adelante": arriba})
    fallos = [d for d in detalle if not d["acierto"]]
    if fallos:
        print(f"\nvolados (margen < 0,60): {volados}/{len(fallos)}")
        print(f"seguro y errado (margen > 0,90): {seguros}/{len(fallos)}")
        nunca = sum(1 for d in fallos if not d["capas_con_la_correcta_adelante"])
        print(f"la correcta NUNCA estuvo adelante en ninguna capa: {nunca}/{len(fallos)}")
        print("\nLa temperatura no toca nada de esto: se decodifica por argmax y dividir")
        print("todos los logits por igual no cambia cual es el mayor. Muestrear si")
        print("cambiaria, y para mal: los volados pasarian a salir cara o cruz de verdad.")
    if args.json:
        Path(args.json).write_text(json.dumps(detalle, ensure_ascii=False, indent=1), encoding="utf-8")


if __name__ == "__main__":
    main()
