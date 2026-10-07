#!/usr/bin/env python3
"""tools/entrenar/voz-banco-informe.py — el resumen de un banco de voz (tools/entrenar/voz-banco.py), en Markdown.

Clasifica cada frase:
  ELIGE_MAL     el modelo eligió otra herramienta
  FORMATO       no escribió un JSON usable
  NO_EXISTE     nombró algo que no está en el aparato (correcto si la frase lo nombra así)
  FALTA_DATO    faltó un argumento requerido
  EJEC_ERROR    se ejecutó y la herramienta dio error (casi siempre, del entorno)
  NO_QUEDO      se ejecutó, pero el efecto no es el pedido (verificado)
  OK_EJECUTADA  se ejecutó y (si se puede verificar) quedó como se pidió
  OK_SIN_EJEC   elección y argumentos válidos, no se ejecuta en la prueba

    python3 tools/entrenar/voz-banco-informe.py build/voz-banco.json > informe.md
"""
import collections, json, sys

def clase(x):
    if x.get("elegida") != x.get("esperada"): return "ELIGE_MAL"
    m = x.get("motivo", "")
    if "no es JSON" in m: return "FORMATO"
    if "no existe" in m: return "NO_EXISTE"
    if "falta" in m: return "FALTA_DATO"
    if x.get("ejecutada"):
        if not x.get("ok"): return "EJEC_ERROR"
        v = x.get("verificacion", "")
        return "OK_EJECUTADA" if v in ("", "ok") else "NO_QUEDO"
    return "OK_SIN_EJEC"

r = json.load(open(sys.argv[1]))
porh = collections.defaultdict(list)
for x in r: porh[x["esperada"]].append(x)
tot = collections.Counter(clase(x) for x in r)
n = len(r)
print("| | frases | % |\n|---|---:|---:|")
for k in ["OK_EJECUTADA","OK_SIN_EJEC","NO_EXISTE","EJEC_ERROR","NO_QUEDO","FALTA_DATO","FORMATO","ELIGE_MAL"]:
    print("| %s | %d | %.0f %% |" % (k, tot[k], 100.0*tot[k]/max(1,n)))
print("\n**Elección de herramienta:** %d de %d (%.0f %%)." % (n - tot["ELIGE_MAL"], n, 100.0*(n-tot["ELIGE_MAL"])/max(1,n)))
ms = [x["ms_args"] for x in r if x.get("ms_args")]
if ms:
    ms.sort(); print("**Argumentos:** mediana %d ms, p90 %d ms (%d frases con argumentos)." % (ms[len(ms)//2], ms[int(len(ms)*0.9)], len(ms)))
print("\n## Por herramienta\n\n| herramienta | frases | resultado |\n|---|---:|---|")
for h, xs in porh.items():
    c = collections.Counter(clase(x) for x in xs)
    print("| `%s` | %d | %s |" % (h, len(xs), ", ".join("%s ×%d" % (k, v) for k, v in c.most_common())))
print("\n## Los casos que no salieron\n")
for h, xs in porh.items():
    malos = [x for x in xs if clase(x) not in ("OK_EJECUTADA", "OK_SIN_EJEC")]
    if not malos: continue
    print("### `%s`\n" % h)
    for x in malos:
        det = {"ELIGE_MAL": "eligió `%s`" % x.get("elegida"),
               "EJEC_ERROR": "error: " + str(x.get("respuesta", ""))[:140],
               "NO_QUEDO": x.get("verificacion", "")}.get(clase(x), x.get("motivo", ""))
        print("- «%s» → **%s**: %s. Argumentos: `%s`" % (x["frase"], clase(x), det.replace("|", "/"),
              json.dumps(x.get("args"), ensure_ascii=False)))
    print()
