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
    ap.add_argument("--amortiguar", default="",
                    help="CAPAS:FACTOR, p.ej. 27,28:0.5 · 0 = como si la capa no estuviera")
    ap.add_argument("--solo", default="atencion,mlp",
                    help="que bloques amortiguar: atencion, mlp o los dos")
    ap.add_argument("--casos", help="evaluar otro archivo en vez del examen: jsonl con"
                    " {frase, aceptables} o {frase, herramienta} (el banco de orientacion)")
    ap.add_argument("--confusion", type=int, default=0,
                    help="mostrar los N pares (esperada -> elegida) mas frecuentes")
    ap.add_argument("--sesgo", type=float, default=0.0,
                    help="cuanto se le suma al logit de `derivar` (el dial simple)")
    ap.add_argument("--vector", help="vector de control (.gguf) de vector-control.py")
    ap.add_argument("--escala", type=float, default=1.0,
                    help="cuanto se empuja. 0 = como si no estuviera")
    args = ap.parse_args()

    import torch  # noqa: E402
    from transformers import AutoModelForCausalLM, AutoTokenizer  # noqa: E402
    from peft import PeftModel  # noqa: E402
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

    # TRANSFORMERS PELADO Y EL ADAPTADOR FUSIONADO. La ruta rapida de unsloth
    # exige un token por vez cuando hay cache, y aca el cache del prefijo es lo
    # que ahorra el 90% del tiempo. Fusionar el LoRA ademas saca el matmul extra
    # y la indireccion de Python por capa: 21,4 -> 11,8 ms por forward, medido.
    cfg = json.loads((Path(args.modelo) / "adapter_config.json").read_text())
    tok = AutoTokenizer.from_pretrained(args.modelo)
    modelo = AutoModelForCausalLM.from_pretrained(
        cfg["base_model_name_or_path"], dtype=torch.bfloat16, device_map="cuda:0")
    modelo = PeftModel.from_pretrained(modelo, args.modelo).merge_and_unload()
    modelo.eval()

    # EL VECTOR DE CONTROL, aplicado igual que en llama.cpp: se SUMA a la salida
    # de cada capa, escalado. Se hace aca tambien —y no solo al servir— porque
    # la escala hay que elegirla mirando la curva de cobertura contra precision,
    # y esa curva se mide con el mismo evaluador que todo lo demas. Medir en un
    # lado y servir en el otro es como se pierden cuarenta puntos sin enterarse.
    ganchos = []

    # AMORTIGUAR LAS ULTIMAS CAPAS. Una abliteracion apaga una direccion; esto
    # baja el VOLUMEN de lo que escriben las capas que pisan la decision. El
    # acierto leido capa por capa sube hasta la 26 y cae diez puntos en las dos
    # ultimas: si esa caida es que esas capas escriben algo que estorba, bajarles
    # el factor tiene que recuperar casos.
    #
    # Se mide con el decodificador DE VERDAD —gramatica, token por token— y no
    # con el logit lens, que ademas de ser una lectura debil favorece a los
    # nombres cortos. Es la unica forma de que el numero compare con los otros.
    if args.amortiguar:
        capas_txt, factor = args.amortiguar.split(":")
        factor = float(factor)
        cuales = {int(x) for x in capas_txt.split(",") if x.strip()}
        bloques = {b.strip() for b in args.solo.split(",")}
        from vector_control_util import capas_del as _cap
        capas_m = _cap(modelo)

        def amortiguador(fn_factor):
            def fn(_m, _e, sal):
                if isinstance(sal, tuple):
                    return (sal[0] * fn_factor,) + tuple(sal[1:])
                return sal * fn_factor
            return fn

        for i, capa in enumerate(capas_m):
            if (i + 1) not in cuales:
                continue
            if "atencion" in bloques:
                ganchos.append(capa.self_attn.register_forward_hook(amortiguador(factor)))
            if "mlp" in bloques:
                ganchos.append(capa.mlp.register_forward_hook(amortiguador(factor)))
        print(f"amortiguado: capas {sorted(cuales)} · factor {factor} · bloques {sorted(bloques)}")

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

    # LA GRAMATICA COMO ARBOL, precalculado y con los indices ya en la GPU.
    #
    # La version anterior le pasaba a generate() una funcion que en CADA paso
    # bajaba la secuencia entera de la GPU al CPU —ids.tolist(), mas de 3.000
    # enteros— y recorria los 86 nombres en Python para ver cuales seguian
    # siendo posibles. Ahora el arbol se arma una vez: para cada prefijo de
    # tokens, que tokens siguen y que nombres quedan. Por paso cruza al CPU un
    # solo numero, el token elegido, que es lo minimo para bajar por el arbol.
    dev = modelo.device
    secuencias = {n: tuple(tok(n, add_special_tokens=False).input_ids) for n in nombres}
    siguientes, quedan = {}, {}
    for n, sq in secuencias.items():
        for k in range(len(sq) + 1):
            quedan.setdefault(sq[:k], set()).add(n)
            if k < len(sq):
                siguientes.setdefault(sq[:k], set()).add(sq[k])
    sig_t = {pre: torch.tensor(sorted(v), device=dev) for pre, v in siguientes.items()}

    # EL DIAL SIMPLE: sesgar el logit de `derivar`. Mueve el punto de operacion
    # de forma monotona y sin tocar nada mas. `derivar` empieza con un token que
    # ninguna herramienta comparte, asi que sesgarlo no arrastra a ninguna otra.
    tok_derivar = secuencias[lp.DERIVAR][0]

    # EL PREFIJO SE LEE UNA SOLA VEZ. Son 3.140 tokens identicos en todos los
    # casos, y la version anterior los volvia a procesar en cada uno: eso era
    # casi todo el tiempo de una evaluacion.
    ent_cab = tok(cabeza, return_tensors="pt").to(dev)
    with torch.no_grad():
        kv = modelo(**ent_cab, use_cache=True).past_key_values
    n_cab = ent_cab.input_ids.shape[1]

    def elegir(frase):
        """Decodificacion voraz con gramatica, identica a la de antes pero sin
        repetir el prefijo. Corta en cuanto queda UN solo nombre posible: desde
        ahi la gramatica fuerza el resto y seguir no decide nada."""
        r = tok(frase + cola, add_special_tokens=False, return_tensors="pt").input_ids.to(dev)
        with torch.no_grad():
            lg = modelo(input_ids=r, past_key_values=kv, use_cache=True).logits[0, -1]
        pre = ()
        try:
            for paso in range(12):
                if len(quedan[pre]) == 1:
                    return next(iter(quedan[pre]))
                opciones = sig_t[pre]
                val = lg.index_select(0, opciones).float()
                if paso == 0 and args.sesgo:
                    val = val + args.sesgo * (opciones == tok_derivar).float()
                t = int(opciones[int(val.argmax())])
                pre = pre + (t,)
                with torch.no_grad():
                    lg = modelo(input_ids=torch.tensor([[t]], device=dev),
                                past_key_values=kv, use_cache=True).logits[0, -1]
            return next(iter(quedan[pre]))
        finally:
            kv.crop(n_cab)

    fuente = Path(args.casos) if args.casos else EXAMEN
    casos = [json.loads(l) for l in fuente.read_text(encoding="utf-8").splitlines() if l.strip()]
    # El banco de orientacion viene como {frase, herramienta} y cubre las 292. Para
    # este modelo, una herramienta que no conoce tiene UNA respuesta correcta,
    # derivar; se guarda la real en `real` para poder contar por categoria.
    conocidas = set(nombres)
    for c in casos:
        if "aceptables" not in c:
            h = c["herramienta"]
            c["real"] = h
            c["aceptables"] = [h] if h in conocidas else [lp.DERIVAR]
    # PARTIR EL PROMPT NO TIENE QUE CAMBIAR LA TOKENIZACION. llama.cpp tokeniza
    # el prompt entero de una vez; aca se tokeniza el prefijo por un lado y el
    # resto por otro. Si en algun caso la frontera junta o separa tokens
    # distinto, el evaluador mediria otra cosa que el servidor. Se verifica
    # caso por caso antes de empezar, y si no coincide, se aborta.
    base_ids = ent_cab.input_ids[0].tolist()
    distintos = [c["frase"] for c in casos
                 if tok(cabeza + c["frase"] + cola).input_ids
                 != base_ids + tok(c["frase"] + cola, add_special_tokens=False).input_ids]
    if distintos:
        sys.exit(f"la tokenizacion partida no coincide con la entera en {len(distintos)} casos,"
                 f" por ejemplo «{distintos[0]}»: el evaluador no mediria lo mismo que el servidor")

    aciertos, fallos, elegidas, t0 = 0, [], {}, time.time()
    for c in casos:
        elegida = elegir(c["frase"])
        elegidas[c["frase"]] = elegida
        if elegida in c["aceptables"]:
            aciertos += 1
        else:
            fallos.append((c["frase"], elegida, c["aceptables"]))

    for g in ganchos:
        g.remove()
    if args.confusion:
        import collections
        conf = collections.Counter((c["aceptables"][0], elegidas[c["frase"]])
                                   for c in casos if elegidas[c["frase"]] not in c["aceptables"])
        print(f"\nconfusiones mas frecuentes (esperada -> elegida):")
        for (a, b), n in conf.most_common(args.confusion):
            print(f"  {a:24s} -> {b:24s} {n:3d}")
    print(f"\nmodelo: {args.modelo} · ejemplos en contexto: {args.ejemplos}"
          + (f" · escala {args.escala}" if args.vector else "")
          + (f" · sesgo {args.sesgo:+.1f}" if args.sesgo else "")
          + (f" · amortiguado {args.amortiguar}" if args.amortiguar else ""))
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
