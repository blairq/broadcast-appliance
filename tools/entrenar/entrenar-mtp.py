#!/usr/bin/env python3
"""Entrenar la cabeza MTP (multi-token prediction) de Qwen3.5 para NUESTRO modelo ajustado.

POR QUE. llama.cpp (y un servidor MLX con MTP) la usa para la decodificacion
especulativa: la cabeza adivina varios tokens por delante y el modelo los
verifica de una. Rinde tanto como acierte, y la de fabrica adivina lo que
contesta el modelo DE FABRICA. Se la reentrena con el modelo principal
CONGELADO para que adivine lo que contesta el ajustado.

QUE ES, igual que en llama.cpp (src/models/qwen35.cpp y common/speculative.cpp, b11255):
  h      el estado del modelo DESPUES de la norma final
  x      = mtp.fc( [ RMSNorm_e(emb(token)) , RMSNorm_h(h) ] )
  x      una capa de atencion completa de Qwen3.5 (con su propio cache)
  salida = mtp.norm(x);  logits = lm_head(salida)
Borrador 1: h del modelo en la posicion i y el token i+1 -> el token i+2.
Borrador k>1: EN CADENA. La cabeza recibe SU PROPIA salida del paso anterior
(como h) y el token que acaba de adivinar, en la posicion siguiente.

LO QUE ESTABA MAL (version anterior): se entrenaba solo el borrador 1, y solo
sobre los tokens del nombre de herramienta. En vivo, con 3 borradores, los
pasos 2 y 3 reciben una entrada que la cabeza nunca vio en el entrenamiento; y
en texto libre la cabeza, ajustada solo a nombres, adivinaba peor que la de
fabrica. Ahora:

  EN CADENA (como EAGLE-3, «training-time test»): los pasos 1..--pasos-mtp se
  entrenan juntos. Cada paso k ve EXACTAMENTE el contexto que tendria en vivo:
  las entradas del paso 1 hasta la base de la cadena, mas las de los pasos
  2..k-1 de esa misma cadena (una mascara de atencion arma eso para todas las
  posiciones a la vez). Peso 0,8^(k-1) por paso.

  CON TEXTO: ademas de los nombres (con el prefijo de las fichas, compartido),
  texto escrito por el PROPIO modelo ajustado (greedy) respondiendo frases del
  operador y pedidos sobre fragmentos de docs/. Lo que la cabeza tiene que
  adivinar es lo que el modelo va a escribir, asi que el texto sale de el. Se
  genera una vez y queda en <salida>-texto.jsonl.

MEDIDA: tokens aceptados por ciclo (cuantos borradores seguidos acierta, de 0 a
--pasos-mtp) y la tasa por paso (dado que los anteriores acertaron), en
validacion, por separado en nombres y en texto: con la cabeza de fabrica y con
la entrenada.

    tools/entrenar/entrenar-mtp.py --modelo <fusionado-hf> --base-ruta <snapshot de la base> \\
        --datos <corpus> --salida mtp.safetensors [--epocas 1] [--repartir]
"""
import argparse
import copy
import json
import math
import random
import re
import shutil
import sys
import time
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(RAIZ / "tools"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

SISTEMA_TEXTO = ("Sos el asistente de un estudio de television en vivo. Contestale al operador en espanol, "
                 "claro y concreto.")
PEDIDOS_DOC = ["Resumi en pocas lineas:", "Explicalo con tus palabras:", "Hace una lista con los puntos clave de esto:",
               "Que problema resuelve esto?", "Escribi un mensaje corto para el equipo contando esto:"]


PEDIDOS_FRASE = ["{f}. Explicame que haria falta y como se hace.",
                 "Contame en dos o tres frases que pasa en el estudio si hago esto: {f}",
                 "{f}. Que tengo que revisar antes de hacerlo?",
                 "Si un operador nuevo me dice «{f}», como se lo explico?",
                 "{f}. Que puede salir mal y como lo evito?"]


def prompts_texto(n, semilla):
    """Pedidos variados para que el modelo escriba: frases del operador (las
    SEMILLAS de entrenamiento; nunca el banco ni el examen) y fragmentos de docs/."""
    rnd = random.Random(semilla)
    sem = json.loads((RAIZ / "datos/semillas-reactivas.json").read_text())["herramientas"]
    frases = [f for v in sem.values() for f in v.get("frases", [])]
    trozos = []
    for f in sorted((RAIZ / "docs").rglob("*.md")):
        for p in re.split(r"\n\s*\n", f.read_text(errors="ignore")):
            p = p.strip()
            if 250 <= len(p) <= 1200 and not p.startswith(("|", "```")):
                trozos.append(p)
    rnd.shuffle(frases)
    rnd.shuffle(trozos)
    # siempre como pedido de explicacion: la frase sola la contesta con un nombre
    # (2-3 tokens), y eso ya esta en los nombres; aca hace falta prosa
    out = [rnd.choice(PEDIDOS_FRASE).format(f=f) for f in frases]
    out += [f"{rnd.choice(PEDIDOS_DOC)}\n\n{t}" for t in trozos]
    rnd.shuffle(out)
    return out[:n]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--modelo", required=True, help="el modelo ajustado y fusionado (carpeta HF)")
    ap.add_argument("--base-ruta", required=True, help="snapshot de la base: de ahi sale la cabeza original")
    ap.add_argument("--datos", required=True)
    ap.add_argument("--salida", required=True)
    ap.add_argument("--epocas", type=float, default=1.0)
    ap.add_argument("--lr", type=float, default=1e-4)
    ap.add_argument("--lote", type=int, default=16, help="ejemplos de nombres por paso")
    ap.add_argument("--lote-texto", type=int, default=8, help="ejemplos de texto por paso")
    ap.add_argument("--peso-texto", type=float, default=2.0,
                    help="cuanto pesa el texto en la perdida: los nombres se aprenden enseguida y tiran para su lado")
    ap.add_argument("--elegir-cada", type=int, default=100,
                    help="cada cuantos pasos se mide (en una parte aparte de validacion) para quedarse con la mejor")
    ap.add_argument("--pasos-mtp", type=int, default=3, help="borradores en cadena que se entrenan (y miden)")
    ap.add_argument("--texto", type=int, default=2400, help="respuestas de texto a generar (0: solo nombres)")
    ap.add_argument("--largo-texto", type=int, default=160, help="tokens maximos por respuesta generada")
    ap.add_argument("--pasos", type=int, default=0, help="cortar a N pasos (prueba)")
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
    K = args.pasos_mtp
    PESO = [0.8 ** k for k in range(K)]
    tok = AutoTokenizer.from_pretrained(args.modelo)
    dm = "balanced" if args.repartir else {"": "cuda:0"}
    cfg_m = json.loads((Path(args.modelo) / "config.json").read_text())
    if cfg_m.get("model_type") == "qwen3_5":  # el que se exporto para mlx-lm
        cfg_m["model_type"] = "qwen3_5_text"
        tmp = Path(args.salida).parent / "modelo-hf-mtp-config"
        shutil.rmtree(tmp, ignore_errors=True)  # enlaces viejos (a un fusionado que ya no esta) rompen
        tmp.mkdir(parents=True)
        for f in Path(args.modelo).iterdir():
            if f.name != "config.json":
                (tmp / f.name).symlink_to(f.resolve())
        (tmp / "config.json").write_text(json.dumps(cfg_m))
        ruta_modelo = tmp
    else:
        ruta_modelo = Path(args.modelo)
    modelo = AutoModelForCausalLM.from_pretrained(ruta_modelo, dtype=torch.bfloat16, device_map=dm,
                                                  attn_implementation="sdpa").eval()
    for p in modelo.parameters():
        p.requires_grad_(False)
    hibrido = ch.es_hibrido(modelo)
    texto_m, cabeza, emb = modelo.model, modelo.lm_head, modelo.model.embed_tokens
    dev_out = cabeza.weight.device
    cfg = modelo.config
    n_mtp = getattr(cfg, "mtp_num_hidden_layers", 0)
    if n_mtp != 1:
        sys.exit(f"este modelo declara {n_mtp} capas MTP; se soporta 1 (como llama.cpp)")
    dev = emb.weight.device
    fin_tok = tok.convert_tokens_to_ids("<|im_end|>")

    # --- el texto: lo escribe el propio modelo (antes del parche del cache) ---
    texto_f = Path(str(args.salida).replace(".safetensors", "") + "-texto.jsonl")
    if args.texto and not texto_f.exists():
        pedidos = prompts_texto(args.texto, args.semilla)
        tok.padding_side = "left"
        filas, t0 = [], time.time()
        for i in range(0, len(pedidos), 32):
            ps = [f"<|im_start|>system\n{SISTEMA_TEXTO}<|im_end|>\n<|im_start|>user\n{p}<|im_end|>\n"
                  f"<|im_start|>assistant\n<think>\n\n</think>\n\n" for p in pedidos[i:i + 32]]
            enc = tok(ps, return_tensors="pt", padding=True, add_special_tokens=False).to(dev)
            with torch.no_grad():
                gen = modelo.generate(**enc, max_new_tokens=args.largo_texto, do_sample=False,
                                      pad_token_id=tok.pad_token_id or fin_tok)
            for j in range(len(ps)):
                ids_p = enc.input_ids[j][enc.attention_mask[j].bool()].tolist()
                r = gen[j, enc.input_ids.shape[1]:].tolist()
                r = r[:r.index(fin_tok) + 1] if fin_tok in r else r
                if len(r) >= 8:
                    filas.append({"prompt": ids_p, "respuesta": r})
            print(f"texto: {len(filas)} respuestas · {time.time() - t0:.0f} s", flush=True)
        texto_f.write_text("\n".join(json.dumps(f) for f in filas))
        tok.padding_side = "right"
    if hibrido:
        ch.parchear()

    # LA CABEZA: una capa de atencion completa con su norma final, mas fc y normas de entrada
    cfg1 = copy.deepcopy(cfg)
    cfg1.num_hidden_layers, cfg1.layer_types = 1, ["full_attention"]
    H = cfg.hidden_size

    class Cabeza(nn.Module):
        def __init__(self):
            super().__init__()
            self.capa = q.Qwen3_5TextModel(cfg1)  # da los nombres de los pesos, la mRoPE y mtp.norm
            self.capa.embed_tokens = None  # usa el del modelo
            self.fc = nn.Linear(2 * H, H, bias=False)
            self.enorm = q.Qwen3_5RMSNorm(H, eps=cfg.rms_norm_eps)
            self.hnorm = q.Qwen3_5RMSNorm(H, eps=cfg.rms_norm_eps)

        def entrada(self, h, e):
            return self.fc(torch.cat([self.enorm(e), self.hnorm(h)], dim=-1))

        def qkv(self, x, pos):
            """La primera mitad de la capa: norma, proyecciones, q/k_norm y mRoPE
            (como Qwen3_5Attention.forward, sin el cache)."""
            lay = self.capa.layers[0]
            at = lay.self_attn
            B, N, _ = x.shape
            d = at.head_dim
            y = lay.input_layernorm(x)
            qq, gate = torch.chunk(at.q_proj(y).view(B, N, -1, 2 * d), 2, dim=-1)
            gate = gate.reshape(B, N, -1)
            qq = at.q_norm(qq).transpose(1, 2)
            kk = at.k_norm(at.k_proj(y).view(B, N, -1, d)).transpose(1, 2)
            vv = at.v_proj(y).view(B, N, -1, d).transpose(1, 2)
            cos, sin = self.capa.rotary_emb(y, pos[None, None].expand(3, B, N))
            qq, kk = q.apply_rotary_pos_emb(qq, kk, cos, sin)
            return qq, kk, vv, gate

        def fin(self, x, a, gate):
            lay = self.capa.layers[0]
            B, N, _ = x.shape
            a = a.transpose(1, 2).reshape(B, N, -1) * torch.sigmoid(gate)
            x = x + lay.self_attn.o_proj(a)
            x = x + lay.mlp(lay.post_attention_layernorm(x))
            return self.capa.norm(x)

    escala = None

    def cadena(cab, h1, e, g, val, pasos, pre=None):
        """Los `pasos` borradores en cadena para todas las posiciones a la vez.

        h1, e: [B,N,H] el h del modelo y el embedding del token que entra, por
        posicion; g: [N] su indice global (tambien la posicion de mRoPE); val:
        [B,N] posiciones reales (no relleno). pre: (K,V,g,salida_ultima) del
        paso 1 sobre un prefijo compartido. El paso k en la posicion g es la
        cadena con base b = g-k+1: ve el paso 1 hasta b y los pasos 2..k-1 de
        esa cadena (el paso m en b+m-1); y a si mismo siempre (sin filas vacias)."""
        B, N, _ = h1.shape
        Ks, Vs, outs = [], [], []
        gq = g[:, None]
        diag = torch.eye(N, dtype=torch.bool, device=g.device)[None]
        for k in range(1, pasos + 1):
            if k == 1:
                hin = h1
            else:
                primero = (pre[3] if pre is not None else outs[-1][:, :1]).expand(B, 1, -1)
                hin = torch.cat([primero, outs[-1][:, :-1]], dim=1)
            x = cab.entrada(hin, e)
            qq, kk, vv, gate = cab.qkv(x, g)
            Ks.append(kk)
            Vs.append(vv)
            mk, kt, vt = [], [], []
            if pre is not None:
                mk.append((pre[2][None, :] <= gq - k + 1)[None].expand(B, -1, -1))
                kt.append(pre[0].expand(B, -1, -1, -1))
                vt.append(pre[1].expand(B, -1, -1, -1))
            for m in range(1, k + 1):
                regla = (g[None, :] <= gq - k + 1) if m == 1 else (g[None, :] == gq - k + m)
                regla = regla[None] & val[:, None, :]
                if m == k:
                    regla = regla | diag
                mk.append(regla)
                kt.append(Ks[m - 1])
                vt.append(Vs[m - 1])
            a = F.scaled_dot_product_attention(qq, torch.cat(kt, 2), torch.cat(vt, 2),
                                               attn_mask=torch.cat(mk, 2)[:, None], enable_gqa=True,
                                               scale=escala)
            outs.append(cab.fin(x, a, gate))
        return outs

    cab = Cabeza()
    escala = cab.capa.layers[0].self_attn.scaling
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
    cab = cab.to(dev).float()

    # --- los datos ---
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
                out.append((a + b, len(a)))
        return out
    E, V = armar(ent), armar(val)
    TX, TV = [], []
    if texto_f.exists():
        tx = [json.loads(l) for l in texto_f.read_text().splitlines() if l.strip()]
        tx = [(f["prompt"] + f["respuesta"], len(f["prompt"])) for f in tx]
        corte = max(1, len(tx) // 10)
        TV, TX = tx[:corte], tx[corte:]
    print(f"nombres: {len(E)} entrenar / {len(V)} validar · texto: {len(TX)} / {len(TV)}", flush=True)
    Lp = len(ids_pre)
    pre_t = torch.tensor([ids_pre], device=dev)

    def objetivos(outs, tgt, base_ok):
        """Perdida (con peso por paso) y aciertos por cadena.

        tgt: [B,N] el token que se adivina en cada posicion (-100 si no hay);
        base_ok: [B,N] si una cadena puede EMPEZAR ahi (el modelo acaba de poner
        un token de la respuesta). El paso k en n es de la cadena con base n-k+1.
        Devuelve por paso (acierto, hay), alineados por BASE."""
        B, N = tgt.shape
        perd = 0.0
        bien = []
        for k, out in enumerate(outs, start=1):
            sel = torch.zeros_like(base_ok)
            sel[:, k - 1:] = base_ok[:, :N - k + 1]
            sel &= tgt != -100
            acierto = torch.zeros_like(sel)
            if sel.any():
                lg = cabeza(out[sel].to(dev_out).to(cabeza.weight.dtype)).float()
                t = tgt[sel].to(dev_out)
                perd = perd + PESO[k - 1] * F.cross_entropy(lg, t, reduction="sum")
                acierto[sel] = (lg.argmax(-1) == t).to(sel.device)
            por_base, hay = torch.zeros_like(sel), torch.zeros_like(sel)
            por_base[:, :N - k + 1] = acierto[:, k - 1:]
            hay[:, :N - k + 1] = sel[:, k - 1:]
            bien.append((por_base, hay))
        return perd, bien

    def lote_nombres(lote, grad):
        """Prefijo de las fichas compartido (la parte del modelo y el paso 1 de la cabeza)."""
        B = len(lote)
        L = max(len(s) for s, _ in lote)
        ids = torch.zeros((B, L), dtype=torch.long, device=dev)
        mask = torch.zeros((B, Lp + L), dtype=torch.long, device=dev)
        mask[:, :Lp] = 1
        tgt = torch.full((B, L), -100, dtype=torch.long, device=dev)
        base_ok = torch.zeros((B, L), dtype=torch.bool, device=dev)
        for i, (s, a0) in enumerate(lote):
            ids[i, :len(s)] = torch.tensor(s, device=dev)
            mask[i, Lp:Lp + len(s)] = 1
            # en la posicion n entra el token s[n] y se adivina s[n+1]; una cadena
            # empieza donde el modelo acaba de poner un token de la respuesta
            tgt[i, :len(s) - 1] = torch.tensor(s[1:], device=dev)
            base_ok[i, a0:len(s) - 1] = True
        with torch.no_grad(), torch.autocast("cuda", dtype=torch.bfloat16):
            kv = DynamicCache(config=cfg)
            hp = texto_m(input_ids=pre_t, past_key_values=kv, use_cache=True).last_hidden_state
            ch.repetir(kv, B) if hibrido else kv.batch_repeat_interleave(B)
            pos = torch.arange(Lp, Lp + L, device=dev).unsqueeze(0).expand(B, -1)
            hs = texto_m(input_ids=ids, attention_mask=mask, position_ids=pos, past_key_values=kv,
                         use_cache=True).last_hidden_state
            e_pre, e_suf = emb(pre_t[:, 1:]), emb(ids)
        with torch.set_grad_enabled(grad), torch.autocast("cuda", dtype=torch.bfloat16):
            g_pre = torch.arange(0, Lp - 1, device=dev)
            x = cab.entrada(hp[:, :-1].to(dev), e_pre)
            qq, kk, vv, gate = cab.qkv(x, g_pre)
            a = F.scaled_dot_product_attention(qq, kk, vv, is_causal=True, enable_gqa=True, scale=escala)
            out_pre = cab.fin(x, a, gate)
            pre = (kk, vv, g_pre, out_pre[:, -1:])
            h1 = torch.cat([hp[:, -1:].expand(B, -1, -1), hs[:, :-1]], dim=1).to(dev)
            g = torch.arange(Lp - 1, Lp - 1 + L, device=dev)
            outs = cadena(cab, h1, e_suf, g, mask[:, Lp:].bool(), K, pre)
            return objetivos(outs, tgt, base_ok)

    def lote_texto(lote, grad):
        """Cada respuesta con su propio pedido, entera (sin prefijo compartido)."""
        B = len(lote)
        T = max(len(s) for s, _ in lote)
        ids = torch.zeros((B, T), dtype=torch.long, device=dev)
        mask = torch.zeros((B, T), dtype=torch.long, device=dev)
        for i, (s, _) in enumerate(lote):
            ids[i, :len(s)] = torch.tensor(s, device=dev)
            mask[i, :len(s)] = 1
        with torch.no_grad(), torch.autocast("cuda", dtype=torch.bfloat16):
            hs = texto_m(input_ids=ids, attention_mask=mask).last_hidden_state
            e = emb(ids[:, 1:])
        # posicion n (0..T-2): h del modelo en n y el token n+1 -> el token n+2
        N = T - 1
        tgt = torch.full((B, N), -100, dtype=torch.long, device=dev)
        base_ok = torch.zeros((B, N), dtype=torch.bool, device=dev)
        for i, (s, a0) in enumerate(lote):
            tgt[i, :len(s) - 2] = torch.tensor(s[2:], device=dev)
            base_ok[i, a0 - 1:len(s) - 2] = True     # entra (n+1) un token de la respuesta
        with torch.set_grad_enabled(grad), torch.autocast("cuda", dtype=torch.bfloat16):
            outs = cadena(cab, hs[:, :-1].to(dev), e, torch.arange(N, device=dev), mask[:, 1:].bool(), K)
            return objetivos(outs, tgt, base_ok)

    def medir(filas, fn, tam):
        """Tokens aceptados por ciclo (borradores seguidos acertados) y la tasa
        de cada paso dado que los anteriores acertaron."""
        cab.eval()
        suma = bases = 0
        por_paso = [[0, 0] for _ in range(K)]
        for i in range(0, len(filas), tam):
            _, bien = fn(filas[i:i + tam], grad=False)
            seguido = None
            for k, (ok, hay) in enumerate(bien):
                cond = hay if seguido is None else hay & seguido
                if seguido is None:
                    bases += hay.sum().item()
                por_paso[k][0] += (ok & cond).sum().item()
                por_paso[k][1] += cond.sum().item()
                seguido = ok & cond
                suma += seguido.sum().item()
        cab.train()
        return {"aceptados_por_ciclo": suma / max(1, bases), "por_paso": [a / max(1, b) for a, b in por_paso]}

    # validacion en dos partes: una para ELEGIR la mejor cabeza durante el
    # entrenamiento y otra, que no participa de esa eleccion, para INFORMAR
    VS, V = V[:300], V[300:]
    TVS, TV = TV[:len(TV) // 2], TV[len(TV) // 2:]

    def informe(etiqueta, nombres=None, texto=None, callado=False):
        nombres = V if nombres is None else nombres
        texto = TV if texto is None else texto
        r = {"nombres": medir(nombres, lote_nombres, 32)}
        if texto:
            r["texto"] = medir(texto, lote_texto, 8)
        if callado:
            return r
        for k, v in r.items():
            print(f"{etiqueta} · {k}: {v['aceptados_por_ciclo']:.2f} tokens aceptados por ciclo de {K} · por paso "
                  + " / ".join(f"{100 * x:.1f}%" for x in v["por_paso"]), flush=True)
        return r

    antes = informe("cabeza ORIGINAL")

    def puntaje(r):
        """Tokens aceptados por ciclo en nombres + texto."""
        return sum(v["aceptados_por_ciclo"] for v in r.values())
    sel0 = informe("", VS, TVS, callado=True)
    piso_texto = sel0.get("texto", {}).get("aceptados_por_ciclo", 0.0) - 0.01
    mejor = {"puntaje": puntaje(sel0), "paso": 0,
             "pesos": {k: v.detach().clone().cpu() for k, v in cab.state_dict().items()}}

    def elegir(paso):
        """La MEJOR cabeza hasta ahora: mas tokens aceptados (nombres + texto) sin
        aceptar menos texto que la original. La original tambien es candidata."""
        r = informe("", VS, TVS, callado=True)
        ok = r.get("texto", {}).get("aceptados_por_ciclo", 1.0) >= piso_texto
        p = puntaje(r)
        print(f"  eleccion paso {paso}: nombres {r['nombres']['aceptados_por_ciclo']:.2f}"
              + (f" · texto {r['texto']['aceptados_por_ciclo']:.2f}" if "texto" in r else "")
              + ("" if ok else " (menos texto que la original: no sirve)"), flush=True)
        if ok and p > mejor["puntaje"]:
            mejor.update(puntaje=p, paso=paso,
                         pesos={k: v.detach().clone().cpu() for k, v in cab.state_dict().items()})
    opt = torch.optim.AdamW([p for p in cab.parameters() if p.requires_grad], lr=args.lr, weight_decay=0.0)
    total = args.pasos or int(len(E) // args.lote * args.epocas)
    rnd = random.Random(args.semilla)
    orden, orden_t, t0 = [], [], time.time()
    for paso in range(total):
        if len(orden) < args.lote:
            orden = list(range(len(E)))
            rnd.shuffle(orden)
        lote, orden = [E[i] for i in orden[:args.lote]], orden[args.lote:]
        # calentamiento (5%): el primer golpe a lr pleno tumbaba lo que la cabeza
        # ya sabia de texto; despues coseno
        calor = max(1, total // 20)
        for g_ in opt.param_groups:
            g_["lr"] = (args.lr * (paso + 1) / calor if paso < calor else
                        args.lr * 0.5 * (1 + math.cos(math.pi * (paso - calor) / max(1, total - calor))))
        perd, bien = lote_nombres(lote, grad=True)
        # con --repartir la perdida queda en la placa de lm_head, no en la de la cabeza
        n = bien[0][1].sum().clamp(min=1).to(perd.device)
        (perd / n).backward()
        if TX:
            if len(orden_t) < args.lote_texto:
                orden_t = list(range(len(TX)))
                rnd.shuffle(orden_t)
            lt, orden_t = [TX[i] for i in orden_t[:args.lote_texto]], orden_t[args.lote_texto:]
            perd_t, bien_t = lote_texto(lt, grad=True)
            n_t = bien_t[0][1].sum().clamp(min=1).to(perd_t.device)
            (args.peso_texto * perd_t / n_t).backward()
        opt.step()
        opt.zero_grad(set_to_none=True)
        if (paso + 1) % 50 == 0:
            print(f"paso {paso + 1}/{total} · perdida nombres {(perd / n).item():.3f}"
                  + (f" · texto {(perd_t / n_t).item():.3f}" if TX else "")
                  + f" · {(time.time() - t0) / (paso + 1):.2f} s/paso", flush=True)
        if (paso + 1) % args.elegir_cada == 0 or paso + 1 == total:
            elegir(paso + 1)
    cab.load_state_dict({k: v.to(dev) for k, v in mejor["pesos"].items()})
    print(f"se queda la cabeza del paso {mejor['paso']} (0 = la original)", flush=True)
    despues = informe("cabeza ENTRENADA")
    # Lo que cuesta cuantizar las lineales de la capa (como MTPLX: int4; o q8),
    # simulado: afin por grupos de 64 como MLX, y se mide igual.
    def cuantizar_falso(w, bits):
        W = w.float().reshape(-1, 64)
        mn, mx_ = W.min(1, keepdim=True).values, W.max(1, keepdim=True).values
        esc = (mx_ - mn).clamp(min=1e-8) / (2 ** bits - 1)
        return (torch.round((W - mn) / esc).clamp(0, 2 ** bits - 1) * esc + mn).reshape(w.shape).to(w.dtype)
    lineales = [m for m in cab.capa.layers[0].modules() if isinstance(m, nn.Linear)]
    originales = [m.weight.data.clone() for m in lineales]
    cuantizadas = {}
    for bits in (8, 4):
        for m, w0 in zip(lineales, originales):
            m.weight.data = cuantizar_falso(w0, bits)
        cuantizadas[bits] = informe(f"cabeza ENTRENADA, lineales q{bits}")
    for m, w0 in zip(lineales, originales):
        m.weight.data = w0
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
        {"pasos_mtp": K, "antes": antes, "despues": despues, "pasos": total, "paso_elegido": mejor["paso"],
         "despues_q8": cuantizadas[8], "despues_q4": cuantizadas[4],
         # lo que lee la tarjeta: el paso 1 en nombres, y ahora el resto
         "aceptacion_antes": antes["nombres"]["por_paso"][0],
         "aceptacion_despues": despues["nombres"]["por_paso"][0],
         "texto_respuestas": len(TX) + len(TV)}, indent=1))
    print(f"-> {args.salida} ({len(out)} tensores)")


if __name__ == "__main__":
    main()
