#!/usr/bin/env python3
"""Entrenamiento del modelo reactivo, rapido: prefijo compartido y las dos placas.

DONDE SE IBA EL TIEMPO. Cada ejemplo es el MISMO prefijo de ~3.100 tokens (las
fichas de las 84 herramientas) mas ~50 propios (la frase y la respuesta). El
entrenador de unsloth procesaba el prefijo entero en cada ejemplo: con lote 16,
dieciseis veces lo mismo por paso. Mas del 95% del computo era repetir.

LO QUE HACE ESTE, y por que da lo mismo que antes:
  · En cada paso, el prefijo se procesa UNA vez por placa y los ejemplos de esa
    placa lo reusan (el cache de atencion se expande al lote). El gradiente
    vuelve por el prefijo igual que antes: el LoRA de todas las capas se
    entrena con la misma senal. Solo cambia que no se recalcula lo identico.
  · Las dos placas: torchrun con dos procesos; cada uno hace la mitad del lote y
    los gradientes se promedian a mano (all_reduce). Sin el envoltorio DDP, que
    con unsloth fallaba y que tampoco admite dos forward por paso.
  · Lo mismo que el anterior: LoRA r=32 sobre todas las lineales, alfa=r, sin
    dropout; AdamW lr 2e-4, decaimiento 0,01, calentamiento 5% y coseno; lote
    efectivo 16; perdida SOLO sobre la respuesta; evaluacion cada N pasos, se
    queda con el mejor por perdida de validacion; guarda checkpoints (los usa
    margen-frases para promediar el margen, como AUM).
  · Sin unsloth: transformers + peft pelados, bf16.

    torchrun --nproc_per_node 2 tools/entrenar/entrenar-rapido.py \\
        --datos ~/entrenamientos/corpus-v15 --salida ~/entrenamientos/reactivo-v15
"""
import argparse
import json
import math
import os
import random
import sys
import time
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(RAIZ / "tools"))
sys.path.insert(0, str(Path(__file__).resolve().parent))


def cargar(carpeta, nombre):
    return [json.loads(l) for l in (Path(carpeta) / nombre).read_text(encoding="utf-8").splitlines()
            if l.strip()]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", default="unsloth/Qwen3-1.7B")
    ap.add_argument("--datos", required=True)
    ap.add_argument("--salida", required=True)
    ap.add_argument("--epocas", type=float, default=2.0)
    ap.add_argument("--lr", type=float, default=2e-4)
    ap.add_argument("--rango", type=int, default=32)
    ap.add_argument("--lote", type=int, default=16, help="lote efectivo, repartido entre las placas")
    ap.add_argument("--eval-cada", type=int, default=100)
    ap.add_argument("--semilla", type=int, default=7)
    ap.add_argument("--pasos", type=int, default=0, help="cortar a N pasos (prueba)")
    # PARA LOS MODELOS QUE NO ENTRAN EN 24 GB (Qwen3.5-9B: 17 GB solo los pesos).
    ap.add_argument("--cuatro-bits", action="store_true",
                    help="QLoRA: el modelo base en 4 bits (NF4) y el LoRA en bf16 encima")
    ap.add_argument("--repartir", action="store_true",
                    help="un solo proceso con el modelo repartido entre las placas visibles (capas"
                         " 0..k en una, el resto en la otra; las activaciones pasan por P2P). Para"
                         " modelos que no entran en una placa: el 9B entra en bf16, sin cuantizar")
    ap.add_argument("--recalcular", action="store_true",
                    help="no guardar las activaciones del prefijo: se recalculan en el backward")
    args = ap.parse_args()

    import torch
    import torch.distributed as dist
    import torch.nn.functional as F
    from peft import LoraConfig, get_peft_model
    from transformers import AutoModelForCausalLM, AutoTokenizer, DynamicCache

    dist_on = "WORLD_SIZE" in os.environ and int(os.environ["WORLD_SIZE"]) > 1
    if dist_on:
        dist.init_process_group("nccl")
        rank, mundo = dist.get_rank(), dist.get_world_size()
    else:
        rank, mundo = 0, 1
    dev = torch.device(f"cuda:{int(os.environ.get('LOCAL_RANK', 0))}")
    torch.cuda.set_device(dev)
    torch.manual_seed(args.semilla)
    yo = rank == 0

    def log(*a):
        if yo:
            print(*a, flush=True)

    tok = AutoTokenizer.from_pretrained(args.base)
    ent, val = cargar(args.datos, "entrenar.jsonl"), cargar(args.datos, "validar.jsonl")

    # EL PREFIJO COMUN, sacado de los datos (con o sin etiquetas, es el de ese
    # corpus) y cortado donde empieza el turno del usuario, que es donde lo corta
    # lib_prompt.partes(). Si una fila no empieza asi, no se puede compartir.
    marca = "<|im_start|>user\n"
    p0 = ent[0]["prompt"]
    prefijo = p0[:p0.rindex(marca) + len(marca)]
    for f in ent + val:
        if not f["prompt"].startswith(prefijo):
            raise SystemExit("una fila no empieza con el prefijo comun: no se puede compartir")
    ids_pre = tok(prefijo, add_special_tokens=False).input_ids

    def armar(filas):
        """(ids del sufijo, cuantos de esos son la respuesta). Verifica que partir
        la tokenizacion en prefijo | sufijo | respuesta da lo mismo que el texto
        entero: si no, se entrenaria con otra secuencia que la que se sirve."""
        out, malas = [], 0
        for f in filas:
            suf = f["prompt"][len(prefijo):]
            a = tok(suf, add_special_tokens=False).input_ids
            b = tok(f["completion"], add_special_tokens=False).input_ids
            if tok(f["prompt"] + f["completion"], add_special_tokens=False).input_ids != ids_pre + a + b:
                malas += 1
                continue
            out.append((a + b, len(b)))
        return out, malas

    E, me = armar(ent)
    V, mv = armar(val)
    log(f"prefijo {len(ids_pre)} tokens · entrenar {len(E)} · validar {len(V)}"
        f" · descartadas por tokenizacion distinta: {me + mv}")
    ej_ids, ej_n = E[0]
    log("la perdida se calcula sobre:", repr(tok.decode(ej_ids[-ej_n:])))

    if args.cuatro_bits:
        from transformers import BitsAndBytesConfig
        q4 = BitsAndBytesConfig(load_in_4bit=True, bnb_4bit_quant_type="nf4",
                                bnb_4bit_compute_dtype=torch.bfloat16, bnb_4bit_use_double_quant=True)
        modelo = AutoModelForCausalLM.from_pretrained(args.base, dtype=torch.bfloat16, attn_implementation="sdpa",
                                                      quantization_config=q4, device_map={"": dev})
    elif args.repartir:
        if dist_on:
            raise SystemExit("--repartir es un solo proceso: correr con python, no con torchrun")
        # «balanced» reparte las capas parejo; la placa 0 lleva ademas la
        # entrada y la 1 la cabeza de salida, que con 248k tokens pesa.
        modelo = AutoModelForCausalLM.from_pretrained(args.base, dtype=torch.bfloat16,
                                                      attn_implementation="sdpa", device_map="balanced")
        log("repartido:", sorted({str(d) for d in modelo.hf_device_map.values()}))
    else:
        modelo = AutoModelForCausalLM.from_pretrained(args.base, dtype=torch.bfloat16,
                                                      attn_implementation="sdpa").to(dev)
    modelo.config.use_cache = True
    import cache_hibrido as ch
    hibrido = ch.es_hibrido(modelo)
    blancos = ["q_proj", "k_proj", "v_proj", "o_proj", "gate_proj", "up_proj", "down_proj"]
    if hibrido:
        # Qwen3.5: las capas de atencion lineal tienen otras proyecciones. TODAS
        # las lineales, como con Qwen3 (in_proj_a/b son de 16 salidas: no suman).
        ch.parchear()
        blancos += ["in_proj_qkv", "in_proj_z", "out_proj"]
    log(f"modelo {type(modelo).__name__} · {'hibrido (atencion lineal + completa)' if hibrido else 'atencion completa'}")
    cfg = LoraConfig(r=args.rango, lora_alpha=args.rango, lora_dropout=0.0, bias="none",
                     target_modules=blancos, task_type="CAUSAL_LM")
    modelo = get_peft_model(modelo, cfg)
    entrenables = [p for p in modelo.parameters() if p.requires_grad]
    for p in entrenables:
        p.data = p.data.float()
    if dist_on:  # mismos pesos iniciales en las dos placas
        for p in entrenables:
            dist.broadcast(p.data, 0)
    opt = torch.optim.AdamW(entrenables, lr=args.lr, weight_decay=0.01, fused=True)
    ids_pre_t = torch.tensor([ids_pre], device=dev)
    Lp = len(ids_pre)

    def perdida(lote, grad=True):
        """Prefijo una vez; sufijos en lote sobre el cache expandido."""
        with torch.autocast("cuda", dtype=torch.bfloat16), torch.set_grad_enabled(grad):
            if args.recalcular and grad:
                # El prefijo dentro de un checkpoint: la funcion arma su propio
                # cache y devuelve solo los tensores del estado, asi el recalculo
                # del backward no pisa el cache del lote.
                def prefijo_fn(ids):
                    k0 = DynamicCache(config=modelo.config)
                    modelo(input_ids=ids, past_key_values=k0, use_cache=True)
                    return tuple(ch.tensores(k0))
                ts = torch.utils.checkpoint.checkpoint(prefijo_fn, ids_pre_t, use_reentrant=False)
                kv = ch.desde_tensores(modelo.config, list(ts))
            else:
                kv = DynamicCache(config=modelo.config)
                modelo(input_ids=ids_pre_t, past_key_values=kv, use_cache=True)
            if hibrido:
                ch.repetir(kv, len(lote))
            else:
                kv.batch_repeat_interleave(len(lote))
            L = max(len(s) for s, _ in lote)
            ids = torch.full((len(lote), L), tok.pad_token_id or 0, device=dev)
            mask = torch.zeros((len(lote), Lp + L), dtype=torch.long, device=dev)
            mask[:, :Lp] = 1
            etq = torch.full((len(lote), L), -100, device=dev)
            for i, (s, n) in enumerate(lote):
                ids[i, :len(s)] = torch.tensor(s, device=dev)
                mask[i, Lp:Lp + len(s)] = 1
                # la etiqueta del token t es el token t+1: se predicen los n de la
                # respuesta desde la posicion anterior a cada uno
                etq[i, len(s) - n - 1:len(s) - 1] = torch.tensor(s[len(s) - n:], device=dev)
            pos = torch.arange(Lp, Lp + L, device=dev).unsqueeze(0).expand(len(lote), -1)
            lg = modelo(input_ids=ids, attention_mask=mask, position_ids=pos,
                        past_key_values=kv, use_cache=True).logits
            etq = etq.to(lg.device)  # con --repartir los logits salen en la ultima placa
            suma = F.cross_entropy(lg.float().reshape(-1, lg.shape[-1]), etq.reshape(-1),
                                   ignore_index=-100, reduction="sum")
            return suma, (etq != -100).sum()

    por_placa = args.lote // mundo
    pasos_epoca = len(E) // args.lote
    total = int(pasos_epoca * args.epocas) if not args.pasos else args.pasos
    calor = max(1, int(0.05 * total))

    def lr_en(paso):
        if paso < calor:
            return args.lr * (paso + 1) / calor
        return args.lr * 0.5 * (1 + math.cos(math.pi * (paso - calor) / max(1, total - calor)))

    def evaluar():
        modelo.eval()
        mias = V[rank::mundo]
        s = torch.zeros(2, device=dev)
        for i in range(0, len(mias), 32):
            a, b = perdida(mias[i:i + 32], grad=False)
            s += torch.stack([a.detach(), b.float()]).to(dev)
        if dist_on:
            dist.all_reduce(s)
        modelo.train()
        return (s[0] / s[1]).item()

    def guardar(destino):
        if yo:
            modelo.save_pretrained(destino)
            tok.save_pretrained(destino)

    rnd = random.Random(args.semilla)
    orden = []
    mejor, mejor_paso = float("inf"), -1
    Path(args.salida).mkdir(parents=True, exist_ok=True)
    guardados = []
    t0 = time.time()
    modelo.train()
    log(f"pasos {total} · lote {args.lote} ({por_placa} por placa x {mundo}) · calentamiento {calor}")
    for paso in range(total):
        # el resto que no llena un lote se descarta al final de cada epoca (como
        # hace el entrenador de transformers): con un lote corto, una placa podia
        # quedarse sin ejemplos y caerse (paso con v15: 7.800 % 16 = 8)
        if len(orden) < args.lote:
            orden = list(range(len(E)))
            rnd.shuffle(orden)  # misma semilla en las dos placas: mismo orden
        lote_ids = orden[:args.lote]
        orden = orden[args.lote:]
        mio = [E[i] for i in lote_ids[rank * por_placa:(rank + 1) * por_placa]]
        for g in opt.param_groups:
            g["lr"] = lr_en(paso)
        suma, n = perdida(mio)
        n_tot = n.float()
        if dist_on:
            dist.all_reduce(n_tot)
        (suma / n_tot * mundo).backward()   # promedio por token sobre TODO el lote
        if dist_on:
            for p in entrenables:
                dist.all_reduce(p.grad)
                p.grad /= mundo
        opt.step()
        opt.zero_grad(set_to_none=True)
        if yo and paso == 0:
            mem = " + ".join(f"{torch.cuda.max_memory_allocated(i) / 2**30:.1f}" for i in range(torch.cuda.device_count()))
            print(f"memoria maxima en el primer paso: {mem} GiB", flush=True)
        if yo and (paso + 1) % 10 == 0:
            dt = time.time() - t0
            print(f"paso {paso + 1}/{total} · perdida {(suma / n).item():.4f} · "
                  f"{dt / (paso + 1):.2f} s/paso · faltan {dt / (paso + 1) * (total - paso - 1) / 60:.0f} min",
                  flush=True)
        if (paso + 1) % args.eval_cada == 0 or paso + 1 == total:
            vl = evaluar()
            log(f"  eval paso {paso + 1}: perdida de validacion {vl:.4f}")
            cp = Path(args.salida) / f"checkpoint-{paso + 1}"
            guardar(cp)
            guardados.append(cp)
            if vl < mejor:
                mejor, mejor_paso = vl, paso + 1
                if yo:
                    estado_mejor = {k: v.detach().cpu().clone()
                                    for k, v in modelo.state_dict().items() if "lora_" in k}
            # quedan los dos ultimos y el mejor
            if yo:
                import shutil
                for c in guardados[:-2]:
                    if c.name != f"checkpoint-{mejor_paso}" and c.exists():
                        shutil.rmtree(c)
    if yo:
        modelo.load_state_dict(estado_mejor, strict=False)
        guardar(args.salida)
        import lib_prompt as _lp
        (Path(args.salida) / "huella-prompt.json").write_text(
            json.dumps(_lp.huella(), ensure_ascii=False, indent=1), encoding="utf-8")
        print(f"listo: mejor paso {mejor_paso} (validacion {mejor:.4f}) · "
              f"{(time.time() - t0) / 60:.1f} min · {args.salida}", flush=True)
    if dist_on:
        dist.barrier()
        dist.destroy_process_group()


if __name__ == "__main__":
    main()
