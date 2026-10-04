#!/usr/bin/env python3
"""Verifica que entrenar con las dos placas da el MISMO gradiente que con una.

Con un modelo chico de la misma arquitectura, la VERDAD se calcula en fp64 en
una placa, y se miden contra ella dos cosas en fp32 (sin TF32):
  · una placa (el camino de siempre, cache_hibrido), los mismos dos medios lotes;
  · las dos placas: FSDP + prefijo repartido por secuencia + medio lote cada una.
Con medio prefijo por placa las cuentas se hacen en otro orden (otros tamanos
de matriz, otros algoritmos de cuBLAS y de atencion), asi que no puede dar
identico bit a bit; lo que se pide es que este tan cerca de la verdad como la
version de una placa: un termino que faltara o sobrara daria errores de ordenes
de magnitud mas. Se compara el cache del prefijo (lo que ven los sufijos) y el
gradiente del LoRA. Pasa si el error de las dos placas es a lo sumo 3 veces el
de una placa (y nunca mas de 1e-3).

La regla delta va por su version en torch (--nucleo torch): el kernel de fla
usa TF32 adentro y taparia la comparacion. --nucleo fla mide el error practico
del kernel de entrenamiento, sin pasar o no pasar.

    .venv-entrenar/bin/torchrun --nproc_per_node 2 tools/entrenar/verificar-gradiente.py [--pesos enteros|repartidos]

Los dos modos de entrenar-rapido --fsdp:
  enteros     una copia por placa, el prefijo repartido por secuencia y desfasado
              una capa (prefijo_repartido), gradientes sumados al final;
  repartidos  FSDP2 (cada placa la mitad de cada capa), el prefijo entero en cada
              placa.

Sale con 0 si pasa y con 1 si no. Correrlo despues de tocar repartido.py,
prefijo_repartido.py o cache_hibrido.py.
"""
import argparse
import os
import sys
from pathlib import Path

import torch
import torch.distributed as dist

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cache_hibrido as ch  # noqa: E402
import prefijo_repartido as pr  # noqa: E402
import repartido  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("--base", default="Qwen/Qwen3.5-0.8B")
ap.add_argument("--prefijo", type=int, default=301, help="tokens (impar: la mitad no es exacta)")
ap.add_argument("--pesos", choices=["enteros", "repartidos"], default="enteros")
ap.add_argument("--lora-fusionado", action="store_true",
                help="las corridas en fp32 con lora_fusionado (la verdad en fp64 va con peft)")
ap.add_argument("--nucleo", choices=["torch", "fla"], default="torch",
                help="la regla delta: torch (fp32 de verdad: verifica el METODO) o fla (el kernel"
                     " de entrenamiento, con TF32 adentro: mide el error practico)")
args = ap.parse_args()

rank = int(os.environ["LOCAL_RANK"])
dist.init_process_group("nccl")
torch.cuda.set_device(rank)
dev = torch.device(f"cuda:{rank}")
torch.backends.cuda.matmul.allow_tf32 = False
torch.backends.cudnn.allow_tf32 = False
ch.parchear()
from peft import LoraConfig, get_peft_model  # noqa: E402
from transformers import AutoModelForCausalLM, DynamicCache  # noqa: E402

BLANCOS = ["q_proj", "k_proj", "v_proj", "o_proj", "gate_proj", "up_proj", "down_proj", "in_proj_qkv", "in_proj_z",
           "out_proj"]


def doble():
    """Que la regla delta en torch y las normas calculen en la precision de su
    entrada (fp64 para la verdad) y no en fp32 fijo. Para fp32 no cambia nada."""
    import inspect
    from transformers.models.qwen3_5 import modeling_qwen3_5 as q
    prom = "torch.promote_types(x.dtype, torch.float32)"
    fuente = inspect.getsource(q.torch_chunk_gated_delta_rule)
    fuente = fuente.replace(".to(torch.float32)", f".to({prom})").replace(".tril().exp().float()", ".tril().exp()")
    exec(compile(fuente, q.__file__, "exec"), q.__dict__)
    q.Qwen3_5RMSNorm._norm_orig = q.Qwen3_5RMSNorm.forward

    def norma(self, x):
        y = self._norm(x.to(torch.promote_types(x.dtype, torch.float32)))
        return (y * (1.0 + self.weight.to(y.dtype))).type_as(x)
    q.Qwen3_5RMSNorm.forward = norma

    def norma_comp(self, hidden_states, gate=None):
        tipo = hidden_states.dtype
        t = torch.promote_types(tipo, torch.float32)
        h = hidden_states.to(t)
        h = h * torch.rsqrt(h.pow(2).mean(-1, keepdim=True) + self.variance_epsilon)
        h = self.weight * h.to(tipo)
        return (h * torch.nn.functional.silu(gate.to(t))).to(tipo)
    q.Qwen3_5RMSNormGated.forward = norma_comp


doble()  # para fp32 no cambia nada


def cargar(tipo=torch.float32):
    torch.manual_seed(0)
    # .float(): dtype=float32 no alcanza, la base viene en bf16 igual; y en bf16 el
    # redondeo cambia con como se parten los tokens y tapa cualquier error real
    m = AutoModelForCausalLM.from_pretrained(args.base, dtype=torch.float32, device_map={"": dev}).float()
    m = get_peft_model(m, LoraConfig(r=8, lora_alpha=8, target_modules=BLANCOS, init_lora_weights=False))
    m = m.to(tipo)
    if args.lora_fusionado and tipo == torch.float32:
        import lora_fusionado
        lora_fusionado.instalar(m)
    ch.recalcular_por_capa(m)
    if args.nucleo == "torch" or tipo == torch.float64:  # fla no compila en fp64
        from transformers.models.qwen3_5 import modeling_qwen3_5 as q
        for mod in m.modules():
            if isinstance(mod, q.Qwen3_5GatedDeltaNet):
                mod.chunk_gated_delta_rule = q.torch_chunk_gated_delta_rule
    return m


def texto_de(m):
    return m.get_base_model().model


torch.manual_seed(1)
Lp, Ls, B = args.prefijo, 30, 4
pre = torch.randint(0, 50000, (1, Lp), device=dev)
suf = torch.randint(0, 50000, (B, Ls), device=dev)
N = B * Ls


def sufijos(m, ts, lote, escala):
    hojas = [t.detach().requires_grad_(True) for t in ts]
    kv = ch.desde_tensores(m.config, hojas)
    ch.repetir(kv, lote.shape[0])
    pos = torch.arange(Lp, Lp + Ls, device=dev).unsqueeze(0).expand(lote.shape[0], -1)
    with ch.recalculando():
        lg = m(input_ids=lote, position_ids=pos, past_key_values=kv, use_cache=True).logits
    (lg.to(torch.promote_types(lg.dtype, torch.float32)).logsumexp(-1).sum() / N * escala).backward()
    return hojas


def una_placa(m, lotes):
    """El camino de una placa (cache_hibrido): prefijo grabado, sufijos, atras por capas."""
    for lote in lotes:
        kv0 = DynamicCache(config=m.config)
        with ch.grabando() as reg:
            texto_de(m)(input_ids=pre, past_key_values=kv0, use_cache=True)
        ts = ch.tensores(kv0)
        hojas = sufijos(m, ts, lote, 1.0)
        ch.atras_por_capas(reg, kv0, [h.grad for h in hojas])
    return ts


def grads(m, completo=False):
    out = {}
    for n, p in m.named_parameters():
        if p.requires_grad and p.grad is not None:
            g = p.grad.full_tensor() if completo else p.grad
            out[n.replace("_fsdp_wrapped_module.", "")] = g.detach().double().clone()
    return out


def comparar(a, b):
    ks = [k for k in a if k in b]
    err = max(((a[k] - b[k]).abs().max() / (a[k].abs().max() + 1e-12)).item() for k in ks)
    x = torch.cat([a[k].flatten() for k in ks])
    y = torch.cat([b[k].flatten() for k in ks])
    return err, (x @ y / (x.norm() * y.norm())).item(), len(ks), len(a)


if rank == 0:
    r = cargar(torch.float64)
    ts_64 = [t.detach().double().clone() for t in una_placa(r, [suf[:2], suf[2:]])]
    g_64 = grads(r)
    del r
    r = cargar()
    ts_1 = [t.detach().double().clone() for t in una_placa(r, [suf[:2], suf[2:]])]
    g_1 = grads(r)
    del r
    torch.cuda.empty_cache()
dist.barrier()

# las dos placas, medio lote cada una
m = cargar()
torch.cuda.reset_peak_memory_stats()
if args.pesos == "enteros":
    for p in m.parameters():  # mismos pesos de partida (como entrenar-rapido)
        if p.requires_grad:
            dist.broadcast(p.data, 0)
    P = pr.Prefijo(texto_de(m), pre, rank, 2)
    kv0 = DynamicCache(config=m.config)
    reg = P.adelante(kv0)
    ts = P.juntar(kv0)
    hojas = sufijos(m, ts, suf[rank * 2:(rank + 1) * 2], 2.0)  # x mundo, como entrenar-rapido
    P.atras(reg, kv0, pr.sumar(hojas))
    for p in m.parameters():  # el promedio entre placas, como entrenar-rapido
        if p.requires_grad and p.grad is not None:
            dist.all_reduce(p.grad)
            p.grad /= 2
    pedazos = True
    g = grads(m)
else:
    repartido.repartir(m, soltar=True, bf16=False)
    # lo que recibiria el optimizador: los pedazos (DTensor), no copias armadas
    from torch.distributed.tensor import DTensor  # noqa: E402
    pedazos = all(isinstance(p, DTensor) for p in m.parameters() if p.requires_grad)
    kv0 = DynamicCache(config=m.config)
    with ch.grabando() as reg:
        texto_de(m)(input_ids=pre, past_key_values=kv0, use_cache=True)
    ts = [t.detach().clone() for t in ch.tensores(kv0)]
    hojas = sufijos(m, ch.tensores(kv0), suf[rank * 2:(rank + 1) * 2], 2.0)  # FSDP promedia
    ch.atras_por_capas(reg, kv0, [h.grad for h in hojas])
    g = grads(m, completo=True)
pico = torch.cuda.max_memory_allocated() / 2**30

if rank == 0:
    def e_cache(ts_x):
        return max(((a - b.double()).abs().max() / (a.abs().max() + 1e-30)).item() for a, b in zip(ts_64, ts_x))
    c1, c2 = e_cache(ts_1), e_cache(ts)
    e1, cos1, _, _ = comparar(g_64, g_1)
    e2, cos2, n, total = comparar(g_64, g)
    print(f"cache del prefijo vs fp64:   una placa {c1:.2e} · dos placas {c2:.2e}")
    print(f"gradiente vs fp64:           una placa {e1:.2e} (coseno {cos1:.9f})"
          f" · dos placas {e2:.2e} (coseno {cos2:.9f}) · {n}/{total} tensores")
    print(f"pico de memoria (dos placas): {pico:.2f} GiB · parametros del optimizador bien: {'si' if pedazos else 'NO'}")
    if args.nucleo == "fla":
        print("(con fla no se juzga: TF32 adentro del kernel)")
        codigo = 0
    else:
        ok = (pedazos and n == total and c2 <= 3 * c1 + 1e-9 and e2 <= 3 * e1 + 1e-9 and e2 < 1e-3
              and (1 - cos2) <= 3 * (1 - cos1) + 1e-12)
        print("PASA" if ok else "NO PASA")
        codigo = 0 if ok else 1
else:
    codigo = 0
t = torch.tensor([codigo], device=dev)
dist.broadcast(t, 0)
dist.destroy_process_group()
sys.exit(int(t.item()))
