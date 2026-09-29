#!/usr/bin/env python3
"""QUIEN pisa la respuesta correcta en las ultimas capas.

LA PREGUNTA. La anatomia mostro que en 11 de 16 fallos la correcta ganaba hasta
la capa 24 y la perdia al final. Falta el mecanismo: no que pasa, sino quien lo
hace.

COMO SE AVERIGUA. El flujo residual es una suma: cada capa le agrega la salida
de su atencion y la de su MLP. Si se toma la direccion que SEPARA los dos
nombres —la fila de la cabeza de salida del token correcto menos la del token
elegido— y se proyecta sobre ella la contribucion de cada bloque, sale cuanto
empujo cada uno para cada lado, en unidades de diferencia de logit. Es
atribucion directa al logit, y como la suma de todas las contribuciones da la
diferencia final, se puede auditar que cierre.

SE MIRA EN LA POSICION DONDE LOS NOMBRES SE SEPARAN, no en la ultima del
prompt: si comparten prefijo (capture_feed y capture_program), la decision no
existe hasta el segundo token.

LA HIPOTESIS QUE TAMBIEN PRUEBA. Si las ultimas capas estuvieran aplicando el
PRIOR DE FRECUENCIA —«ante la duda, la clase que mas vi»— el empuje final
deberia correlacionar con cuantos ejemplos de entrenamiento tiene cada nombre.
Eso se mide acá mismo y se informa.

  tools/entrenar/mecanismo-fallos.py --modelo ~/entrenamientos/reactivo-v7
"""
import argparse
import collections
import json
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(RAIZ / "tools"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
EXAMEN = RAIZ / "datos/entrenamiento/examen.jsonl"
ENTRENAR = RAIZ / "datos/entrenamiento/entrenar.jsonl"


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
    W = nucleo.lm_head.weight
    norma, m = None, modelo
    for _ in range(6):
        if hasattr(m, "norm"):
            norma = m.norm
            break
        m = getattr(m, "model", getattr(m, "base_model", m))
    capas = capas_del(modelo)
    n_capas = len(capas)
    secuencias = {n: tok(n, add_special_tokens=False).input_ids for n in nombres}
    frec = collections.Counter(json.loads(l)["herramienta"]
                               for l in ENTRENAR.read_text(encoding="utf-8").splitlines() if l.strip())

    # Ganchos: la salida de la atencion y la del MLP de cada capa.
    partes = {}

    def poner(i, cual):
        def fn(_m, _e, sal):
            partes[(i, cual)] = (sal[0] if isinstance(sal, tuple) else sal)[0, -1, :].detach().float()
        return fn

    ganchos = []
    for i, capa in enumerate(capas):
        ganchos.append(capa.self_attn.register_forward_hook(poner(i, "atencion")))
        ganchos.append(capa.mlp.register_forward_hook(poner(i, "mlp")))

    casos = [json.loads(l) for l in EXAMEN.read_text(encoding="utf-8").splitlines() if l.strip()]
    suma = {("atencion", i): 0.0 for i in range(n_capas)}
    suma.update({("mlp", i): 0.0 for i in range(n_capas)})
    n_fallos, filas, mas_frecuente = 0, [], 0

    for c in casos:
        ent = tok(cabeza + c["frase"] + cola, return_tensors="pt").to(dev)
        with torch.no_grad():
            sal = modelo(**ent)
        lg = sal.logits[0, -1].float()
        # Elegida y correcta por el primer token, que es lo que decide la rama.
        cand = {n: lg[secuencias[n][0]].item() for n in nombres}
        elegida = max(cand, key=cand.get)
        if elegida in c["aceptables"]:
            continue
        mejor_ac = max((a for a in c["aceptables"] if a in cand), key=lambda a: cand[a], default=None)
        if mejor_ac is None:
            continue
        ia, ib = secuencias[elegida], secuencias[mejor_ac]
        k = 0
        while k < min(len(ia), len(ib)) and ia[k] == ib[k]:
            k += 1
        if k >= len(ia) or k >= len(ib):
            continue
        ids = torch.cat([ent.input_ids, torch.tensor([ia[:k]], device=dev)], 1) if k else ent.input_ids
        partes.clear()
        with torch.no_grad():
            sal = modelo(input_ids=ids, output_hidden_states=True)
        final = sal.hidden_states[-1][0, -1, :].float()
        # La escala de la RMSNorm final, congelada: es lo que vuelve comparable
        # cada contribucion con la diferencia de logit que sale al final.
        with torch.no_grad():
            escala = (norma(final.to(W.dtype)).float().norm() / final.norm()).item() if norma else 1.0
        d = (W[ib[k]].float() - W[ia[k]].float()) * escala   # + = hacia la CORRECTA
        n_fallos += 1
        mas_frecuente += frec.get(elegida, 0) > frec.get(mejor_ac, 0)
        det = {}
        for i in range(n_capas):
            for cual in ("atencion", "mlp"):
                v = torch.dot(partes[(i, cual)], d).item()
                suma[(cual, i)] += v
                det[f"{cual}{i+1}"] = v
        filas.append({"frase": c["frase"], "elegida": elegida, "correcta": mejor_ac,
                      "frec_elegida": frec.get(elegida, 0), "frec_correcta": frec.get(mejor_ac, 0),
                      "contribuciones": det})

    for g in ganchos:
        g.remove()
    if not n_fallos:
        print("sin fallos")
        return

    print(f"{n_fallos} fallos · contribucion MEDIA a la diferencia de logit")
    print("(+ empuja hacia la CORRECTA, - hacia la que eligio)\n")
    print("  capa   atencion      mlp")
    for i in range(n_capas):
        a, m_ = suma[("atencion", i)] / n_fallos, suma[("mlp", i)] / n_fallos
        def barra(v):
            n = min(18, round(abs(v) * 3))
            return ("<" * n).rjust(18) if v < 0 else ("-" * 0) + (">" * n).ljust(18)
        print(f"  {i+1:4d} {a:+8.2f} {m_:+8.2f}   {barra(a+m_)}")
    tot_a = sum(suma[("atencion", i)] for i in range(n_capas)) / n_fallos
    tot_m = sum(suma[("mlp", i)] for i in range(n_capas)) / n_fallos
    print(f"\n  total atencion {tot_a:+.2f} · total mlp {tot_m:+.2f}")
    ult = range(n_capas - 6, n_capas)
    ua = sum(suma[("atencion", i)] for i in ult) / n_fallos
    um = sum(suma[("mlp", i)] for i in ult) / n_fallos
    print(f"  ultimas 6 capas: atencion {ua:+.2f} · mlp {um:+.2f}")
    peor = min(((cual, i) for i in range(n_capas) for cual in ("atencion", "mlp")),
               key=lambda k: suma[k])
    print(f"  el que mas empuja hacia el error: {peor[0]} de la capa {peor[1]+1}"
          f" ({suma[peor]/n_fallos:+.2f})")
    print(f"\n  PRIOR DE FRECUENCIA: en {mas_frecuente} de {n_fallos} fallos, la que eligio")
    print(f"  tiene MAS ejemplos de entrenamiento que la correcta"
          f" ({100*mas_frecuente/n_fallos:.0f}%; al azar seria 50%)")
    if args.json:
        Path(args.json).write_text(json.dumps(filas, ensure_ascii=False, indent=1), encoding="utf-8")


if __name__ == "__main__":
    main()
