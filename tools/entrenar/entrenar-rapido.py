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

LAS DOS PLACAS COMO UNA (--fsdp, el modo de siempre desde la v4). Se llama con
python, no con torchrun: este proceso SUPERVISA (lanza torchrun en las placas
visibles y, si la corrida se cae, la relanza desde el ultimo estado guardado;
si fue por memoria, con micro-lotes a la mitad). Los pesos se reparten entre
las placas (FSDP2, ver repartido.py) y cada una calcula la mitad del lote a la
vez. Cada --guardar-cada-min minutos queda <salida>/estado/estado.pt (LoRA,
optimizador, paso, barajado, mejor): lo maximo que se pierde por un corte.

    python tools/entrenar/entrenar-rapido.py --fsdp --recalcular --micro 4 \\
        --base Qwen/Qwen3.5-4B --datos <corpus> --salida <adaptador>
"""
import argparse
import hashlib
import json
import math
import os
import random
import signal
import subprocess
import sys
import time
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(RAIZ / "tools" / "entrenar"))
sys.path.insert(0, str(Path(__file__).resolve().parent))


def cargar(carpeta, nombre):
    return [json.loads(l) for l in (Path(carpeta) / nombre).read_text(encoding="utf-8").splitlines()
            if l.strip()]


def supervisar(args):
    """Lanza torchrun y relanza desde el ultimo estado si la corrida se cae."""
    import torch
    placas = torch.cuda.device_count()
    estado = Path(args.salida) / "estado"
    marca = estado / "sin-memoria"
    micro = args.micro
    base = []
    saltar = False
    for a in sys.argv[1:]:  # sin --micro N ni --reanudar: los pone el supervisor
        if saltar:
            saltar = False
        elif a == "--micro":
            saltar = True
        elif a != "--reanudar":
            base.append(a)
    for intento in range(1, 6):
        cmd = [str(Path(sys.executable).parent / "torchrun"), "--nproc_per_node", str(placas),
               "--master_port", str(29500 + os.getpid() % 1000), __file__] + base + ["--micro", str(micro)]
        if (estado / "estado.pt").exists() and (args.reanudar or intento > 1):
            cmd.append("--reanudar")
        # torchrun en su propio grupo de procesos: si se cae (o lo matan) sus hijos
        # quedaban vivos con la placa ocupada y el relanzamiento no tenia memoria.
        # Antes de relanzar se termina el grupo entero
        p = subprocess.Popen(cmd, start_new_session=True)
        try:
            rc = p.wait()
        except KeyboardInterrupt:
            os.killpg(p.pid, signal.SIGTERM)
            raise
        try:
            os.killpg(p.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        if rc == 0:
            return
        if marca.exists():
            marca.unlink()
            micro = max(1, micro // 2)
            print(f"[supervisor] sin memoria: relanzo con micro-lotes de {micro}", flush=True)
        else:
            print(f"[supervisor] la corrida termino con {rc}: relanzo desde el ultimo estado", flush=True)
    raise SystemExit("[supervisor] cinco intentos sin terminar")


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
    ap.add_argument("--micro", type=int, default=0,
                    help="micro-lotes de este tamano sobre UN prefijo (gradiente exacto del lote entero)")
    ap.add_argument("--recalcular", action="store_true",
                    help="no guardar las activaciones (prefijo y sufijo): se recalculan en el backward, capa por capa")
    ap.add_argument("--fsdp", action="store_true",
                    help="las dos placas como una (FSDP2): pesos repartidos, lote repartido, a la vez")
    ap.add_argument("--pesos", choices=["enteros", "repartidos"],
                    help="con --fsdp: «enteros» = una copia del modelo por placa y el prefijo repartido"
                         " por secuencia (lo mas rapido si entra: el 4B); «repartidos» = FSDP2, cada placa"
                         " la mitad de cada capa (para lo que no entra en una: el 9B). Sin decir: segun"
                         " el tamano (enteros si los pesos ocupan menos del 35%% de la placa)")
    ap.add_argument("--prefijo-repartido", choices=["si", "no"], default="si",
                    help="con --fsdp en dos placas: cada placa hace la mitad de los tokens del prefijo"
                         " (prefijo_repartido.py; verificado con verificar-gradiente.py)")
    ap.add_argument("--lora-fusionado", choices=["si", "no"], default="no",
                    help="cada lineal con LoRA en un kernel de triton (lora_fusionado.py) en vez de base y"
                         " LoRA por separado. Verificado; con el 4B no gana (6,26 vs 6,34 s/paso)")
    ap.add_argument("--adaptador-inicial", help="arrancar desde este LoRA (carpeta con adapter_model.safetensors)"
                    " en vez de uno nuevo; mismo rango y mismas lineales")
    ap.add_argument("--reanudar", action="store_true", help="seguir desde <salida>/estado/estado.pt")
    ap.add_argument("--guardar-cada-min", type=float, default=10.0,
                    help="cada cuantos minutos se guarda el estado para reanudar")
    args = ap.parse_args()
    if args.fsdp and "LOCAL_RANK" not in os.environ:
        if not args.micro:
            args.micro = 4
        return supervisar(args)

    import torch
    import torch.distributed as dist
    import torch.nn.functional as F
    from peft import LoraConfig, get_peft_model
    from transformers import AutoModelForCausalLM, AutoTokenizer, DynamicCache

    if sys.platform == "linux" and "LOCAL_RANK" in os.environ:
        # si muere torchrun (el padre), este proceso muere con el: si no, queda
        # huerfano entrenando con la placa ocupada (prctl PR_SET_PDEATHSIG = 1)
        import ctypes
        ctypes.CDLL("libc.so.6").prctl(1, signal.SIGKILL)
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

    # LA TOKENIZACION, EN CACHE: armar() tokeniza cada fila tres veces (minutos con
    # 20.000 filas); al reanudar, o con el mismo corpus, se lee de disco
    clave = hashlib.sha256()
    for nombre in ("entrenar.jsonl", "validar.jsonl"):
        clave.update((Path(args.datos) / nombre).read_bytes())
    clave.update(args.base.encode())
    clave = clave.hexdigest()[:16]
    cache_tok = Path(args.datos) / f".tokens-{clave}.json"
    if cache_tok.exists():
        E, V, me, mv = json.loads(cache_tok.read_text())
    else:
        E, me = armar(ent)
        V, mv = armar(val)
        if yo:
            tmp = cache_tok.with_suffix(".tmp")
            tmp.write_text(json.dumps([E, V, me, mv]))
            os.replace(tmp, cache_tok)
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
        # derecho a la placa: con dos procesos, cargar en RAM primero no entra (9B: 2 x 18 GB)
        modelo = AutoModelForCausalLM.from_pretrained(args.base, dtype=torch.bfloat16,
                                                      attn_implementation="sdpa", device_map={"": dev})
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
    if args.adaptador_inicial:
        # seguir desde un LoRA ya entrenado (laboratorios cortos sobre el mejor)
        from peft import set_peft_model_state_dict
        from safetensors.torch import load_file
        sd = load_file(str(Path(args.adaptador_inicial).expanduser() / "adapter_model.safetensors"))
        r = set_peft_model_state_dict(modelo, sd)
        faltan = [k for k in r.unexpected_keys] if hasattr(r, "unexpected_keys") else []
        log(f"LoRA inicial: {args.adaptador_inicial} ({len(sd)} tensores{', sobran ' + str(len(faltan)) if faltan else ''})")
    if args.lora_fusionado == "si":
        import lora_fusionado
        log(f"LoRA fusionado (base + rango bajo en un kernel): {lora_fusionado.instalar(modelo)} lineales")
    if args.recalcular:
        log(f"recalcular el prefijo capa por capa: {ch.recalcular_por_capa(modelo)} capas")
    entrenables = [p for p in modelo.parameters() if p.requires_grad]
    for p in entrenables:
        p.data = p.data.float()
    if dist_on:  # mismos pesos iniciales en las dos placas
        for p in entrenables:
            dist.broadcast(p.data, 0)
    raiz = pre_rep = None
    if args.fsdp:
        if not dist_on or args.repartir or args.cuatro_bits or not args.recalcular:
            raise SystemExit("--fsdp: con dos o mas placas, con --recalcular, sin --repartir ni --cuatro-bits")
        import repartido
        pesos = args.pesos
        if pesos is None:
            tam = sum(p.numel() * p.element_size() for p in modelo.parameters())
            pesos = "enteros" if tam < 0.35 * torch.cuda.get_device_properties(dev).total_memory else "repartidos"
        if pesos == "enteros":
            # UNA COPIA POR PLACA, el lote y el prefijo repartidos, y los gradientes
            # sumados al final del paso. Sin comunicacion de FSDP en medio del prefijo:
            # las placas van desfasadas una capa sin esperarse (prefijo_repartido)
            if hibrido and mundo == 2 and args.prefijo_repartido == "si":
                import prefijo_repartido
                pre_rep = prefijo_repartido.Prefijo(modelo.get_base_model().model,
                                                    torch.tensor([ids_pre], device=dev), rank, mundo)
                log(f"pesos enteros en cada placa · prefijo repartido por secuencia: tokens 0..{pre_rep.m}"
                    f" en la placa 0, el resto en la 1")
            else:
                log("pesos enteros en cada placa · el prefijo entero en cada una")
        else:
            raiz, n, _ = repartido.repartir(modelo, soltar=True)
            entrenables = [p for p in modelo.parameters() if p.requires_grad]  # ahora son pedazos (DTensor)
            log(f"FSDP2: {n} capas repartidas entre {mundo} placas (cada placa la mitad de cada capa)")
    opt = torch.optim.AdamW(entrenables, lr=args.lr, weight_decay=0.01, fused=True)
    ids_pre_t = torch.tensor([ids_pre], device=dev)
    Lp = len(ids_pre)
    # el prefijo va por el modelo de TEXTO: el envoltorio calculaba logits para
    # sus ~8.000 tokens (8.000 x 248k: unos 4 GB) que nadie usaba
    texto = (modelo.get_base_model() if hasattr(modelo, "get_base_model") else modelo).model

    def perdida(lote, grad=True, kv_dado=None):
        """Prefijo una vez; sufijos en lote sobre el cache expandido. Con kv_dado,
        el prefijo ya viene hecho (paso_amortizado)."""
        with torch.autocast("cuda", dtype=torch.bfloat16), torch.set_grad_enabled(grad):
            if kv_dado is not None:
                kv = kv_dado
            elif args.recalcular and grad:
                # el prefijo se recalcula capa por capa en el backward (cache_hibrido)
                kv = DynamicCache(config=modelo.config)
                with ch.recalculando():
                    texto(input_ids=ids_pre_t, past_key_values=kv, use_cache=True)
            else:
                kv = DynamicCache(config=modelo.config)
                texto(input_ids=ids_pre_t, past_key_values=kv, use_cache=True)
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
            # LOGITS SOLO DONDE HAY PERDIDA, y por tandas. Con respuestas largas
            # (la charla del formato de tool calling, ~220 tokens) los logits de
            # todo el lote son lote x largo x 248k: varios GB que no hacen falta.
            interno = modelo.get_base_model() if hasattr(modelo, "get_base_model") else modelo
            # con --recalcular tambien el sufijo se recalcula capa por capa: las
            # respuestas largas (charla) llenaban la placa; cada capa vuelve a su
            # cache «con el prefijo» antes de recalcularse (cache_hibrido)
            import contextlib
            ctx = ch.recalculando() if (args.recalcular and grad) else contextlib.nullcontext()
            with ctx:
                hs = interno.model(input_ids=ids, attention_mask=mask, position_ids=pos,
                                   past_key_values=kv, use_cache=True).last_hidden_state
            etq = etq.to(hs.device)  # con --repartir sale en la ultima placa
            sel = etq != -100
            h_sel, t_sel = hs[sel], etq[sel]
            cabeza_lm = interno.lm_head
            suma = 0.0
            for i in range(0, h_sel.shape[0], 512):
                lg = cabeza_lm(h_sel[i:i + 512].to(cabeza_lm.weight.device))
                suma = suma + F.cross_entropy(lg.float(), t_sel[i:i + 512].to(lg.device), reduction="sum")
            return suma, sel.sum().to(suma.device)  # con embeddings atados lm_head esta en la placa 0

    def paso_amortizado(lote, micro):
        """EL PREFIJO UNA SOLA VEZ PARA VARIOS MICRO-LOTES, con el gradiente exacto.

        El prefijo (las fichas o las herramientas: ~8.000 tokens en el formato de
        tool calling) era casi todo el costo de un paso, y se pagaba por cada lote
        de 8. Aca: (1) el prefijo hacia adelante una vez; (2) su cache se corta del
        grafo en HOJAS que piden gradiente; (3) cada micro-lote va y vuelve sobre
        una copia armada con esas hojas, que acumulan el gradiente que llega al
        prefijo; (4) un solo backward del prefijo con ese gradiente acumulado. La
        suma de los gradientes es la del lote entero: mismo resultado, un prefijo."""
        import contextlib
        reg = None
        with torch.autocast("cuda", dtype=torch.bfloat16):
            kv0 = DynamicCache(config=modelo.config)
            if pre_rep is not None:
                # cada placa su mitad del prefijo, a la vez (prefijo_repartido)
                reg = pre_rep.adelante(kv0)
            elif args.recalcular:
                # hacia adelante sin grafo, guardando la entrada de cada capa: el
                # backward del prefijo va capa por capa (cache_hibrido.atras_por_capas)
                with ch.grabando() as reg:
                    texto(input_ids=ids_pre_t, past_key_values=kv0, use_cache=True)
            else:
                texto(input_ids=ids_pre_t, past_key_values=kv0, use_cache=True)
        ts = pre_rep.juntar(kv0) if pre_rep is not None else ch.tensores(kv0)
        hojas = [t.detach().requires_grad_(t.is_floating_point()) for t in ts]
        n_total = torch.tensor(float(sum(n for _, n in lote)), device=dev)
        if dist_on:
            dist.all_reduce(n_total)
        suma_total = 0.0
        if raiz is not None:  # los micro-lotes acumulan sin comunicar; reduce el backward del prefijo
            raiz.set_requires_gradient_sync(False)
        for i in range(0, len(lote), micro):
            kv = ch.desde_tensores(modelo.config, hojas)
            suma, _ = perdida(lote[i:i + micro], kv_dado=kv)
            (suma / n_total.to(suma.device) * mundo).backward()
            suma_total = suma_total + suma.detach().to(dev)
        if raiz is not None:
            raiz.set_requires_gradient_sync(True)
        if pre_rep is not None:
            # el gradiente del cache, sumado entre placas (el backward del prefijo es
            # lineal en el); cada placa hace el backward de su mitad
            with torch.autocast("cuda", dtype=torch.bfloat16):
                pre_rep.atras(reg, kv0, prefijo_repartido.sumar(hojas))
        elif reg is not None:
            with torch.autocast("cuda", dtype=torch.bfloat16):
                ch.atras_por_capas(reg, kv0, [h.grad for h in hojas])
        else:
            pares = [(t, h.grad) for t, h in zip(ts, hojas) if h.grad is not None and t.requires_grad]
            if pares:
                torch.autograd.backward([t for t, _ in pares], [g for _, g in pares])
        return suma_total, n_total

    por_placa = args.lote // mundo
    pasos_epoca = len(E) // args.lote
    total = int(pasos_epoca * args.epocas) if not args.pasos else args.pasos
    calor = max(1, int(0.05 * total))

    def lr_en(paso):
        if paso < calor:
            return args.lr * (paso + 1) / calor
        return args.lr * 0.5 * (1 + math.cos(math.pi * (paso - calor) / max(1, total - calor)))

    def evaluar():
        """El prefijo UNA vez para toda la validacion (antes, una vez cada 32
        ejemplos) y lotes chicos sobre copias de su cache. Cada placa la misma
        cantidad de lotes: con FSDP cada forward es una comunicacion entre placas."""
        modelo.eval()
        mias = V[:len(V) // mundo * mundo][rank::mundo]
        s = torch.zeros(2, device=dev)
        tam = max(1, 2 * args.micro) if args.micro else 32
        with torch.no_grad(), torch.autocast("cuda", dtype=torch.bfloat16):
            kv0 = DynamicCache(config=modelo.config)
            if pre_rep is not None:
                pre_rep.adelante(kv0)
                ts = pre_rep.juntar(kv0)
                prefijo_repartido.esperar_envios()
                pre_rep.recibidos = None
            else:
                texto(input_ids=ids_pre_t, past_key_values=kv0, use_cache=True)
                ts = ch.tensores(kv0)
            for i in range(0, len(mias), tam):
                a, b = perdida(mias[i:i + tam], grad=False, kv_dado=ch.desde_tensores(modelo.config, ts))
                s += torch.stack([a.detach(), b.float()]).to(dev)
        if dist_on:
            dist.all_reduce(s)
        if raiz is not None:
            repartido.soltar_todo(modelo)
        modelo.train()
        return (s[0] / s[1]).item()

    import repartido

    def guardar(destino, sd=None):
        """Colectivo: con FSDP el LoRA esta repartido y lo arma la placa 0."""
        sd = sd if sd is not None else repartido.lora_completo(modelo)
        if yo:
            modelo.save_pretrained(destino, state_dict=sd)
            tok.save_pretrained(destino)
        return sd

    rnd = random.Random(args.semilla)
    orden = []
    mejor, mejor_paso = float("inf"), -1
    Path(args.salida).mkdir(parents=True, exist_ok=True)
    guardados = []
    estado_mejor = None
    dir_estado = Path(args.salida) / "estado"
    # lo que tiene que coincidir para retomar: los mismos datos y el mismo plan
    plan = {"datos": clave, "base": args.base, "lote": args.lote, "epocas": args.epocas, "lr": args.lr,
            "rango": args.rango, "semilla": args.semilla, "pasos": total, "mundo": mundo}
    desde, t_previo = 0, 0.0
    if args.reanudar and (dir_estado / "estado.pt").exists():
        e = repartido.cargar_estado(dir_estado, modelo, opt)
        if e["plan"] != plan:
            raise SystemExit(f"--reanudar: el estado es de otro plan\n  estado {e['plan']}\n  ahora  {plan}")
        desde, orden, t_previo = e["paso"], e["orden"], e["segundos"]
        rnd.setstate(e["rnd"])
        mejor, mejor_paso, estado_mejor, guardados = e["mejor"], e["mejor_paso"], e["estado_mejor"], e["guardados"]
        log(f"reanudo en el paso {desde + 1}/{total} (mejor hasta ahora: paso {mejor_paso}, {mejor:.4f})")

    def guardar_estado(paso_hecho):
        repartido.guardar_estado(dir_estado, modelo, opt, {
            "plan": plan, "paso": paso_hecho, "orden": orden, "rnd": rnd.getstate(),
            "segundos": t_previo + time.time() - t0, "mejor": mejor, "mejor_paso": mejor_paso,
            "estado_mejor": estado_mejor, "guardados": guardados}, yo)

    t0 = time.time()
    t_estado = time.time()
    modelo.train()
    log(f"pasos {total} · lote {args.lote} ({por_placa} por placa x {mundo}) · calentamiento {calor}")
    for paso in range(desde, total):
        # el resto que no llena un lote se descarta al final de cada epoca (como
        # hace el entrenador de transformers): con un lote corto, una placa podia
        # quedarse sin ejemplos y caerse (paso con v15: 7.800 % 16 = 8)
        if len(orden) < args.lote:
            orden = list(range(len(E)))
            rnd.shuffle(orden)  # misma semilla en las dos placas: mismo orden
        lote_ids = orden[:args.lote]
        orden = orden[args.lote:]
        # REPARTO PAREJO: el lote ordenado por largo y repartido en zigzag (0,1,1,0,...):
        # cada placa recibe la misma cantidad de tokens (la charla, ~220, iba toda a
        # una y la otra esperaba) y dentro de cada placa los micro-lotes juntan largos
        # parecidos (menos relleno). Son los mismos 16 ejemplos: el gradiente es el mismo
        lote_ids = sorted(lote_ids, key=lambda i: -len(E[i][0]))
        mio = [E[i] for k, i in enumerate(lote_ids)
               if (k % mundo if (k // mundo) % 2 == 0 else mundo - 1 - k % mundo) == rank]
        for g in opt.param_groups:
            g["lr"] = lr_en(paso)
        # con FSDP siempre por aca: es el camino que lleva el cero por la salida del prefijo
        if args.micro and (args.micro < len(mio) or args.fsdp):
            # si un lote con respuestas largas no entra, se reintenta EL MISMO paso con
            # micro-lotes mas chicos (mismo gradiente) en vez de tirar la corrida
            micro = args.micro
            while True:
                try:
                    suma, n = paso_amortizado(mio, micro)   # ya hizo el backward
                    break
                except torch.OutOfMemoryError as err:
                    if args.fsdp:
                        import traceback
                        donde = traceback.extract_tb(err.__traceback__)[-4:]
                        print(f"  placa {rank}: {str(err).splitlines()[0][:200]}\n    en "
                              + " <- ".join(f"{Path(f.filename).name}:{f.lineno}" for f in reversed(donde)), flush=True)
                        # con FSDP la otra placa queda esperando en una comunicacion: no
                        # se puede reintentar aca. Se avisa al supervisor y se corta; el
                        # supervisor relanza desde el ultimo estado con micro a la mitad
                        dir_estado.mkdir(parents=True, exist_ok=True)
                        (dir_estado / "sin-memoria").write_text(str(micro))
                        print(f"  paso {paso + 1}: sin memoria con micro {micro} (placa {rank})", flush=True)
                        os._exit(3)
                    opt.zero_grad(set_to_none=True)
                    torch.cuda.empty_cache()
                    if micro <= 1:
                        raise
                    micro = max(1, micro // 2)
                    print(f"  paso {paso + 1}: sin memoria, reintento con micro-lotes de {micro}", flush=True)
            n_tot = n
        else:
            suma, n = perdida(mio)
            n_tot = n.float()
            if dist_on:
                dist.all_reduce(n_tot)
            (suma / n_tot * mundo).backward()   # promedio por token sobre TODO el lote
        if dist_on and raiz is None:  # con FSDP ya los promedio el reduce-scatter
            for p in entrenables:
                dist.all_reduce(p.grad)
                p.grad /= mundo
        opt.step()
        opt.zero_grad(set_to_none=True)
        if paso == desde:
            if dist_on:  # cada proceso ve solo su placa
                picos = [None] * mundo
                dist.all_gather_object(picos, torch.cuda.max_memory_allocated(dev) / 2**30)
            else:
                picos = [torch.cuda.max_memory_allocated(i) / 2**30 for i in range(torch.cuda.device_count())]
            log(f"memoria maxima en el primer paso: {' + '.join(f'{x:.1f}' for x in picos)} GiB")
        if yo and (paso + 1) % 10 == 0:
            dt, hechos = time.time() - t0, paso + 1 - desde
            print(f"paso {paso + 1}/{total} · perdida {(suma / n).item():.4f} · "
                  f"{dt / hechos:.2f} s/paso · faltan {dt / hechos * (total - paso - 1) / 60:.0f} min",
                  flush=True)
        if (paso + 1) % args.eval_cada == 0 or paso + 1 == total:
            vl = evaluar()
            log(f"  eval paso {paso + 1}: perdida de validacion {vl:.4f}")
            cp = Path(args.salida) / f"checkpoint-{paso + 1}"
            sd = guardar(cp)
            guardados.append(cp)
            if vl < mejor:
                mejor, mejor_paso = vl, paso + 1
                estado_mejor = sd if yo else None
            # quedan los dos ultimos y el mejor
            if yo:
                import shutil
                for c in guardados[:-2]:
                    if c.name != f"checkpoint-{mejor_paso}" and c.exists():
                        shutil.rmtree(c)
            guardar_estado(paso + 1)
            t_estado = time.time()
        # EL ESTADO PARA REANUDAR: la decision la toma la placa 0 (los relojes de
        # cada proceso difieren) y la comparte, porque guardar es colectivo. Se
        # pregunta cada 10 pasos: leer la respuesta (.item) sincroniza la CPU con
        # la placa, y en cada paso dejaba a la placa esperando el paso siguiente
        toca = False
        if (paso + 1) % 10 == 0 and paso + 1 < total:
            t = torch.tensor([float(time.time() - t_estado > args.guardar_cada_min * 60)], device=dev)
            if dist_on:
                dist.broadcast(t, 0)
            toca = bool(t.item())
        if toca:
            guardar_estado(paso + 1)
            t_estado = time.time()
            log(f"  estado guardado (paso {paso + 1})")
    if yo:
        guardar(args.salida, sd=estado_mejor)
        import lib_prompt as _lp
        (Path(args.salida) / "huella-prompt.json").write_text(
            json.dumps(_lp.huella(), ensure_ascii=False, indent=1), encoding="utf-8")
        print(f"listo: mejor paso {mejor_paso} (validacion {mejor:.4f}) · "
              f"{(t_previo + time.time() - t0) / 60:.1f} min · {args.salida}", flush=True)
    if dist_on:
        dist.barrier()
        dist.destroy_process_group()


if __name__ == "__main__":
    main()
