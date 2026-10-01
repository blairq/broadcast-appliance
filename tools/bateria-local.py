#!/usr/bin/env python3
"""Cuanto tarda y cuanto acierta un modelo CHICO corriendo ACA, con el prefijo cacheado.

LO QUE RESPONDE. Para los eventos inmediatos —«esta acoplando», «se cayo un
invitado»— no alcanza con que el modelo acierte: tiene que llegar antes que el
operador, que reacciona en medio segundo. Este banco separa las dos mitades del
tiempo, que es lo que nadie mide:

  · PREFILL: leer el prompt. Con 73 fichas son miles de tokens y es LO CARO.
  · GENERAR: escribir la llamada. Son treinta tokens y es barato.

Y mide el prefill DOS VECES: la primera (frio) y las siguientes (caliente, con
el prefijo ya en el cache). Esa diferencia es la que decide si hace falta
entrenar un modelo para poder achicar el prompt, o si alcanza con cachearlo.

COMO SE APAGA EL THINKING. Qwen3 es hibrido: el mismo peso piensa o no piensa.
Se le cierra el bloque de pensamiento en el propio prompt (`<think></think>`
vacio, que es lo que hace su plantilla en modo no-thinking) Y ADEMAS se le pone
una gramatica que obliga a que lo primero sea `{`. Con las dos cosas no puede
pensar aunque quiera: no es un pedido, es imposible.

La gramatica tambien lista los nombres de herramienta permitidos, asi que no
puede inventar una que no exista.

  tools/bateria-local.py --modelo extern/modelos/Qwen3-1.7B-Q4_K_M.gguf
"""
import argparse, json, subprocess, sys, time, urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lib_prompt as lp  # noqa: E402

RAIZ = Path(__file__).resolve().parent.parent
INVENTARIO = RAIZ / "docs" / "mcp.json"
CORPUS = RAIZ / "src/app/tests/datos/preguntas.json"
LLAMA = RAIZ / "extern/llamacpp"
# Lo que se usa cuando algo se rompe en vivo. NO son las 291: el repertorio
# reactivo es chico y fijo, y por eso se puede cachear entero.
# El prompt lo arma lib_prompt, NO este archivo. Tenerlo en dos lados es como
# se rompe esto: se entrena con una forma y se sirve con otra. Ver el
# comentario de tools/lib_prompt.py, que cuenta la vez que paso.
def pedir(puerto, cuerpo, timeout=180):
    req = urllib.request.Request(f"http://127.0.0.1:{puerto}/completion",
                                 data=json.dumps(cuerpo).encode(),
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--modelo", required=True)
    ap.add_argument("--puerto", type=int, default=8099)
    ap.add_argument("--hilos", type=int, default=8)
    ap.add_argument("--casos", type=int, default=0, help="0 = todos los reactivos")
    ap.add_argument("--ejemplos", type=int, default=0,
                    help="cuantos pares resueltos meter DENTRO del prefijo cacheado")
    ap.add_argument("--solo-nombre", action="store_true",
                    help="que conteste solo el nombre de la herramienta, sin argumentos")
    args = ap.parse_args()

    inv = lp.inventario()
    nombres = lp.herramientas_reactivas(inv)

    corpus = json.loads(CORPUS.read_text(encoding="utf-8"))["casos"]
    casos = [c for c in corpus if any(t in nombres for t in c["herramientas"])]
    if args.casos:
        casos = casos[:args.casos]

    # El prefijo TIENE que ser identico en cada pedido: lo variable va al final.
    cabeza, cola = lp.partes(inv, args.ejemplos)

    env = {"LD_LIBRARY_PATH": str(LLAMA), "PATH": "/usr/bin:/bin"}
    srv = subprocess.Popen(
        [str(LLAMA / "llama-server"), "-m", args.modelo, "-c", "16384", "-t", str(args.hilos),
         "--host", "127.0.0.1", "--port", str(args.puerto), "--no-warmup"],
        env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(180):
            try:
                urllib.request.urlopen(f"http://127.0.0.1:{args.puerto}/health", timeout=2)
                break
            except Exception:
                time.sleep(1)
        else:
            sys.exit("el servidor no levanto")

        # Si el prompt cambio desde que se entreno, el numero que salga de aca
        # no compara con nada: al modelo le estan mostrando otras fichas.
        hjson = RAIZ / "datos/entrenamiento/huella.json"
        if hjson.exists():
            vieja = json.loads(hjson.read_text(encoding="utf-8"))
            ahora = lp.huella(inv, args.ejemplos)
            if vieja.get("prompt") != ahora["prompt"] and args.ejemplos == 0:
                print(f"\n  OJO: el prompt cambio desde que se armo el conjunto"
                      f" ({vieja.get('prompt')} -> {ahora['prompt']}).")
                print("       Si el modelo es uno entrenado, fue con otras fichas.\n")

        gram = lp.gramatica_nombre(nombres) if args.solo_nombre else gramatica(nombres)
        base = {"n_predict": 80, "temperature": 0.0, "cache_prompt": True, "grammar": gram}

        filas, aciertos = [], 0
        for i, c in enumerate(casos):
            r = pedir(args.puerto, dict(base, prompt=cabeza + c["pregunta"] + cola))
            t = r.get("timings", {})
            try:
                elegida = r["content"].strip() if args.solo_nombre else json.loads(r["content"])["herramienta"]
            except Exception:
                elegida = "(no parseo)"
            ok = elegida in c["herramientas"]
            aciertos += ok
            filas.append({"pregunta": c["pregunta"], "elegida": elegida, "ok": ok,
                          "aceptables": c["herramientas"],
                          "prompt_n": t.get("prompt_n", 0), "prompt_ms": round(t.get("prompt_ms", 0), 1),
                          "gen_n": t.get("predicted_n", 0), "gen_ms": round(t.get("predicted_ms", 0), 1),
                          "total_ms": round(t.get("prompt_ms", 0) + t.get("predicted_ms", 0), 1)})

        frio, calientes = filas[0], filas[1:]
        def med(k, xs): return round(sum(x[k] for x in xs) / max(1, len(xs)), 1)
        print(f"\nmodelo: {Path(args.modelo).name} · {args.hilos} hilos · {len(casos)} casos reactivos"
              f" · ejemplos en el prefijo: {args.ejemplos}")
        print(f"fichas: {len(nombres)} herramientas · prefijo: {frio['prompt_n']} tokens")
        print(f"acierto: {aciertos}/{len(casos)}  ({100*aciertos//max(1,len(casos))}%)")
        print(f"\n  PRIMERA (prefijo frio):  prefill {frio['prompt_ms']} ms · generar {frio['gen_ms']} ms"
              f" · TOTAL {frio['total_ms']} ms")
        print(f"  DESPUES (cache caliente): prefill {med('prompt_ms', calientes)} ms"
              f" · generar {med('gen_ms', calientes)} ms · TOTAL {med('total_ms', calientes)} ms")
        print(f"  tokens releidos por pedido (caliente): {med('prompt_n', calientes)}")
        peor = max(calientes, key=lambda x: x["total_ms"]) if calientes else frio
        print(f"  el peor caliente: {peor['total_ms']} ms  ({peor['pregunta']!r})")
        fallos = [f for f in filas if not f["ok"]]
        if fallos:
            print(f"\n  fallos ({len(fallos)}):")
            for f in fallos[:12]:
                print(f"    {f['pregunta']!r} -> {f['elegida']}")
        (RAIZ / "docs" / "bateria-local.json").write_text(
            json.dumps({"modelo": Path(args.modelo).name, "hilos": args.hilos,
                        "aciertos": aciertos, "casos": len(casos), "filas": filas,
                        # Igual que evaluar.py, para que fallos.py lea cualquiera.
                        "fallos": [{"frase": f["pregunta"], "elegida": f["elegida"],
                                    "aceptables": f["aceptables"]} for f in filas if not f["ok"]]},
                       ensure_ascii=False, indent=1), encoding="utf-8")
    finally:
        srv.terminate()
        srv.wait(timeout=30)


if __name__ == "__main__":
    main()
