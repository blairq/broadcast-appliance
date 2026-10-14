#!/usr/bin/env python3
"""La geometria del corpus (CDNV) en el formato de tool calling.

Lo mismo que bucle-corpus.py (docs/modelos/bucle-corpus.md) hacia con el formato
de nombres, para las anotaciones de tc: cada frase es un vector adentro del
modelo, cada herramienta una nube, y la cuenta decide.

DONDE SE MIDE. En el formato tc el nombre se decide despues de «<function=»: el
tokenizador de Qwen3.5 parte «<function=aire_ftb>» en «<», «function», «=»,
«aire»... y el estado oculto en «=» es el que elige el primer token del nombre.
Se toma ese estado, FINAL (despues de la norma): exactamente lo que multiplica
la cabeza de salida, que es donde vale la cota de la CDNV. El usuario va como en
el corpus (estado del aparato + «Pedido: ...»), con el prefijo del sistema
compartido y su cache recuperado entre frases (cache_hibrido: las capas
lineales de Qwen3.5 no se recortan como un cache KV).

LA CUENTA (de bucle-corpus.py, sin copiarla):

    CDNV_dir(i,j) = (var_i sobre u_ij + var_j sobre u_ij) / (2 . |mu_i - mu_j|^2)

acota el error del clasificador por centro mas cercano por 4 . CDNV_dir. Objetivo
por defecto 0,028 (Fisher 6, cota 11 %).

LOS TRES FILTROS DE UNA ANOTACION NUEVA (filtrar()):
  1. cae en su grupo      su centro mas cercano es el de su herramienta
  2. suma variedad util   fraccion de su desvio fuera de los ejes hacia las
                          vecinas y de las direcciones que el grupo ya cubre
  3. no acerca vecinas    ningun par puede quedar sobre el objetivo y subir mas
                          de la tolerancia
Con una excepcion, la de bucle-corpus: el filtro 1 es circular para los
ejemplos que CORRIGEN un error (solo deja entrar lo que el modelo ya ubica
bien). Si la frase cae en la vecina V y el modelo REAL mando la herramienta a V
al menos --evidencia-min veces (en el autoexamen), entra: es justo el contraste
que falta. Sin esa evidencia, no.

QUE MIDE CON QUE. Para filtrar lo nuevo, el modelo de la vuelta (nunca vio la
frase). Para diagnosticar el corpus, mejor un modelo que NO lo vio (el base, o
una version anterior): el que se entreno con esas frases las tiene apretadas
por memoria.

    tools/entrenar/tc-geometria.py --modelo Qwen/Qwen3.5-4B --json geo.json
    tools/entrenar/tc-geometria.py --modelo ~/entrenamientos/tc-v5/mejor --json geo.json
"""
import argparse
import collections
import hashlib
import importlib.util
import json
import random
import re
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
E = Path(__file__).resolve().parent
sys.path.insert(0, str(E))
import lib_tc as tc  # noqa: E402

ANOT = RAIZ / "datos" / "tc-anotaciones.json"
RE_FUNCION = re.compile(r"<function=([^>\n]+)>")


def _modulo(nombre, archivo):
    spec = importlib.util.spec_from_file_location(nombre, E / archivo)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


_bc = _modulo("bucle_corpus", "bucle-corpus.py")
Geometria = _bc.Geometria
diagnosticar = _bc.diagnosticar


class Medidor:
    """Carga el modelo una vez y da el vector de decision de cualquier usuario."""

    def __init__(self, modelo, cache_dir=Path.home() / ".cache/ba-nubes-tc", dispositivo="cuda:0"):
        import torch
        from transformers import AutoModelForCausalLM, AutoTokenizer
        import cache_hibrido as ch
        self.t, self.ch = torch, ch
        m_dir = Path(modelo).expanduser()
        if (m_dir / "adapter_config.json").exists():
            from peft import PeftModel
            base = json.loads((m_dir / "adapter_config.json").read_text())["base_model_name_or_path"]
            m = AutoModelForCausalLM.from_pretrained(base, dtype=torch.bfloat16, device_map={"": dispositivo})
            self.m = PeftModel.from_pretrained(m, str(m_dir)).merge_and_unload().eval()
            self.tok = AutoTokenizer.from_pretrained(str(m_dir))
            firma_m = str(m_dir.resolve())
        else:
            self.m = AutoModelForCausalLM.from_pretrained(modelo, dtype=torch.bfloat16,
                                                          device_map={"": dispositivo}).eval()
            self.tok = AutoTokenizer.from_pretrained(modelo)
            firma_m = modelo
        self.dev = self.m.device
        ch.parchear()
        self.cab = tc.cabeza()
        ids = self.tok(self.cab, add_special_tokens=False, return_tensors="pt").input_ids.to(self.dev)
        with torch.no_grad():
            self.kv = self.m.model(input_ids=ids, use_cache=True).past_key_values
        self.Lp = ids.shape[1]
        self.foto = ch.foto(self.kv)
        self.forzado = self.tok(tc.COLA + "<tool_call>\n<function=", add_special_tokens=False).input_ids
        firma = hashlib.sha1((firma_m + self.cab).encode()).hexdigest()[:12]
        self.cache_f = Path(cache_dir) / f"h-{firma}.pt"
        self.cache = torch.load(self.cache_f) if self.cache_f.exists() else {}
        self.sucio = False

    def h(self, usuario):
        """usuario: lo que va entre el sistema y la cola (tc.usuario(estado, frase))."""
        if usuario in self.cache:
            return self.cache[usuario]
        torch = self.t
        u = self.tok(usuario, add_special_tokens=False).input_ids
        try:
            with torch.no_grad():
                v = self.m.model(input_ids=torch.tensor([u + self.forzado], device=self.dev),
                                 past_key_values=self.kv, use_cache=True).last_hidden_state[0, -1]
        finally:
            self.ch.volver(self.kv, self.foto, self.Lp)
        v = v.float().cpu()
        self.cache[usuario] = v
        self.sucio = True
        return v

    def guardar(self):
        if self.sucio:
            self.cache_f.parent.mkdir(parents=True, exist_ok=True)
            self.t.save(self.cache, self.cache_f)
            self.sucio = False

    def cerrar(self):
        self.guardar()
        del self.m, self.kv
        self.t.cuda.empty_cache()


_tcc = None


def usuario_de(h, a, semilla=0):
    """La anotacion rendida UNA vez, con un estado fijo por anotacion (mismo texto
    en cada corrida: el cache sirve y las rondas se comparan). Devuelve
    (usuario, destino): destino es el nombre que el modelo tiene que escribir
    (la herramienta, falta_dato o derivar)."""
    global _tcc
    if _tcc is None:
        _tcc = _modulo("tcc_geo", "tc-corpus.py")
    rnd = random.Random(int(hashlib.sha1(f"{semilla}|{h}|{a['plantilla']}".encode()).hexdigest()[:8], 16))
    filas = _tcc.rendir(h, a, rnd, 1, variar=False)
    if not filas:
        return None, None
    f = filas[0]
    m = RE_FUNCION.search(f["completion"])
    return f["prompt"][len(tc.cabeza()):-len(tc.COLA)], (m.group(1) if m else None)


def nubes(med, anot=None, herramientas=None):
    """Las nubes de las anotaciones del perfil. Devuelve (Geometria, etiquetas):
    etiquetas[h][i] = {herramienta, plantilla, agente} de la fila i de la nube h."""
    torch = med.t
    anot = json.loads(ANOT.read_text()) if anot is None else anot
    mias = set(tc.esquemas()) if herramientas is None else set(herramientas)
    grupos, etiq = collections.defaultdict(list), collections.defaultdict(list)
    for h, lista in sorted(anot.items()):
        if h not in mias:
            continue
        for a in lista:
            if not a.get("plantilla"):
                continue
            if "falta" in a:
                continue  # pedir el dato depende del ESTADO, no de la frase: no es una nube
            u, destino = usuario_de(h, a)
            if u is None or destino is None:
                continue
            grupos[destino].append(med.h(u))
            etiq[destino].append({"herramienta": h, "plantilla": a["plantilla"], "agente": bool(a.get("nueva"))})
    med.guardar()
    geo = Geometria(torch, dict(grupos))
    geo.medibles = {h for h, g in grupos.items() if len(g) >= 3}
    return geo, dict(etiq)


def fuera_de_su_grupo(geo, etiq):
    """Las anotaciones cuyo vector cae mas cerca del centro de OTRA herramienta."""
    out = []
    for h in geo.hs:
        d = geo.t.cdist(geo.P[h], geo.C)
        for i, j in enumerate(d.argmin(1).tolist()):
            if geo.hs[j] != h:
                out.append({**etiq[h][i], "destino": h, "cae_en": geo.hs[j]})
    return out


def diagnostico(geo, etiq, objetivo=0.028):
    """El de bucle-corpus (pares, centros de choque, angostos, culpables) mas las
    anotaciones que caen en otro grupo. Sin «todos_los_pares» (miles de claves)."""
    frases = {h: [e["plantilla"] for e in v] for h, v in etiq.items()}
    d = diagnosticar(geo, objetivo, frases)
    d.pop("todos_los_pares", None)
    d["fuera_de_su_grupo"] = fuera_de_su_grupo(geo, etiq)
    d["por_herramienta"] = {h: {"n": len(geo.P[h]), "dim": round(geo.dim(h), 2),
                                "peor": dict(zip(("cdnv", "con"), geo.peor_cdnv(h)))}
                            for h in geo.hs if h in geo.medibles}
    return d


def resumen(d):
    return {"cdnv_mediana": round(d["cdnv_mediana"], 4), "cdnv_peor": round(d["cdnv_peor"], 4),
            "pares_sobre_objetivo": d["pares_sobre_objetivo"], "pares_medidos": d["pares_medidos"],
            "dim_mediana": round(d["dim_mediana"], 2), "fuera_de_su_grupo": len(d["fuera_de_su_grupo"])}


def filtrar(geo, med, candidatas, evidencia, objetivo=0.028, novedad_min=0.45, tolerancia=0.05,
            evidencia_min=2):
    """candidatas: [(herramienta, anotacion)]. evidencia: Counter((h, elegida)) de
    lo que el modelo real hizo mal. Devuelve (aprobadas, rechazadas[(h, a, motivo)]).
    Las aprobadas se suman a la geometria en el acto: la siguiente se mide contra
    el grupo ya cambiado."""
    torch = geo.t
    aprobadas, rechazadas = [], []
    for h, a in candidatas:
        u, destino = usuario_de(h, a)
        if u is None:
            rechazadas.append((h, a, "no se pudo rendir"))
            continue
        if destino not in geo.P:  # herramienta sin nube todavia: nada contra que medir
            aprobadas.append((h, a))
            continue
        v = med.h(u)
        cerca = geo.mas_cercano(v)
        if cerca != destino:
            n_ev = evidencia.get((h, cerca), 0)
            if n_ev < evidencia_min:
                rechazadas.append((h, a, f"cae en {cerca} (sin evidencia: {n_ev})"))
                continue
            # corrige un error visto: entra sin los filtros 2 y 3 (es el contraste)
            aprobadas.append((h, a))
            geo.agregar(destino, v)
            continue
        nov = geo.novedad(destino, v)
        if nov < novedad_min:
            rechazadas.append((h, a, f"no suma variedad ({nov:.2f})"))
            continue
        P2 = torch.cat([geo.P[destino], v[None, :]])
        malo = None
        for o in (geo.hs if destino in geo.medibles else []):
            if o == destino or o not in geo.medibles:
                continue
            c0, c1 = geo.cdnv(destino, o), geo.cdnv(destino, o, Pa=P2)
            if c1 > c0 * (1 + tolerancia) and c1 > objetivo:
                malo = (o, c0, c1)
                break
        if malo:
            rechazadas.append((h, a, f"acerca a {malo[0]} ({malo[1]:.3f}->{malo[2]:.3f})"))
            continue
        aprobadas.append((h, a))
        geo.agregar(destino, v)
        if len(geo.P[destino]) >= 3:
            geo.medibles.add(destino)
    med.guardar()
    return aprobadas, rechazadas


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--modelo", required=True, help="adaptador LoRA o el repo de la base")
    ap.add_argument("--objetivo", type=float, default=0.028)
    ap.add_argument("--json", help="donde dejar el diagnostico")
    ap.add_argument("--filtrar", help="jsonl de candidatas {herramienta, plantilla, args|falta, ...}: los tres filtros")
    ap.add_argument("--evidencia", help="con --filtrar: json [[herramienta, elegida, veces], ...] de lo que el modelo hizo mal")
    ap.add_argument("--aprobadas", help="con --filtrar: jsonl de salida (las descartadas van a <...>.descartadas.jsonl)")
    args = ap.parse_args()
    med = Medidor(args.modelo)
    geo, etiq = nubes(med)
    if args.filtrar:
        cands = []
        for l in Path(args.filtrar).read_text().splitlines():
            if l.strip():
                a = json.loads(l)
                cands.append((a.pop("herramienta"), a))
        ev = collections.Counter({(h, e): n for h, e, n in json.loads(Path(args.evidencia).read_text())}) \
            if args.evidencia else collections.Counter()
        aprob, rech = filtrar(geo, med, cands, ev, objetivo=args.objetivo)
        sal = Path(args.aprobadas)
        sal.write_text("".join(json.dumps({"herramienta": h, **a}, ensure_ascii=False) + "\n" for h, a in aprob))
        sal.with_suffix(".descartadas.jsonl").write_text(
            "".join(json.dumps({"herramienta": h, **a, "motivo": m}, ensure_ascii=False) + "\n" for h, a, m in rech))
        motivos = collections.Counter(m.split(" (")[0].split(" ")[0] if not m.startswith("cae en") else "cae en otra"
                                      for _, _, m in rech)
        print(f"filtro geometrico: {len(aprob)} de {len(cands)} aprobadas · descartadas {dict(motivos)}")
        return
    d = diagnostico(geo, etiq, args.objetivo)
    print(json.dumps(resumen(d), ensure_ascii=False))
    print("peores pares:")
    for p in d["peores_pares"][:12]:
        print(f"  {p['a']:26} {p['b']:26} cdnv {p['cdnv']:.3f} (cota {100 * p['cota_error']:.0f} %)")
    print("angostos:", ", ".join(f"{x['herramienta']} ({x['dim']:.1f})" for x in d["angostos"][:8]))
    por_dest = collections.Counter((x["destino"], x["cae_en"]) for x in d["fuera_de_su_grupo"])
    print("caen en otro grupo:", len(d["fuera_de_su_grupo"]), "·",
          ", ".join(f"{a}->{b} {n}" for (a, b), n in por_dest.most_common(10)))
    if args.json:
        Path(args.json).expanduser().write_text(json.dumps(d, ensure_ascii=False, indent=1))


if __name__ == "__main__":
    main()
