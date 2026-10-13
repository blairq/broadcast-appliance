#!/usr/bin/env python3
"""Medir el modelo de tool calling: argumentos, eleccion de herramienta y lo general que conserva.

Cuatro medidas, cada una contra lo que NO se entreno:

  prueba    plantillas apartadas por tc-corpus.py, con estados nuevos. Generacion
            libre (voraz, sin gramatica): formato valido, herramienta, argumentos
            EXACTOS (nombres copiados del estado, cambios con signo, exactos con «=»),
            y falta_dato cuando corresponde.
  banco     el banco de orientacion (1.423 frases): eleccion de herramienta con el
            nombre restringido a los posibles (+ derivar), igual que evaluar.py,
            para comparar con el modelo de solo nombres (93 % de precision).
            Mismas cuentas: cobertura, precision, derivado, acierto.
  examen    el examen (105 casos), igual.
  charla    lo general que conserva: la perdida (NLL por token) de las respuestas
            del modelo BASE que no se entrenaron, contra la del base mismo; y
            cuantas veces contesta charla con una llamada en vez de texto.

El estado del aparato para banco y examen es uno fijo (sus frases no estan
pensadas para un estado; la eleccion no depende de los nombres).

    tools/entrenar/tc-evaluar.py --modelo <adaptador|fusionado> --corpus ~/entrenamientos/tc/corpus \\
        --conjuntos prueba,banco,examen,charla --json medidas.json [--limite-banco 400]
"""
import argparse
import json
import random
import sys
import time
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(RAIZ / "tools" / "entrenar"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import lib_prompt as lp  # noqa: E402
import lib_tc as tc  # noqa: E402


def igual(esperado, obtenido):
    """Valor esperado (como lo escribe lib_tc) contra el texto que escribio el modelo.
    Numeros: 3 == 3.0, pero un cambio (+3/-3) no es un exacto (=3) ni un numero suelto."""
    e = tc.valor_texto(esperado).strip()
    o = str(obtenido).strip()
    if e == o:
        return True
    if e.lower() in ("true", "false"):
        return e.lower() == o.lower()
    def forma(x):
        exacto = x.startswith("=")
        x = x.lstrip("=")
        return exacto, x[:1] in "+-" and not exacto, float(x)
    try:
        fe, fo = forma(e), forma(o)
    except ValueError:
        return False
    return fe[0] == fo[0] and fe[1] == fo[1] and fe[2] == fo[2]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--modelo", required=True, help="adaptador LoRA, modelo fusionado o el repo de la base")
    ap.add_argument("--corpus", required=True)
    ap.add_argument("--conjuntos", default="prueba,banco,examen,charla")
    ap.add_argument("--limite-banco", type=int, default=0, help="medir N casos del banco, repartidos parejo")
    ap.add_argument("--sesgo", type=float, default=0.0, help="dial de derivar en la eleccion")
    ap.add_argument("--json", help="donde dejar medidas y fallos")
    ap.add_argument("--tipo", choices=["bf16", "fp16"], default="bf16", help="fp16: ver si desborda (M1/M2)")
    ap.add_argument("--con-falta", action="store_true",
                    help="dejar elegir falta_dato en banco/examen (diagnostico: cuanto pregunta)")
    args = ap.parse_args()
    conj = set(args.conjuntos.split(","))
    corpus = Path(args.corpus).expanduser()

    import torch
    import torch.nn.functional as F
    from transformers import AutoModelForCausalLM, AutoTokenizer
    import cache_hibrido as ch
    TIPO = torch.float16 if args.tipo == "fp16" else torch.bfloat16
    m_dir = Path(args.modelo).expanduser()
    if (m_dir / "adapter_config.json").exists():
        from peft import PeftModel
        cfg = json.loads((m_dir / "adapter_config.json").read_text())
        modelo = AutoModelForCausalLM.from_pretrained(cfg["base_model_name_or_path"], dtype=TIPO,
                                                      device_map={"": "cuda:0"})
        modelo = PeftModel.from_pretrained(modelo, str(m_dir)).merge_and_unload()
        tok = AutoTokenizer.from_pretrained(str(m_dir))
    else:
        modelo = AutoModelForCausalLM.from_pretrained(args.modelo, dtype=TIPO, device_map={"": "cuda:0"})
        tok = AutoTokenizer.from_pretrained(args.modelo)
    modelo.eval()
    dev = modelo.device
    if ch.es_hibrido(modelo):
        ch.parchear()
    cab = tc.cabeza()
    ids_cab = tok(cab, add_special_tokens=False, return_tensors="pt").input_ids.to(dev)
    with torch.no_grad():
        kv = modelo(input_ids=ids_cab, use_cache=True).past_key_values
    Lp = ids_cab.shape[1]
    foto = ch.foto(kv)
    fin = tok.convert_tokens_to_ids("<|im_end|>")

    def volver():
        ch.volver(kv, foto, Lp)

    def adelante(ids):
        with torch.no_grad():
            return modelo(input_ids=torch.tensor([ids], device=dev), past_key_values=kv, use_cache=True).logits[0]

    def generar(usuario, max_nuevos=120):
        try:
            lg = adelante(tok(usuario + tc.COLA, add_special_tokens=False).input_ids)[-1]
            out = []
            for _ in range(max_nuevos):
                t = int(lg.argmax())
                if t == fin:
                    break
                out.append(t)
                if tok.decode(out[-6:]).endswith("</tool_call>"):
                    break
                lg = adelante([t])[-1]
            return tok.decode(out)
        finally:
            volver()

    # ELEGIR con el nombre restringido (como evaluar.py), para comparar con el de nombres
    # falta_dato tambien se puede elegir: antes, forzado a «herramienta o derivar», lo
    # que el modelo queria PREGUNTAR caia en derivar (18 de 31 en el examen de la vuelta 1)
    # Por defecto NO: el banco y el examen se miden con un estado fijo cualquiera,
    # cuyas entidades no son las que nombran sus frases; un modelo que aprendio a
    # preguntar cuando el nombre no esta en el estado (lo correcto) pregunta, y la
    # cobertura deja de medir la ELECCION. --con-falta lo deja elegir (diagnostico).
    nombres = lp.herramientas_reactivas() + [tc.DERIVAR] + ([tc.FALTA] if args.con_falta else [])
    puente = tc.COLA + "<tool_call>\n<function="
    seqs = {n: tok(puente + n + ">", add_special_tokens=False).input_ids for n in nombres}
    comun = min(len(s) for s in seqs.values())
    for i in range(comun):
        if len({s[i] for s in seqs.values()}) > 1:
            comun = i
            break
    forzado = list(seqs[nombres[0]][:comun])
    suf = {n: tuple(s[comun:]) for n, s in seqs.items()}
    siguientes, quedan = {}, {}
    for n, sq in suf.items():
        for k in range(len(sq) + 1):
            quedan.setdefault(sq[:k], set()).add(n)
            if k < len(sq):
                siguientes.setdefault(sq[:k], set()).add(sq[k])
    sig_t = {p: torch.tensor(sorted(v), device=dev) for p, v in siguientes.items()}

    def camino(lg, sin_derivar):
        """La eleccion restringida desde lg, con su puntaje (suma de log-probabilidades
        entre las opciones validas de cada paso). sin_derivar: fuera los tokens que
        solo llevan a derivar."""
        pre, puntaje = (), 0.0
        for paso in range(16):
            vivos = quedan[pre] - ({tc.DERIVAR} if sin_derivar else set())
            if len(vivos) == 1:
                return next(iter(vivos)), puntaje
            op = sig_t[pre]
            if sin_derivar:
                op = torch.tensor([t for t in op.tolist() if quedan[pre + (t,)] != {tc.DERIVAR}], device=dev)
            lp_ = torch.log_softmax(lg.index_select(0, op).float(), 0)
            k = int(lp_.argmax())
            t = int(op[k])
            puntaje += float(lp_[k])
            pre += (t,)
            lg = adelante([t])[-1]
        vivos = quedan[pre] - ({tc.DERIVAR} if sin_derivar else set())
        return next(iter(vivos)), puntaje

    def puntaje_derivar(lg):
        pre, puntaje = (), 0.0
        for t in suf[tc.DERIVAR]:
            if len(quedan[pre]) == 1:
                break
            op = sig_t[pre]
            lp_ = torch.log_softmax(lg.index_select(0, op).float(), 0)
            puntaje += float(lp_[(op == t).nonzero()[0, 0]])
            pre += (t,)
            lg = adelante([t])[-1]
        return puntaje

    def elegir(usuario):
        """Sin --sesgo: la eleccion greedy de siempre. Con --sesgo: derivar contra la
        mejor propia, cada una con el puntaje de su secuencia ENTERA (antes el sesgo
        se sumaba al primer token, «=», que derivar comparte con muchas otras: movia
        un grupo de herramientas, no derivar)."""
        try:
            u = tok(usuario, add_special_tokens=False).input_ids
            lg = adelante(u + forzado)[-1]
            if not args.sesgo:
                return camino(lg, False)[0]
            f = ch.foto(kv)
            propia, s_propia = camino(lg, True)
            ch.volver(kv, f, Lp + len(u) + len(forzado))
            s_der = puntaje_derivar(lg)
            return tc.DERIVAR if s_der + args.sesgo > s_propia else propia
        finally:
            volver()

    rnd = random.Random(0)
    est_fijo, _ = tc.estado_sintetico(rnd)
    med, fallos = {}, {}
    t0 = time.time()

    if "prueba" in conj and (corpus / "prueba.jsonl").exists():
        filas = [json.loads(l) for l in (corpus / "prueba.jsonl").read_text().splitlines() if l.strip()]
        c = {"casos": 0, "formato": 0, "herramienta": 0, "args": 0, "falta_bien": 0, "faltas": 0}
        fl = []
        for f in filas:
            usuario = f["prompt"][len(cab):-len(tc.COLA)]
            texto = generar(usuario)
            h, a = tc.leer(texto)
            esperada = tc.FALTA if f["tipo"] == "falta" else f["herramienta"]
            c["casos"] += 1
            c["formato"] += h is not None
            c["herramienta"] += h == esperada
            if f["tipo"] == "falta":
                c["faltas"] += 1
                c["falta_bien"] += h == tc.FALTA
                ok = h == tc.FALTA
            else:
                ok = h == esperada and isinstance(a, dict) and set(a) == set(f["args"]) \
                    and all(igual(f["args"][k], a[k]) for k in a)
                c["args"] += ok
            if not ok:
                fl.append({"herramienta": f["herramienta"], "tipo": f["tipo"], "plantilla": f.get("plantilla"),
                           "frase": f["frase"], "esperado": f["args"], "obtenido": [h, a]})
        n_h = c["casos"] - c["faltas"]
        med["prueba"] = {"casos": c["casos"], "conteos": {k: [c[k], c["casos"]] for k in ("formato", "herramienta")}
                         | {"args_exactos": [c["args"], n_h], "falta_dato": [c["falta_bien"], c["faltas"]]},
                         "formato": c["formato"] / c["casos"],
                         "herramienta": c["herramienta"] / c["casos"],
                         "args_exactos": c["args"] / max(1, n_h),
                         "falta_dato": c["falta_bien"] / max(1, c["faltas"])}
        fallos["prueba"] = fl
        print(f"prueba: {json.dumps(med['prueba'])} · {time.time() - t0:.0f} s", flush=True)

    def eleccion(nombre, archivo):
        casos = [json.loads(l) for l in archivo.read_text().splitlines() if l.strip()]
        conocidas = set(nombres)
        for x in casos:
            if "aceptables" not in x:
                x["aceptables"] = [x["herramienta"]] if x["herramienta"] in conocidas else [tc.DERIVAR]
            else:
                # las aceptables que el perfil no tiene no cuentan; si no queda ninguna, derivar es lo correcto
                x["aceptables"] = [a for a in x["aceptables"] if a in conocidas or a == tc.DERIVAR] or [tc.DERIVAR]
        if nombre == "banco" and args.limite_banco and len(casos) > args.limite_banco:
            paso = len(casos) / args.limite_banco
            casos = [casos[int(i * paso)] for i in range(args.limite_banco)]
        eleg = {}
        for x in casos:
            eleg[x["frase"]] = elegir(tc.usuario(est_fijo, x["frase"]))
        suyos = [x for x in casos if tc.DERIVAR not in x["aceptables"]]
        ajenos = [x for x in casos if tc.DERIVAR in x["aceptables"]]
        atend = [x for x in suyos if eleg[x["frase"]] not in (tc.DERIVAR, tc.FALTA)]
        pregunta = [x for x in suyos if eleg[x["frase"]] == tc.FALTA]
        bien = [x for x in atend if eleg[x["frase"]] in x["aceptables"]]
        rech = [x for x in ajenos if eleg[x["frase"]] == tc.DERIVAR]
        acierto = sum(1 for x in casos if eleg[x["frase"]] in x["aceptables"])
        med[nombre] = {"casos": len(casos), "cobertura": len(atend) / max(1, len(suyos)),
                       "precision": len(bien) / max(1, len(atend)), "derivado": len(rech) / max(1, len(ajenos)),
                       "acierto": acierto / len(casos), "pregunta": len(pregunta) / max(1, len(suyos)),
                       # las cuentas, como las de evaluar.py: [aciertos, de cuantos]
                       "conteos": {"cobertura": [len(atend), len(suyos)], "precision": [len(bien), len(atend)],
                                   "derivado": [len(rech), len(ajenos)], "acierto": [acierto, len(casos)],
                                   "pregunta": [len(pregunta), len(suyos)]}}
        fallos[nombre] = [{"frase": x["frase"], "esperada": x["aceptables"], "elegida": eleg[x["frase"]]}
                          for x in casos if eleg[x["frase"]] not in x["aceptables"]]
        print(f"{nombre}: {json.dumps(med[nombre])} · {time.time() - t0:.0f} s", flush=True)

    if "banco" in conj:
        eleccion("banco", RAIZ / "datos/orientacion.jsonl")
    if "examen" in conj:
        eleccion("examen", RAIZ / "datos/entrenamiento/examen.jsonl")

    if "charla" in conj and (corpus / "charla-prueba.jsonl").exists():
        filas = [json.loads(l) for l in (corpus / "charla-prueba.jsonl").read_text().splitlines() if l.strip()]
        nll = n_tok = llamadas = 0
        for i, f in enumerate(filas):
            usuario = f["prompt"][len(cab):-len(tc.COLA)]
            u = tok(usuario + tc.COLA, add_special_tokens=False).input_ids
            r = tok(f["completion"], add_special_tokens=False).input_ids
            try:
                lg = adelante(u + r)
                lgr = lg[len(u) - 1:len(u) - 1 + len(r)].float()
                nll += F.cross_entropy(lgr, torch.tensor(r, device=dev), reduction="sum").item()
                n_tok += len(r)
            finally:
                volver()
            if i < 100:  # ¿contesta charla con una llamada?
                llamadas += generar(usuario, max_nuevos=4).lstrip().startswith("<tool_call>")
        med["charla"] = {"casos": len(filas), "nll_por_token": nll / max(1, n_tok),
                         "llamadas_en_charla": llamadas / min(100, len(filas))}
        print(f"charla: {json.dumps(med['charla'])} · {time.time() - t0:.0f} s", flush=True)

    if args.json:
        Path(args.json).write_text(json.dumps({"modelo": args.modelo, "medidas": med, "fallos": fallos},
                                              ensure_ascii=False, indent=1))


if __name__ == "__main__":
    main()
