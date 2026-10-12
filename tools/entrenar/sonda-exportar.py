#!/usr/bin/env python3
"""La SONDA de derivar como pieza del modelo: entrenarla sobre el modelo final y exportarla.

QUE ES (lab-sonda.py, docs/modelos/entrenamiento-avanzado.md). Una regresion
logistica sobre el estado oculto en el punto donde el modelo va a escribir el
nombre de la herramienta. Separa «esto es mio» de «esto es del asistente
grande» mejor que la propia decision del modelo; combinada con ella («derivar si
el modelo deriva O la sonda dice ajeno») atiende mas de lo suyo Y deriva mas.

LO QUE LA APP TIENE QUE HACER (sale en modelo.json → sonda):
  1. Al prompt de siempre (cabeza + usuario) sumarle `forzado` (el comienzo fijo
     de toda respuesta con llamada: «…<tool_call>\\n<function»).
  2. Tomar el estado oculto FINAL (despues de la norma, la entrada de la cabeza de
     salida: lo que llama.cpp da como embedding y MLX como salida de model.model)
     del ULTIMO token.
  3. p = sigmoid(w·h + b) con w, b de sonda.safetensors (la normalizacion ya va
     adentro). Si p > umbral, derivar aunque el modelo haya elegido una propia.

DATOS: el corpus del propio modelo, cada fila con SU estado: «propio» = filas de
herramienta; «ajeno» = filas de derivar (ya filtradas por STE-ES). El umbral se
elige en una validacion aparte (exactitud balanceada), nunca con el banco.

    BA_PERFIL=vivo5 python tools/entrenar/sonda-exportar.py --modelo <adaptador> --corpus <corpus> --salida <dir>
"""
import argparse
import json
import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lib_tc as tc  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--modelo", required=True, help="adaptador LoRA")
    ap.add_argument("--corpus", required=True, help="el corpus con que se entreno (entrenar.jsonl)")
    ap.add_argument("--salida", required=True)
    ap.add_argument("--n", type=int, default=1600, help="filas por clase")
    args = ap.parse_args()
    out = Path(args.salida).expanduser()
    out.mkdir(parents=True, exist_ok=True)

    import torch
    from transformers import AutoModelForCausalLM, AutoTokenizer
    from peft import PeftModel
    import cache_hibrido as ch
    from safetensors.torch import save_file
    m_dir = Path(args.modelo).expanduser()
    cfg = json.loads((m_dir / "adapter_config.json").read_text())
    modelo = AutoModelForCausalLM.from_pretrained(cfg["base_model_name_or_path"], dtype=torch.bfloat16,
                                                  device_map={"": "cuda:0"})
    modelo = PeftModel.from_pretrained(modelo, str(m_dir)).merge_and_unload().eval()
    tok = AutoTokenizer.from_pretrained(str(m_dir))
    dev = modelo.device
    ch.parchear()
    cab = tc.cabeza()
    ids_cab = tok(cab, add_special_tokens=False, return_tensors="pt").input_ids.to(dev)
    with torch.no_grad():
        kv = modelo.model(input_ids=ids_cab, use_cache=True).past_key_values
    Lp = ids_cab.shape[1]
    foto = ch.foto(kv)
    forzado_txt = tc.COLA + "<tool_call>\n<function"
    forzado = tok(forzado_txt, add_special_tokens=False).input_ids

    filas = [json.loads(l) for l in (Path(args.corpus).expanduser() / "entrenar.jsonl").read_text().splitlines()]
    rnd = random.Random(7)
    prop = [f for f in filas if f["tipo"] == "herramienta" and f["herramienta"] != tc.DERIVAR]
    ajen = [f for f in filas if f["tipo"] == "derivar" or f["herramienta"] == tc.DERIVAR]
    prop, ajen = rnd.sample(prop, min(args.n, len(prop))), rnd.sample(ajen, min(args.n, len(ajen)))

    def rasgo(f):
        """El estado oculto FINAL (normado) del ultimo token de usuario + forzado."""
        assert f["prompt"].startswith(cab), "el corpus tiene otro prompt: correr con el mismo BA_PERFIL"
        usuario = f["prompt"][len(cab):-len(tc.COLA)] if f["prompt"].endswith(tc.COLA) else f["prompt"][len(cab):]
        u = tok(usuario, add_special_tokens=False).input_ids
        try:
            with torch.no_grad():
                h = modelo.model(input_ids=torch.tensor([u + forzado], device=dev), past_key_values=kv,
                                 use_cache=True).last_hidden_state[0, -1]
            return h.float().cpu()
        finally:
            ch.volver(kv, foto, Lp)

    X, y = [], []
    for i, f in enumerate(prop + ajen):
        X.append(rasgo(f))
        y.append(0.0 if i < len(prop) else 1.0)
        if (i + 1) % 400 == 0:
            print(f"  {i + 1}/{len(prop) + len(ajen)}", flush=True)
    X, y = torch.stack(X), torch.tensor(y)
    idx = torch.randperm(len(y), generator=torch.Generator().manual_seed(0))
    nv = len(y) // 5
    iv, it = idx[:nv], idx[nv:]

    def entrenar(Xc, yc, l2=1e-3):
        mu, sd = Xc.mean(0), Xc.std(0) + 1e-5
        Z = (Xc - mu) / sd
        w = torch.zeros(Z.shape[1], requires_grad=True)
        b = torch.zeros(1, requires_grad=True)
        opt = torch.optim.LBFGS([w, b], max_iter=400, line_search_fn="strong_wolfe")

        def cierre():
            opt.zero_grad()
            perdida = torch.nn.functional.binary_cross_entropy_with_logits(Z @ w + b, yc) + l2 * (w ** 2).sum()
            perdida.backward()
            return perdida
        opt.step(cierre)
        # la normalizacion adentro: la app solo hace sigmoid(w·h + b)
        wf = (w / sd).detach()
        bf = (b - (w * mu / sd).sum()).detach()
        return wf, bf

    w, b = entrenar(X[it], y[it])
    pv = torch.sigmoid(X[iv] @ w + b)
    yv = y[iv]
    bal, umbral = max(((float((pv[yv == 1] > t).float().mean()) + float((pv[yv == 0] <= t).float().mean())) / 2, t)
                      for t in [x / 100 for x in range(5, 96)])
    w, b = entrenar(X, y)  # con todo, el umbral ya elegido
    save_file({"w": w.to(torch.float16).contiguous(), "b": b.to(torch.float32).contiguous()},
              str(out / "sonda.safetensors"))
    meta = {"version": 1, "que": "derivar si el modelo deriva O sigmoid(w·h + b) > umbral",
            "estado_oculto": "final (despues de la norma, entrada de la cabeza de salida), ultimo token",
            "forzado": forzado_txt, "dimension": int(w.shape[0]), "umbral": umbral,
            "exactitud_balanceada_validacion": round(bal, 4), "prompt_huella": tc.huella(),
            "datos": {"propias": len(prop), "ajenas": len(ajen)}}
    (out / "sonda.json").write_text(json.dumps(meta, ensure_ascii=False, indent=1))
    print(json.dumps(meta, ensure_ascii=False))


if __name__ == "__main__":
    main()
