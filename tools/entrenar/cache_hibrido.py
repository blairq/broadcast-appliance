"""Cache de prefijo para modelos hibridos (Qwen3.5: atencion lineal + completa).

POR QUE HACE FALTA. Todo lo que mide o entrena en este directorio lee el
prefijo (las fichas, ~3.000 tokens) UNA vez y despues procesa cada frase ENTERA
encima de ese cache. Con Qwen3 es solo atencion, y funciona. Qwen3.5 tiene 3 de
cada 4 capas de atencion lineal (Gated DeltaNet), que no guardan claves sino un
ESTADO: una convolucion corta y una memoria recurrente. Y transformers 5.5 solo
usa ese estado cuando llega UN token por vez (`seq_len == 1`): con varios, la
capa arranca de cero y el prefijo desaparece para ella. Medido: logits
distintos hasta en 17 y la mitad de los tokens elegidos cambiados. Sin este
arreglo, evaluar.py mediria un modelo que no es el que se sirve, sin avisar.

Tres piezas:
  parchear()           la capa lineal continua desde el estado guardado tambien
                       con varios tokens (convolucion con el contexto anterior y
                       la regla delta con estado inicial). Diferenciable.
  repetir(kv, n)       el cache de UN prefijo, copiado para un lote de n.
  foto(kv)/volver(...) el estado despues del prefijo, para volver a el entre
                       frase y frase. Un estado recurrente no se «recorta»
                       como un cache de atencion: se restaura de una copia.
"""
import torch
import torch.nn.functional as F


def es_hibrido(modelo):
    tipos = getattr(getattr(modelo, "config", None), "layer_types", None)
    return bool(tipos) and "linear_attention" in tipos


def parchear():
    from transformers.models.qwen3_5 import modeling_qwen3_5 as q
    if getattr(q.Qwen3_5GatedDeltaNet, "_parche_prefijo", False):
        return
    original = q.Qwen3_5GatedDeltaNet.forward

    def forward(self, hidden_states, cache_params=None, attention_mask=None):
        B, L, _ = hidden_states.shape
        capa = cache_params.layers[self.layer_idx] if cache_params is not None else None
        continuar = (capa is not None and L > 1 and cache_params.has_previous_state(self.layer_idx))
        if not continuar:
            return original(self, hidden_states, cache_params=cache_params, attention_mask=attention_mask)
        # CONTINUAR con varios tokens desde el estado del prefijo.
        hidden_states = q.apply_mask_to_padding_states(hidden_states, attention_mask)
        mixed = self.in_proj_qkv(hidden_states).transpose(1, 2)          # (B, C, L)
        z = self.in_proj_z(hidden_states).reshape(B, L, -1, self.head_v_dim)
        b = self.in_proj_b(hidden_states)
        a = self.in_proj_a(hidden_states)
        K = self.conv_kernel_size
        previo = capa.conv_states.to(mixed.dtype)                         # (B, C, K): entradas anteriores
        x = torch.cat([previo[..., -(K - 1):], mixed], dim=-1)
        conv = F.conv1d(x, self.conv1d.weight, self.conv1d.bias, groups=x.shape[1])
        mixed = F.silu(conv).transpose(1, 2)
        capa.conv_states = x[..., -K:]
        query, key, value = torch.split(mixed, [self.key_dim, self.key_dim, self.value_dim], dim=-1)
        query = query.reshape(B, L, -1, self.head_k_dim)
        key = key.reshape(B, L, -1, self.head_k_dim)
        value = value.reshape(B, L, -1, self.head_v_dim)
        beta = b.sigmoid()
        g = -self.A_log.float().exp() * F.softplus(a.float() + self.dt_bias)
        if self.num_v_heads // self.num_k_heads > 1:
            query = query.repeat_interleave(self.num_v_heads // self.num_k_heads, dim=2)
            key = key.repeat_interleave(self.num_v_heads // self.num_k_heads, dim=2)
        out, estado = self.chunk_gated_delta_rule(
            query, key, value, g=g, beta=beta, initial_state=capa.recurrent_states,
            output_final_state=True, use_qk_l2norm_in_kernel=True)
        capa.recurrent_states = estado
        out = self.norm(out.reshape(-1, self.head_v_dim), z.reshape(-1, self.head_v_dim))
        return self.out_proj(out.reshape(B, L, -1))

    q.Qwen3_5GatedDeltaNet.forward = forward
    q.Qwen3_5GatedDeltaNet._parche_prefijo = True


def _lineal(c):
    return hasattr(c, "recurrent_states")


def repetir(kv, n):
    for c in kv.layers:
        if _lineal(c):
            c.conv_states = c.conv_states.repeat_interleave(n, 0)
            c.recurrent_states = c.recurrent_states.repeat_interleave(n, 0)
            c.max_batch_size = n
        else:
            c.batch_repeat_interleave(n)


def foto(kv):
    return [(c.conv_states.clone(), c.recurrent_states.clone()) if _lineal(c) else None
            for c in kv.layers]


def volver(kv, f, n_prefijo):
    """Deja el cache como estaba despues del prefijo."""
    kv.crop(n_prefijo)
    for c, e in zip(kv.layers, f):
        if e is not None:
            c.conv_states, c.recurrent_states = e[0].clone(), e[1].clone()
