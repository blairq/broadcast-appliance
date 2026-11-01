#!/usr/bin/env python3
"""tools/carga/medir-sala.py — la «Prueba de sala»: N invitados simulados contra la app andando

Lanza el probador «hinchada» (../hinchada) contra el gateway con N invitados y mide la
máquina y la app cada 0,5 s hasta que la sala se estabiliza y después un tiempo de
régimen (de fábrica 60 s). Escribe en --salida:

  muestras.jsonl   una línea cada 0,5 s: CPU, hilos y memoria por proceso (ba-app,
                   ba-gateway, ba-web, hinchada, WindowServer), memoria del sistema,
                   carga, GPU (ioreg), cambios de contexto y despertares (top), red por
                   proceso (nettop) y la sala (room_status por ba-mcp, cada 1 s)
  app.jsonl        lo de adentro de la app cada 5 s: sistema_recursos y grilla_personas_estado
  registro.txt     las líneas de app.log y gateway.log de la prueba
  grilla.jpg       la página de la sala al final (ventana_captura)
  hinchada/        lo de hinchada (muestras.jsonl, informe.json/.html, consola.txt)
  resumen.json/.md el resumen: estabilización, p50/p95/máx y picos, creación de hilos,
                   memoria, red, la app por dentro y el costo de conectar (por invitado)

LA GUARDIA (kernel panic del 2026-11-01): cada 0,5 s; si la memoria disponible baja de
--min-libre-mb, ba-app pasa de --max-hilos-app o la carga (1 min) pasa de --max-carga,
corta hinchada y lo anota (ese es el dato). Antes de empezar exige la máquina quieta
(sin colima, sin compilaciones, carga y memoria bajas): --sin-chequeo lo saltea.

SEGURIDAD DE LOS DATOS: solo corre si el evento en curso es --evento («prueba linea L0»
de fábrica). Con --cupo pone el cupo en N (y la sala sin lobby) y al final lo vuelve a
--cupo-final (y el lobby como estaba).

Ejemplo:
  tools/carga/medir-sala.py --ba-mcp dist/BroadcastAppliance.app/Contents/Resources/bin/64bit/ba-mcp \\
      --participantes 20 --cupo --cupo-final 2 --salida ~/mediciones/rama-20
Sin sudo: el motor de video (VideoToolbox) no se ve sin powermetrics; la GPU es la de ioreg.
"""
import argparse, base64, json, os, re, signal, statistics, subprocess, sys, threading, time
from pathlib import Path

ENV = {"LC_ALL": "C", "PATH": "/bin:/usr/bin:/usr/sbin:/sbin:/opt/homebrew/bin"}
PROCS = ("ba-app", "ba-gateway", "ba-web", "hinchada", "WindowServer")


def correr(args, timeout=10):
    try:
        return subprocess.run(args, capture_output=True, text=True, env=ENV, timeout=timeout).stdout
    except Exception:
        return ""


# --------------------------------------------------------------------------- MCP
class Mcp:
    """Un cliente de ba-mcp (stdio) con candado: lo usan varios hilos."""

    def __init__(self, exe):
        self.p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        self.n = 0
        self.mu = threading.Lock()
        self._enviar({"jsonrpc": "2.0", "id": 0, "method": "initialize",
                      "params": {"capabilities": {}, "clientInfo": {"name": "medir-sala", "version": "1"}, "protocolVersion": "2024-11-05"}})
        self._esperar(0)

    def _enviar(self, o):
        self.p.stdin.write(json.dumps(o) + "\n")
        self.p.stdin.flush()

    def _esperar(self, i):
        while True:
            l = self.p.stdout.readline()
            if not l:
                raise RuntimeError("ba-mcp se cerró")
            r = json.loads(l)
            if r.get("id") == i:
                return r

    def llamar(self, nombre, args=None):
        with self.mu:
            self.n += 1
            i = self.n
            self._enviar({"jsonrpc": "2.0", "id": i, "method": "tools/call", "params": {"name": nombre, "arguments": args or {}}})
            r = self._esperar(i)
        if "error" in r:
            raise RuntimeError(f"{nombre}: {r['error']}")
        return r.get("result", {})

    def datos(self, nombre, args=None):
        res = self.llamar(nombre, args)
        if res.get("structuredContent"):
            return res["structuredContent"]
        for c in res.get("content", []):
            if c.get("type") == "text":
                try:
                    return json.loads(c["text"])
                except Exception:
                    return {"texto": c["text"]}
        return res

    def cerrar(self):
        try:
            self.p.kill()
        except Exception:
            pass


def sin_pin(o):
    """La sala sin el PIN (las mediciones se guardan y se comparten)."""
    s = json.dumps(o, ensure_ascii=False)
    return json.loads(re.sub(r'"pin": *"[^"]*"', '"pin": "***"', s))


# --------------------------------------------------------------------------- el sistema
def pids():
    out = {}
    for l in correr(["ps", "-axo", "pid=,ppid=,time=,rss=,comm="]).splitlines():
        p = l.split(None, 4)
        if len(p) == 5:
            nombre = p[4].rsplit("/", 1)[-1]
            if nombre in PROCS:
                out.setdefault(nombre, []).append({"pid": int(p[0]), "ppid": int(p[1]), "cpu_s": segs(p[2]), "rss_mb": int(p[3]) // 1024})
    return out


def segs(x):
    d = 0
    if "-" in x:
        d, x = x.split("-")
        d = int(d)
    v = 0.0
    for c in x.split(":"):
        v = v * 60 + float(c)
    return d * 86400 + v


def hilos(pid):
    return max(0, len(correr(["ps", "-M", "-p", str(pid)]).splitlines()) - 1)


def memoria():
    t = correr(["vm_stat"])
    m = re.search(r"page size of (\d+)", t)
    ps = int(m.group(1)) if m else 16384
    v = {}
    for k, n in (("libre", "Pages free"), ("inactiva", "Pages inactive"), ("especulativa", "Pages speculative"),
                 ("purgable", "Pages purgeable"), ("comprimida", "Pages occupied by compressor"), ("wired", "Pages wired down"),
                 ("swapouts", "Swapouts")):
        mm = re.search(n + r":\s+(\d+)", t)
        v[k] = int(mm.group(1)) if mm else 0
    sw = correr(["sysctl", "-n", "vm.swapusage"])
    usado = re.search(r"used = ([\d.]+)M", sw)
    return {"disponible_mb": (v["libre"] + v["inactiva"] + v["especulativa"] + v["purgable"]) * ps // 1048576,
            "comprimida_mb": v["comprimida"] * ps // 1048576, "wired_mb": v["wired"] * ps // 1048576,
            "swap_usado_mb": float(usado.group(1)) if usado else 0.0, "swapouts": v["swapouts"]}


def gpu():
    t = correr(["ioreg", "-r", "-d", "1", "-c", "IOAccelerator"])
    m = re.search(r'"PerformanceStatistics" = \{([^}]*)\}', t)
    if not m:
        return {}
    g = {}
    for k, v in re.findall(r'"([^"]+)"=(\d+)', m.group(1)):
        g[k] = int(v)
    return {"dispositivo_pct": g.get("Device Utilization %"), "render_pct": g.get("Renderer Utilization %"),
            "tiler_pct": g.get("Tiler Utilization %"), "memoria_en_uso_mb": g.get("In use system memory", 0) // 1048576,
            "memoria_asignada_mb": g.get("Alloc system memory", 0) // 1048576}


def carga():
    t = correr(["sysctl", "-n", "vm.loadavg"]).replace(",", ".").split()
    try:
        return float(t[1]), float(t[2]), float(t[3])
    except Exception:
        return (0.0, 0.0, 0.0)


def nucleos():
    try:
        return int(correr(["sysctl", "-n", "hw.ncpu"]).strip())
    except Exception:
        return os.cpu_count() or 8


class Top(threading.Thread):
    """top en modo delta (dos muestras de 1 s, la segunda): cambios de contexto y
    despertares por proceso y la CPU de toda la máquina. Se corre de a una (con una
    tubería, top -l 0 junta en su búfer y llega tarde)."""

    def __init__(self):
        super().__init__(daemon=True)
        self.ultimo = {}
        self.cpu_total = None
        self.mu = threading.Lock()
        self.seguir = True

    def run(self):
        while self.seguir:
            t = correr(["top", "-l", "2", "-s", "1", "-c", "d", "-n", "40", "-o", "cpu",
                        "-stats", "pid,command,cpu,th,csw,idlew,mem"], timeout=10)
            partes = t.split("Processes:")
            if len(partes) < 3:
                continue
            ult = partes[-1]
            actual, total = {}, None
            for l in ult.splitlines():
                if l.startswith("CPU usage:"):
                    d = {k: float(v) for v, k in re.findall(r"([\d.]+)% (user|sys|idle)", l)}
                    total = {"user": d.get("user"), "sys": d.get("sys"), "idle": d.get("idle")}
                c = l.split()
                if len(c) >= 7 and c[0].isdigit() and c[1] in PROCS:
                    num = lambda x: int(re.sub(r"[^\d]", "", x.split("/")[0]) or 0)
                    actual[c[1] + ":" + c[0]] = {"csw": num(c[4]), "idlew": num(c[5])}
            with self.mu:
                self.ultimo, self.cpu_total = actual, total

    def leer(self):
        with self.mu:
            return dict(self.ultimo), self.cpu_total

    def parar(self):
        self.seguir = False


class Red(threading.Thread):
    """nettop de a una muestra por segundo: bytes por proceso (acumulados)."""

    def __init__(self):
        super().__init__(daemon=True)
        self.acum = {}
        self.mu = threading.Lock()
        self.seguir = True

    def run(self):
        while self.seguir:
            t0 = time.time()
            acum = {}
            for l in correr(["nettop", "-P", "-L", "1", "-x", "-J", "bytes_in,bytes_out"], timeout=10).splitlines():
                c = l.strip().split(",")
                if len(c) >= 3 and c[0].rsplit(".", 1)[0] in PROCS:
                    try:
                        acum[c[0]] = (int(c[1] or 0), int(c[2] or 0))
                    except ValueError:
                        pass
            with self.mu:
                self.acum = acum
            time.sleep(max(0.0, 1 - (time.time() - t0)))

    def leer(self):
        with self.mu:
            return dict(self.acum)

    def parar(self):
        self.seguir = False


def footprint(pid):
    t = correr(["footprint", "-p", str(pid)], timeout=8)
    m = re.search(r"phys_footprint:\s*([\d.]+)\s*([KMG]B)", t) or re.search(r"Footprint:\s*([\d.]+)\s*([KMG]B)", t)
    if not m:
        return None
    f = {"KB": 1 / 1024, "MB": 1, "GB": 1024}[m.group(2)]
    return round(float(m.group(1)) * f, 1)


# --------------------------------------------------------------------------- guardia
def chequear(min_libre):
    malos = []
    try:
        if subprocess.run(["colima", "status"], capture_output=True, env=ENV, timeout=10).returncode == 0:
            malos.append("colima prendida")
    except Exception:
        pass
    if correr(["pgrep", "-f", "clang -cc1|ninja|cmake --build"]).strip():
        malos.append("hay compilaciones")
    m = memoria()["disponible_mb"]
    if m < 2 * min_libre:
        malos.append(f"memoria disponible {m} MB")
    c = carga()[0]  # la de 1 minuto (la misma que mira la guardia del contrato)
    if c > 8:
        malos.append(f"carga {c}")
    return malos


# --------------------------------------------------------------------------- estadística
def pct(xs, p):
    xs = sorted(x for x in xs if x is not None)
    if not xs:
        return None
    k = (len(xs) - 1) * p / 100
    a, b = int(k), min(int(k) + 1, len(xs) - 1)
    return round(xs[a] + (xs[b] - xs[a]) * (k - a), 2)


def resumen_serie(xs, pico=None):
    xs = [x for x in xs if x is not None]
    if not xs:
        return None
    r = {"p50": pct(xs, 50), "p95": pct(xs, 95), "max": round(max(xs), 2)}
    if pico is not None:
        r["picos"] = sum(1 for i, x in enumerate(xs) if x > pico and (i == 0 or xs[i - 1] <= pico))
    return r


# --------------------------------------------------------------------------- la prueba
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ba-mcp", required=True)
    ap.add_argument("--participantes", type=int, required=True)
    ap.add_argument("--salida", required=True)
    ap.add_argument("--url", default="http://127.0.0.1:8787")
    ap.add_argument("--hinchada", default=str(Path(__file__).resolve().parents[3] / "hinchada/build/hinchada")
                    if (Path(__file__).resolve().parents[3] / "hinchada/build/hinchada").exists() else "/Users/usuario/Proyectos/hinchada/build/hinchada")
    ap.add_argument("--regimen", type=float, default=60, help="segundos medidos después de estabilizar")
    ap.add_argument("--estabilizar-max", type=float, default=120, help="tope para estabilizar (s)")
    ap.add_argument("--evento", default="prueba linea L0")
    ap.add_argument("--cupo", action="store_true", help="poner el cupo en N (y sin lobby) y volverlo al final")
    ap.add_argument("--cupo-final", type=int, default=2)
    ap.add_argument("--min-libre-mb", type=int, default=3000)
    ap.add_argument("--max-hilos-app", type=int, default=1200)
    ap.add_argument("--max-carga", type=float, default=40)
    ap.add_argument("--sin-chequeo", action="store_true")
    ap.add_argument("--logs", default=None, help="carpeta Logs de los datos (app.log, gateway.log)")
    ap.add_argument("hinchada_extra", nargs="*", help="opciones para hinchada (después de --)")
    a = ap.parse_args()
    sal = Path(a.salida).expanduser()
    sal.mkdir(parents=True, exist_ok=True)
    logs = Path(a.logs) if a.logs else Path(a.ba_mcp).resolve().parents[5] / "BroadcastAppliance Datos" / "Logs"

    if not a.sin_chequeo:
        malos = chequear(a.min_libre_mb)
        if malos:
            sys.exit("la máquina no está quieta: " + "; ".join(malos))
    mcp = Mcp(a.ba_mcp)
    ev = mcp.datos("evento_en_curso").get("en_curso", {}).get("nombre")
    if ev != a.evento:
        sys.exit(f"el evento en curso es «{ev}», no «{a.evento}»: no toco nada")
    sala0 = mcp.datos("room_status")
    lobby0 = bool(sala0.get("lobby", True))
    if a.cupo:
        mcp.llamar("settings_set", {"values": {"guests.count": a.participantes}})
        mcp.llamar("room_set", {"lobby": False})
        time.sleep(3)
    pin = mcp.datos("room_status").get("pin", "")

    # Lo que ya está en los registros: se lee lo nuevo.
    marcas = {}
    for n in ("app.log", "gateway.log"):
        try:
            marcas[n] = (logs / n).stat().st_size
        except OSError:
            marcas[n] = None

    top, red = Top(), Red()
    top.start()
    red.start()
    ncpu = nucleos()
    hdir = sal / "hinchada"
    hdir.mkdir(exist_ok=True)
    env = dict(os.environ, HINCHADA_PIN=pin)
    durante = a.estabilizar_max + a.regimen + 10
    cmd = [a.hinchada, "--servidor", "gateway", "--url", a.url, "--participantes", str(a.participantes),
           "--durante", f"{int(durante)}s", "--vigilar", "ba-gateway,ba-app,ba-web", "--salida", str(hdir), "--detalle"] + a.hinchada_extra
    t0 = time.time()
    hin = subprocess.Popen(cmd, stdout=open(hdir / "consola.txt", "w"), stderr=subprocess.STDOUT, env=env)

    muestras, app_info = [], []
    corte = None
    anterior = {}
    ultimo_mcp = ultimo_app = ultimo_fp = 0
    sala = {}
    estable_t = None
    fin_t = None
    f = open(sal / "muestras.jsonl", "w")
    fa = open(sal / "app.jsonl", "w")
    try:
        while hin.poll() is None:
            t = time.time()
            m = {"t": round(t, 2), "s": round(t - t0, 2)}
            ps = pids()
            m["procesos"] = {}
            for nombre, lista in ps.items():
                for p in lista:
                    clave = f"{nombre}:{p['pid']}"
                    prev = anterior.get(clave)
                    cpu = None if prev is None else round((p["cpu_s"] - prev[0]) / max(1e-3, t - prev[1]) * 100, 1)
                    anterior[clave] = (p["cpu_s"], t)
                    e = {"pid": p["pid"], "cpu_pct": cpu, "rss_mb": p["rss_mb"]}
                    if nombre in ("ba-app", "ba-gateway", "hinchada"):
                        e["hilos"] = hilos(p["pid"])
                    m["procesos"][clave] = e
            m["hijos_app"] = sum(1 for lista in ps.values() for p in lista if any(p["ppid"] == q["pid"] for q in ps.get("ba-app", [])))
            m["memoria"] = memoria()
            m["carga"] = carga()
            m["gpu"] = gpu()
            ctx, total = top.leer()
            m["contexto"] = ctx
            m["cpu_maquina"] = total
            m["red"] = red.leer()
            if t - ultimo_fp >= 10:
                ultimo_fp = t
                m["footprint_mb"] = {k: footprint(e["pid"]) for k, e in m["procesos"].items() if k.startswith(("ba-app:", "ba-gateway:"))}
            if t - ultimo_mcp >= 1:
                ultimo_mcp = t
                try:
                    sala = sin_pin(mcp.datos("room_status"))
                    ps_sala = sala.get("participants", [])
                    m["sala"] = {"personas": len(ps_sala), "con_medios": sum(1 for p in ps_sala if p.get("mediaUp")),
                                 "adentro": sum(1 for p in ps_sala if p.get("state") == "admitted"),
                                 "uso": sala.get("uso", {})}
                except Exception as ex:
                    m["sala"] = {"error": str(ex)}
            if t - ultimo_app >= 5:
                ultimo_app = t
                try:
                    rec = mcp.datos("sistema_recursos")
                    gr = mcp.datos("grilla_personas_estado")
                    app_info.append({"s": m["s"], "recursos": rec, "grilla": gr})
                    fa.write(json.dumps({"s": m["s"], "recursos": rec, "grilla": sin_pin(gr)}, ensure_ascii=False) + "\n")
                    fa.flush()
                except Exception as ex:
                    fa.write(json.dumps({"s": m["s"], "error": str(ex)}) + "\n")
            f.write(json.dumps(m, ensure_ascii=False) + "\n")
            f.flush()
            muestras.append(m)
            # La guardia.
            app = [e for k, e in m["procesos"].items() if k.startswith("ba-app:")]
            hil = max((e.get("hilos", 0) for e in app), default=0)
            motivo = None
            if m["memoria"]["disponible_mb"] < a.min_libre_mb:
                motivo = f"memoria disponible {m['memoria']['disponible_mb']} MB"
            elif hil > a.max_hilos_app:
                motivo = f"ba-app con {hil} hilos"
            elif m["carga"][0] > a.max_carga:
                motivo = f"carga {m['carga'][0]}"
            if motivo:
                corte = {"s": m["s"], "motivo": motivo}
                hin.send_signal(signal.SIGTERM)
                break
            # ¿Estable? Todos con medios, la CPU de ba-app quieta (±10 %) y sin hilos nuevos por 5 s.
            if estable_t is None and m.get("sala", {}).get("con_medios", 0) >= a.participantes:
                v = [x for x in muestras if x["s"] >= m["s"] - 5]
                cpus = [e.get("cpu_pct") for x in v for k, e in x["procesos"].items() if k.startswith("ba-app:")]
                hs = [e.get("hilos") for x in v for k, e in x["procesos"].items() if k.startswith("ba-app:")]
                cpus = [c for c in cpus if c is not None]
                if len(v) >= 9 and cpus and hs and max(hs) == min(hs):
                    med = statistics.mean(cpus)
                    if all(abs(c - med) <= max(10.0, 0.1 * med) for c in cpus):
                        estable_t = m["s"] - 5
                        fin_t = m["s"] + a.regimen
            if fin_t is not None and m["s"] >= fin_t:
                break
            if estable_t is None and m["s"] > a.estabilizar_max + 30:
                fin_t = m["s"] + a.regimen  # no se estabilizó: se mide igual (y se dice)
                estable_t = -1
            time.sleep(max(0.0, 0.5 - (time.time() - t)))
        # La grilla al final, con la sala llena: el programa (la fuente «Grilla de personas» al
        # aire) y la página de la sala. Las herramientas guardan la imagen y dicen dónde.
        for nombre, herr, args in (("programa", "foto_programa", {}), ("sala", "ventana_captura", {"ancho": 1600})):
            try:
                if herr == "ventana_captura":
                    mcp.llamar("ui_show_page", {"page": "sala"})
                    time.sleep(2)
                res = mcp.llamar(herr, args)
                for c in res.get("content", []):
                    if c.get("type") == "image":
                        (sal / f"{nombre}.jpg").write_bytes(base64.b64decode(c["data"]))
                    mm = re.search(r"(/[^\n]+?\.(?:jpg|png))", c.get("text", "") or "")
                    if mm and Path(mm.group(1)).exists():
                        (sal / f"{nombre}{Path(mm.group(1)).suffix}").write_bytes(Path(mm.group(1)).read_bytes())
            except Exception as ex:
                (sal / f"{nombre}-error.txt").write_text(str(ex))
    finally:
        if hin.poll() is None:
            hin.send_signal(signal.SIGTERM)
            try:
                hin.wait(20)
            except Exception:
                hin.kill()
        top.parar()
        red.parar()
        f.close()
        fa.close()
        if a.cupo:
            try:
                time.sleep(5)  # que se vayan
                mcp.llamar("settings_set", {"values": {"guests.count": a.cupo_final}})
                time.sleep(8)
                mcp.llamar("settings_set", {"values": {"guests.count": a.cupo_final}})
                mcp.llamar("room_set", {"lobby": lobby0})
            except Exception as ex:
                print("no pude volver el cupo:", ex, file=sys.stderr)
        mcp.cerrar()

    # Los registros de la prueba.
    lineas = {}
    for n, desde in marcas.items():
        try:
            with open(logs / n, "rb") as r:
                if desde is not None:
                    r.seek(desde)
                lineas[n] = r.read().decode("utf-8", "replace").splitlines()
        except OSError:
            lineas[n] = []
    with open(sal / "registro.txt", "w") as r:
        for n, ls in lineas.items():
            r.write(f"==== {n}\n")
            r.write("\n".join(ls) + "\n")

    resumen = resumir(a, muestras, app_info, lineas, corte, estable_t, ncpu, t0, hdir)
    (sal / "resumen.json").write_text(json.dumps(resumen, ensure_ascii=False, indent=1))
    (sal / "resumen.md").write_text(en_texto(resumen))
    print(en_texto(resumen))


def hora_log(l):
    m = re.match(r"(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d+)Z", l)
    if not m:
        return None
    import datetime
    return datetime.datetime.strptime(m.group(1), "%Y-%m-%dT%H:%M:%S.%f").replace(tzinfo=datetime.timezone.utc).timestamp()


def resumir(a, muestras, app_info, lineas, corte, estable_t, ncpu, t0, hdir):
    r = {"participantes": a.participantes, "nucleos": ncpu, "corte": corte,
         "estable_s": estable_t if estable_t not in (None, -1) else None,
         "se_estabilizo": estable_t not in (None, -1)}
    # Cuándo todos tuvieron medios.
    for m in muestras:
        if m.get("sala", {}).get("con_medios", 0) >= a.participantes:
            r["todos_con_medios_s"] = m["s"]
            break
    reg = [m for m in muestras if estable_t not in (None, -1) and m["s"] >= estable_t] or muestras[len(muestras) // 2:]
    r["regimen_s"] = round(reg[-1]["s"] - reg[0]["s"], 1) if reg else 0
    r["carga"] = {"inicio": muestras[0]["carga"] if muestras else None, "regimen_max": max((m["carga"][0] for m in reg), default=None)}
    # CPU, hilos y memoria por proceso (por nombre).
    procs = {}
    for nombre in PROCS:
        cpu = [sum(e["cpu_pct"] or 0 for k, e in m["procesos"].items() if k.startswith(nombre + ":")) for m in reg]
        todos = [sum(e["cpu_pct"] or 0 for k, e in m["procesos"].items() if k.startswith(nombre + ":")) for m in muestras]
        rss = [sum(e["rss_mb"] for k, e in m["procesos"].items() if k.startswith(nombre + ":")) for m in reg]
        hil = [sum(e.get("hilos", 0) for k, e in m["procesos"].items() if k.startswith(nombre + ":")) for m in muestras]
        csw = [sum(v["csw"] for k, v in m["contexto"].items() if k.startswith(nombre)) for m in reg]
        idw = [sum(v["idlew"] for k, v in m["contexto"].items() if k.startswith(nombre)) for m in reg]
        procs[nombre] = {"cpu_pct_regimen": resumen_serie(cpu, pico=80 * ncpu),  # % de un núcleo: pico > 80 % de todos
                         "cpu_pct_toda": resumen_serie(todos),
                         "rss_mb_regimen": resumen_serie(rss),
                         "hilos": {"inicio": hil[0] if hil else None, "max": max(hil) if hil else None, "fin": hil[-1] if hil else None},
                         "cambios_contexto_s": resumen_serie(csw), "despertares_s": resumen_serie(idw)}
    r["procesos"] = procs
    # Toda la máquina: picos > 80 % de los núcleos disponibles.
    tot = [100 - (m["cpu_maquina"] or {}).get("idle", 100) if m.get("cpu_maquina") else None for m in muestras]
    r["cpu_maquina_pct"] = resumen_serie(tot, pico=80)
    r["gpu"] = {k: resumen_serie([m["gpu"].get(k) for m in reg]) for k in ("dispositivo_pct", "render_pct", "tiler_pct", "memoria_en_uso_mb")}
    r["gpu"]["nota"] = "ioreg PerformanceStatistics; el motor de video (VideoToolbox) no se ve sin sudo (powermetrics)"
    fps = [v for m in muestras for k, v in (m.get("footprint_mb") or {}).items() if k.startswith("ba-app:") and v]
    r["footprint_app_mb_max"] = max(fps) if fps else None
    r["memoria"] = {"disponible_mb_min": min((m["memoria"]["disponible_mb"] for m in muestras), default=None),
                    "comprimida_mb_max": max((m["memoria"]["comprimida_mb"] for m in muestras), default=None),
                    "swap_usado_mb_max": max((m["memoria"]["swap_usado_mb"] for m in muestras), default=None)}
    # Creación de hilos de ba-app: de dónde a dónde y en cuánto.
    ha = [(m["s"], sum(e.get("hilos", 0) for k, e in m["procesos"].items() if k.startswith("ba-app:"))) for m in muestras]
    if ha:
        h0, hmax = ha[0][1], max(h for _, h in ha)
        llego = next((s for s, h in ha if h >= hmax), None)
        r["hilos_app"] = {"inicio": h0, "max": hmax, "por_invitado": round((hmax - h0) / max(1, a.participantes), 1), "llego_al_max_s": llego}
    hg = [sum(e.get("hilos", 0) for k, e in m["procesos"].items() if k.startswith("ba-gateway:")) for m in muestras]
    if hg:
        r["hilos_gateway"] = {"inicio": hg[0], "max": max(hg), "por_invitado": round((max(hg) - hg[0]) / max(1, a.participantes), 1)}
    # Red por proceso en régimen (Mbps), por la diferencia de nettop.
    if len(reg) >= 2:
        dt = reg[-1]["t"] - reg[0]["t"]
        red = {}
        for k, (bi, bo) in reg[-1]["red"].items():
            if k in reg[0]["red"]:
                n = k.rsplit(".", 1)[0]
                b0 = reg[0]["red"][k]
                x = red.setdefault(n, [0, 0])
                x[0] += (bi - b0[0]) * 8 / dt / 1e6
                x[1] += (bo - b0[1]) * 8 / dt / 1e6
        r["red_mbps"] = {n: {"entra": round(x[0], 2), "sale": round(x[1], 2)} for n, x in red.items()}
    # La sala: lo que dice el gateway (uso, transporte: faltantes de los anillos de retorno).
    usos = [m["sala"].get("uso", {}) for m in reg if isinstance(m.get("sala"), dict) and m["sala"].get("uso")]
    if usos:
        r["sala"] = {"cpu_gateway_pct": resumen_serie([u.get("cpuPct") for u in usos]),
                     "kbps_entra": resumen_serie([u.get("kbpsEntrada") for u in usos]),
                     "kbps_sale": resumen_serie([u.get("kbpsSalida") for u in usos]),
                     "puertos": usos[-1].get("puertosRtc")}
        falt = {}
        for u in usos:
            tr = u.get("transporte") or []
            for c in tr if isinstance(tr, list) else tr.get("canales", []):
                n = c.get("nombre", "")
                if n.startswith("BA.Audio.Return"):
                    falt[n] = max(falt.get(n, 0), c.get("faltantes", 0))
        if falt:
            r["sala"]["retornos_faltantes_max_10s"] = {"max": max(falt.values()), "suma": sum(falt.values()), "anillos": len(falt)}
    # La grilla: cuántos se ven.
    if app_info:
        g = app_info[-1].get("grilla", {})
        # Cuántos muestra cada grilla, cuántos quedaron en el «+N» y su tope (lo crudo, en app.jsonl).
        r["grilla"] = [{"fuente": x.get("fuente"), "muestra": len(x.get("personas", [])), "mas": x.get("mas"), "tope": x.get("tope")}
                       for x in (g.get("grillas", []) if isinstance(g, dict) else [])]
        r["gente_con_camara"] = sum(1 for x in (g.get("gente", []) if isinstance(g, dict) else []) if x.get("con_camara"))
        rec = app_info[-1].get("recursos", {})
        r["app_bucle_ui"] = rec.get("bucle_ui")
    # Del registro: demoras y candados.
    app = lineas.get("app.log", [])
    r["registro_app"] = {"se_demoro": sum("se demoró" in l for l in app),
                         "candado_tomado": sum("candado del motor" in l for l in app),
                         "colgado": sum("colg" in l for l in app),
                         "lineas": len(app)}
    # Costo de conectar, por invitado: entró → offer → medios conectados (gateway.log).
    gw = lineas.get("gateway.log", [])
    ent, ofe, med = {}, {}, {}
    for l in gw:
        h = hora_log(l)
        if h is None:
            continue
        m = re.search(r"entro a la conferencia \((p[0-9a-f]+)\)", l)
        if m:
            ent.setdefault(m.group(1), h)
        m = re.search(r"offer de (p[0-9a-f]+) aceptado", l)
        if m:
            ofe.setdefault(m.group(1), h)
        m = re.search(r"medios de (p[0-9a-f]+) \(sesión \d+\) conectados", l)
        if m:
            med.setdefault(m.group(1), h)
    a_ofe = [(ofe[p] - ent[p]) * 1000 for p in ent if p in ofe]
    a_med = [(med[p] - ofe[p]) * 1000 for p in ofe if p in med]
    total = [(med[p] - t0) for p in med]
    r["conectar"] = {"entrar_a_offer_ms": resumen_serie(a_ofe), "offer_a_medios_ms": resumen_serie(a_med),
                     "desde_el_lanzamiento_s": resumen_serie(total), "conectados_en_registro": len(med)}
    # Lo de hinchada: unirse (por participante).
    try:
        un = []
        with open(hdir / "muestras.jsonl") as h:
            ult = None
            for l in h:
                ult = l
            if ult:
                for p in json.loads(ult).get("participantes", []):
                    if p.get("unionMs", -1) >= 0:
                        un.append(p["unionMs"])
        r["hinchada_union_ms"] = resumen_serie(un)
    except Exception:
        pass
    return r


def en_texto(r):
    l = [f"# Prueba de sala: {r['participantes']} invitados", ""]
    if r.get("corte"):
        l.append(f"**LA GUARDIA CORTÓ** a los {r['corte']['s']} s: {r['corte']['motivo']}")
    l.append(f"- estable a los {r.get('estable_s')} s (todos con medios a los {r.get('todos_con_medios_s')} s); régimen {r.get('regimen_s')} s; carga {r.get('carga')}")
    for n, p in r.get("procesos", {}).items():
        c = p.get("cpu_pct_regimen") or {}
        rs = p.get("rss_mb_regimen") or {}
        l.append(f"- {n}: CPU p50 {c.get('p50')} % p95 {c.get('p95')} % máx {c.get('max')} %; RSS p50 {rs.get('p50')} MB; hilos {p.get('hilos')}; "
                 f"cambios de contexto/s p50 {(p.get('cambios_contexto_s') or {}).get('p50')}; despertares/s p50 {(p.get('despertares_s') or {}).get('p50')}")
    l.append(f"- máquina: CPU {r.get('cpu_maquina_pct')}; GPU {json.dumps({k: v for k, v in r.get('gpu', {}).items() if k != 'nota'})}")
    l.append(f"- memoria: {r.get('memoria')}")
    l.append(f"- hilos de ba-app: {r.get('hilos_app')}; del gateway: {r.get('hilos_gateway')}")
    l.append(f"- red (Mbps): {r.get('red_mbps')}")
    l.append(f"- sala: {r.get('sala')}")
    l.append(f"- grilla: {r.get('grilla')}")
    l.append(f"- registro de la app: {r.get('registro_app')}")
    l.append(f"- conectar: {r.get('conectar')}; hinchada (unirse, ms): {r.get('hinchada_union_ms')}")
    return "\n".join(l) + "\n"


if __name__ == "__main__":
    main()
