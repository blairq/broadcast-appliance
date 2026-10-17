"""Un Qwen3.5 de juguete (4 capas, 256 de ancho, pesos al azar) con el tokenizador real.

Para probar entrenar-rapido (sft, dpo, kto) en segundos y con menos de 1 GB de
placa, sin molestar a un entrenamiento que este corriendo en la otra:

    .venv-entrenar/bin/python tools/entrenar/pruebas/juguete.py <carpeta>
"""
import sys
import torch
from transformers import AutoConfig, AutoTokenizer, AutoModelForCausalLM
dest = sys.argv[1]
c = AutoConfig.from_pretrained("Qwen/Qwen3.5-0.8B")
t = c.text_config if hasattr(c, "text_config") else c
t.num_hidden_layers = 4
t.layer_types = ["linear_attention"] * 3 + ["full_attention"]
t.hidden_size, t.intermediate_size = 256, 512
t.num_attention_heads, t.num_key_value_heads, t.head_dim = 4, 2, 64
t.linear_num_key_heads, t.linear_num_value_heads = 4, 4
t.linear_key_head_dim = t.linear_value_head_dim = 32
for k in ("mtp_num_hidden_layers",):
    if hasattr(t, k):
        setattr(t, k, 0)
torch.manual_seed(0)
m = AutoModelForCausalLM.from_config(t, dtype=torch.bfloat16)
print(type(m).__name__, sum(p.numel() for p in m.parameters()) / 1e6, "M")
m.save_pretrained(dest)
AutoTokenizer.from_pretrained("Qwen/Qwen3.5-4B").save_pretrained(dest)
