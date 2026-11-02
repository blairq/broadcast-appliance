#!/usr/bin/env python3
"""tools/carga/xctrace.py — qué hace cada hilo con nombre de un proceso en la Mac (xctrace)

Graba con Instruments por línea de comandos (xctrace, viene con Xcode) y lo resume por HILO
CON NOMBRE (los que pone ba::platform::nombrarHilo): nunca `sample`, que frena el proceso y
bajo carga lo deja sin latido (el launcher lo reinicia).

  - Time Profiler + Thread State Trace: por hilo, la CPU (lo que estuvo «Running»), cuántas
    veces entró a correr (≈ cambios de contexto), en qué núcleos (P o E) y las funciones
    propias que más muestras tienen; por núcleo, cuánto corrió ahí el proceso.
  - --contadores: la plantilla «CPU Counters» (los contadores del procesador). La de fábrica
    da ciclos y el análisis «CPU Bottlenecks» (useful / delivery / processing / discarded,
    la fracción de cada cosa por hilo). Instrucciones por ciclo y fallos de caché por evento
    piden una plantilla propia armada en Instruments (no se puede desde la línea de comandos).

Uso:
  xctrace.py grabar --pid PID --segundos 10 --salida CARPETA [--contadores]
  xctrace.py resumir CARPETA/xxx.trace [--pid PID] [--json salida.json]
medir-sala.py lo usa con --xctrace 10 (gateway y app, en régimen).
"""
import argparse, collections, json, os, re, subprocess, sys
import xml.etree.ElementTree as ET

XCTRACE = "/Applications/Xcode.app/Contents/Developer/usr/bin/xctrace"


def grabar(pid, segundos, salida, nombre=None, contadores=False):
    """Graba `segundos` del proceso `pid`. Devuelve la ruta del .trace (o None)."""
    os.makedirs(salida, exist_ok=True)
    base = nombre or f"pid{pid}"
    out = os.path.join(salida, f"{base}{'-contadores' if contadores else ''}.trace")
    if contadores:
        cmd = [XCTRACE, "record", "--template", "CPU Counters"]
    else:
        cmd = [XCTRACE, "record", "--template", "Time Profiler", "--instrument", "Thread State Trace"]
    cmd += ["--attach", str(pid), "--time-limit", f"{int(segundos)}s", "--output", out, "--no-prompt"]
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=segundos + 180)
    if r.returncode != 0 or not os.path.exists(out):
        sys.stderr.write(f"xctrace: {r.returncode} {r.stderr[-400:]}\n")
        return None
    return out


def exportar(trace, esquema):
    xml = os.path.join(os.path.dirname(trace), f"{os.path.basename(trace)}.{esquema}.xml")
    if not os.path.exists(xml):
        with open(xml, "w") as f:
            subprocess.run([XCTRACE, "export", "--input", trace, "--xpath",
                            f'/trace-toc/run[@number="1"]/data/table[@schema="{esquema}"]'], stdout=f, check=True, timeout=600)
    return xml


def filas(xml):
    """Las filas de una tabla exportada, con las referencias (ref=) resueltas: dict tag → elemento."""
    ids = {}

    def registrar(e):
        if e.get("id"):
            ids[e.get("id")] = e
        for c in e:
            registrar(c)

    def res(e):
        r = e.get("ref")
        return ids.get(r, e) if r else e

    padre = None
    for ev, e in ET.iterparse(xml, events=("start", "end")):
        if ev == "start":
            if e.tag == "node":
                padre = e
            continue
        if e.tag != "row":
            continue
        registrar(e)
        yield {c.tag: res(c) for c in e}, res
        # La fila se suelta (lo que otra fila nombre por ref= sigue vivo en `ids`): con cientos
        # de miles de filas, guardar el árbol entero no entra en memoria.
        if padre is not None:
            padre.remove(e)


HILO = re.compile(r"^(.*?)\s*\((0x[0-9a-f]+)\)\s*\((.*), pid: (\d+)\)$")


def hilo_de(el):
    """(nombre agrupado, tid, proceso, pid) de un elemento <thread>."""
    fmt = el.get("fmt", "") if el is not None else ""
    m = HILO.match(fmt)
    if not m:
        return ("?", "", "?", -1)
    nombre, tid, proc, pid = m.group(1).strip(), m.group(2), m.group(3), int(m.group(4))
    if nombre == proc:
        nombre = "(sin nombre)"
    return (re.sub(r"\d+", "#", nombre) or "(sin nombre)", tid, proc, pid)


def nucleo_de(el):
    if el is None or el.tag != "core":
        return None
    return el.get("fmt", el.text or "?").replace("CPU ", "")  # «3 (P Core)»


def resumir(trace, pid=None):
    r = {"trace": trace}
    # --- Thread State Trace: lo exacto (cuándo corrió cada hilo y dónde) --------------------
    estado = exportar(trace, "thread-state")
    hil = collections.defaultdict(lambda: {"cpu_ns": 0, "corridas": 0, "tids": set(), "nucleos": collections.Counter()})
    nuc = collections.Counter()
    t0, t1 = None, None
    proc = None
    for f, res in filas(estado):
        th = f.get("thread")
        if th is None:
            continue
        nombre, tid, pr, p = hilo_de(th)
        st = f.get("start-time")
        du = f.get("duration")
        if st is not None and st.text:
            ini = int(st.text)
            fin = ini + (int(du.text) if du is not None and du.text else 0)
            t0 = ini if t0 is None else min(t0, ini)
            t1 = fin if t1 is None else max(t1, fin)
        if pid is not None and p != pid:
            continue
        estado_txt = (f.get("thread-state").text if f.get("thread-state") is not None else "") or ""
        if estado_txt != "Running":
            continue
        proc = pr
        ns = int(du.text) if du is not None and du.text else 0
        h = hil[nombre]
        h["cpu_ns"] += ns
        h["corridas"] += 1
        h["tids"].add(tid)
        c = nucleo_de(f.get("core"))
        if c is not None:
            h["nucleos"][c] += ns
            nuc[c] += ns
    seg = (t1 - t0) / 1e9 if t0 is not None and t1 is not None and t1 > t0 else 0
    r["segundos"] = round(seg, 2)
    r["proceso"] = proc
    if seg > 0:
        r["hilos"] = sorted(({"nombre": n, "n": len(h["tids"]), "cpu_pct": round(100 * h["cpu_ns"] / 1e9 / seg, 1),
                              "cambios_s": round(h["corridas"] / seg),
                              "nucleos": {c: round(100 * v / 1e9 / seg, 1) for c, v in h["nucleos"].most_common(6)}}
                             for n, h in hil.items()), key=lambda x: -x["cpu_pct"])
        r["nucleos"] = {c: round(100 * v / 1e9 / seg, 1) for c, v in sorted(nuc.items(), key=lambda x: int(x[0].split()[0]))}
        r["cpu_pct"] = round(sum(h["cpu_ns"] for h in hil.values()) / 1e9 / seg * 100, 1)
        r["cambios_s"] = round(sum(h["corridas"] for h in hil.values()) / seg)
    # --- Time Profiler: dónde se va (las funciones propias que más muestras tienen) ------------
    try:
        perfil = exportar(trace, "time-profile")
        propia = collections.Counter()
        por_hilo = collections.defaultdict(collections.Counter)
        total = 0.0
        for f, res in filas(perfil):
            th = f.get("thread")
            nombre, _, _, p = hilo_de(th)
            if pid is not None and p != pid:
                continue
            w = f.get("weight")
            peso = int(w.text) / 1e6 if w is not None and w.text else 1.0
            bt = f.get("tagged-backtrace")
            if bt is None:
                bt = f.get("backtrace")
            hoja = "?"
            if bt is not None:
                if bt.tag == "tagged-backtrace":
                    b2 = bt.find("backtrace")
                    bt = res(b2) if b2 is not None else bt
                marcos = bt.findall("frame")
                if marcos:
                    m0 = res(marcos[0])
                    hoja = m0.get("name") or m0.get("addr") or "?"
            total += peso
            propia[hoja] += peso
            por_hilo[nombre][hoja] += peso
        if total:
            r["funciones"] = [{"funcion": k[:140], "pct": round(100 * v / total, 1)} for k, v in propia.most_common(20)]
            r["funciones_por_hilo"] = {n: [{"funcion": k[:100], "pct": round(100 * v / total, 1)} for k, v in c.most_common(3)]
                                       for n, c in sorted(por_hilo.items(), key=lambda x: -sum(x[1].values()))[:12]}
    except Exception as ex:  # una traza de contadores no tiene time-profile
        r["funciones_error"] = str(ex)
    return r


def resumir_contadores(trace, pid=None):
    """CPU Counters (la de fábrica): por hilo, el promedio de cada métrica y los ciclos."""
    xml = exportar(trace, "MetricAggregationForThread")
    acc = collections.defaultdict(lambda: collections.defaultdict(list))
    ciclos = collections.Counter()
    for f, res in filas(xml):
        nombre, _, _, p = hilo_de(f.get("thread"))
        if pid is not None and p != pid:
            continue
        met = (f.get("string").text if f.get("string") is not None else "") or "?"
        if met == "cycle":
            u = f.get("uint64")
            ciclos[nombre] += int(u.text) if u is not None and u.text else 0
        else:
            d = f.get("fixed-decimal")
            if d is not None and d.text:
                acc[nombre][met].append(float(d.text))
    out = []
    for n in sorted(set(acc) | set(ciclos), key=lambda x: -ciclos[x]):
        o = {"nombre": n, "ciclos": ciclos[n]}
        for met, v in acc[n].items():
            o[met] = round(sum(v) / len(v), 3)
        out.append(o)
    return {"hilos": out[:25], "nota": "plantilla CPU Counters de fábrica (CPU Bottlenecks): useful/delivery/processing/discarded "
                                       "por hilo; IPC y fallos de caché piden una plantilla propia de Instruments"}


def en_texto(r, top=10):
    l = [f"{r.get('proceso')}: {r.get('cpu_pct')} % de un núcleo, {r.get('cambios_s')} cambios de contexto/s ({r.get('segundos')} s)"]
    hs = r.get("hilos") or []
    l.append("  más CPU: " + "; ".join(f"{h['nombre']} x{h['n']} {h['cpu_pct']} %" for h in hs[:top]))
    l.append("  más cambios de contexto: " + "; ".join(f"{h['nombre']} {h['cambios_s']}/s" for h in sorted(hs, key=lambda x: -x["cambios_s"])[:top]))
    l.append("  por núcleo: " + ", ".join(f"{c} {v} %" for c, v in (r.get("nucleos") or {}).items()))
    return "\n".join(l)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    g = sub.add_parser("grabar")
    g.add_argument("--pid", type=int, required=True)
    g.add_argument("--segundos", type=int, default=10)
    g.add_argument("--salida", required=True)
    g.add_argument("--nombre")
    g.add_argument("--contadores", action="store_true")
    s = sub.add_parser("resumir")
    s.add_argument("trace")
    s.add_argument("--pid", type=int)
    s.add_argument("--json")
    s.add_argument("--contadores", action="store_true")
    a = ap.parse_args()
    if a.cmd == "grabar":
        t = grabar(a.pid, a.segundos, a.salida, a.nombre, a.contadores)
        if not t:
            sys.exit(1)
        r = resumir_contadores(t, a.pid) if a.contadores else resumir(t, a.pid)
    else:
        r = resumir_contadores(a.trace, a.pid) if a.contadores else resumir(a.trace, a.pid)
    if getattr(a, "json", None):
        with open(a.json, "w") as f:
            json.dump(r, f, ensure_ascii=False, indent=1)
    print(en_texto(r) if "segundos" in r else json.dumps(r, ensure_ascii=False, indent=1))


if __name__ == "__main__":
    main()
