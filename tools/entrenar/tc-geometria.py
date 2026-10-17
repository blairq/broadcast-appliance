#!/usr/bin/env python3
"""La geometria del corpus (CDNV) en el formato de tool calling.

Lo mismo que bucle-corpus.py (docs/modelos/bucle-corpus.md) hacia con el formato
de nombres, para las anotaciones de tc: cada frase es un vector adentro del
modelo, cada herramienta una nube, y la cuenta decide.

DONDE SE MIDE. En el formato tc el nombre se decide despues de «<function=»: el
tokenizador de Qwen3.5 parte «<function=aire_ftb>» en «<», «function», «=»,
«aire»... y el estado oculto en «=» es el que elige el primer token del nombre.
Pero en unos 35 nombres el tokenizador pega «=» con la primera letra
(«=f»oto_invitado, «=s»peaker_next, «=l»aminas_ir, «=f»alta_dato): para esos
la eleccion empieza en «function». Se toman LOS DOS estados, concatenados: cada
frase en la misma posicion, y cada nombre con el estado donde se decide. FINALES
(despues de la norma): exactamente lo que multiplica la cabeza de salida, que es
donde vale la cota de la CDNV. El usuario va como en
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
import os
import random
import re
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
E = Path(__file__).resolve().parent
sys.path.insert(0, str(E))
import lib_tc as tc  # noqa: E402

# BA_TC_ANOTACIONES: otro archivo (tc-parejo mide las de la vuelta, antes de escribirlas)
ANOT = Path(os.environ.get("BA_TC_ANOTACIONES") or RAIZ / "datos" / "tc-anotaciones.json")
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
        self.firma_m = firma_m
        self.dev = self.m.device
        ch.parchear()
        self.cab = tc.cabeza()
        ids = self.tok(self.cab, add_special_tokens=False, return_tensors="pt").input_ids.to(self.dev)
        with torch.no_grad():
            self.kv = self.m.model(input_ids=ids, use_cache=True).past_key_values
        self.Lp = ids.shape[1]
        self.foto = ch.foto(self.kv)
        self.forzado = self.tok(tc.COLA + "<tool_call>\n<function=", add_special_tokens=False).input_ids
        firma = hashlib.sha1((firma_m + self.cab + "|function+=").encode()).hexdigest()[:12]
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
                                 past_key_values=self.kv, use_cache=True).last_hidden_state[0, -2:]
        finally:
            self.ch.volver(self.kv, self.foto, self.Lp)
        v = v.float().cpu().reshape(-1)  # «function» y «=», uno detras del otro
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


_FIJO = None


def estado_fijo():
    """UN estado para medir todas las frases: con cada clase de entidad (dos de cada
    una) para que cualquier plantilla se pueda llenar. Antes cada anotacion iba con
    un estado al azar —cientos de tokens distintos— y parte de la «variedad» de una
    nube era el estado y no la frase."""
    global _FIJO
    if _FIJO is None:
        necesita = [t for t in list(tc.TIPOS) + ["posicion"] for _ in range(2)]
        est, formas = tc.estado_sintetico(random.Random(1234), necesita)
        if not est["escena_armada"]:
            est["escena_armada"] = next(e for e in est["escenas"] if e != est["escena_actual"])
        _FIJO = (est, formas)
    return _FIJO


def usuario_de(h, a):
    """La anotacion con el estado fijo y sus huecos llenos con una eleccion fija por
    anotacion (mismo texto en cada corrida: el cache sirve y las rondas se comparan).
    Devuelve (usuario, destino): destino es el nombre REAL que el modelo tiene que
    escribir (la herramienta o derivar). Las de falta no tienen destino de nube."""
    if "falta" in a or not a.get("plantilla"):
        return None, None
    est, formas = estado_fijo()
    rnd = random.Random(int(hashlib.sha1(f"{h}|{a['plantilla']}".encode()).hexdigest()[:8], 16))
    frase, _ = tc.rellenar(a["plantilla"], a.get("args", {}), formas, rnd)
    if frase is None:
        return None, None
    return tc.usuario(est, frase), h


def nubes(med, anot=None, herramientas=None):
    """Las nubes de las anotaciones del perfil. Devuelve (Geometria, etiquetas):
    etiquetas[h][i] = {herramienta, plantilla, agente} de la fila i de la nube h."""
    torch = med.t
    anot = json.loads(ANOT.read_text()) if anot is None else anot
    mias = set(tc.esquemas()) if herramientas is None else set(herramientas)
    grupos, etiq, usuarios = collections.defaultdict(list), collections.defaultdict(list), collections.defaultdict(list)
    for h, lista in sorted(anot.items()):
        if h not in mias:
            continue
        for a in lista:
            if not a.get("plantilla"):
                continue
            u, destino = usuario_de(h, a)  # sin las de falta: pedir el dato depende del estado
            if u is None or destino is None:
                continue
            grupos[destino].append(med.h(u))
            etiq[destino].append({"herramienta": h, "plantilla": a["plantilla"], "agente": bool(a.get("nueva"))})
            usuarios[destino].append(u)
    med.guardar()
    geo = Geometria(torch, dict(grupos))
    geo.medibles = {h for h, g in grupos.items() if len(g) >= 3}
    geo.usuarios = dict(usuarios)
    return geo, dict(etiq)


# --- LA FOTO DE LAS NUBES ---------------------------------------------------------
# Las nubes de las herramientas CONGELADAS (datos/tc-congeladas.json) no cambian si
# no cambian sus frases: se guardan una vez (--foto-guardar) y lo nuevo se mide
# contra ellas (--contra), calculando solo sus frases. La aproximacion: al sumar
# fichas al prompt, el estado oculto de las frases viejas se corre un poco; el
# control de deriva lo mide en cada --contra (5 frases por herramienta) y avisa
# si la foto ya no vale. Lo que mostro al probarlo (17/10/2026, dos fichas nuevas de
# camaras): la deriva NO es un corrimiento comun que se pueda restar; se mueven las
# vecinas de lo nuevo (recinto_apuntar 0,84 de la distancia a su vecina, las ptz
# 0,5-0,7). Por eso --contra recalcula las que se corrieron mas de CORRIDA y usa la
# foto solo para las quietas.
NUBES = Path.home() / "entrenamientos" / "nubes"
CORRIDA = 0.15
RESUMEN_FOTO = RAIZ / "datos" / "tc-nubes-foto.json"


def sacar_foto(med, nombre, objetivo):
    import datetime
    torch = med.t
    geo, etiq = nubes(med)
    d = diagnostico(geo, etiq, objetivo)
    NUBES.mkdir(parents=True, exist_ok=True)
    archivo = NUBES / f"{nombre}.pt"
    torch.save({"modelo": med.firma_m, "prompt": tc.huella(), "fecha": datetime.date.today().isoformat(),
                "grupos": {h: {"vectores": geo.P[h], "usuarios": geo.usuarios[h], "etiq": etiq[h]} for h in geo.hs}},
               archivo)
    RESUMEN_FOTO.write_text(json.dumps({
        "foto": nombre, "archivo": str(archivo), "modelo": med.firma_m, "prompt": tc.huella(),
        "fecha": datetime.date.today().isoformat(), "resumen": resumen(d),
        "herramientas": d["por_herramienta"]}, ensure_ascii=False, indent=1) + "\n")
    return archivo, d


def cargar_foto(nombre):
    archivo = Path(nombre).expanduser()
    if not archivo.exists():
        archivo = NUBES / f"{nombre}.pt"
    import torch
    return torch.load(archivo)


def nuevas_de(foto, pedidas):
    """Las pedidas, o (por defecto) las del perfil de hoy que no estan en la foto."""
    return set(pedidas) if pedidas else (set(tc.esquemas()) - set(foto["grupos"]) - {tc.FALTA})


def nubes_con_foto(med, foto, nuevas, recalcular=()):
    """Las nubes de la foto y, recalculadas con el prompt de hoy, las de `nuevas` y
    las de `recalcular` (las que el control de deriva vio corridas). Las de la foto
    que ya no estan en el perfil se dejan afuera."""
    torch = med.t
    perfil = set(tc.esquemas()) | {tc.DERIVAR}
    nuevas = set(nuevas) | set(recalcular)
    geo_n, etiq_n = nubes(med, herramientas=nuevas) if nuevas else (None, {})
    grupos, etiq, usuarios = {}, {}, {}
    for h, g in foto["grupos"].items():
        if h in perfil and h not in nuevas:
            grupos[h] = list(g["vectores"])
            etiq[h] = g["etiq"]
            usuarios[h] = g["usuarios"]
    if geo_n is not None:
        for h in geo_n.hs:
            grupos[h] = list(geo_n.P[h])
            etiq[h] = etiq_n[h]
            usuarios[h] = geo_n.usuarios[h]
    geo = Geometria(torch, grupos)
    geo.medibles = {h for h, g in grupos.items() if len(g) >= 3}
    geo.usuarios = usuarios
    return geo, etiq


def deriva(med, foto, por_herramienta=5, nuevas=()):
    """Cuanto se corrio la foto con el prompt de hoy: se recalculan `por_herramienta`
    frases de cada nube congelada (siempre las mismas) y se comparan con las guardadas.
      misma_asignacion  fraccion que cae en el mismo centro (de la foto) que antes
      corrimiento       |centro nuevo - centro viejo| de esas frases, sobre la
                        distancia a la vecina mas cercana (0 = nada; 1 = llega a la vecina)"""
    torch = med.t
    hs = sorted(h for h in foto["grupos"] if h not in set(nuevas) and h in set(tc.esquemas()) | {tc.DERIVAR})
    C = torch.stack([foto["grupos"][h]["vectores"].mean(0) for h in hs])
    total = iguales = 0
    cosenos, corr = [], {}
    for k, h in enumerate(hs):
        g = foto["grupos"][h]
        idx = sorted(range(len(g["usuarios"])), key=lambda i: hashlib.sha1(g["usuarios"][i].encode()).hexdigest())
        idx = idx[:por_herramienta]
        viejos = g["vectores"][idx]
        nuevos = torch.stack([med.h(g["usuarios"][i]) for i in idx])
        cosenos += torch.nn.functional.cosine_similarity(viejos, nuevos, dim=1).tolist()
        a_v = torch.cdist(viejos, C).argmin(1)
        a_n = torch.cdist(nuevos, C).argmin(1)
        total += len(idx)
        iguales += int((a_v == a_n).sum())
        dist = (C - C[k]).norm(dim=1)
        dist[k] = float("inf")
        corr[h] = float((nuevos.mean(0) - viejos.mean(0)).norm() / dist.min())
    med.guardar()
    v = sorted(corr.values())
    p95 = v[int(0.95 * (len(v) - 1))] if v else 0.0
    return {"prompt_foto": foto["prompt"], "prompt_hoy": tc.huella(), "frases": total,
            "misma_asignacion": round(iguales / max(1, total), 4),
            "coseno_medio": round(sum(cosenos) / max(1, len(cosenos)), 5), "coseno_min": round(min(cosenos or [1]), 5),
            "corrimiento_mediana": round(v[len(v) // 2], 4) if v else 0.0, "corrimiento_p95": round(p95, 4),
            "peores": sorted(corr.items(), key=lambda x: -x[1])[:5], "por_herramienta": corr,
            "vale": iguales / max(1, total) >= 0.98 and p95 <= 0.10}


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
            evidencia_min=2, permitir_otra=False):
    """candidatas: [(herramienta, anotacion)]. evidencia: Counter((h, elegida)) de
    lo que el modelo real hizo mal. Devuelve (aprobadas, rechazadas[(h, a, motivo)]).
    Las aprobadas se suman a la geometria en el acto: la siguiente se mide contra
    el grupo ya cambiado."""
    torch = geo.t
    aprobadas, rechazadas = [], []
    for h, a in candidatas:
        if "falta" in a:  # pedir el dato depende del estado: no hay nube contra que medir
            aprobadas.append((h, a))
            continue
        u, destino = usuario_de(h, a)
        if u is None:
            rechazadas.append((h, a, "no se pudo rendir"))
            continue
        if u is not None and destino not in geo.P:  # herramienta sin nube todavia: nada contra que medir
            aprobadas.append((h, a))
            continue
        v = med.h(u)
        cerca = geo.mas_cercano(v)
        if cerca != destino:
            n_ev = evidencia.get((h, cerca), 0)
            if n_ev < evidencia_min and not permitir_otra:
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


def podar(geo, etiq, tope, topes=None):
    """Para cada herramienta con mas de `tope` frases: saca de a una la que menos
    suma (novedad contra las demas de su grupo: la copia segun el MODELO, no segun
    las palabras), primero las del agente, hasta quedar en `tope`. Devuelve
    {herramienta: [plantillas que salen]}."""
    t = geo.t
    sale = {}
    for h in list(geo.hs):
        P, et = geo.P[h], list(etiq[h])
        fuera = []
        while len(P) > (topes or {}).get(h, tope):
            nov = []
            for i in range(len(P)):
                resto = t.cat([P[:i], P[i + 1:]])
                geo.P[h] = resto
                geo._recalcular()
                nov.append((not et[i]["agente"], geo.novedad(h, P[i]), i))
            _, _, i = min(nov)
            fuera.append(et[i]["plantilla"])
            P = t.cat([P[:i], P[i + 1:]])
            et.pop(i)
        geo.P[h] = P
        geo._recalcular()
        etiq[h] = et
        if fuera:
            sale[h] = fuera
    return sale


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--modelo", required=True, help="adaptador LoRA o el repo de la base")
    ap.add_argument("--objetivo", type=float, default=0.028)
    ap.add_argument("--json", help="donde dejar el diagnostico")
    ap.add_argument("--filtrar", help="jsonl de candidatas {herramienta, plantilla, args|falta, ...}: los tres filtros")
    ap.add_argument("--evidencia", help="con --filtrar: json [[herramienta, elegida, veces], ...] de lo que el modelo hizo mal")
    ap.add_argument("--aprobadas", help="con --filtrar: jsonl de salida (las descartadas van a <...>.descartadas.jsonl)")
    ap.add_argument("--permitir-otra", action="store_true",
                    help="con --filtrar: no descartar lo que cae en otro grupo (medido con un modelo que no distingue el par)")
    ap.add_argument("--podar", type=int, help="dejar cada herramienta en N frases, sacando las que menos suman")
    ap.add_argument("--salida-poda", help="con --podar: json {herramienta: [plantillas que salen]}")
    ap.add_argument("--topes", help="con --podar: json {herramienta: N} (el tope de la nube: sin las de falta)")
    ap.add_argument("--foto-guardar", metavar="NOMBRE",
                    help="calcular todas las nubes y guardarlas en ~/entrenamientos/nubes/NOMBRE.pt (y el resumen en"
                         " datos/tc-nubes-foto.json)")
    ap.add_argument("--contra", metavar="FOTO", help="usar las nubes de la foto y calcular solo lo nuevo (con control de deriva)")
    ap.add_argument("--nuevas", help="con --contra: herramientas a recalcular (separadas por coma); por defecto, las del"
                                     " perfil que no estan en la foto")
    args = ap.parse_args()
    med = Medidor(args.modelo)
    if args.foto_guardar:
        archivo, d = sacar_foto(med, args.foto_guardar, args.objetivo)
        print(json.dumps(resumen(d), ensure_ascii=False))
        print(f"foto: {archivo} ({len(d['por_herramienta'])} nubes) · resumen en {RESUMEN_FOTO.relative_to(RAIZ)}")
        return
    nuevas = set()
    der = None
    if args.contra:
        foto = cargar_foto(args.contra)
        nuevas = nuevas_de(foto, [x for x in (args.nuevas or "").split(",") if x])
        der = deriva(med, foto, nuevas=nuevas)
        corridas = sorted(h for h, c in der["por_herramienta"].items() if c > CORRIDA)
        geo, etiq = nubes_con_foto(med, foto, nuevas, recalcular=corridas)
        der["recalculadas"] = corridas
        print(f"foto {args.contra} (prompt {der['prompt_foto']}, hoy {der['prompt_hoy']}) · nuevas: {sorted(nuevas)}")
        print(f"deriva: {der['frases']} frases de control · misma asignacion {der['misma_asignacion']:.1%} · "
              f"corrimiento mediana {der['corrimiento_mediana']:.3f} p95 {der['corrimiento_p95']:.3f} · "
              f"recalculadas {len(corridas)} de {len(der['por_herramienta'])} (corridas > {CORRIDA}): "
              + (", ".join(corridas[:10]) + ("..." if len(corridas) > 10 else "") if corridas else "ninguna")
              + ("" if len(corridas) < len(der["por_herramienta"]) / 2 else
                 " · se corrio mas de la mitad: conviene sacar otra foto con --foto-guardar"))
    else:
        geo, etiq = nubes(med)
    if args.filtrar:
        cands = []
        for l in Path(args.filtrar).read_text().splitlines():
            if l.strip():
                a = json.loads(l)
                cands.append((a.pop("herramienta"), a))
        ev = collections.Counter({(h, e): n for h, e, n in json.loads(Path(args.evidencia).read_text())}) \
            if args.evidencia else collections.Counter()
        aprob, rech = filtrar(geo, med, cands, ev, objetivo=args.objetivo, permitir_otra=args.permitir_otra)
        sal = Path(args.aprobadas)
        sal.write_text("".join(json.dumps({"herramienta": h, **a}, ensure_ascii=False) + "\n" for h, a in aprob))
        sal.with_suffix(".descartadas.jsonl").write_text(
            "".join(json.dumps({"herramienta": h, **a, "motivo": m}, ensure_ascii=False) + "\n" for h, a, m in rech))
        motivos = collections.Counter("cae en otra" if m.startswith("cae en") else "acerca vecinas"
                                      if m.startswith("acerca") else m.split(" (")[0] for _, _, m in rech)
        print(f"filtro geometrico: {len(aprob)} de {len(cands)} aprobadas · descartadas {dict(motivos)}")
        return
    if args.podar:
        sale = podar(geo, etiq, args.podar, json.loads(Path(args.topes).read_text()) if args.topes else None)
        Path(args.salida_poda).write_text(json.dumps(sale, ensure_ascii=False, indent=1))
        print(f"poda a {args.podar}: salen {sum(map(len, sale.values()))} de {len(sale)} herramientas")
        return
    d = diagnostico(geo, etiq, args.objetivo)
    if der is not None:
        d["deriva"] = der
        d["nuevas"] = sorted(nuevas)
    print(json.dumps(resumen(d), ensure_ascii=False))
    if nuevas:
        print("las nuevas:")
        for h in sorted(nuevas):
            ph = d["por_herramienta"].get(h)
            caen = [x["cae_en"] for x in d["fuera_de_su_grupo"] if x["destino"] == h]
            reciben = [x["destino"] for x in d["fuera_de_su_grupo"] if x["cae_en"] == h]
            print(f"  {h:26} n {ph['n'] if ph else '-':>3} · peor vecina {ph['peor']['con'] if ph else '-'} "
                  f"cdnv {ph['peor']['cdnv'] if ph else float('nan'):.3f} · caen en otra {len(caen)} "
                  f"{dict(collections.Counter(caen))} · reciben {len(reciben)}")
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
