"""LoRA fusionado: la proyeccion base y la de bajo rango en UN kernel (triton).

QUE HACE PEFT. Cada lineal con LoRA calcula y = x·Wᵀ (la base) y aparte
z = s·(x·Aᵀ)·Bᵀ (el LoRA), y despues los suma: la salida se escribe en la
memoria de la placa dos veces y se vuelve a leer para sumar. Igual en el
backward con el gradiente de la entrada: dx = dy·W + s·(dy·B)·A.

QUE HACE ESTE. Un solo kernel C = X·Y + U·V: recorre la K de la base y, en el
mismo bloque de salida que ya esta en los registros, suma el termino de rango
bajo (U = s·x·Aᵀ, de rango 32; V = Bᵀ). La salida se escribe una vez. Sirve
para el forward (X = x, Y = Wᵀ) y para el gradiente de la entrada (X = dy,
Y = W, U = s·dy·B, V = A). Los productos chicos (x·Aᵀ, dy·B) y los gradientes de
A y B van con cuBLAS: son de rango 32 y no escriben nada grande.

PRECISION. En bf16 (el entrenamiento, con autocast) los productos de tl.dot
son bf16 con acumulacion fp32, como cuBLAS. En fp32 (verificar-gradiente.py)
van en fp32 de verdad (input_precision="ieee"), no TF32: la verificacion contra
fp64 sigue diciendo algo.
"""
import types

import torch
import triton
import triton.language as tl


@triton.autotune(
    configs=[
        triton.Config({"BM": 128, "BN": 128, "BK": 32, "G": 8}, num_stages=4, num_warps=8),
        triton.Config({"BM": 128, "BN": 64, "BK": 32, "G": 8}, num_stages=4, num_warps=4),
        triton.Config({"BM": 64, "BN": 128, "BK": 32, "G": 8}, num_stages=4, num_warps=4),
        triton.Config({"BM": 128, "BN": 256, "BK": 32, "G": 8}, num_stages=3, num_warps=8),
        triton.Config({"BM": 64, "BN": 64, "BK": 64, "G": 8}, num_stages=3, num_warps=4),
    ],
    # sin M: cambia en cada micro-lote (largo con relleno) y re-autoajustaba cada vez
    key=["N", "K", "R", "IEEE"],
)
@triton.jit
def _mm_mas_rango(X, Y, U, V, C, M, N, K, R,
                  sxm, sxk, syk, syn, sum_, sur, svr, svn, scm, scn,
                  IEEE: tl.constexpr, BR: tl.constexpr, BM: tl.constexpr, BN: tl.constexpr,
                  BK: tl.constexpr, G: tl.constexpr):
    pid = tl.program_id(0)
    n_m = tl.cdiv(M, BM)
    n_n = tl.cdiv(N, BN)
    # bloques agrupados: los de una misma franja de filas reusan Y en la cache L2
    grupo = pid // (G * n_n)
    primero = grupo * G
    alto = min(n_m - primero, G)
    pm = primero + (pid % alto)
    pn = (pid % (G * n_n)) // alto
    om = pm * BM + tl.arange(0, BM)
    on = pn * BN + tl.arange(0, BN)
    acc = tl.zeros((BM, BN), dtype=tl.float32)
    for k0 in range(0, K, BK):
        ok = k0 + tl.arange(0, BK)
        x = tl.load(X + om[:, None] * sxm + ok[None, :] * sxk, mask=(om[:, None] < M) & (ok[None, :] < K), other=0.0)
        y = tl.load(Y + ok[:, None] * syk + on[None, :] * syn, mask=(ok[:, None] < K) & (on[None, :] < N), other=0.0)
        if IEEE:
            acc += tl.dot(x, y, input_precision="ieee")
        else:
            acc += tl.dot(x, y)
    for r0 in range(0, R, BR):  # el termino de rango bajo, sobre el mismo bloque de salida
        orr = r0 + tl.arange(0, BR)
        u = tl.load(U + om[:, None] * sum_ + orr[None, :] * sur, mask=(om[:, None] < M) & (orr[None, :] < R), other=0.0)
        v = tl.load(V + orr[:, None] * svr + on[None, :] * svn, mask=(orr[:, None] < R) & (on[None, :] < N), other=0.0)
        if IEEE:
            acc += tl.dot(u, v, input_precision="ieee")
        else:
            acc += tl.dot(u, v)
    tl.store(C + om[:, None] * scm + on[None, :] * scn, acc.to(C.dtype.element_ty),
             mask=(om[:, None] < M) & (on[None, :] < N))


def mm_mas_rango(x, y, u, v):
    """x·y + u·v con x (M,K), y (K,N), u (M,R), v (R,N): vistas con cualquier paso."""
    M, K = x.shape
    N = y.shape[1]
    R = u.shape[1]
    c = torch.empty((M, N), dtype=x.dtype, device=x.device)
    br = max(16, triton.next_power_of_2(min(R, 64)))
    grilla = lambda meta: (triton.cdiv(M, meta["BM"]) * triton.cdiv(N, meta["BN"]),)  # noqa: E731
    _mm_mas_rango[grilla](x, y, u, v, c, M, N, K, R,
                          x.stride(0), x.stride(1), y.stride(0), y.stride(1),
                          u.stride(0), u.stride(1), v.stride(0), v.stride(1), c.stride(0), c.stride(1),
                          IEEE=x.dtype == torch.float32, BR=br)
    return c


class _LoraFusionado(torch.autograd.Function):
    @staticmethod
    def forward(ctx, x, W, A, B, s):
        # la precision de calculo: la de autocast si esta activo (bf16), si no la de x
        tipo = torch.get_autocast_dtype("cuda") if torch.is_autocast_enabled("cuda") else x.dtype
        forma = x.shape
        x2 = x.reshape(-1, forma[-1]).to(tipo)
        Wc, Ac, Bc = W.to(tipo), A.to(tipo), B.to(tipo)
        with torch.autocast("cuda", enabled=False):
            u = (x2 @ Ac.t()) * s                                   # (M, r)
            y = mm_mas_rango(x2, Wc.t(), u, Bc.t())                 # x·Wᵀ + u·Bᵀ
        ctx.save_for_backward(x2, Wc, Ac, Bc, u)
        ctx.s, ctx.forma, ctx.tipo_x = s, forma, x.dtype
        ctx.tipos = (A.dtype, B.dtype)
        return y.reshape(*forma[:-1], W.shape[0])

    @staticmethod
    def backward(ctx, dy):
        x2, Wc, Ac, Bc, u = ctx.saved_tensors
        s = ctx.s
        dy2 = dy.reshape(-1, dy.shape[-1]).to(x2.dtype)
        with torch.autocast("cuda", enabled=False):
            v = (dy2 @ Bc) * s                                      # (M, r) = s·dy·B
            dx = mm_mas_rango(dy2, Wc, v, Ac) if ctx.needs_input_grad[0] else None  # dy·W + v·A
            dA = (v.t() @ x2).to(ctx.tipos[0]) if ctx.needs_input_grad[2] else None
            dB = (dy2.t() @ u).to(ctx.tipos[1]) if ctx.needs_input_grad[3] else None
        if dx is not None:
            dx = dx.reshape(ctx.forma).to(ctx.tipo_x)
        return dx, None, dA, dB, None


def _forward(self, x, *args, **kwargs):
    """El forward de peft para una lineal con UN adaptador, sin dropout ni DoRA ni
    fusion previa: por el kernel. Cualquier otro caso, por el de peft."""
    if (self.disable_adapters or self.merged or len(self.active_adapters) != 1 or args or kwargs
            or self.base_layer.bias is not None):
        return self._forward_peft(x, *args, **kwargs)
    nombre = self.active_adapters[0]
    if nombre not in self.lora_A or not isinstance(self.lora_dropout[nombre], torch.nn.Identity) \
            or self.use_dora.get(nombre, False):
        return self._forward_peft(x, *args, **kwargs)
    return _LoraFusionado.apply(x, self.base_layer.weight, self.lora_A[nombre].weight,
                                self.lora_B[nombre].weight, self.scaling[nombre])


def instalar(modelo):
    """Pasa cada lineal LoRA de peft al kernel fusionado. Devuelve cuantas."""
    from peft.tuners.lora.layer import Linear
    n = 0
    for m in modelo.modules():
        if isinstance(m, Linear) and isinstance(m.base_layer, torch.nn.Linear):
            m._forward_peft = m.forward
            m.forward = types.MethodType(_forward, m)
            n += 1
    return n
