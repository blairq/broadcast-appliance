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

# El prompt del banco. MEDIDO: sin la tercera linea el modelo elige una y otra
# vez la herramienta que LISTA en vez de la que HACE —`receta_listar` cuando le
# piden guardar una receta—. No es falta de informacion: la correcta le llega
# primera. Es que ante la duda prefiere mirar antes que actuar, y hay que
# decirselo.
SISTEMA = ("Sos el asistente de un estudio de television en vivo. "
           "Usa la herramienta que corresponda a lo que te piden. Contesta corto. "
           "Si te piden HACER algo, llama directamente a la herramienta que lo hace: "
           "no listes catalogos ni inventarios primero. Los listados son para cuando "
           "te preguntan QUE HAY, no para cuando te piden una accion.")


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
    p.add_argument("--claude", default="",
                   help="ruta al CLI de claude: usa ese en vez del endpoint. NO le conecta el MCP "
                        "—ejecutaria las herramientas de verdad y cambiaria el aire—: le pasa las "
                        "candidatas en el texto y le pide que elija una. Mide la eleccion, no la "
                        "ejecucion, asi que no es comparable al 100% con el tool-calling nativo.")
    p.add_argument("--bundle", default=str(RAIZ / "release"))
    a = p.parse_args()

    clave = os.environ.get(a.clave_env, "")
    if not clave and not a.claude:
        sys.exit(f"falta la clave en ${a.clave_env}")
    token = subprocess.run([a.bundle + "/bin/64bit/ba-mcp", "--token"],
                           capture_output=True, text=True).stdout.strip()
    if not token:
        sys.exit("no se pudo leer el token de MCP: ¿está corriendo la app?")

    todas = {t["name"]: t for t in mcp(a.bundle, token, "tools/list", rol=a.rol)["result"]["tools"]}
    casos = json.load(open(CORPUS))["casos"]

    ok = varias = 0
    tiempos, tokens = [], []
    # QUIEN LE ROBA A QUIEN. La matriz de confusion es el mapa de donde se
    # solapan los dominios DE VERDAD: no lo que uno cree al leer las fichas,
    # sino lo que el modelo confunde teniendo las dos al lado. Cada par que
    # aparece dos o mas veces es un par de descripciones que hay que separar.
    confusion = {}
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
        if a.claude:
            fichas = "\n".join("- %s: %s" % (h["function"]["name"], h["function"]["description"])
                                for h in sel)
            pedido = (SISTEMA + "\n\nHerramientas disponibles:\n" + fichas +
                      "\n\nPedido del operador: «" + c["pregunta"] + "»\n\n"
                      "Contesta UNICAMENTE con el nombre exacto de la herramienta que llamarias "
                      "primero. Nada mas: ni explicacion, ni comillas, ni puntuacion.")
            # stdin cerrado: si no, el CLI espera 3 s por cada pregunta.
            r = subprocess.run([a.claude, "-p", pedido], capture_output=True, text=True,
                               timeout=240, stdin=subprocess.DEVNULL)
            dt = time.time() - t0
            tiempos.append(dt)
            elegida = r.stdout.strip().split()[0].strip(".,`«»\"'") if r.stdout.strip() else "(vacio)"
            bien = elegida in c["herramientas"]
            ok += bien
            if not bien:
                print(f"  ✗ {c['pregunta'][:44]:46s} -> {elegida}  (se esperaba {'/'.join(c['herramientas'])})")
                confusion.setdefault((c["herramientas"][0], elegida), []).append(c["pregunta"])
            continue
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
            confusion.setdefault((c["herramientas"][0], elegida), []).append(c["pregunta"])

    n = len(casos)
    print()
    print(f"  modelo {'claude (CLI, sin MCP)' if a.claude else a.modelo}"
          f" | {'264 herramientas' if a.todas else f'buscador top {a.top}'}"
          f"{' | rol ' + a.rol if a.rol else ''}")
    print(f"  aciertos: {ok}/{n} ({100*ok//n}%) | {varias} respuestas con más de una herramienta")
    if tokens:
        print(f"  prompt_tokens promedio: {sum(tokens)//len(tokens)}")
    print(f"  latencia: mediana {sorted(tiempos)[len(tiempos)//2]:.1f}s  max {max(tiempos):.1f}s")
    if confusion:
        print()
        print("  DONDE SE SOLAPAN (esperada <- la que se la llevo):")
        ladronas = {}
        for (esperada, elegida), qs in confusion.items():
            ladronas.setdefault(elegida, []).extend(qs)
        for elegida, qs in sorted(ladronas.items(), key=lambda x: -len(x[1])):
            print(f"    {len(qs)}x  {elegida}")
            for q in qs:
                print(f"           «{q}»")
    return 0 if ok == n else 1


if __name__ == "__main__":
    sys.exit(main())
