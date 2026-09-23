#!/usr/bin/env python3
"""Cuán bien elige la herramienta un modelo, con el mismo corpus que el buscador.

PARA QUE SIRVE. `test_mcp_busqueda` mide si la herramienta que hace falta está
entre las primeras que devuelve el buscador —sin modelo, en microsegundos—.
Esto mide lo otro: dadas ESAS herramientas, si el modelo elige la correcta.

LO QUE DECIDE. Si un modelo más chico (14B, 9B) alcanza. Con 264 herramientas
en el prompt hace falta uno grande; con las diez que trae el buscador, quizá
no. La diferencia son 36.500 tokens contra 2.400 en cada pedido.

NO ES UN TEST DE CI: necesita red y un servidor de modelos. Se corre a mano
cuando se quiere comparar modelos o tocar el prompt.

  tools/bateria-modelo.py --modelo qwen3.8
  tools/bateria-modelo.py --modelo qwen3.6 --top 8
"""
import argparse, json, os, subprocess, sys, time
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
CORPUS = RAIZ / "src/app/tests/datos/preguntas.json"

SISTEMA = ("Sos el asistente de un estudio de television en vivo. "
           "Usa la herramienta que corresponda a lo que te piden. Contesta corto.")


def mcp(bundle, token, metodo, params=None, rol=""):
    cuerpo = {"jsonrpc": "2.0", "id": 1, "method": metodo}
    if params is not None:
        cuerpo["params"] = params
    cmd = ["curl", "-s", "-m", "25", "-H", "Authorization: Bearer " + token,
           "-H", "Content-Type: application/json"]
    # El rol PRIMERO: filtra el grupo, y la busqueda corre adentro de ese grupo.
    # Un agente que maneja escenas no tiene por que recibir filtros LADSPA.
    if rol:
        cmd += ["-H", "X-BA-Rol: " + rol]
    cmd += ["-d", json.dumps(cuerpo), "http://127.0.0.1:8790/mcp"]
    return json.loads(subprocess.run(cmd, capture_output=True, text=True).stdout)


def como_openai(t):
    return {"type": "function",
            "function": {"name": t["name"],
                         "description": (t.get("description") or "")[:400],
                         "parameters": t.get("inputSchema") or {"type": "object", "properties": {}}}}


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--modelo", default="qwen3.8")
    p.add_argument("--url", default="https://llm.saitama.com.ar/v1/chat/completions")
    p.add_argument("--clave-env", default="BA_MODELO_PROPIO_KEY",
                   help="variable de entorno con la clave de API")
    p.add_argument("--top", type=int, default=8, help="cuantas herramientas trae el buscador")
    p.add_argument("--todas", action="store_true",
                   help="mandarle las 264 en vez de las que encuentra el buscador (para comparar)")
    p.add_argument("--rol", default="", help="filtrar el grupo antes de buscar (ej: asistente)")
    p.add_argument("--bundle", default=str(RAIZ / "release"))
    a = p.parse_args()

    clave = os.environ.get(a.clave_env, "")
    if not clave:
        sys.exit(f"falta la clave en ${a.clave_env}")
    token = subprocess.run([a.bundle + "/bin/64bit/ba-mcp", "--token"],
                           capture_output=True, text=True).stdout.strip()
    if not token:
        sys.exit("no se pudo leer el token de MCP: ¿está corriendo la app?")

    todas = {t["name"]: t for t in mcp(a.bundle, token, "tools/list", rol=a.rol)["result"]["tools"]}
    casos = json.load(open(CORPUS))["casos"]

    ok = varias = 0
    tiempos, tokens = [], []
    for c in casos:
        if a.todas:
            sel = [como_openai(t) for t in todas.values()]
        else:
            r = mcp(a.bundle, token, "tools/call",
                    {"name": "help_search", "arguments": {"query": c["pregunta"], "limit": a.top}},
                    rol=a.rol)
            txt = r["result"]["content"][0]["text"]
            nombres = [l.split("[herramienta] ")[1].split(" —")[0].strip()
                       for l in txt.split("\n") if "[herramienta] " in l][:a.top]
            sel = [como_openai(todas[n]) for n in nombres if n in todas]
        cuerpo = {"model": a.modelo,
                  "messages": [{"role": "system", "content": SISTEMA},
                               {"role": "user", "content": c["pregunta"]}],
                  "tools": sel, "max_tokens": 300, "temperature": 0,
                  # Sin pensar: para elegir una herramienta no hace falta, y el
                  # tiempo es lo que se esta midiendo.
                  "chat_template_kwargs": {"enable_thinking": False, "reasoning_effort": "low"}}
        t0 = time.time()
        r = subprocess.run(["curl", "-s", "-m", "240", "-H", "Authorization: Bearer " + clave,
                            "-H", "Content-Type: application/json", "-d", json.dumps(cuerpo), a.url],
                           capture_output=True, text=True)
        dt = time.time() - t0
        tiempos.append(dt)
        try:
            d = json.loads(r.stdout)
            m = d["choices"][0]["message"]
            llamadas = m.get("tool_calls") or []
            elegida = llamadas[0]["function"]["name"] if llamadas else "(ninguna)"
            tokens.append(d.get("usage", {}).get("prompt_tokens", 0))
            if len(llamadas) > 1:
                varias += 1
        except Exception:
            elegida = "(error)"
        bien = elegida in c["herramientas"]
        ok += bien
        if not bien:
            print(f"  ✗ {c['pregunta'][:44]:46s} -> {elegida}  (se esperaba {'/'.join(c['herramientas'])})")

    n = len(casos)
    print()
    print(f"  modelo {a.modelo} | {'264 herramientas' if a.todas else f'buscador top {a.top}'}"
          f"{' | rol ' + a.rol if a.rol else ''}")
    print(f"  aciertos: {ok}/{n} ({100*ok//n}%) | {varias} respuestas con más de una herramienta")
    if tokens:
        print(f"  prompt_tokens promedio: {sum(tokens)//len(tokens)}")
    print(f"  latencia: mediana {sorted(tiempos)[len(tiempos)//2]:.1f}s  max {max(tiempos):.1f}s")
    return 0 if ok == n else 1


if __name__ == "__main__":
    sys.exit(main())
