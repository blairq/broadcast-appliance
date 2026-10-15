"""Entropia cruzada fusionada con la cabeza (entrenar-rapido --entropia-fusionada).

POR QUE. La perdida sale de logits de 248 mil columnas (el vocabulario de
Qwen3.5). Por tandas de 512 tokens ya no se arman todos a la vez en el forward,
pero autograd GUARDA lo de cada tanda para el backward (el log_softmax en fp32:
tokens x 248k x 4 bytes). Con las respuestas largas de la charla, micro 2 ya no
entraba en la placa 1 de la v6b.

QUE HACE (la tecnica de unsloth_zoo/fused_losses, reescrita aca: es
corta y lo de unsloth es AGPL). En el forward, por tandas, calcula a la vez la
perdida y su gradiente respecto de la entrada de la cabeza:
    p = softmax(h W^T);  dL/dh = (p - uno_en_la_etiqueta) W
y guarda solo dL/dh (tokens x 2560). En el backward lo devuelve multiplicado por
el gradiente que llega. El pico es UNA tanda de logits, que se suelta enseguida.

Las mismas cuentas que el camino de siempre: la cabeza en bf16 (como bajo
autocast), la entropia en fp32, el gradiente de vuelta a bf16 antes de
multiplicar por W. Solo con la cabeza CONGELADA (en LoRA lo esta) y sin sesgo.
"""
import torch


class _Fusionada(torch.autograd.Function):
    @staticmethod
    def forward(ctx, h, w, etq, tanda):
        suma = torch.zeros((), dtype=torch.float32, device=h.device)
        dh = torch.empty_like(h)
        for i in range(0, h.shape[0], tanda):
            lg = (h[i:i + tanda].to(w.dtype) @ w.t()).float()
            t = etq[i:i + tanda]
            lse = torch.logsumexp(lg, dim=-1)
            suma += (lse - lg.gather(1, t[:, None]).squeeze(1)).sum()
            lg.sub_(lse[:, None]).exp_()  # softmax, en el mismo lugar
            lg[torch.arange(t.shape[0], device=lg.device), t] -= 1
            dh[i:i + tanda] = (lg.to(w.dtype) @ w).to(dh.dtype)
            del lg
        ctx.save_for_backward(dh)
        return suma

    @staticmethod
    def backward(ctx, g):
        (dh,) = ctx.saved_tensors
        return dh * g.to(dh.dtype), None, None, None


def perdida_suma(h, cabeza, etq, tanda=512):
    """Suma de la entropia cruzada de cabeza(h) contra etq (sin -100: ya filtradas)."""
    w = cabeza.weight
    if w.requires_grad or getattr(cabeza, "bias", None) is not None:
        raise ValueError("entropia fusionada: la cabeza tiene que estar congelada y sin sesgo")
    if type(w) is not torch.nn.Parameter and type(w) is not torch.Tensor:
        raise ValueError(f"entropia fusionada: la cabeza es {type(w).__name__} (repartida con FSDP?)")
    return _Fusionada.apply(h.to(w.device), w, etq.to(w.device), tanda)
