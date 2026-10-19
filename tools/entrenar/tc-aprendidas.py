#!/usr/bin/env python3
"""Lo que el modelo erro en un banco intermedio, convertido en corpus (tc-ciclo.py).

Por cada error: la frase TAL CUAL (el «eje», con los nombres como huecos) y
variaciones del mismo pedido escritas por un agente (consignas/tc-errores.md).

EL EJE EN EL CENTRO. La frase que fallo es la que importa: si las variaciones
quedan todas hacia un lado, el entrenamiento ensancha la nube hacia alli y la
frase original queda en el borde. Por eso se piden de mas (--variaciones) y se
ELIGEN las que dejan al eje en el centro: con el mismo instrumento que las nubes
(tc-geometria: el estado oculto donde el modelo escribe el nombre), se van
sumando de a una las que acercan el promedio del grupo al eje, hasta --quedan.
El «descentrado» es |promedio - eje| / radio del grupo; se acepta hasta --centrado.

Y ninguna variacion puede quedar MAS cerca de lo que el modelo eligio mal que el
propio eje: corregir la confusion, no empujarla.

EL CDNV DEL CORPUS ACUMULADO (antes de entrenar el paso siguiente). La geometria
parte de las anotaciones (corpus 0) y lo aprendido en los bancos anteriores; cada
eje entra tal cual y sus variaciones pasan por los tres filtros de tc-geometria
(no caer en otro grupo salvo que sea la confusion que el modelo tuvo, sumar
variedad, no acercar a una vecina). Queda datos/bancos-ciclo/cdnv-N.json: las
nubes antes y despues, y las frases del corpus anterior que quedaron empujadas.

  BA_PERFIL=vivo6 tools/entrenar/tc-aprendidas.py --numero 1 \\
      --medidas ~/entrenamientos/tc-ciclo/paso-1/banco-1.json
  -> datos/tc-aprendidas.json («banco-1»: {herramienta: [anotaciones]})
"""
import argparse
import collections
import importlib.util
import json
import os
import re
import sys
import tempfile
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
E = Path(__file__).resolve().parent
sys.path.insert(0, str(E))
import agente  # noqa: E402
import lib_tc as tc  # noqa: E402

APR = RAIZ / "datos" / "tc-aprendidas.json"


def _modulo(nombre, archivo):
    spec = importlib.util.spec_from_file_location(nombre, E / archivo)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--numero", type=int, required=True, help="el banco intermedio que se midio")
    ap.add_argument("--medidas", required=True, help="el json de tc-evaluar con ese banco (sus fallos)")
    ap.add_argument("--instrumento", default="Qwen/Qwen3.5-4B", help="el modelo con el que se miden las nubes")
    ap.add_argument("--variaciones", type=int, default=12, help="las que escribe el agente por error")
    ap.add_argument("--quedan", type=int, default=6, help="las que se eligen alrededor del eje")
    ap.add_argument("--centrado", type=float, default=0.35, help="descentrado maximo aceptado")
    ap.add_argument("--parecido", type=float, default=0.6)
    ap.add_argument("--paralelo", type=int, default=4)
    ap.add_argument("--dispositivo", default="cuda:0")
    args = ap.parse_args()
    if not os.environ.get("BA_PERFIL"):  # sin el, todo sale con el perfil reactivo y nada falla
        sys.exit("falta BA_PERFIL (el perfil del modelo, p. ej. BA_PERFIL=vivo6)")
    tca = _modulo("tca", "tc-anotar.py")
    geo = _modulo("tc_geometria", "tc-geometria.py")
    tbe = _modulo("tbe", "tc-banco-escribir.py")
    fichas, esq = tc.fichas(), tc.esquemas()
    fallos = json.loads(Path(args.medidas).expanduser().read_text())["fallos"]["banco"]
    casos = []
    for i, f in enumerate(fallos):
        correcta = tc.DERIVAR if tc.DERIVAR in f["esperada"] else f["esperada"][0]
        if correcta != tc.DERIVAR and correcta not in esq:
            continue
        el = f["elegida"]
        casos.append({"id": str(i), "frase": f["frase"], "correcta": correcta, "eligio": el,
                      "lote": {"id": str(i), "frase": f["frase"], "variaciones": args.variaciones,
                               "correcta": ({"nombre": correcta, "descripcion": fichas[correcta]["description"],
                                             "esquema": esq[correcta]} if correcta != tc.DERIVAR else "derivar"),
                               "eligio": ({"nombre": el, "descripcion": fichas[el]["description"]} if el in fichas
                                          else {"nombre": el, "descripcion": "pasarlo a otro asistente" if el == tc.DERIVAR
                                                else "preguntar un dato que faltaba"})}})
    print(f"banco {args.numero}: {len(casos)} errores", flush=True)

    # 1. el agente: eje + variaciones
    tmp = Path(tempfile.mkdtemp(prefix=f"aprendidas-{args.numero}-"))
    trabajos = []
    for k in range(0, len(casos), 6):
        p = tmp / f"l{k // 6:02d}.json"
        p.write_text(json.dumps({"casos": [c["lote"] for c in casos[k:k + 6]]}, ensure_ascii=False, indent=1))
        trabajos.append(agente.Trabajo("tc-errores.md", p, p.with_suffix(".jsonl"), ["id", "tipo", "plantilla"]))
    res = agente.correr_lotes(trabajos, args.paralelo)
    por_caso = collections.defaultdict(list)
    for r in agente.leer([t.salida for t in trabajos]):
        por_caso[str(r["id"])].append(r)

    # 2. validar (esquema; STE-ES no: son frases de operador) y que el eje sea la frase
    motivos = collections.Counter()

    def valida(a, h):
        if h == tc.DERIVAR:
            return not a.get("args")  # derivar no lleva argumentos (los huecos de la frase se llenan igual)
        return not [e for e in tca.validar(a, esq[h], h) if not e.startswith("STE-ES")]

    grupos = []
    for c in casos:
        h = c["correcta"]
        filas = por_caso.get(c["id"], [])
        ejes = [r for r in filas if r.get("tipo") == "eje"]
        if not ejes:
            motivos["sin eje"] += 1
            continue
        eje = {"plantilla": ejes[0]["plantilla"], "args": ejes[0].get("args") or {}}
        # el eje es la frase: sus palabras (sin los huecos) salen de la original
        t_eje, t_fr = tbe.toks(tc.RE_HUECO.sub(" ", eje["plantilla"])), tbe.toks(c["frase"])
        if not t_eje or len(t_eje & t_fr) / len(t_eje) < 0.8:
            motivos["el eje no es la frase tal cual"] += 1
            continue
        if not valida(eje, h):
            motivos["el eje no valida"] += 1
            continue
        vs = []
        for r in filas:
            if r.get("tipo") == "variacion":
                a = {"plantilla": r["plantilla"], "args": r.get("args") or {}}
                if valida(a, h):
                    vs.append(a)
                else:
                    motivos["variacion que no valida"] += 1
        grupos.append({**c, "eje": eje, "vars": vs})

    # 3. parecidas al banco final, al examen y a los intermedios QUE VIENEN (son
    # mediciones futuras: lo que se aprende de este no puede filtrarse a esos)
    par = tbe.Parecidas(args.parecido)
    for f in [RAIZ / "datos/orientacion.jsonl", RAIZ / "datos/entrenamiento/examen.jsonl"] + \
            [x for x in (RAIZ / "datos/bancos-ciclo").glob("banco-*.jsonl")
             if int(re.search(r"(\d+)", x.name).group(1)) > args.numero]:
        for l in f.read_text().splitlines():
            if l.strip():
                par.sumar(json.loads(l)["frase"])
    for g in grupos:
        n0 = len(g["vars"])
        g["vars"] = [a for a in g["vars"] if not par.parecida(tc.RE_HUECO.sub(" ", a["plantilla"]))]
        motivos["variacion parecida al banco final, examen o un intermedio futuro"] += n0 - len(g["vars"])
        g["eje_parecido"] = par.parecida(tc.RE_HUECO.sub(" ", g["eje"]["plantilla"]))

    # 4. EL EJE EN EL CENTRO y EL CDNV DEL CORPUS ACUMULADO, con el instrumento de las nubes.
    # La geometria parte de TODO lo que va a entrenar el paso siguiente: las anotaciones
    # (corpus 0, congelado) y lo aprendido en los bancos anteriores. Cada eje entra tal
    # cual (es la frase del operador); sus variaciones pasan por los tres filtros de
    # tc-geometria (filtrar): que no caigan en otro grupo salvo que sea justo la confusion
    # que el modelo tuvo, que sumen variedad y que no acerquen a una vecina.
    med = geo.Medidor(args.instrumento, dispositivo=args.dispositivo)
    torch = med.t
    ext = {h: list(v) for h, v in json.loads((RAIZ / "datos/tc-anotaciones.json").read_text()).items()}
    previas = json.loads(APR.read_text()) if APR.exists() else {}
    for banco, por_h in previas.items():
        if int(banco.split("-")[-1]) < args.numero:
            for h, lista in por_h.items():
                ext.setdefault(h, []).extend(lista)
    g0, etiq0 = geo.nubes(med, anot=ext)
    d0 = geo.diagnostico(g0, etiq0)
    r0 = geo.resumen(d0)
    print("CDNV antes (corpus 0 + aprendidas anteriores):", json.dumps(r0, ensure_ascii=False), flush=True)
    cen = {h: g0.P[h].mean(0) for h in g0.hs}
    evidencia = collections.Counter((g["correcta"], g["eligio"]) for g in grupos)

    def vec(h, a):
        u, _ = geo.usuario_de(h, a)
        return None if u is None else med.h(u)
    salida = collections.defaultdict(list)
    descentrados = []
    for g in grupos:
        h, el = g["correcta"], g["eligio"]
        v0 = vec(h, g["eje"])
        if v0 is None:
            motivos["el eje no se puede llenar"] += 1
            continue
        cand = [(a, vec(h, a)) for a in g["vars"]]
        cand = [(a, v) for a, v in cand if v is not None]

        def margen(v):  # cuanto mas cerca de la correcta que de la elegida (mas = mejor)
            if el not in cen or h not in cen:
                return 0.0
            return float((v - cen[el]).norm() - (v - cen[h]).norm())
        m0 = margen(v0)
        r = [float((v - v0).norm()) for _, v in cand]
        if r:
            med_r = sorted(r)[len(r) // 2]
            cand = [(a, v) for (a, v), ri in zip(cand, r) if ri <= 2 * med_r and margen(v) >= m0 - 0.1 * med_r]
        motivos["variacion lejos del eje o hacia la confusion"] += len(g["vars"]) - len(cand)
        elegidas, resto = [], list(cand)
        while resto and len(elegidas) < args.quedan:
            mejor = min(resto, key=lambda x: float((torch.stack([v for _, v in elegidas] + [x[1]]).mean(0) - v0).norm()))
            elegidas.append(mejor)
            resto.remove(mejor)
        # el descentrado del grupo final; si no alcanza, se achica hasta 3
        while len(elegidas) >= 3:
            V = torch.stack([v for _, v in elegidas])
            radio = float(((V - v0) ** 2).sum(1).mean().sqrt())
            d = float((V.mean(0) - v0).norm()) / max(radio, 1e-6)
            if d <= args.centrado:
                break
            elegidas.pop()
        if len(elegidas) < 3:
            elegidas, d = [], None
            motivos["sin variaciones centradas (queda solo el eje)"] += 1
        descentrados.append(d)
        base = {"origen": f"banco-{args.numero}", "frase_banco": g["frase"], "eligio": el}
        if g["eje_parecido"]:
            motivos["eje parecido al banco final o examen (no entra)"] += 1
            elegidas = []   # sin eje no hay centro: tampoco entran sus variaciones
        else:
            salida[h].append({**g["eje"], **base, "eje": True, "descentrado": None if d is None else round(d, 3)})
            if h in g0.P:
                g0.agregar(h, v0)   # el eje, tal cual: las variaciones se miden contra el
        if elegidas:
            aprob, rech = geo.filtrar(g0, med, [(h, a) for a, _ in elegidas], evidencia)
            for _, _, m in rech:
                motivos["filtro CDNV: " + ("cae en otro grupo" if m.startswith("cae en") else
                                           "acerca a una vecina" if m.startswith("acerca") else m.split(" (")[0])] += 1
            for _, a in aprob:
                salida[h].append({**a, **base})
    med.guardar()

    # el corpus acumulado DESPUES: cuanto se movieron las nubes (y si alguna del corpus 0 quedo fuera de su grupo)
    ext2 = {h: list(v) for h, v in ext.items()}
    for h, lista in salida.items():
        ext2.setdefault(h, []).extend(lista)
    g1, etiq1 = geo.nubes(med, anot=ext2)
    d1 = geo.diagnostico(g1, etiq1)
    r1 = geo.resumen(d1)
    nuevas = {(h, a["plantilla"]) for h, l in salida.items() for a in l}
    f0 = {(x["herramienta"], x["plantilla"]) for x in d0["fuera_de_su_grupo"]}
    f1 = [x for x in d1["fuera_de_su_grupo"]]
    empujadas = [x for x in f1 if (x["herramienta"], x["plantilla"]) not in f0
                 and (x["herramienta"], x["plantilla"]) not in nuevas]
    propias_fuera = [x for x in f1 if (x["herramienta"], x["plantilla"]) in nuevas]
    print("CDNV despues (con lo aprendido):", json.dumps(r1, ensure_ascii=False))
    print(f"del corpus anterior empujadas a otro grupo: {len(empujadas)} · aprendidas fuera de su grupo: {len(propias_fuera)}")
    informe = {"banco": args.numero, "antes": r0, "despues": r1,
               "empujadas": [{"herramienta": x["herramienta"], "plantilla": x["plantilla"], "cae_en": x["cae_en"]} for x in empujadas],
               "aprendidas_fuera_de_su_grupo": [{"herramienta": x["herramienta"], "plantilla": x["plantilla"], "cae_en": x["cae_en"]}
                                                for x in propias_fuera]}
    (RAIZ / "datos/bancos-ciclo" / f"cdnv-{args.numero}.json").write_text(json.dumps(informe, ensure_ascii=False, indent=1))
    if len(empujadas) > 5:
        print("OJO: lo aprendido empuja a mas de 5 frases del corpus anterior a otro grupo: revisar datos/bancos-ciclo/"
              f"cdnv-{args.numero}.json antes de entrenar")

    apr = json.loads(APR.read_text()) if APR.exists() else {}
    apr[f"banco-{args.numero}"] = dict(sorted(salida.items()))
    APR.write_text(json.dumps(apr, ensure_ascii=False, indent=1))
    ds = [d for d in descentrados if d is not None]
    resumen = {"banco": args.numero, "errores": len(casos), "cdnv_antes": r0, "cdnv_despues": r1,
               "empujadas": len(empujadas), "con_eje": sum(1 for x in salida.values() for a in x if a.get("eje")),
               "variaciones": sum(1 for x in salida.values() for a in x if not a.get("eje")),
               "descentrado_mediana": round(sorted(ds)[len(ds) // 2], 3) if ds else None,
               "descartes": dict(motivos), "costo_usd": round(sum(r.get("costo", 0) for r in res), 2)}
    (RAIZ / "datos/bancos-ciclo" / f"aprendidas-{args.numero}.resumen.json").write_text(
        json.dumps(resumen, ensure_ascii=False, indent=1))
    print(json.dumps(resumen, ensure_ascii=False))


if __name__ == "__main__":
    main()
