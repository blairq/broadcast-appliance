#!/usr/bin/env python3
"""El acierto del examen leyendo la respuesta desde CADA capa.

DE DONDE SALE. La anatomia de los fallos de v7 mostro que en 15 de 16 la
respuesta correcta iba ganando adentro del modelo, y en 11 seguia ganando hasta
las ultimas ocho capas: la perdia al final. Si eso es sistematico, hay una capa
intermedia que acierta MAS que la ultima, y estamos tirando puntos.

COMO SE MIDE. Logit lens: se toma el estado oculto de la capa L, se le pasa la
norma final y la cabeza de salida, y eso da los logits que ESA capa produciria.
Con eso se puntua cada uno de los 86 nombres enteros y se elige el mejor. O sea
el mismo examen, 28 veces, una por capa.

Se puntua el nombre COMPLETO y no el primer token: `capture_feed` y
`foto_programa` comparten el primero, asi que mirar solo ese no decide nada.
El prompt se lee una vez y se reusa su cache para los 86 nombres.

LO QUE HAY QUE MIRAR. Si la curva sube y baja —pico en 24, caida en 28— hay dos
salidas y las dos son baratas: leer desde la capa del pico, o contrastar la capa
madura contra una prematura al estilo DoLa. Si la curva sube monotona hasta el
final, los fallos de la anatomia eran casos sueltos y no un patron, y esto se
cierra acá.

  tools/entrenar/acierto-por-capa.py --modelo ~/entrenamientos/reactivo-v7
"""
import argparse
import json
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(RAIZ / "tools" / "entrenar"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
EXAMEN = RAIZ / "datos/entrenamiento/examen.jsonl"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--modelo", required=True)
    ap.add_argument("--json")
    args = ap.parse_args()

    import torch
    from transformers import AutoModelForCausalLM, AutoTokenizer
    from peft import PeftModel
    import lib_prompt as lp
    from vector_control_util import capas_del

    inv = lp.inventario()
    nombres = lp.nombres_posibles(inv)
    cabeza, cola = lp.partes(inv)
    cfg = json.loads((Path(args.modelo) / "adapter_config.json").read_text())
    tok = AutoTokenizer.from_pretrained(args.modelo)
    modelo = AutoModelForCausalLM.from_pretrained(
        cfg["base_model_name_or_path"], dtype=torch.bfloat16, device_map="cuda:0")
    modelo = PeftModel.from_pretrained(modelo, args.modelo)
    modelo.eval()
    dev = modelo.device

    nucleo = modelo
    while not hasattr(nucleo, "lm_head") and hasattr(nucleo, "model"):
        nucleo = nucleo.model
    lm_head = nucleo.lm_head
    norma, m = None, modelo
    for _ in range(6):
        if hasattr(m, "norm"):
            norma = m.norm
            break
        m = getattr(m, "model", getattr(m, "base_model", m))
    n_capas = len(capas_del(modelo))
    # LA ULTIMA ENTRADA DE hidden_states YA VIENE NORMALIZADA. En transformers,
    # el modelo hace `hidden_states = self.norm(hidden_states)` y DESPUES la
    # agrega a la tupla, asi que hidden_states[-1] esta post-norma. Aplicarle la
    # norma otra vez —lo que hacia la primera version de esto— da norm(norm(h)),
    # que no es lo que el modelo calcula: la «capa 28» daba 73,6% donde el
    # decodificador real da 84,9%, y de ahi salio la falsa caida de diez puntos
    # en las ultimas capas.
    #
    # El residual PRE-norma de la ultima capa no esta en la tupla, asi que se
    # saca con un gancho en la norma final. Con eso las 28 capas se leen igual:
    # una sola norma, la que el modelo aplica.
    pre_norma = {}
    if norma is not None:
        norma.register_forward_pre_hook(
            lambda _m, ent: pre_norma.__setitem__("x", ent[0].detach()))
    secuencias = {n: tok(n, add_special_tokens=False).input_ids for n in nombres}

    def logits_por_capa(ocultos):
        """[capas, posiciones, vocabulario] en log-probabilidades."""
        out = []
        for h in ocultos:
            x = norma(h) if norma is not None else h
            with torch.no_grad():
                lg = lm_head(x.to(lm_head.weight.dtype)).float()
            out.append(torch.log_softmax(lg, -1))
        return out

    casos = [json.loads(l) for l in EXAMEN.read_text(encoding="utf-8").splitlines() if l.strip()]
    aciertos = [0] * n_capas
    detalle = []
    for j, c in enumerate(casos):
        ent = tok(cabeza + c["frase"] + cola, return_tensors="pt").to(dev)
        n_prompt = ent.input_ids.shape[1]
        with torch.no_grad():
            sal = modelo(**ent, use_cache=True, output_hidden_states=True)
        cache = sal.past_key_values
        # El primer token de cada nombre sale de la ultima posicion del prompt.
        ocultos = list(sal.hidden_states[1:n_capas]) + [pre_norma["x"]]
        primeros = logits_por_capa([h[:, -1:, :] for h in ocultos])
        puntajes = torch.zeros(n_capas, len(nombres))
        for i, (n, ids) in enumerate(secuencias.items()):
            for L in range(n_capas):
                puntajes[L, i] = primeros[L][0, 0, ids[0]].item()
            if len(ids) > 1:
                t = torch.tensor([ids[:-1]], device=dev)
                with torch.no_grad():
                    s2 = modelo(input_ids=t, past_key_values=cache, use_cache=True,
                                output_hidden_states=True)
                sig = logits_por_capa(list(s2.hidden_states[1:n_capas]) + [pre_norma["x"]])
                for L in range(n_capas):
                    for k, tid in enumerate(ids[1:]):
                        puntajes[L, i] += sig[L][0, k, tid].item()
                cache.crop(n_prompt)
        lista = list(secuencias)
        elegidas = [lista[int(puntajes[L].argmax())] for L in range(n_capas)]
        for L, e in enumerate(elegidas):
            aciertos[L] += e in c["aceptables"]
        detalle.append({"frase": c["frase"], "por_capa": elegidas,
                        "aceptables": c["aceptables"]})
        if (j + 1) % 20 == 0:
            print(f"  {j+1}/{len(casos)}", flush=True)

    print(f"\nacierto leyendo desde cada capa ({len(casos)} casos):")
    mejor = max(range(n_capas), key=lambda L: aciertos[L])
    for L in range(n_capas):
        a = aciertos[L]
        barra = "#" * round(40 * a / len(casos))
        marca = "  <- LA MEJOR" if L == mejor else ("  (la que usamos)" if L == n_capas - 1 else "")
        print(f"  capa {L+1:2d}: {a:3d}/{len(casos)} {100*a/len(casos):5.1f}% {barra}{marca}")
    ult = aciertos[-1]
    print(f"\nultima capa: {ult}/{len(casos)} ({100*ult/len(casos):.1f}%)")
    print(f"mejor capa:  {aciertos[mejor]}/{len(casos)} ({100*aciertos[mejor]/len(casos):.1f}%)"
          f" en la capa {mejor+1}")
    if aciertos[mejor] > ult:
        print(f"\nHAY {aciertos[mejor]-ult} CASOS TIRADOS: el modelo los tiene bien en la capa"
              f" {mejor+1} y los pierde antes de contestar.")
    else:
        print("\nNo hay capa mejor que la ultima: los fallos de la anatomia eran casos")
        print("sueltos y no un patron. Por aca no hay nada que ganar.")
    if args.json:
        Path(args.json).write_text(json.dumps(
            {"aciertos_por_capa": aciertos, "casos": len(casos), "detalle": detalle},
            ensure_ascii=False, indent=1), encoding="utf-8")


if __name__ == "__main__":
    main()
