#!/usr/bin/env python3
"""Entrenar la cabeza MTP (multi-token prediction) de Qwen3.5 para NUESTRO modelo ajustado.

POR QUE. llama.cpp la usa para la decodificacion especulativa (--spec-type
draft-mtp): la cabeza adivina el token siguiente al siguiente y el modelo lo
verifica de una. Rinde tanto como acierte. La de fabrica adivina lo que contesta
el modelo DE FABRICA; despues de nuestro ajuste el modelo contesta otra cosa (un
nombre de herramienta), y la cabeza vieja acierta menos. Se la reentrena con el
modelo principal CONGELADO para que adivine lo que contesta el ajustado.

QUE ES, igual que en llama.cpp (src/models/qwen35.cpp, b11255):
  h(t)   el estado del modelo en la posicion t, DESPUES de la norma final
  x      = mtp.fc( [ RMSNorm_e(emb(token t+1)) , RMSNorm_h(h(t)) ] )
  x      una capa de atencion completa de Qwen3.5 (con su propio cache)
  logits = lm_head( mtp.norm(x) )         -> el token t+2
Aca: un Qwen3_5TextModel de UNA capa (trae mascara, mRoPE y cache) cuya norma
final es mtp.norm, mas mtp.fc y las dos normas de entrada. El embedding y la
cabeza de salida son los del modelo (compartidos, congelados).

QUE SE ENTRENA: solo la cabeza. La perdida va donde la cabeza trabaja en vivo:
dado el primer token de la respuesta (que pone el modelo), los siguientes del
nombre y el fin. Prefijo compartido como en entrenar-rapido.py, tambien para la
capa MTP. Se mide la ACEPTACION (top-1 de la cabeza sobre esos tokens) en
validacion, antes y despues.

    tools/entrenar/entrenar-mtp.py --modelo <fusionado-hf> --base-ruta <snapshot de la base> \\
        --datos <corpus> --salida mtp.safetensors [--epocas 1] [--repartir]
"""
import argparse
import copy
import json
import math
import random
import sys
import time
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(RAIZ / "tools"))
sys.path.insert(0, str(Path(__file__).resolve().parent))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--modelo", required=True, help="el modelo ajustado y fusionado (carpeta HF)")
    ap.add_argument("--base-ruta", required=True, help="snapshot de la base: de ahi sale la cabeza original")
    ap.add_argument("--datos", required=True)
    ap.add_argument("--salida", required=True)
    ap.add_argument("--epocas", type=float, default=1.0)
    ap.add_argument("--lr", type=float, default=1e-4)
    ap.add_argument("--lote", type=int, default=16)
    ap.add_argument("--repartir", action="store_true", help="el modelo principal entre las dos placas")
    ap.add_argument("--semilla", type=int, default=7)
    args = ap.parse_args()

    import torch
    import torch.nn as nn
    import torch.nn.functional as F
    from safetensors import safe_open
    from safetensors.torch import save_file
    from transformers import AutoModelForCausalLM, AutoTokenizer, DynamicCache
    from transformers.models.qwen3_5 import modeling_qwen3_5 as q
    import cache_hibrido as ch

    torch.manual_seed(args.semilla)
    tok = AutoTokenizer.from_pretrained(args.modelo)
    dm = "balanced" if args.repartir else {"": "cuda:0"}
    cfg_m = json.loads((Path(args.modelo) / "config.json").read_text())
    if cfg_m.get("model_type") == "qwen3_5":  # el que se exporto para mlx-lm
        cfg_m["model_type"] = "qwen3_5_text"
        tmp = Path(args.salida).parent / "modelo-hf-mtp-config"
        tmp.mkdir(parents=True, exist_ok=True)
        for f in Path(args.modelo).iterdir():
            if f.name != "config.json" and not (tmp / f.name).exists():
                (tmp / f.name).symlink_to(f.resolve())
        (tmp / "config.json").write_text(json.dumps(cfg_m))
        ruta_modelo = tmp
    else:
        ruta_modelo = Path(args.modelo)
    modelo = AutoModelForCausalLM.from_pretrained(ruta_modelo, dtype=torch.bfloat16, device_map=dm,
                                                  attn_implementation="sdpa").eval()
    for p in modelo.parameters():
        p.requires_grad_(False)
    if ch.es_hibrido(modelo):
        ch.parchear()
    texto, cabeza, emb = modelo.model, modelo.lm_head, modelo.model.embed_tokens
    dev_out = cabeza.weight.device
    cfg = modelo.config
    n_mtp = getattr(cfg, "mtp_num_hidden_layers", 0)
    if n_mtp != 1:
        sys.exit(f"este modelo declara {n_mtp} capas MTP; se soporta 1 (como llama.cpp)")

    # LA CABEZA: una capa de atencion completa con su norma final, mas fc y normas de entrada
    cfg1 = copy.deepcopy(cfg)
    cfg1.num_hidden_layers, cfg1.layer_types = 1, ["full_attention"]
    H = cfg.hidden_size

    class Cabeza(nn.Module):
        def __init__(self):
            super().__init__()
            self.capa = q.Qwen3_5TextModel(cfg1)
            self.capa.embed_tokens = None  # usa el del modelo
            # una capa sola de atencion completa: no hay mascara de atencion lineal
            # que armar (y el cache, sin capas lineales, no acepta la pregunta)
            self.capa._update_linear_attn_mask = lambda attention_mask, past_key_values: None
            self.fc = nn.Linear(2 * H, H, bias=False)
            self.enorm = q.Qwen3_5RMSNorm(H, eps=cfg.rms_norm_eps)
            self.hnorm = q.Qwen3_5RMSNorm(H, eps=cfg.rms_norm_eps)

        def forward(self, h, e, kv, pos, mask=None):
            x = self.fc(torch.cat([self.enorm(e), self.hnorm(h)], dim=-1))
            return self.capa(inputs_embeds=x, position_ids=pos, attention_mask=mask,
                             past_key_values=kv, use_cache=True).last_hidden_state

    cab = Cabeza()
    nombres = {"mtp.fc.weight": "fc.weight", "mtp.pre_fc_norm_embedding.weight": "enorm.weight",
               "mtp.pre_fc_norm_hidden.weight": "hnorm.weight", "mtp.norm.weight": "capa.norm.weight"}
    pesos = {}
    for f in sorted(Path(args.base_ruta).glob("*.safetensors")):
        with safe_open(str(f), "pt") as st:
            for k in st.keys():
                if k.startswith("mtp."):
                    pesos[k] = st.get_tensor(k)
    sd = {}
    for k, v in pesos.items():
        if k in nombres:
            sd[nombres[k]] = v
        elif k.startswith("mtp.layers.0."):
            sd["capa.layers.0." + k[len("mtp.layers.0."):]] = v
    falta = cab.load_state_dict(sd, strict=False)
    reales = [m for m in falta.missing_keys if "rotary" not in m]
    if reales or falta.unexpected_keys:
        sys.exit(f"la cabeza no calza con los pesos: faltan {reales[:5]} · sobran {falta.unexpected_keys[:5]}")
    dev = emb.weight.device
    cab = cab.to(dev).float()

    # los datos, como entrenar-rapido: prefijo comun + sufijo + respuesta
    cargar = lambda n: [json.loads(l) for l in (Path(args.datos) / n).read_text().splitlines() if l.strip()]
    ent, val = cargar("entrenar.jsonl"), cargar("validar.jsonl")
    marca = "<|im_start|>user\n"
    p0 = ent[0]["prompt"]
    prefijo = p0[:p0.rindex(marca) + len(marca)]
    ids_pre = tok(prefijo, add_special_tokens=False).input_ids

    def armar(filas):
        out = []
        for f in filas:
            a = tok(f["prompt"][len(prefijo):], add_special_tokens=False).input_ids
            b = tok(f["completion"], add_special_tokens=False).input_ids
            if len(b) >= 2:
                out.append((a + b, len(b)))
        return out
    E, V = armar(ent), armar(val)
    Lp = len(ids_pre)
    pre_t = torch.tensor([ids_pre], device=dev)

    def lote_perdida(lote, grad):
        B = len(lote)
        L = max(len(s) for s, _ in lote)
        ids = torch.zeros((B, L), dtype=torch.long, device=dev)
        mask = torch.zeros((B, Lp + L), dtype=torch.long, device=dev)
        mask[:, :Lp] = 1
        etq = torch.full((B, L), -100, dtype=torch.long, device=dev_out)
        for i, (s, n) in enumerate(lote):
            ids[i, :len(s)] = torch.tensor(s, device=dev)
            mask[i, Lp:Lp + len(s)] = 1
            # posicion j del sufijo: la cabeza ve h(Lp-1+j) y el token s[j] y adivina s[j+1].
            # Trabaja en vivo desde el primer token de la respuesta (j = len-n) hasta el penultimo.
            for j in range(len(s) - n, len(s) - 1):
                etq[i, j] = s[j + 1]
        with torch.no_grad(), torch.autocast("cuda", dtype=torch.bfloat16):
            kv = DynamicCache(config=cfg)
            hp = texto(input_ids=pre_t, past_key_values=kv, use_cache=True).last_hidden_state
            ch.repetir(kv, B) if ch.es_hibrido(modelo) else kv.batch_repeat_interleave(B)
            pos = torch.arange(Lp, Lp + L, device=dev).unsqueeze(0).expand(B, -1)
            hs = texto(input_ids=ids, attention_mask=mask, position_ids=pos, past_key_values=kv,
                       use_cache=True).last_hidden_state
            e_pre, e_suf = emb(pre_t[:, 1:]), emb(ids)
        with torch.set_grad_enabled(grad), torch.autocast("cuda", dtype=torch.bfloat16):
            # la parte del prefijo de la cabeza (posiciones 0..Lp-2), una vez
            kvm = DynamicCache(config=cfg1)
            cab(hp[:, :-1].to(dev), e_pre, kvm, torch.arange(0, Lp - 1, device=dev).unsqueeze(0))
            kvm.batch_repeat_interleave(B)
            h_in = torch.cat([hp[:, -1:].expand(B, -1, -1), hs[:, :-1]], dim=1).to(dev)
            pos_m = torch.arange(Lp - 1, Lp - 1 + L, device=dev).unsqueeze(0).expand(B, -1)
            mask_m = torch.cat([torch.ones((B, Lp - 1), dtype=torch.long, device=dev), mask[:, Lp:]], dim=1)
            out = cab(h_in, e_suf, kvm, pos_m, mask_m)
            lg = cabeza(out.to(dev_out).to(cabeza.weight.dtype)).float()
            perd = F.cross_entropy(lg.reshape(-1, lg.shape[-1]), etq.reshape(-1), ignore_index=-100, reduction="sum")
            ok = ((lg.argmax(-1) == etq) & (etq != -100)).sum()
        return perd, ok, (etq != -100).sum()

    def aceptacion():
        cab.eval()
        a = n = 0
        for i in range(0, len(V), 32):
            _, ok, m = lote_perdida(V[i:i + 32], grad=False)
            a, n = a + ok.item(), n + m.item()
        cab.train()
        return a / max(1, n)

    antes = aceptacion()
    print(f"aceptacion de la cabeza ORIGINAL en validacion: {100 * antes:.1f}%", flush=True)
    opt = torch.optim.AdamW([p for p in cab.parameters() if p.requires_grad], lr=args.lr, weight_decay=0.0)
    total = int(len(E) // args.lote * args.epocas)
    rnd = random.Random(args.semilla)
    orden, t0 = [], time.time()
    for paso in range(total):
        if len(orden) < args.lote:
            orden = list(range(len(E)))
            rnd.shuffle(orden)
        lote, orden = [E[i] for i in orden[:args.lote]], orden[args.lote:]
        for g in opt.param_groups:
            g["lr"] = args.lr * 0.5 * (1 + math.cos(math.pi * paso / max(1, total)))
        perd, ok, n = lote_perdida(lote, grad=True)
        (perd / n).backward()
        opt.step()
        opt.zero_grad(set_to_none=True)
        if (paso + 1) % 50 == 0:
            print(f"paso {paso + 1}/{total} · perdida {(perd / n).item():.3f} · acierto del lote "
                  f"{100 * ok.item() / n.item():.0f}% · {(time.time() - t0) / (paso + 1):.2f} s/paso", flush=True)
    despues = aceptacion()
    print(f"aceptacion de la cabeza ENTRENADA en validacion: {100 * despues:.1f}% (antes {100 * antes:.1f}%)")
    inv = {v: k for k, v in nombres.items()}
    out = {}
    for k, v in cab.state_dict().items():
        if "rotary" in k:
            continue
        nk = inv.get(k) or ("mtp.layers.0." + k[len("capa.layers.0."):] if k.startswith("capa.layers.0.") else None)
        if nk:
            out[nk] = v.detach().to(torch.bfloat16).contiguous().cpu()
    falta = set(pesos) - set(out)
    if falta:
        sys.exit(f"no se guardaron {sorted(falta)[:5]}")
    save_file(out, args.salida, metadata={"format": "pt"})
    (Path(args.salida).with_suffix(".json")).write_text(json.dumps(
        {"aceptacion_antes": antes, "aceptacion_despues": despues, "pasos": total}, indent=1))
    print(f"-> {args.salida} ({len(out)} tensores)")


if __name__ == "__main__":
    main()
