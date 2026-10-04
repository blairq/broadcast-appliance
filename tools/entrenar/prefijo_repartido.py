"""El prefijo repartido por SECUENCIA entre las dos placas (entrenar-rapido --fsdp).

POR QUE. Con FSDP cada placa calcula la mitad del lote, pero el prefijo (las
fichas de las herramientas, ~8.200 tokens) es uno solo y cada placa lo hacia
ENTERO: 6 de los 9,2 s de un paso con el 4B. Aca la placa 0 hace los tokens
[0, m) y la 1 los [m, L), a la vez, capa por capa.

QUE CRUZA DE UNA PLACA A LA OTRA (por causalidad, solo de la 0 a la 1; en el
backward los gradientes vuelven de la 1 a la 0):
  · atencion lineal (Gated DeltaNet, 3 de cada 4 capas): el ESTADO recurrente
    al llegar a m (cabezas x 128 x 128, fp32) y los 3 ultimos tokens de la
    entrada de la convolucion (el «halo»). La placa 1 arranca la regla delta
    desde ese estado (initial_state de fla, que es diferenciable). Es la idea
    de LASP (arXiv:2404.02882) en su forma mas simple: el nucleo recurrente es
    ~5% de la capa, asi que la espera en cadena cuesta poco.
  · atencion completa (1 de cada 4): K y V de los tokens de la placa 0; la
    placa 1 atiende a todo con la mascara causal corrida en m.
El resto de la capa (normas, proyecciones, MLP, LoRA) es token a token: cada
placa lo suyo.

EL ORDEN DE LOS MENSAJES. Las dos placas recorren las capas a la par (FSDP
arma los pesos de cada capa en las dos a la vez). Dentro de una capa, la placa
0 manda halo -> estado (o K/V) y la 1 los recibe en ese orden; en el backward
el motor de autograd ejecuta primero lo creado despues, asi que los gradientes
vuelven estado -> halo en las dos. Los mensajes van por un grupo de NCCL
propio, separado del de FSDP.

LOS CACHES. Al terminar el prefijo, cada placa tiene el cache de SUS tokens; se
juntan (juntar) para que los sufijos de las dos placas vean el prefijo entero.
En el backward, el gradiente del cache (sumado entre placas: el backward del
prefijo es lineal en el) se reparte: K/V por rango de tokens, los estados
finales de la atencion lineal a la placa 1, que es la que los produjo.
"""
import contextlib

import torch
import torch.distributed as dist
import torch.nn.functional as F

import cache_hibrido as ch

_sp = {"activo": False, "rank": 0, "m": 0, "grupo": None, "fichas": None}


class _Enviar(torch.autograd.Function):
    """Placa 0 -> 1. Devuelve una ficha (escalar) que entra como raiz del
    backward: al ejecutarse su backward, recibe el gradiente de la placa 1."""

    @staticmethod
    def forward(ctx, x):
        dist.send(x.contiguous(), 1, group=_sp["grupo"])
        ctx.forma, ctx.tipo = x.shape, x.dtype
        return x.new_zeros(())

    @staticmethod
    def backward(ctx, _):
        g = torch.empty(ctx.forma, dtype=ctx.tipo, device=torch.cuda.current_device())
        dist.recv(g, 1, group=_sp["grupo"])
        return g


class _Recibir(torch.autograd.Function):
    @staticmethod
    def forward(ctx, ancla, forma, tipo):
        t = torch.empty(forma, dtype=tipo, device=ancla.device)
        dist.recv(t, 0, group=_sp["grupo"])
        return t

    @staticmethod
    def backward(ctx, g):
        dist.send(g.contiguous(), 0, group=_sp["grupo"])
        return None, None, None


def _pasar(x=None, forma=None, tipo=None, device=None):
    """En la placa 0 manda x; en la 1 devuelve lo recibido (forma y tipo dados)."""
    if _sp["rank"] == 0:
        ficha = _Enviar.apply(x)
        if _sp["fichas"] is not None and ficha.requires_grad:
            _sp["fichas"].append(ficha)
        return None
    ancla = torch.zeros((), device=device, requires_grad=torch.is_grad_enabled())
    return _Recibir.apply(ancla, tuple(forma), tipo)


def _lineal(self, hidden_states, cache_params):
    """Gated DeltaNet sobre los tokens de esta placa (prefijo desde cache vacio)."""
    B, L, _ = hidden_states.shape
    rank = _sp["rank"]
    mixed = self.in_proj_qkv(hidden_states).transpose(1, 2)          # (B, C, L)
    z = self.in_proj_z(hidden_states).reshape(B, L, -1, self.head_v_dim)
    b = self.in_proj_b(hidden_states)
    a = self.in_proj_a(hidden_states)
    K = self.conv_kernel_size
    if rank == 0:
        _pasar(mixed[..., -(K - 1):])
        x = F.pad(mixed, (K - 1, 0))
    else:
        halo = _pasar(forma=(B, mixed.shape[1], K - 1), tipo=mixed.dtype, device=mixed.device)
        x = torch.cat([halo, mixed], dim=-1)
    conv = F.conv1d(x, self.conv1d.weight, self.conv1d.bias, groups=x.shape[1])
    mixed = F.silu(conv).transpose(1, 2)
    query, key, value = torch.split(mixed, [self.key_dim, self.key_dim, self.value_dim], dim=-1)
    query = query.reshape(B, L, -1, self.head_k_dim)
    key = key.reshape(B, L, -1, self.head_k_dim)
    value = value.reshape(B, L, -1, self.head_v_dim)
    beta = b.sigmoid()
    g = -self.A_log.float().exp() * F.softplus(a.float() + self.dt_bias)
    if self.num_v_heads // self.num_k_heads > 1:
        query = query.repeat_interleave(self.num_v_heads // self.num_k_heads, dim=2)
        key = key.repeat_interleave(self.num_v_heads // self.num_k_heads, dim=2)
    inicial = None
    if rank == 1:
        inicial = _pasar(forma=(B, self.num_v_heads, self.head_k_dim, self.head_v_dim), tipo=torch.float32,
                         device=mixed.device)
    out, estado = self.chunk_gated_delta_rule(query, key, value, g=g, beta=beta, initial_state=inicial,
                                              output_final_state=True, use_qk_l2norm_in_kernel=True)
    if rank == 0:
        _pasar(estado)
    if cache_params is not None:
        c = cache_params.layers[self.layer_idx]
        c.lazy_initialization(conv_states=x[..., -K:], recurrent_states=estado)
        c.conv_states, c.recurrent_states = x[..., -K:], estado
        c.has_previous_state = True
    out = self.norm(out.reshape(-1, self.head_v_dim), z.reshape(-1, self.head_v_dim))
    return self.out_proj(out.reshape(B, L, -1))


def _completa(self, hidden_states, position_embeddings, past_key_values):
    from transformers.models.qwen3_5.modeling_qwen3_5 import apply_rotary_pos_emb, repeat_kv
    input_shape = hidden_states.shape[:-1]
    hidden_shape = (*input_shape, -1, self.head_dim)
    q, gate = torch.chunk(self.q_proj(hidden_states).view(*input_shape, -1, self.head_dim * 2), 2, dim=-1)
    gate = gate.reshape(*input_shape, -1)
    q = self.q_norm(q.view(hidden_shape)).transpose(1, 2)
    k = self.k_norm(self.k_proj(hidden_states).view(hidden_shape)).transpose(1, 2)
    v = self.v_proj(hidden_states).view(hidden_shape).transpose(1, 2)
    cos, sin = position_embeddings
    q, k = apply_rotary_pos_emb(q, k, cos, sin)
    if past_key_values is not None:  # el cache guarda SOLO los tokens de esta placa
        past_key_values.update(k, v, self.layer_idx)
    Lq = q.shape[2]
    if _sp["rank"] == 0:
        _pasar(torch.stack([k, v]))
        K_, V_, mascara, causal = k, v, None, True
    else:
        m = _sp["m"]
        kv0 = _pasar(forma=(2, k.shape[0], k.shape[1], m, k.shape[3]), tipo=k.dtype, device=k.device)
        K_, V_ = torch.cat([kv0[0], k], dim=2), torch.cat([kv0[1], v], dim=2)
        # la consulta i (posicion m+i) ve las claves 0..m+i
        mascara = (torch.arange(m + Lq, device=q.device)[None, :]
                   <= (m + torch.arange(Lq, device=q.device))[:, None])
        causal = False
    out = F.scaled_dot_product_attention(q, repeat_kv(K_, self.num_key_value_groups),
                                         repeat_kv(V_, self.num_key_value_groups),
                                         attn_mask=mascara, is_causal=causal, scale=self.scaling)
    out = out.transpose(1, 2).reshape(*input_shape, -1).contiguous()
    out = out * torch.sigmoid(gate)
    return self.o_proj(out), None


def instalar():
    """Envuelve las dos atenciones: con repartiendo() activo van por aca."""
    from transformers.models.qwen3_5 import modeling_qwen3_5 as q
    if getattr(q.Qwen3_5Attention, "_repartible", False):
        return
    lin, comp = q.Qwen3_5GatedDeltaNet.forward, q.Qwen3_5Attention.forward

    def lineal(self, hidden_states, cache_params=None, attention_mask=None):
        if _sp["activo"]:
            with torch.cuda.device(hidden_states.device):
                return _lineal(self, hidden_states, cache_params)
        return lin(self, hidden_states, cache_params=cache_params, attention_mask=attention_mask)

    def completa(self, hidden_states, position_embeddings, attention_mask, past_key_values=None, **kw):
        if _sp["activo"]:
            return _completa(self, hidden_states, position_embeddings, past_key_values)
        return comp(self, hidden_states, position_embeddings, attention_mask, past_key_values=past_key_values, **kw)

    q.Qwen3_5GatedDeltaNet.forward = lineal
    q.Qwen3_5Attention.forward = completa
    q.Qwen3_5Attention._repartible = True


@contextlib.contextmanager
def repartiendo(fichas=None):
    _sp["activo"], _sp["fichas"] = True, fichas
    try:
        yield
    finally:
        _sp["activo"], _sp["fichas"] = False, None


class Prefijo:
    """El prefijo de UNA corrida de entrenamiento, repartido entre dos placas.

    Lo constante se calcula una vez (los embeddings del prefijo: la tabla esta
    congelada; y las posiciones con su RoPE). Llamar ANTES de repartir el modelo
    con FSDP: los embeddings se leen de la tabla entera."""

    def __init__(self, texto, ids_pre, rank, mundo, grupo=None):
        if mundo != 2:
            raise SystemExit("el prefijo repartido es para dos placas")
        instalar()
        L = ids_pre.shape[1]
        self.m = m = L // 2
        lo, hi = (0, m) if rank == 0 else (m, L)
        self.rank, self.L, self.lo, self.hi = rank, L, lo, hi
        _sp.update(rank=rank, m=m, grupo=grupo or dist.new_group([0, 1]))
        with torch.no_grad():
            self.emb = texto.embed_tokens(ids_pre[:, lo:hi]).detach().clone()
            pos = torch.arange(lo, hi, device=ids_pre.device).view(1, 1, -1).expand(3, 1, -1)
            self.pos_emb = tuple(t.detach().clone() for t in texto.rotary_emb(self.emb, pos))
        self.capas = list(texto.layers)

    def adelante(self, kv):
        """Hacia adelante sin grafo, guardando la entrada de cada capa (para
        atras). Deja en kv el cache de los tokens de ESTA placa."""
        reg = []
        h = self.emb
        kw = dict(position_embeddings=self.pos_emb, attention_mask=None, position_ids=None,
                  past_key_values=kv, use_cache=True)
        with torch.no_grad(), repartiendo():
            for i, capa in enumerate(self.capas):
                reg.append((capa, i, h, (), kw, ch._foto_capa(kv.layers[i])))
                h = capa(h, **kw)
        return reg

    def juntar(self, kv):
        """El cache del prefijo ENTERO en las dos placas (lista plana como
        cache_hibrido.tensores), para los sufijos."""
        g = _sp["grupo"]
        out = []
        for c in kv.layers:
            if ch._lineal(c):  # el estado final es el de la placa 1
                a, b = c.conv_states, c.recurrent_states
                if self.rank == 1:
                    dist.send(a.contiguous(), 0, group=g)
                    dist.send(b.contiguous(), 0, group=g)
                else:
                    a, b = torch.empty_like(a), torch.empty_like(b)
                    dist.recv(a, 1, group=g)
                    dist.recv(b, 1, group=g)
                out += [a, b]
            else:
                mio = torch.stack([c.keys, c.values]).contiguous()
                forma = list(mio.shape)
                forma[3] = self.L - self.m if self.rank == 0 else self.m
                otro = torch.empty(forma, dtype=mio.dtype, device=mio.device)
                if self.rank == 0:
                    dist.send(mio, 1, group=g)
                    dist.recv(otro, 1, group=g)
                    kvs = torch.cat([mio, otro], dim=3)
                else:
                    dist.recv(otro, 0, group=g)
                    dist.send(mio, 0, group=g)
                    kvs = torch.cat([otro, mio], dim=3)
                out += [kvs[0], kvs[1]]
        return out

    def atras(self, reg, kv, grads):
        """grads: el gradiente de juntar(kv) YA SUMADO entre las placas. Cada placa
        se queda con lo que produjo y hace el backward de sus tokens."""
        mios = []
        for i, c in enumerate(kv.layers):
            gk, gv = grads[2 * i], grads[2 * i + 1]
            if ch._lineal(c):
                mios += [gk, gv] if self.rank == 1 else [None, None]
            else:
                mios += [None if t is None else t[:, :, self.lo:self.hi] for t in (gk, gv)]
        fichas = []
        with repartiendo(fichas):
            ch.atras_por_capas(reg, kv, mios, extra=fichas)


def sumar(hojas):
    """El gradiente del cache que junto cada placa con sus sufijos, sumado entre
    las dos (la misma cantidad de llamadas en las dos: sin gradiente va cero)."""
    out = []
    for h in hojas:
        t = h.grad if h.grad is not None else torch.zeros_like(h)
        dist.all_reduce(t)
        out.append(t)
    return out
