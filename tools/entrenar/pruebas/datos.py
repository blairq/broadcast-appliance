"""Pares de juguete desde un corpus de tc-corpus: la buena es la respuesta real, la
mala la de otra fila (otra herramienta). Deja <destino>/dpo y <destino>/kto con
entrenar.jsonl y validar.jsonl, en el formato de entrenar-rapido --objetivo dpo|kto.

    python3 tools/entrenar/pruebas/datos.py <corpus> <destino>
"""
import json, random, sys
from pathlib import Path
corpus, dest = Path(sys.argv[1]), Path(sys.argv[2])
r = random.Random(1)
filas = [json.loads(l) for l in open(corpus / "entrenar.jsonl")]
filas = [f for f in filas if len(f["completion"]) < 400]
r.shuffle(filas)
filas = filas[:240]
def otra(k):
    while True:
        j = r.randrange(len(filas))
        if filas[j]["completion"] != filas[k]["completion"]:
            return filas[j]["completion"]
for obj in ("dpo", "kto"):
    d = dest / obj
    d.mkdir(parents=True, exist_ok=True)
    out = []
    for k, f in enumerate(filas):
        if obj == "dpo":
            out.append({"prompt": f["prompt"], "chosen": f["completion"], "rejected": otra(k)})
        else:
            buena = k % 2 == 0
            out.append({"prompt": f["prompt"], "completion": f["completion"] if buena else otra(k), "buena": buena})
    (d / "entrenar.jsonl").write_text("".join(json.dumps(x, ensure_ascii=False) + "\n" for x in out[:200]))
    (d / "validar.jsonl").write_text("".join(json.dumps(x, ensure_ascii=False) + "\n" for x in out[200:]))
print("ok", len(filas))
