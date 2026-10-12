#!/usr/bin/env python3
"""Laboratorio de DERIVAR con una SONDA sobre el estado interno del modelo.

LA PREGUNTA. El modelo deriva mal en el borde de su catalogo, y moverle el
umbral no alcanza (los errores son seguros). ¿La informacion de «esto no es
mio» esta adentro y la decision la usa mal? Una sonda lineal (regresion
logistica) sobre el estado oculto en el punto exacto donde el modelo va a
escribir el nombre de la herramienta lo dice: si separa mejor que el propio
modelo, se puede usar como enrutador (y el contraste apareado tiene de donde
sacarla).

  datos de la sonda: «propio» = frases de herramienta del corpus v4; «ajeno» =
    las de derivar del laboratorio (lab-derivar.py). Nunca banco ni examen.
  capa: la que mejor separa en una validacion aparte (AUC).
  medida: en el banco entero y el examen, la sonda decide derivar o no; si no,
    vale la mejor herramienta propia del modelo (sin derivar). Contra la curva
    del modelo mismo (su margen a favor de derivar), A IGUAL COBERTURA.

    BA_PERFIL=vivo4 python tools/entrenar/lab-sonda.py --modelo ~/entrenamientos/tc-v4/mejor
"""
import argparse
import json
import random
import re
import sys
import unicodedata
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(RAIZ / "tools" / "entrenar"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import lib_prompt as lp  # noqa: E402
import lib_tc as tc  # noqa: E402


def norm(s):
    s = unicodedata.normalize("NFKD", s.lower()).encode("ascii", "ignore").decode()
    return re.sub(r"\W+", " ", s).strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--modelo", required=True)
    ap.add_argument("--corpus-v4", default="~/entrenamientos/tc-v4/vuelta-0/corpus")
    ap.add_argument("--corpus-lab", default="~/entrenamientos/lab-derivar/corpus")
    ap.add_argument("--salida", default="~/entrenamientos/lab-derivar/sonda")
    ap.add_argument("--capas", default="8,12,16,20,24,28,32")
    ap.add_argument("--n-propias", type=int, default=1800)
    args = ap.parse_args()
    out = Path(args.salida).expanduser()
    out.mkdir(parents=True, exist_ok=True)

    import torch
    from transformers import AutoModelForCausalLM, AutoTokenizer
    from peft import PeftModel
    import cache_hibrido as ch
    m_dir = Path(args.modelo).expanduser()
    cfg = json.loads((m_dir / "adapter_config.json").read_text())
    modelo = AutoModelForCausalLM.from_pretrained(cfg["base_model_name_or_path"], dtype=torch.bfloat16,
                                                  device_map={"": "cuda:0"})
    modelo = PeftModel.from_pretrained(modelo, str(m_dir)).merge_and_unload().eval()
    tok = AutoTokenizer.from_pretrained(str(m_dir))
    dev = modelo.device
    ch.parchear()
    ids_cab = tok(tc.cabeza(), add_special_tokens=False, return_tensors="pt").input_ids.to(dev)
    with torch.no_grad():
        kv = modelo(input_ids=ids_cab, use_cache=True).past_key_values
    Lp = ids_cab.shape[1]
    foto = ch.foto(kv)

    # la eleccion restringida, igual que tc-evaluar (sin falta_dato)
    nombres = lp.herramientas_reactivas() + [tc.DERIVAR]
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
    capas = [int(c) for c in args.capas.split(",")]
    est_fijo, _ = tc.estado_sintetico(random.Random(0))  # el mismo estado fijo que tc-evaluar

    def camino(lg0, sin_derivar):
        """La eleccion restringida desde los logits lg0 (el punto de decision), con su
        puntaje (suma de log-probabilidades entre las opciones validas de cada paso).
        sin_derivar: fuera los tokens que solo llevan a derivar (no el «=» que
        derivar comparte con otras herramientas)."""
        lg, pre, puntaje = lg0, (), 0.0
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
            with torch.no_grad():
                lg = modelo(input_ids=torch.tensor([[t]], device=dev), past_key_values=kv, use_cache=True).logits[0, -1]
        return next(iter(quedan[pre] - ({tc.DERIVAR} if sin_derivar else set()))), puntaje

    def puntaje_derivar(lg0):
        lg, pre, puntaje = lg0, (), 0.0
        for t in suf[tc.DERIVAR]:
            if len(quedan[pre]) == 1:
                break
            op = sig_t[pre]
            lp_ = torch.log_softmax(lg.index_select(0, op).float(), 0)
            puntaje += float(lp_[(op == t).nonzero()[0, 0]])
            pre += (t,)
            with torch.no_grad():
                lg = modelo(input_ids=torch.tensor([[t]], device=dev), past_key_values=kv, use_cache=True).logits[0, -1]
        return puntaje

    def mirar(frase):
        """(rasgos por capa en el punto de decision, margen del modelo a favor de
        derivar = puntaje de derivar - el de la mejor propia, mejor propia)."""
        u = tok(tc.usuario(est_fijo, frase), add_special_tokens=False).input_ids
        try:
            with torch.no_grad():
                o = modelo(input_ids=torch.tensor([u + forzado], device=dev), past_key_values=kv,
                           use_cache=True, output_hidden_states=True)
            rasgos = torch.stack([o.hidden_states[c][0, -1].float() for c in capas]).cpu()
            lg0 = o.logits[0, -1]
            foto_d = ch.foto(kv)
            propia, s_propia = camino(lg0, True)
            ch.volver(kv, foto_d, Lp + len(u) + len(forzado))
            s_der = puntaje_derivar(lg0)
            return rasgos, s_der - s_propia, propia
        finally:
            ch.volver(kv, foto, Lp)

    banco = [json.loads(l) for l in (RAIZ / "datos/orientacion.jsonl").read_text().splitlines() if l.strip()]
    examen = [json.loads(l) for l in (RAIZ / "datos/entrenamiento/examen.jsonl").read_text().splitlines()
              if l.strip()]
    prohibidas = {norm(x["frase"]) for x in banco + examen}
    rnd = random.Random(1)
    v4 = [json.loads(l) for l in (Path(args.corpus_v4).expanduser() / "entrenar.jsonl").read_text().splitlines()]
    propias = sorted({f["frase"] for f in v4 if f["tipo"] == "herramienta" and norm(f["frase"]) not in prohibidas})
    propias = rnd.sample(propias, min(args.n_propias, len(propias)))
    lab = []
    for parte in ("entrenar.jsonl", "validar.jsonl"):
        lab += [json.loads(l) for l in (Path(args.corpus_lab).expanduser() / parte).read_text().splitlines()]
    import ste_es
    ajenas = sorted({f["frase"] for f in lab if f["tipo"] == "derivar" and norm(f["frase"]) not in prohibidas
                     and not ste_es.errores_derivar(f["frase"])})

    def rasgos_de(frases, etiqueta):
        R = []
        for i, f in enumerate(frases):
            R.append(mirar(f))
            if (i + 1) % 200 == 0:
                print(f"  {etiqueta}: {i + 1}/{len(frases)}", flush=True)
        return R

    print(f"sonda: {len(propias)} propias, {len(ajenas)} ajenas · capas {capas}", flush=True)
    Rp, Ra = rasgos_de(propias, "propias"), rasgos_de(ajenas, "ajenas")
    conocidas = set(nombres)
    for x in banco + examen:
        x["aceptables"] = x.get("aceptables") or ([x["herramienta"]] if x["herramienta"] in conocidas else [tc.DERIVAR])
    Rb, Re = rasgos_de([x["frase"] for x in banco], "banco"), rasgos_de([x["frase"] for x in examen], "examen")
    torch.save({"capas": capas, "propias": Rp, "ajenas": Ra, "banco": Rb, "examen": Re}, out / "rasgos.pt")

    # LA SONDA por capa: regresion logistica (L2), elegida por AUC en validacion aparte
    X = torch.stack([r[0] for r in Rp + Ra])          # (N, capas, d)
    y = torch.tensor([0.0] * len(Rp) + [1.0] * len(Ra))
    idx = torch.randperm(len(y), generator=torch.Generator().manual_seed(0))
    n_val = len(y) // 5
    iv, it = idx[:n_val], idx[n_val:]

    def auc(p, y):
        orden = torch.argsort(p)
        rang = torch.empty_like(orden, dtype=torch.float)
        rang[orden] = torch.arange(1, len(p) + 1, dtype=torch.float)
        n1 = y.sum()
        n0 = len(y) - n1
        return float((rang[y == 1].sum() - n1 * (n1 + 1) / 2) / (n1 * n0))

    def entrenar(Xc, yc, l2=1e-2, pasos=400):
        mu, sd = Xc.mean(0), Xc.std(0) + 1e-5
        Z = ((Xc - mu) / sd).cuda()
        w = torch.zeros(Z.shape[1], device="cuda", requires_grad=True)
        b = torch.zeros(1, device="cuda", requires_grad=True)
        opt = torch.optim.LBFGS([w, b], max_iter=pasos, line_search_fn="strong_wolfe")
        yy = yc.cuda()

        def cierre():
            opt.zero_grad()
            perdida = torch.nn.functional.binary_cross_entropy_with_logits(Z @ w + b, yy) + l2 * (w ** 2).sum()
            perdida.backward()
            return perdida
        opt.step(cierre)
        return lambda Xn: torch.sigmoid((((Xn - mu) / sd).cuda() @ w + b)).detach().cpu()

    mejor = None
    for j, c in enumerate(capas):
        for l2 in (1e-3, 1e-2, 1e-1):
            f = entrenar(X[it, j], y[it], l2)
            a = auc(f(X[iv, j]), y[iv])
            print(f"  capa {c:2d} l2 {l2:g}: AUC validacion {a:.4f}", flush=True)
            if mejor is None or a > mejor[0]:
                mejor = (a, j, l2)
    a_val, j, l2 = mejor
    sonda = entrenar(X[:, j], y, l2)  # con todo
    # el propio modelo como clasificador (su margen a favor de derivar), misma validacion
    marg = torch.tensor([r[1] for r in Rp + Ra])
    print(f"mejor: capa {capas[j]} (l2 {l2:g}) · AUC sonda {a_val:.4f} · AUC margen del modelo {auc(marg[iv], y[iv]):.4f}")

    def medir(R, casos, puntaje, umbral):
        suyos = [i for i, x in enumerate(casos) if tc.DERIVAR not in x["aceptables"]]
        ajenos = [i for i, x in enumerate(casos) if tc.DERIVAR in x["aceptables"]]
        deriva = [float(puntaje[i]) > umbral for i in range(len(casos))]
        elegida = [tc.DERIVAR if deriva[i] else R[i][2] for i in range(len(casos))]
        atend = [i for i in suyos if not deriva[i]]
        bien = [i for i in atend if elegida[i] in casos[i]["aceptables"]]
        return {"cobertura": len(atend) / len(suyos), "precision": len(bien) / max(1, len(atend)),
                "derivado": sum(deriva[i] for i in ajenos) / max(1, len(ajenos)),
                "acierto": sum(elegida[i] in casos[i]["aceptables"] for i in range(len(casos))) / len(casos)}

    p_b = sonda(torch.stack([r[0][j] for r in Rb]))
    p_e = sonda(torch.stack([r[0][j] for r in Re]))
    m_b = torch.tensor([r[1] for r in Rb])
    m_e = torch.tensor([r[1] for r in Re])
    filas = []
    for nombre, pb, pe, umbrales in (("sonda", p_b, p_e, [x / 20 for x in range(2, 19)]),
                                     ("modelo", m_b, m_e, [x / 2 for x in range(-12, 9)])):
        for u in umbrales:
            filas.append({"quien": nombre, "umbral": u, "banco": medir(Rb, banco, pb, u),
                          "examen": medir(Re, examen, pe, u)})
    (out / "curvas.json").write_text(json.dumps({"capa": capas[j], "l2": l2, "auc_sonda": a_val,
                                                 "auc_modelo": auc(marg[iv], y[iv]), "filas": filas}, indent=1))
    print("quien  umbral | banco cob  prec  deriv acierto | examen cob  prec  acierto")
    for f in filas:
        b, e = f["banco"], f["examen"]
        print(f"{f['quien']:6s} {f['umbral']:6.2f} |      {b['cobertura']:.3f} {b['precision']:.3f} {b['derivado']:.3f}"
              f" {b['acierto']:.3f} |       {e['cobertura']:.3f} {e['precision']:.3f} {e['acierto']:.3f}")


if __name__ == "__main__":
    main()
