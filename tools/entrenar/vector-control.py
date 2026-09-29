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
    ap.add_argument("--desde-capa", type=int, default=1,
                    help="dejar en cero las capas anteriores a esta")
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

    normas_act = None

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
            nonlocal normas_act
            na = h.norm(dim=1)
            normas_act = na if normas_act is None else normas_act + na
            del sal
            if (i + 1) % 64 == 0:
                print(f"  {i+1}/{len(filas_)}", flush=True)
        return acc / len(filas_)

    print("activaciones de los pedidos que SON suyos...")
    media_suyas = sumar(suyas)
    print("activaciones de los que van a derivar...")
    media_derivar = sumar(derivar)
    cruda = (media_derivar - media_suyas).cpu().numpy().astype(np.float32)
    act = (normas_act / (2 * len(suyas))).cpu().numpy().astype(np.float32)

    # NORMALIZAR, y es la parte que la primera version se comio. La diferencia
    # de medias sale con la magnitud que tenga el flujo residual de cada capa,
    # que en las ultimas es enorme: sumarla con escala 0,25 no es empujar, es
    # tapar la activacion con otra cosa. Medido en la corrida que fallo: la
    # precision se caia de 88% a 44% con 0,25 y a 11% con 0,5, y las
    # derivaciones no subian NUNCA, que es la firma de estar rompiendo y no
    # dirigiendo.
    #
    # Asi, cada capa queda con norma igual a la activacion tipica de ESA capa,
    # y entonces la escala significa algo parejo en todas: 0,1 es «mové la
    # activacion un diez por ciento». Es una unidad que se puede razonar.
    normas = np.linalg.norm(cruda, axis=1)
    unit = cruda / np.maximum(normas, 1e-6)[:, None]
    direccion = (unit * act[:, None]).astype(np.float32)
    if args.desde_capa > 1:
        direccion[:args.desde_capa - 1] = 0.0
        print(f"capas 1..{args.desde_capa - 1} en cero")

    # CUANTO SEPARA, en relacion a lo que hay. Es el numero que importa y el
    # que la primera version no imprimia: una diferencia de 900 sobre
    # activaciones de 900 es media activacion de separacion; sobre 9000 no es
    # nada. La norma sola no dice cual de las dos.
    rel = normas / np.maximum(act, 1e-6)
    orden = np.argsort(-rel)[:5]
    print(f"\ncapas: {n_capas} · dimension: {cruda.shape[1]}")
    print("las capas donde mas separa (separacion / activacion tipica):")
    for i in orden:
        print(f"   capa {int(i)+1:2d}: {rel[i]:.3f}  (dif {normas[i]:.1f} sobre act {act[i]:.1f})")
    print(f"separacion relativa media {rel.mean():.3f} · maxima {rel.max():.3f}")
    if rel.max() < 0.05:
        print("  OJO: la separacion es chica frente a la activacion. Empujar por")
        print("       ahi va a romper antes de dirigir.")

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
