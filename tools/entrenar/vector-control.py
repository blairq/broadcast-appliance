#!/usr/bin/env python3
"""El vector que empuja al modelo hacia `derivar`, sin volver a entrenarlo.

LA IDEA, que es de Jorge. Una abliteracion borra una direccion del espacio de
activaciones —la del rechazo— y con eso apaga un comportamiento. Esto hace lo
contrario: encuentra la direccion que separa «esto es mio» de «esto lo paso al
grande» y la EMPUJA. Misma maquinaria, signo cambiado.

POR QUE TIENE SENTIDO EN ESTE MODELO. Medido sobre la serie Qwen3
(arxiv 2608.18106): la certeza sale de una coalicion amplia de rasgos
compartidos, y la incertidumbre es un override disperso, mediado por un grupo
CHICO de rasgos dedicados. Un grupo chico es lo que se puede empujar. Y explica
por que un 1,7B «no produce incertidumbre util»: tiene el mecanismo, pero por
defecto pierde contra la coalicion de la certeza.

COMO SE CALCULA. Adicion contrastiva de activaciones (CAA, arxiv 2312.06681):
se pasan pares de ejemplos —unos que van a una herramienta, otros que van a
`derivar`— y se promedia la DIFERENCIA de activaciones, capa por capa, en la
posicion donde el modelo decide (el ultimo token del prompt). Eso es el vector.

POR QUE ES EL UNICO METODO DE ESTA LISTA QUE DA UN DIAL. La escala es un solo
escalar y se aplica al servir. Barrerla sobre el examen da la curva de cobertura
contra precision, o sea que el punto de operacion se elige DESPUES de entrenar:
hay programas donde conviene que el chico arriesgue y programas donde conviene
que derive a la primera duda. Ningun otro metodo permite moverlo sin reentrenar.

EL EXAMEN NO SE TOCA. El vector se calcula desde el conjunto de entrenamiento,
que es dato que el modelo ya vio. Si se calculara desde el examen, la curva que
sale despues estaria ajustada al examen y no mediria nada.

  tools/entrenar/vector-control.py --modelo ~/entrenamientos/reactivo-v6 \\
      --salida ~/entrenamientos/reactivo-v6-derivar.gguf
"""
import argparse
import json
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(RAIZ / "tools"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
DATOS = RAIZ / "datos/entrenamiento"


from vector_control_util import capas_del  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--modelo", required=True, help="el modelo ya entrenado")
    ap.add_argument("--salida", required=True, help=".gguf para --control-vector")
    ap.add_argument("--datos", default=str(DATOS / "entrenar.jsonl"))
    ap.add_argument("--pares", type=int, default=256,
                    help="cuantos ejemplos de cada lado (se equilibran)")
    ap.add_argument("--largo", type=int, default=4096)
    args = ap.parse_args()

    from unsloth import FastLanguageModel  # noqa: E402
    import torch  # noqa: E402
    import numpy as np  # noqa: E402
    import gguf  # noqa: E402
    import lib_prompt as lp  # noqa: E402

    filas = [json.loads(l) for l in Path(args.datos).read_text(encoding="utf-8").splitlines() if l.strip()]
    derivar = [f for f in filas if f["herramienta"] == lp.DERIVAR]
    suyas = [f for f in filas if f["herramienta"] != lp.DERIVAR]
    if not derivar:
        sys.exit("no hay ejemplos de derivar en el conjunto: sin contraste no hay direccion")
    # EQUILIBRADO A PROPOSITO. Si un lado tiene diez veces mas ejemplos, la
    # diferencia de medias mide sobre todo la variedad de ese lado y no lo que
    # separa a los dos.
    import random
    rnd = random.Random(7)
    rnd.shuffle(derivar)
    rnd.shuffle(suyas)
    n = min(args.pares, len(derivar), len(suyas))
    derivar, suyas = derivar[:n], suyas[:n]
    print(f"{n} ejemplos de cada lado, de {Path(args.datos).name}")

    modelo, tok = FastLanguageModel.from_pretrained(
        model_name=args.modelo, max_seq_length=args.largo, load_in_4bit=False, dtype=None)
    FastLanguageModel.for_inference(modelo)
    n_capas = len(capas_del(modelo))
    n_embd = modelo.config.hidden_size if hasattr(modelo, "config") else None

    def sumar(filas_):
        """Suma la activacion del ULTIMO token del prompt, capa por capa.

        El ultimo token es donde el modelo ya leyo todo y esta por contestar:
        es la posicion en la que la decision existe. Cualquier otra mide el
        texto, no la decision.
        """
        acc = None
        for i, f in enumerate(filas_):
            ent = tok(f["prompt"], return_tensors="pt", truncation=True,
                      max_length=args.largo).to(modelo.device)
            with torch.no_grad():
                sal = modelo(**ent, output_hidden_states=True)
            # hidden_states[0] son los embeddings; [i] es la salida de la capa i,
            # que es exactamente el indice que usa llama.cpp para `direction.i`.
            h = torch.stack([e[0, -1, :].float() for e in sal.hidden_states[1:]])
            acc = h if acc is None else acc + h
            del sal
            if (i + 1) % 64 == 0:
                print(f"  {i+1}/{len(filas_)}", flush=True)
        return acc / len(filas_)

    print("activaciones de los pedidos que SON suyos...")
    media_suyas = sumar(suyas)
    print("activaciones de los que van a derivar...")
    media_derivar = sumar(derivar)
    direccion = (media_derivar - media_suyas).cpu().numpy().astype(np.float32)

    # Donde esta la senal. Si las normas son parejas en todas las capas, no hay
    # direccion: hay ruido. Las capas del medio suelen mandar.
    normas = np.linalg.norm(direccion, axis=1)
    orden = np.argsort(-normas)[:5]
    print(f"\ncapas: {n_capas} · dimension: {direccion.shape[1]}")
    print("las capas donde mas separa: " +
          ", ".join(f"{int(i)+1} ({normas[i]:.2f})" for i in orden))
    print(f"norma media {normas.mean():.2f} · minima {normas.min():.2f}")
    if normas.max() < 2 * normas.min():
        print("  OJO: las normas son parejas. Eso es lo que se ve cuando NO hay")
        print("       una direccion clara, y el vector no va a empujar nada.")

    w = gguf.GGUFWriter(args.salida, "controlvector")
    w.add_uint32("controlvector.layer_count", direccion.shape[0])
    for i in range(direccion.shape[0]):
        # `direction.1` es la primera capa: llama.cpp rechaza el indice cero.
        w.add_tensor(f"direction.{i+1}", direccion[i])
    w.write_header_to_file()
    w.write_kv_data_to_file()
    w.write_tensors_to_file()
    w.close()
    print(f"\n-> {args.salida}")
    print("   probarlo:  tools/entrenar/barrer-escala.sh <modelo> <vector>")
    print("   al servir: llama-cli --control-vector-scaled " + args.salida + ":<escala>")


if __name__ == "__main__":
    main()
