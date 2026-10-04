"""Las dos placas como UNA de 48 GB: FSDP2 (PyTorch, fully_shard) para entrenar-rapido.

POR QUE. Con --repartir (capas 0..k en una placa, el resto en la otra) entra
cualquier modelo, pero las placas se turnan: mientras una calcula, la otra
espera. Con datos en paralelo (cada placa el modelo entero) las dos calculan a
la vez, pero cada una carga TODOS los pesos y el prefijo de ~8.000 tokens no
entra. FSDP (Zhao et al., «PyTorch FSDP», arXiv:2304.11277; ZeRO-3, Rajbhandari
et al., arXiv:1910.02054) hace las dos cosas: cada placa guarda la MITAD de cada
capa y calcula la mitad del lote; antes de usar una capa la arma entera trayendo
la otra mitad por P2P (all-gather, NCCL, mismo switch PCIe) y al terminar la
suelta. Los gradientes se promedian y se reparten (reduce-scatter).

LO DELICADO: EL PREFIJO COMPARTIDO. FSDP arma los pesos de una capa para el
backward cuando el gradiente llega a la SALIDA de esa capa (los ganchos van en
las salidas). Nuestro prefijo devuelve el gradiente por el CACHE (K/V y estados
de la atencion lineal), no por la salida del modelo. Arreglo: el backward del
prefijo va capa por capa y a mano (cache_hibrido.atras_por_capas): cada capa se
recalcula llamando al modulo, que dispara los ganchos, y recibe juntos el
gradiente de su salida y el de su cache. De paso, una sola capa viva por vez.
Es el esquema de «Schedule-Level Shared-Prefix Reuse» (arXiv:2606.01143):
el backward del prefijo es lineal en el gradiente que llega por K/V, asi que
acumularlo en los micro-lotes y correrlo una vez da el gradiente exacto.
Verificado con verificar-gradiente.py: contra la verdad en fp64, las dos placas
quedan tan cerca como una sola (coseno 0,999999997 vs 0,999999998).

PRECISION: los pesos del LoRA quedan en fp32 (repartidos, el optimizador
trabaja ahi); para calcular se arman en bf16 y los gradientes se reducen en fp32
(MixedPrecisionPolicy). La base congelada esta en bf16 y tambien se reparte: es
lo que libera la memoria.

REANUDAR. guardar_estado / cargar_estado: el LoRA y el optimizador COMPLETOS
(los arma la placa 0 desde los pedazos; asi un estado guardado con FSDP se puede
retomar sin FSDP o con otra cantidad de placas) mas lo que hace falta para
seguir el mismo camino (paso, orden del barajado, mejor hasta ahora).
"""
import os
from pathlib import Path

import torch
import torch.distributed as dist


def capas_de(modelo):
    return [m for m in modelo.modules() if type(m).__name__.endswith("DecoderLayer")]


def repartir(modelo, soltar=None, bf16=True):
    """Una unidad FSDP por capa del decodificador y la raiz en el modelo de texto
    (embeddings y norma final). La raiz es el modelo de TEXTO y no el envoltorio
    de PEFT porque el prefijo y los sufijos llaman al de texto directamente (los
    logits se calculan aparte, solo donde hay perdida). La cabeza de salida queda
    fuera: congelada y entera en cada placa (o atada a los embeddings).

    soltar: si cada capa suelta sus pesos al terminar el forward (y los vuelve a
    traer para el backward). entrenar-rapido usa FSDP solo para lo que no entra
    en una placa, y ahi suelta (soltar=True); None = decidir por tamano."""
    from torch.distributed.fsdp import MixedPrecisionPolicy, fully_shard
    # bf16=False: todo en la precision de los pesos (las pruebas de gradiente, en fp32)
    mp = MixedPrecisionPolicy(param_dtype=torch.bfloat16 if bf16 else None, reduce_dtype=torch.float32)
    interno = modelo.get_base_model() if hasattr(modelo, "get_base_model") else modelo
    capas = capas_de(interno)
    if soltar is None:
        pesos = sum(p.numel() * p.element_size() for p in modelo.parameters())
        soltar = pesos > 0.35 * torch.cuda.get_device_properties(torch.cuda.current_device()).total_memory
    for c in capas:
        fully_shard(c, mp_policy=mp, reshard_after_forward=soltar)
    fully_shard(interno.model, mp_policy=mp)
    # FSDP se inicializa con el primer forward y toma como raiz al primer modulo
    # que corre: el prefijo repartido llama a las capas directo, asi que un token
    # por la raiz primero (en todas las placas a la vez)
    with torch.no_grad():
        interno.model(input_ids=torch.zeros((1, 1), dtype=torch.long, device=torch.cuda.current_device()))
    soltar_todo(modelo)
    return interno.model, len(capas), soltar


def soltar_todo(modelo):
    """Deja todos los pesos repartidos. IMPRESCINDIBLE despues de un forward sin
    backward (el de inicio, la validacion) cuando las capas conservan los pesos
    armados: quedaban armados con los valores de ese momento; el optimizador
    actualizaba los pedazos y el forward siguiente usaba la copia vieja (y quien
    pidiera los parametros recibia la copia armada en vez del pedazo)."""
    from torch.distributed.fsdp import FSDPModule
    for m in modelo.modules():
        if isinstance(m, FSDPModule):
            m.reshard()


def _opciones(**kw):
    from torch.distributed.checkpoint.state_dict import StateDictOptions
    return StateDictOptions(full_state_dict=True, cpu_offload=True, ignore_frozen_params=True, **kw)


def lora_completo(modelo):
    """El LoRA entero en CPU (en la placa 0; las otras reciben {}). Colectivo:
    lo llaman TODAS las placas."""
    from torch.distributed.checkpoint.state_dict import get_model_state_dict
    return get_model_state_dict(modelo, options=_opciones())


def guardar_estado(carpeta, modelo, opt, extra, yo):
    """Colectivo. Escribe estado.pt de forma atomica (tmp + rename): un corte
    a mitad de la escritura deja el anterior intacto."""
    from torch.distributed.checkpoint.state_dict import get_optimizer_state_dict
    sd = lora_completo(modelo)
    osd = get_optimizer_state_dict(modelo, opt, options=_opciones())
    if yo:
        carpeta = Path(carpeta)
        carpeta.mkdir(parents=True, exist_ok=True)
        tmp = carpeta / "estado.pt.tmp"
        torch.save({"modelo": sd, "opt": osd, **extra}, tmp)
        os.replace(tmp, carpeta / "estado.pt")
    if dist.is_initialized():
        dist.barrier()


def cargar_estado(carpeta, modelo, opt):
    """Todas las placas leen el archivo y cada una se queda con su pedazo."""
    from torch.distributed.checkpoint.state_dict import set_model_state_dict, set_optimizer_state_dict
    e = torch.load(Path(carpeta) / "estado.pt", map_location="cpu", weights_only=False)
    set_model_state_dict(modelo, e.pop("modelo"), options=_opciones())
    set_optimizer_state_dict(modelo, opt, e.pop("opt"), options=_opciones())
    return e
