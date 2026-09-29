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
sys.path.insert(0, str(Path(__file__).resolve().parent))
EXAMEN = RAIZ / "datos/entrenamiento/examen.jsonl"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--modelo", required=True)
    ap.add_argument("--largo", type=int, default=4096)
    ap.add_argument("--json", default="", help="donde dejar los fallos, para tools/entrenar/fallos.py")
    ap.add_argument("--ejemplos", type=int, default=0,
                    help="pares resueltos en el prefijo (para comparar con el aprendizaje en contexto)")
    ap.add_argument("--sesgo", type=float, default=0.0,
                    help="cuanto se le suma al logit de `derivar` (el dial simple)")
    ap.add_argument("--vector", help="vector de control (.gguf) de vector-control.py")
    ap.add_argument("--escala", type=float, default=1.0,
                    help="cuanto se empuja. 0 = como si no estuviera")
    args = ap.parse_args()

    from unsloth import FastLanguageModel  # noqa: E402
    import torch  # noqa: E402
    import lib_prompt as lp  # noqa: E402

    inv = lp.inventario()
    nombres = lp.nombres_posibles(inv)  # las herramientas Y `derivar`
    cabeza, cola = lp.partes(inv, args.ejemplos)

    # ¿Este modelo fue entrenado con ESTE prompt? Si no, el numero no compara
    # con nada: se le estan mostrando otras fichas.
    hp = Path(args.modelo) / "huella-prompt.json"
    if hp.exists():
        vieja = json.loads(hp.read_text(encoding="utf-8"))
        ahora = lp.huella(inv, args.ejemplos)
        if vieja.get("prompt") != ahora["prompt"]:
            print(f"\n  OJO: este modelo se entreno con OTRO prompt.")
            print(f"       entrenado con {vieja.get('prompt')} ({vieja.get('cuantas')} herramientas)")
            print(f"       midiendo con  {ahora['prompt']} ({ahora['cuantas']})")
            print("       El numero que salga no compara con los anteriores.\n")

    modelo, tok = FastLanguageModel.from_pretrained(
        model_name=args.modelo, max_seq_length=args.largo, load_in_4bit=False, dtype=None)
    FastLanguageModel.for_inference(modelo)

    # EL VECTOR DE CONTROL, aplicado igual que en llama.cpp: se SUMA a la salida
    # de cada capa, escalado. Se hace aca tambien —y no solo al servir— porque
    # la escala hay que elegirla mirando la curva de cobertura contra precision,
    # y esa curva se mide con el mismo evaluador que todo lo demas. Medir en un
    # lado y servir en el otro es como se pierden cuarenta puntos sin enterarse.
    ganchos = []
    if args.vector and args.escala != 0.0:
        import gguf  # noqa: E402
        from vector_control_util import capas_del  # noqa: E402
        lector = gguf.GGUFReader(args.vector)
        dirs = {}
        for t in lector.tensors:
            if t.name.startswith("direction."):
                dirs[int(t.name.split(".")[1])] = torch.tensor(t.data.copy())
        capas = capas_del(modelo)
        if len(dirs) != len(capas):
            print(f"  OJO: el vector tiene {len(dirs)} capas y el modelo {len(capas)}")

        def enganchar(i):
            def fn(_mod, _ent, sal):
                v = dirs[i + 1]
                if v is None:
                    return sal
                hs = sal[0] if isinstance(sal, tuple) else sal
                hs = hs + args.escala * v.to(hs.device, hs.dtype)
                return (hs,) + tuple(sal[1:]) if isinstance(sal, tuple) else hs
            return fn

        for i, capa in enumerate(capas):
            if (i + 1) in dirs:
                ganchos.append(capa.register_forward_hook(enganchar(i)))
        print(f"vector de control: {args.vector} · escala {args.escala} "
              f"· {len(ganchos)} capas")

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

    # EL DIAL SIMPLE: sesgar el logit de `derivar`.
    #
    # Por que esto y no el vector de control. La tarea es elegir UN nombre de
    # una lista cerrada, asi que lo que decide esta en el logit de un token, no
    # desparramado en el texto. Sumarle una constante a ese logit mueve el
    # punto de operacion de forma MONOTONA y sin tocar nada mas: es el dial que
    # el vector prometia, pero con garantia en vez de esperanza.
    #
    # `derivar` empieza con un token que ninguna herramienta comparte, asi que
    # sesgarlo no arrastra a ninguna otra.
    tok_derivar = tok(lp.DERIVAR, add_special_tokens=False).input_ids[0]
    def sesgar(ids, puntajes):
        if args.sesgo and ids.shape[-1] == largo_prompt[0]:
            puntajes[:, tok_derivar] += args.sesgo
        return puntajes

    largo_prompt = [0]
    casos = [json.loads(l) for l in EXAMEN.read_text(encoding="utf-8").splitlines() if l.strip()]
    aciertos, fallos, elegidas, t0 = 0, [], {}, time.time()
    for c in casos:
        ent = tok(cabeza + c["frase"] + cola, return_tensors="pt").to(modelo.device)
        largo_prompt[0] = ent.input_ids.shape[1]
        with torch.no_grad():
            sal = modelo.generate(**ent, max_new_tokens=12, do_sample=False,
                                  prefix_allowed_tokens_fn=permitidos,
                                  logits_processor=[sesgar] if args.sesgo else None,
                                  pad_token_id=tok.pad_token_id or fin)
        elegida = tok.decode(sal[0][largo_prompt[0]:], skip_special_tokens=True).strip()
        elegidas[c["frase"]] = elegida
        if elegida in c["aceptables"]:
            aciertos += 1
        else:
            fallos.append((c["frase"], elegida, c["aceptables"]))

    for g in ganchos:
        g.remove()
    print(f"\nmodelo: {args.modelo} · ejemplos en contexto: {args.ejemplos}"
          + (f" · escala {args.escala}" if args.vector else "")
          + (f" · sesgo {args.sesgo:+.1f}" if args.sesgo else ""))
    # EL ACIERTO SOLO YA NO ALCANZA, desde que sabe derivar. Un modelo que
    # deriva todo saca el 28% y parece prudente; uno que no deriva nunca saca
    # 72% y parece mejor de lo que es. Y los dos errores cuestan distinto:
    # derivar de mas cuesta SEGUNDOS —el grande lo resuelve igual—, elegir mal
    # cuesta AL AIRE. Por eso van separados:
    #   COBERTURA  de lo que SI es suyo, cuanto atendio el mismo (lo que
    #              derivo de mas es velocidad perdida, no pedido perdido).
    #   PRECISION  de lo que atendio, cuanto acerto (esto es lo que se ve en
    #              pantalla, y es el numero que no se puede bajar).
    #   DERIVADO   de lo que no es suyo, cuanto paso bien.
    suyos = [c for c in casos if lp.DERIVAR not in c["aceptables"]]
    ajenos = [c for c in casos if lp.DERIVAR in c["aceptables"]]
    atendidos = [c for c in suyos if elegidas[c["frase"]] != lp.DERIVAR]
    bien = [c for c in atendidos if elegidas[c["frase"]] in c["aceptables"]]
    rechazados = [c for c in ajenos if elegidas[c["frase"]] == lp.DERIVAR]
    if ajenos:
        print(f"cobertura: {len(atendidos)}/{len(suyos)} "
              f"({100*len(atendidos)//max(1,len(suyos))}%) de lo suyo lo atendio solo")
        print(f"precision: {len(bien)}/{len(atendidos)} "
              f"({100*len(bien)//max(1,len(atendidos))}%) de lo que atendio lo acerto")
        print(f"derivado: {len(rechazados)}/{len(ajenos)} "
              f"({100*len(rechazados)//max(1,len(ajenos))}%) de lo ajeno lo paso al grande")
    print(f"acierto: {aciertos}/{len(casos)}  ({100*aciertos//len(casos)}%)"
          f"  ·  {(time.time()-t0)/len(casos):.2f} s por caso (3090, bf16)")
    if fallos:
        print(f"\nfallos ({len(fallos)}):")
        for f, e, ac in fallos[:20]:
            print(f"  {f[:44]:46s} -> {e:22s} (esperaba {', '.join(ac)})")
    if args.json:
        Path(args.json).write_text(json.dumps(
            {"modelo": args.modelo, "aciertos": aciertos, "casos": len(casos),
             "fallos": [{"frase": f, "elegida": e, "aceptables": ac} for f, e, ac in fallos]},
            ensure_ascii=False, indent=1), encoding="utf-8")
        print(f"\nfallos -> {args.json}  (clasificarlos: tools/entrenar/fallos.py)")


if __name__ == "__main__":
    main()
