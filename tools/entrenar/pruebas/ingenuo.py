"""log p de la respuesta con la secuencia ENTERA (prefijo+frase+respuesta), sin cache ni
prefijo compartido, contra lo que dejo entrenar-rapido en referencia-*.json. Tiene
que dar lo mismo (redondeo de bf16): si no, el prefijo compartido mide otra cosa.

    .venv-entrenar/bin/python tools/entrenar/pruebas/ingenuo.py <modelo> <datos dpo|kto> <salida de entrenar-rapido>
"""
import json, sys, glob
import torch
import torch.nn.functional as F
from transformers import AutoModelForCausalLM, AutoTokenizer
base, datos, sal = sys.argv[1:4]
tok = AutoTokenizer.from_pretrained(base)
m = AutoModelForCausalLM.from_pretrained(base, dtype=torch.bfloat16, device_map={"": 0}).eval()
filas = [json.loads(l) for l in open(f"{datos}/entrenar.jsonl")][:12]
rE, _ = json.load(open(glob.glob(f"{sal}/referencia-*.json")[0]))
peor = 0.0
for k, f in enumerate(filas):
    textos = [f["chosen"], f["rejected"]] if "chosen" in f else [f["completion"]]
    for j, c in enumerate(textos):
        a = tok(f["prompt"], add_special_tokens=False).input_ids
        b = tok(f["prompt"] + c, add_special_tokens=False).input_ids
        n = len(b) - len(a)
        with torch.no_grad(), torch.autocast("cuda", dtype=torch.bfloat16):
            h = m.model(input_ids=torch.tensor([b], device=0)).last_hidden_state[0, -n - 1:-1]
            lp = -F.cross_entropy(m.lm_head(h).float(), torch.tensor(b[-n:], device=0), reduction="sum").item()
        d = abs(lp - rE[k][j])
        peor = max(peor, d / max(1.0, abs(lp)))
        if k < 3:
            print(f"fila {k} resp {j}: ingenuo {lp:10.3f} · entrenar-rapido {rE[k][j]:10.3f} · dif {d:.3f}")
print(f"peor diferencia relativa en {len(filas)} filas: {peor:.2e}")
