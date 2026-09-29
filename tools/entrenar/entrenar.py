#!/usr/bin/env python3
"""Fine-tune del modelo chico que atiende los eventos inmediatos.

DONDE CORRE: en el servidor con las 3090, no en la notebook. Entrenar es un
paso de CONSTRUCCION; lo que tiene que ser local y portable es el .gguf que
sale de aca y corre en el aparato.

LO QUE SE ENTRENA Y LO QUE NO:
  · Se entrena a ELEGIR la herramienta, no a escribir sus argumentos. Medido en
    la notebook: contestar el JSON entero son 2,8 s y contestar solo el nombre
    0,79 s, con el mismo acierto. Los argumentos los sabe el aparato, que tiene
    la lista de canales y el que esta picando.
  · El prompt es EXACTAMENTE el que se sirve, fichas incluidas. Lo arma
    tools/lib_prompt.py, el mismo modulo que usa el banco. Entrenar con una
    forma y servir con otra degrada el modelo sin avisar: en este proyecto se
    midio de casualidad y fueron 40 puntos.
  · La perdida se calcula SOLO sobre la respuesta. El prompt son 2.650 tokens
    de fichas que se repiten identicos en cada ejemplo: dejar que aporten a la
    perdida es ensenarle a predecir su propio prompt.

LINEA DE BASE A SUPERAR (Qwen3-1.7B sin entrenar, 48 casos del examen):
    Q4_K_M  58%   ·   Q8_0  62%   ·   Q4 + 73 ejemplos en contexto  64%

  pip install unsloth trl datasets
  tools/entrenar/entrenar.py --salida modelos/reactivo-v1
"""
import argparse
import json
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(RAIZ / "tools"))

DATOS = RAIZ / "datos/entrenamiento"
# Donde arranca la respuesta. Tiene que coincidir con lib_prompt.partes():
# si cambia alla y no aca, la mascara de perdida tapa lo que no debe.
MARCA_RESPUESTA = "<|im_start|>assistant\n<think>\n\n</think>\n\n"


def cargar(nombre):
    filas = []
    for linea in (DATOS / nombre).read_text(encoding="utf-8").splitlines():
        if linea.strip():
            f = json.loads(linea)
            filas.append({"text": f["prompt"] + f["completion"]})
    return filas


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", default="unsloth/Qwen3-1.7B")
    ap.add_argument("--salida", default="modelos/reactivo-v1")
    ap.add_argument("--epocas", type=float, default=3.0)
    ap.add_argument("--lr", type=float, default=2e-4)
    ap.add_argument("--rango", type=int, default=32, help="r del LoRA")
    ap.add_argument("--lote", type=int, default=2)
    ap.add_argument("--acumular", type=int, default=8)
    ap.add_argument("--largo", type=int, default=4096)
    ap.add_argument("--semilla", type=int, default=7)
    ap.add_argument("--gguf", default="q8_0,q4_k_m", help="cuantizaciones a exportar")
    ap.add_argument("--prueba", action="store_true", help="20 pasos, para ver que arranca")
    args = ap.parse_args()

    from unsloth import FastLanguageModel  # noqa: E402  (tiene que ir antes que transformers)
    from unsloth.chat_templates import train_on_responses_only  # noqa: E402
    from datasets import Dataset  # noqa: E402
    from trl import SFTTrainer, SFTConfig  # noqa: E402

    ent, val = cargar("entrenar.jsonl"), cargar("validar.jsonl")
    print(f"entrenar {len(ent)} · validar {len(val)} · largo maximo {args.largo}")

    modelo, tok = FastLanguageModel.from_pretrained(
        model_name=args.base, max_seq_length=args.largo,
        load_in_4bit=False,   # un 1.7B entra entero en una 3090: no hace falta QLoRA
        dtype=None)

    modelo = FastLanguageModel.get_peft_model(
        modelo, r=args.rango, lora_alpha=args.rango, lora_dropout=0.0, bias="none",
        # TODAS las lineales, tambien las del MLP: con solo q,v el modelo
        # aprende a reconocer palabras pero no a reasignar clases parecidas,
        # que es justo donde estan los errores (armar contra tomar).
        target_modules=["q_proj", "k_proj", "v_proj", "o_proj",
                        "gate_proj", "up_proj", "down_proj"],
        use_gradient_checkpointing="unsloth", random_state=args.semilla)

    cfg = SFTConfig(
        output_dir=args.salida, per_device_train_batch_size=args.lote,
        gradient_accumulation_steps=args.acumular, warmup_ratio=0.05,
        num_train_epochs=args.epocas, max_steps=20 if args.prueba else -1,
        learning_rate=args.lr, lr_scheduler_type="cosine", optim="adamw_8bit",
        weight_decay=0.01, logging_steps=5, seed=args.semilla,
        bf16=True, max_seq_length=args.largo, dataset_text_field="text",
        # Sin empaquetar: los ejemplos son largos y parejos, y empaquetar
        # mezclaria dos pedidos en una secuencia.
        packing=False,
        eval_strategy="steps", eval_steps=25, save_strategy="steps", save_steps=25,
        load_best_model_at_end=True, metric_for_best_model="eval_loss",
        greater_is_better=False, save_total_limit=2, report_to="none")

    entrenador = SFTTrainer(model=modelo, tokenizer=tok, args=cfg,
                            train_dataset=Dataset.from_list(ent),
                            eval_dataset=Dataset.from_list(val))
    # La perdida SOLO sobre la respuesta.
    entrenador = train_on_responses_only(
        entrenador, instruction_part="<|im_start|>user\n", response_part=MARCA_RESPUESTA)

    # Que la mascara es la correcta se MIRA, no se supone: un error aca no
    # falla, entrena mal y da un modelo mediocre sin decir por que.
    ej = entrenador.train_dataset[0]
    vistos = [t for t, m in zip(ej["input_ids"], ej["labels"]) if m != -100]
    print("la perdida se calcula sobre:", repr(tok.decode(vistos)))

    res = entrenador.train()
    print(json.dumps(res.metrics, indent=1))

    modelo.save_pretrained(args.salida)
    tok.save_pretrained(args.salida)
    for q in args.gguf.split(","):
        q = q.strip()
        if q:
            print(f"exportando GGUF {q}")
            modelo.save_pretrained_gguf(f"{args.salida}-{q}", tok, quantization_method=q)
    print(f"\nlisto. Ahora, EN LA NOTEBOOK, contra el mismo examen:\n"
          f"  tools/bateria-local.py --modelo <el .gguf> --solo-nombre")


if __name__ == "__main__":
    main()
