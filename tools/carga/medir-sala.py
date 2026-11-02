#!/usr/bin/env python3
"""tools/carga/medir-sala.py — la «Prueba de sala»: N invitados simulados contra la app andando

Lanza el probador «hinchada» (../hinchada) contra el gateway con N invitados y mide la
máquina y la app cada 0,5 s hasta que la sala se estabiliza y después un tiempo de
régimen (de fábrica 60 s). Escribe en --salida:

  muestras.jsonl   una línea cada 0,5 s: CPU, hilos y memoria por proceso (ba-app,
                   ba-gateway, ba-web, hinchada, WindowServer), memoria del sistema,
                   carga, GPU (ioreg), cambios de contexto y despertares (top), red por
                   proceso (nettop) y la sala (room_status por ba-mcp, cada 1 s)
  app.jsonl        lo de adentro de la app cada 5 s: sistema_recursos, grilla_personas_estado y
                   estudio_latencia (la edad del audio de cada invitado)
  registro.txt     las líneas de app.log y gateway.log de la prueba
  grilla.jpg       la página de la sala al final (ventana_captura)
  grilla-fps.json  los cuadros NUEVOS por segundo de cada cara de la grilla (grilla_personas_medir,
                   --medir-grilla-s segundos apenas se estabiliza): en la entrada de la app (lo que
                   pintó el gateway y llegó) y en el programa (lo que se ve)
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

MODO REMOTO (--remoto HOST): la app corre en OTRA máquina (Windows) y hinchada acá genera la
carga por la red (la red entre las dos es parte de la prueba). Lo que cambia:
  - ba-mcp habla con la app remota por HTTP: BA_MCP_URL y BA_MCP_TOKEN en el entorno (p. ej.
    un túnel `ssh -f -N -L 18790:127.0.0.1:8790 windows`, BA_MCP_URL=http://127.0.0.1:18790/mcp y
    el token de `ba-mcp.exe --token` allá, que cambia en cada arranque). El ba-mcp local sirve.
  - --url es el gateway en la LAN (http://192.168.1.177:8787): «gateway.bind» allá en 0.0.0.0.
  - Los procesos ba-app/ba-gateway/ba-web, la GPU (motores 3D/video por proceso), la red de los
    adaptadores, la memoria y la CPU de la máquina se toman por ssh con tools/carga/muestrear-win.ps1
    (se copia a --remoto-dir); acá se miden solo hinchada (cpu_generador: la CPU de esta máquina) y
    su red (nettop: lo que cruza la LAN).
  - app.log y gateway.log se traen por scp de --remoto-logs al final (desde donde estaban al empezar).
  - La guardia mira además la memoria disponible de la máquina remota.
  - Todos los invitados salen de UNA dirección IP: con más de 32 (room.maxConexionesPorIp, de
    fábrica 32) el gateway rechaza el resto; subirlo en el evento de prueba (settings_set).
CAMINO (--camino): por dónde entran los invitados de hinchada.
  directo         WebRTC directo por UDP (lo de siempre)
  turn-udp        por el TURN de la caja con política relay (GET /api/ice, como un invitado que viene
  turn-tcp        por pinggy): por UDP, por TCP o PCT % por TCP y el resto por UDP. El TURN tiene que
  turn-mezcla:PCT estar prendido (pinggy + «TURN por el túnel»; para UDP, «turnUdp»).
  pinggy[:udp|tcp|mezcla:PCT]  por los extremos PÚBLICOS de pinggy: la señalización por la URL pública
                  y el medio por su TURN público (tcp de fábrica). La credencial de pinggy la tiene el
                  gateway (nunca va en la línea de comandos ni en archivos): acá solo se lee la URL.
OTRAS MEDICIONES
  --duracion 30m        régimen largo (resistencia): el resumen trae la tendencia por minuto (memoria,
                        hilos, edad del audio, faltantes del retorno)
  --rafaga              los N entran a la vez: tiempo hasta medios y hasta el primer cuadro del retorno
                        por invitado, y los picos de CPU e hilos mientras entran
  --palabra K           K veces en el régimen se destaca a un invitado (grilla_personas_destacar) y se
                        mide cuánto tarda la HD («capas: llegó la HD … ms desde el pedido» del gateway)
  --grabar-transmitir   graba el programa (record_start) y transmite a un destino SRT LOCAL de prueba
                        (127.0.0.1, modo listener: nada sale de la máquina) durante la prueba
  --xctrace S           (Mac) S s de xctrace en régimen al gateway y a la app: los hilos por nombre (CPU,
                        cambios de contexto, núcleos P/E). --contadores suma la plantilla CPU Counters
  --etw S               (Windows, --remoto) S s de WPR en régimen: núcleo lógico → hilos, DPC e ISR
                        (tools/carga/etw-win.ps1; consola de administrador)
  Más de 32 invitados: con --cupo también sube room.maxConexionesPorIp (todos salen de una IP) y lo
  vuelve a su valor al final (los dos cambios reinician la sala: se avisa).
Ejemplo (desde la Mac, con la app abierta en la PC y sin la app abierta acá):
  BA_MCP_URL=http://127.0.0.1:18790/mcp BA_MCP_TOKEN=… tools/carga/medir-sala.py \\
      --ba-mcp dist/BroadcastAppliance.app/Contents/Resources/bin/64bit/ba-mcp --remoto windows \\
      --url http://192.168.1.177:8787 --participantes 20 --cupo --salida ~/mediciones/pc-20 -- --modo grabado --hablan 1
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


class Remoto(threading.Thread):
    """Las muestras de la máquina remota: muestrear-win.ps1 por ssh, una línea JSON por muestra."""

    def __init__(self, host, script, segundos):
        super().__init__(daemon=True)
        self.p = subprocess.Popen(["ssh", host, "powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script,
                                   "-Segundos", str(int(segundos))], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        self.ultimo = {}
        self.placa = None
        self.mu = threading.Lock()

    def run(self):
        for l in self.p.stdout:
            try:
                o = json.loads(l)
            except Exception:
                continue
            if "rss" in o or "adaptadores" in o:  # la primera línea: la placa de red
                self.placa = o
                continue
            with self.mu:
                self.ultimo = o

    def leer(self):
        with self.mu:
            return dict(self.ultimo)

    def parar(self):
        try:
            self.p.terminate()
        except Exception:
            pass


def ssh_ps(host, comando, timeout=30):
    """Un comando de PowerShell en el host remoto (la salida, o "" si falla)."""
    try:
        return subprocess.run(["ssh", host, "powershell", "-NoProfile", "-Command", comando], capture_output=True, text=True,
                              timeout=timeout).stdout
    except Exception:
        return ""


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


def segs_texto(t):
    """«30m», «1h», «90s», «90» → segundos."""
    m = re.fullmatch(r"\s*([\d.]+)\s*(ms|s|m|h)?\s*", str(t))
    if not m:
        sys.exit(f"--duracion: no entiendo «{t}» (30m, 1h, 90s)")
    return float(m.group(1)) * {"ms": 0.001, "s": 1, "m": 60, "h": 3600, None: 1}[m.group(2)]


def camino_a_turn(c):
    """(--turn de hinchada o None, ¿por los extremos públicos de pinggy?)."""
    if c == "directo":
        return None, False
    if c in ("turn-udp", "turn-tcp"):
        return c[5:], False
    if c.startswith("turn-mezcla:"):
        return "mezcla:" + c.split(":", 1)[1], False
    if c == "pinggy" or c.startswith("pinggy:"):
        t = c.split(":", 1)[1] if ":" in c else "tcp"
        return t, True
    sys.exit(f"--camino: directo, turn-udp, turn-tcp, turn-mezcla:PCT o pinggy[:udp|tcp|mezcla:PCT] (no «{c}»)")


# Los núcleos de la Mac (host_processor_info: ticks de user/system/idle/nice por núcleo; sin sudo).
# Las interrupciones por núcleo no se ven sin sudo (powermetrics): se anota.
def _nucleos_mac_ticks():
    import ctypes
    try:
        libc = ctypes.CDLL("/usr/lib/libSystem.B.dylib")
        host = libc.mach_host_self()
        n = ctypes.c_uint(0)
        info = ctypes.POINTER(ctypes.c_int)()
        cnt = ctypes.c_uint(0)
        if libc.host_processor_info(host, 2, ctypes.byref(n), ctypes.byref(info), ctypes.byref(cnt)) != 0:  # PROCESSOR_CPU_LOAD_INFO
            return None
        t = [[info[i * 4 + k] for k in range(4)] for i in range(n.value)]  # user, system, idle, nice
        yo = ctypes.c_uint.in_dll(libc, "mach_task_self_").value
        libc.vm_deallocate(yo, ctypes.cast(info, ctypes.c_void_p), ctypes.c_size_t(cnt.value * 4))
        return t
    except Exception:
        return None


class NucleosMac:
    def __init__(self):
        self.prev = _nucleos_mac_ticks()

    def leer(self):
        act = _nucleos_mac_ticks()
        out = None
        if act and self.prev and len(act) == len(self.prev):
            out = []
            for a, p in zip(act, self.prev):
                d = [x - y for x, y in zip(a, p)]
                tot = sum(d) or 1
                out.append([round(100 * (d[0] + d[1] + d[3]) / tot, 1), round(100 * d[1] / tot, 1)])  # ocupado, sistema
        self.prev = act
        return out


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
    ap.add_argument("--medir-grilla-s", type=int, default=20, help="segundos de grilla_personas_medir en régimen (0 = no)")
    ap.add_argument("--remoto", default=None, help="la app corre en este host (alias de ssh, Windows): ver MODO REMOTO")
    ap.add_argument("--remoto-dir", default="C:\\ba-pruebas", help="carpeta de trabajo en el host remoto (ahí va muestrear-win.ps1)")
    ap.add_argument("--remoto-logs", default="C:\\ba-pruebas\\BroadcastAppliance\\Logs", help="la carpeta Logs de la app remota")
    ap.add_argument("--camino", default="directo", help="directo | turn-udp | turn-tcp | turn-mezcla:PCT | pinggy[:udp|tcp|mezcla:PCT]")
    ap.add_argument("--duracion", default=None, help="régimen largo (30m, 1h): resistencia, con tendencia por minuto")
    ap.add_argument("--rafaga", action="store_true", help="todos entran a la vez")
    ap.add_argument("--palabra", type=int, default=0, help="veces que se destaca a un invitado y se mide la HD")
    ap.add_argument("--grabar-transmitir", action="store_true", help="grabar y transmitir a un SRT local mientras se mide")
    ap.add_argument("--xctrace", type=int, default=0, help="(Mac) segundos de xctrace en régimen (gateway y app)")
    ap.add_argument("--contadores", action="store_true", help="(Mac) con --xctrace, también CPU Counters")
    ap.add_argument("--etw", type=int, default=0, help="(Windows remoto) segundos de WPR en régimen")
    ap.add_argument("hinchada_extra", nargs="*", help="opciones para hinchada (después de --)")
    a = ap.parse_args()
    if a.duracion:
        a.regimen = segs_texto(a.duracion)
    turn_hinchada, por_pinggy = camino_a_turn(a.camino)
    if a.remoto and not (os.environ.get("BA_MCP_URL") and os.environ.get("BA_MCP_TOKEN")):
        sys.exit("--remoto: faltan BA_MCP_URL y BA_MCP_TOKEN (ba-mcp habla con la app remota por HTTP)")
    sal = Path(a.salida).expanduser()
    sal.mkdir(parents=True, exist_ok=True)
    logs = Path(a.logs) if a.logs else Path(a.ba_mcp).resolve().parents[5] / "BroadcastAppliance Datos" / "Logs"
    if a.remoto:
        logs = sal / "logs-remotos"  # se traen al final

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
    # Más de 32: todos salen de UNA dirección (room.maxConexionesPorIp). Se sube y se vuelve al final.
    por_ip0 = None
    if a.cupo and a.participantes + 4 > 32:
        try:
            for s in (mcp.datos("settings_list", {"section": "Red de la conferencia"}).get("settings") or []):
                if s.get("key") == "room.maxConexionesPorIp":
                    por_ip0 = s.get("value")
        except Exception:
            pass
        por_ip0 = por_ip0 if isinstance(por_ip0, int) else 32
        if por_ip0 < a.participantes + 4:
            print(f"aviso: room.maxConexionesPorIp {por_ip0} → {a.participantes + 8} (reinicia la sala; vuelve al final)", file=sys.stderr)
            mcp.llamar("settings_set", {"values": {"room.maxConexionesPorIp": a.participantes + 8}})
            time.sleep(12)
        else:
            por_ip0 = None
    pin = mcp.datos("room_status").get("pin", "")
    url = a.url
    if por_pinggy:
        # La URL pública de pinggy (la credencial la tiene el gateway; acá no se ve ni se guarda).
        rem = mcp.datos("acceso_remoto_estado")
        url = ((rem.get("pinggy") or {}).get("urlPublica") or "").rstrip("/")
        if not url:
            sys.exit("--camino pinggy: pinggy no tiene dirección pública ahora (acceso_remoto_estado)")
    # Grabar y transmitir mientras se mide: el destino es SRT LOCAL en modo listener (nada sale de acá).
    destino_prueba = None
    if a.grabar_transmitir:
        try:
            mcp.llamar("record_start")
            r = mcp.datos("stream_destino_agregar", {"nombre": "medir-sala (local)", "url": "srt://127.0.0.1:18990",
                                                     "modo": "listener", "latencia_ms": 120})
            destino_prueba = r.get("id") or (r.get("destino") or {}).get("id")
            mcp.llamar("stream_start", {"destino": destino_prueba} if destino_prueba else {})
        except Exception as ex:
            print("--grabar-transmitir:", ex, file=sys.stderr)

    # Lo que ya está en los registros: se lee lo nuevo.
    marcas = {}
    for n in ("app.log", "gateway.log"):
        if a.remoto:
            t = ssh_ps(a.remoto, f"(Get-Item '{a.remoto_logs}\\{n}').Length").strip()
            marcas[n] = int(t) if t.isdigit() else None
            continue
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
    remoto = None
    if a.remoto:
        script = a.remoto_dir.rstrip("\\") + "\\muestrear-win.ps1"
        subprocess.run(["scp", "-q", str(Path(__file__).with_name("muestrear-win.ps1")), f"{a.remoto}:{script.replace(chr(92), '/')}"], check=True)
        if a.etw:
            for n in ("etw-win.ps1", "ba-nucleos.wprp"):
                subprocess.run(["scp", "-q", str(Path(__file__).with_name(n)), f"{a.remoto}:{(a.remoto_dir.rstrip(chr(92)) + chr(92) + n).replace(chr(92), '/')}"],
                               check=True)
        remoto = Remoto(a.remoto, script, durante + 120)
        remoto.start()
        # La primera muestra antes de lanzar hinchada: los hilos y la memoria «de inicio» son de la sala vacía.
        espera = time.time() + 30
        while not remoto.leer() and time.time() < espera:
            time.sleep(0.5)
    # --vigilar mira procesos de esta máquina: en remoto lo hace muestrear-win.ps1.
    vigilar = [] if a.remoto else ["--vigilar", "ba-gateway,ba-app,ba-web"]
    extra = []
    if turn_hinchada:
        extra += ["--turn", turn_hinchada]
    if a.rafaga:
        extra += ["--entrada-cada", "0ms"]
    cmd = [a.hinchada, "--servidor", "gateway", "--url", url, "--participantes", str(a.participantes),
           "--durante", f"{int(durante)}s"] + vigilar + ["--salida", str(hdir), "--detalle"] + extra + a.hinchada_extra
    t0 = time.time()
    hin = subprocess.Popen(cmd, stdout=open(hdir / "consola.txt", "w"), stderr=subprocess.STDOUT, env=env)

    muestras, app_info = [], []
    # La medición de la grilla por celda: con su propio ba-mcp (la llamada espera los segundos
    # que mide, y el bucle sigue pidiendo room_status por el otro).
    fps_grilla = {}
    hilo_fps = None

    def medir_grilla():
        m2 = None
        try:
            m2 = Mcp(a.ba_mcp)
            fps_grilla["r"] = m2.datos("grilla_personas_medir", {"segundos": a.medir_grilla_s})
        except Exception as ex:
            fps_grilla["error"] = str(ex)
        finally:
            if m2:
                m2.cerrar()

    # Lo que se mide en régimen, en hilos aparte (cada uno con su ba-mcp si lo necesita).
    extras = {}
    hilos_extra = []

    def en_regimen():
        def xctrace():
            from importlib import util as iu
            spec = iu.spec_from_file_location("xctrace_ba", str(Path(__file__).with_name("xctrace.py")))
            xt = iu.module_from_spec(spec)
            spec.loader.exec_module(xt)
            out = {}
            ps = pids()
            # Los dos a la vez (lo mismo del régimen); los contadores después.
            trazas = {}

            def uno(nombre, pid, contadores):
                trazas[(nombre, contadores)] = xt.grabar(pid, a.xctrace, str(sal / "xctrace"), nombre, contadores)

            # De a una: dos grabaciones a la vez no pueden (kperf es de una sola sesión).
            for contadores in ([False, True] if a.contadores else [False]):
                for n in ("ba-gateway", "ba-app"):
                    if ps.get(n):
                        uno(n, ps[n][0]["pid"], contadores)
            for (nombre, contadores), t in trazas.items():
                try:
                    pid = ps[nombre][0]["pid"]
                    r = xt.resumir_contadores(t, pid) if contadores else xt.resumir(t, pid)
                    out[nombre + (":contadores" if contadores else "")] = r
                except Exception as ex:
                    out[nombre] = {"error": str(ex)}
            extras["xctrace"] = out
            (sal / "xctrace.json").write_text(json.dumps(out, ensure_ascii=False, indent=1))

        def etw():
            script = a.remoto_dir.rstrip("\\") + "\\etw-win.ps1"
            try:
                r = subprocess.run(["ssh", a.remoto, "powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script,
                                    "-Segundos", str(a.etw)], capture_output=True, text=True, timeout=a.etw + 900)
                l = [x for x in r.stdout.splitlines() if x.startswith("{")]
                extras["etw"] = json.loads(l[-1]) if l else {"error": (r.stdout + r.stderr)[-500:]}
            except Exception as ex:
                extras["etw"] = {"error": str(ex)}
            (sal / "etw.json").write_text(json.dumps(extras["etw"], ensure_ascii=False, indent=1))

        def palabra():
            # Destaca a invitados distintos de a uno, cada 8 s; la medida la da el gateway en su registro.
            m3 = None
            hechos = []
            try:
                m3 = Mcp(a.ba_mcp)
                g = m3.datos("grilla_personas_estado")
                fuentes = [x.get("fuente") for x in (g.get("grillas") or []) if x.get("fuente")]
                gente = [x for x in (g.get("gente") or []) if x.get("con_camara")]
                if not fuentes or not gente:
                    extras["palabra"] = {"error": "no hay grilla de personas o nadie con cámara"}
                    return
                for k in range(a.palabra):
                    # Uno que esté en la BAJA (si ya tiene la HD no hay nada que medir); si no hay, cualquiera.
                    g = m3.datos("grilla_personas_estado")
                    bajas = [x for gr in (g.get("grillas") or []) for x in (gr.get("personas") or []) if x.get("capa") == "baja"]
                    cand = bajas or gente
                    p = cand[(k * 7 + 3) % len(cand)]
                    clave = p.get("clave") or p.get("persona") or p.get("id")
                    m3.llamar("grilla_personas_destacar", {"fuente": fuentes[0], "clave": clave})
                    hechos.append({"t": time.time(), "clave": clave})
                    time.sleep(8)
                m3.llamar("grilla_personas_destacar", {"fuente": fuentes[0], "clave": ""})
            except Exception as ex:
                extras["palabra_error"] = str(ex)
            finally:
                extras["palabra_pedidos"] = hechos
                if m3:
                    m3.cerrar()

        for activo, fn in ((a.xctrace and not a.remoto, xctrace), (a.etw and a.remoto, etw), (a.palabra, palabra)):
            if activo:
                h = threading.Thread(target=fn, daemon=True)
                h.start()
                hilos_extra.append(h)

    nmac = None if a.remoto else NucleosMac()
    ultimo_gw = 0
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
            if remoto:
                ps = {k: v for k, v in ps.items() if k == "hinchada"}  # lo de la app está en la otra máquina
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
            if remoto:
                rm = remoto.leer()
                for k, e in (rm.get("procesos") or {}).items():
                    m["procesos"][k] = e
                    m["contexto"][k] = {"csw": e.get("csw_s") or 0}
                m["gpu"] = rm.get("gpu") or {}
                m["cpu_generador"] = total  # la de esta máquina (hinchada)
                m["cpu_maquina"] = rm.get("cpu_maquina")
                m["memoria_remota"] = rm.get("memoria")
                m["red_remota"] = rm.get("red")
                m["remoto_t"] = rm.get("t")
                m["hilos_nombre"] = rm.get("hilos")  # cada hilo por su nombre (GetThreadDescription)
                m["nucleos"] = rm.get("nucleos")     # [ocupado, dpc, irq, interrupciones/s, dpc/s, MHz]
            else:
                m["nucleos"] = nmac.leer()           # [ocupado, sistema] (sin sudo no hay interrupciones)
            if t - ultimo_gw >= 2:
                ultimo_gw = t
                try:
                    ge = mcp.datos("gateway_estado")
                    m["gateway"] = {"fronteras": (ge.get("fronteras") or {}).get("fronteras"), "turn": ge.get("turn"),
                                    # Los códecs y si van por la placa (lo agrega el agente de códecs, si está).
                                    "codecs": ge.get("codecs") or (ge.get("uso") or {}).get("codecs")}
                except Exception as ex:
                    m["gateway"] = {"error": str(ex)}
            if t - ultimo_fp >= 10 and not remoto:
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
                    lat = mcp.datos("estudio_latencia")  # la edad del audio de cada invitado
                    app_info.append({"s": m["s"], "recursos": rec, "grilla": gr, "latencia": lat})
                    fa.write(json.dumps({"s": m["s"], "recursos": rec, "grilla": sin_pin(gr), "latencia": lat}, ensure_ascii=False) + "\n")
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
            elif (m.get("memoria_remota") or {}).get("disponible_mb", 1 << 30) < a.min_libre_mb:
                motivo = f"memoria disponible en {a.remoto}: {m['memoria_remota']['disponible_mb']} MB"
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
                        # Lo que se mide en régimen tiene que entrar en el régimen.
                        fin_t = m["s"] + max(a.regimen, a.medir_grilla_s + 5, a.xctrace * 2 * (2 if a.contadores else 1) + 20,
                                             a.etw + 10, a.palabra * 8 + 10)
                        if a.medir_grilla_s > 0:
                            hilo_fps = threading.Thread(target=medir_grilla, daemon=True)
                            hilo_fps.start()
                        en_regimen()
            if fin_t is not None and m["s"] >= fin_t:
                break
            if estable_t is None and m["s"] > a.estabilizar_max + 30:
                fin_t = m["s"] + a.regimen  # no se estabilizó: se mide igual (y se dice)
                estable_t = -1
                en_regimen()
            time.sleep(max(0.0, 0.5 - (time.time() - t)))
        if hilo_fps is not None:
            hilo_fps.join(a.medir_grilla_s + 30)
        for h in hilos_extra:  # xctrace, etw, la palabra (el análisis puede tardar)
            h.join(900)
        if fps_grilla:
            (sal / "grilla-fps.json").write_text(json.dumps(sin_pin(fps_grilla), ensure_ascii=False, indent=1))
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
                    mr = re.search(r"([A-Za-z]:\\[^\n\"]+?\.(?:jpg|png))", c.get("text", "") or "")
                    if a.remoto and mr and not (sal / f"{nombre}.jpg").exists():
                        subprocess.run(["scp", "-q", f"{a.remoto}:{mr.group(1).replace(chr(92), '/')}",
                                        str(sal / f"{nombre}{Path(mr.group(1).replace(chr(92), '/')).suffix}")], timeout=60)
                    elif mm and Path(mm.group(1)).exists():
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
        if remoto:
            remoto.parar()
        f.close()
        fa.close()
        if a.grabar_transmitir:
            try:
                (sal / "transmision.json").write_text(json.dumps(mcp.datos("stream_salud", {"minutos": 1}), ensure_ascii=False, indent=1))
            except Exception:
                pass
            for herr, args in (("stream_stop", {"destino": destino_prueba} if destino_prueba else {}), ("record_stop", {}),
                               ("stream_destino_borrar", {"id": destino_prueba} if destino_prueba else None)):
                if args is None:
                    continue
                try:
                    mcp.llamar(herr, args)
                except Exception as ex:
                    print(f"{herr}:", ex, file=sys.stderr)
        if por_ip0 is not None:
            try:
                mcp.llamar("settings_set", {"values": {"room.maxConexionesPorIp": por_ip0}})
                print(f"room.maxConexionesPorIp vuelve a {por_ip0} (reinicia la sala)", file=sys.stderr)
            except Exception as ex:
                print("no pude volver room.maxConexionesPorIp:", ex, file=sys.stderr)
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
    if a.remoto:
        logs.mkdir(exist_ok=True)
        for n in marcas:
            subprocess.run(["scp", "-q", f"{a.remoto}:{(a.remoto_logs + chr(92) + n).replace(chr(92), '/')}", str(logs / n)], timeout=300)
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

    if a.remoto:
        ncpu = int(ssh_ps(a.remoto, "(Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors").strip() or ncpu)
    resumen = resumir(a, muestras, app_info, lineas, corte, estable_t, ncpu, t0, hdir)
    if a.remoto:
        # Los registros traídos ocupan lugar y ya están en registro.txt (lo de la prueba).
        for n in marcas:
            try:
                (logs / n).unlink()
            except OSError:
                pass
    resumen["grilla_fps"] = resumir_fps(fps_grilla)
    resumir_f0(resumen, a, muestras, lineas, extras, hdir, remoto, estable_t, app_info)
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
    r = {"participantes": a.participantes, "nucleos": ncpu, "corte": corte, "remoto": a.remoto,
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
        idw = [sum(v["idlew"] for k, v in m["contexto"].items() if k.startswith(nombre) and "idlew" in v) for m in reg]
        if not any("idlew" in v for m in reg for k, v in m["contexto"].items() if k.startswith(nombre)):
            idw = []  # en remoto (Windows) no hay despertares
        procs[nombre] = {"cpu_pct_regimen": resumen_serie(cpu, pico=80 * ncpu),  # % de un núcleo: pico > 80 % de todos
                         "cpu_pct_toda": resumen_serie(todos),
                         "rss_mb_regimen": resumen_serie(rss),
                         "hilos": {"inicio": hil[0] if hil else None, "max": max(hil) if hil else None, "fin": hil[-1] if hil else None},
                         "cambios_contexto_s": resumen_serie(csw), "despertares_s": resumen_serie(idw)}
    r["procesos"] = procs
    # Toda la máquina: picos > 80 % de los núcleos disponibles.
    tot = [100 - (m["cpu_maquina"] or {}).get("idle", 100) if m.get("cpu_maquina") else None for m in muestras]
    r["cpu_maquina_pct"] = resumen_serie(tot, pico=80)
    if a.remoto:
        # Los motores que no se usaron (todo en 0) no entran: en una Radeon son una docena.
        claves = sorted({k for m in reg for k, v in (m.get("gpu") or {}).items() if v})
        r["gpu"] = {k: resumen_serie([(m.get("gpu") or {}).get(k) for m in reg]) for k in claves}
        r["gpu"]["nota"] = ("muestrear-win.ps1: GPU Engine por proceso (ba-*), el motor más ocupado de cada tipo; "
                            "total_3d_pct es de todos los procesos")
        gen = [100 - (m["cpu_generador"] or {}).get("idle", 100) if m.get("cpu_generador") else None for m in muestras]
        r["cpu_generador_pct"] = resumen_serie(gen)
        r["memoria_remota"] = {"disponible_mb_min": min(((m.get("memoria_remota") or {}).get("disponible_mb") for m in muestras
                                                          if (m.get("memoria_remota") or {}).get("disponible_mb") is not None), default=None),
                               "comprometida_mb_max": max(((m.get("memoria_remota") or {}).get("comprometida_mb") or 0 for m in muestras), default=None)}
        r["red_remota_mbps"] = {"entra": resumen_serie([(m.get("red_remota") or {}).get("entra_mbps") for m in reg]),
                                "sale": resumen_serie([(m.get("red_remota") or {}).get("sale_mbps") for m in reg])}
        priv = [e.get("privados_mb") for m in muestras for k, e in m["procesos"].items() if k.startswith("ba-app:") and e.get("privados_mb")]
        r["privados_app_mb_max"] = max(priv) if priv else None
    else:
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
        # El audio de los invitados (estudio_latencia, la última del régimen): lo viejo que llega
        # a la mezcla (la edad real: con la demora propia sumada) y cuántos van con demora propia.
        ents = [e for e in (app_info[-1].get("latencia") or {}).get("entradas", [])
                if e.get("audio") and not e.get("video") and str(e.get("entrada", "")).startswith("Invitado")]
        if ents:
            r["audio_invitados"] = {"entradas": len(ents),
                                    "edad_p50_ms": resumen_serie([e.get("edad_p50_ms", 0) for e in ents]),
                                    "edad_p95_ms": resumen_serie([e.get("edad_p95_ms", 0) for e in ents]),
                                    "con_demora_propia": sum(1 for e in ents if e.get("retraso_propio_ms", 0) > 0)}
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


def resumir_fps(fps):
    """Lo de grilla_personas_medir por grilla y por lugar (entrada, programa): el mosaico, y
    las celdas por capa (cps p50 y p5, % por debajo de lo que manda su capa, intervalo p95)."""
    if not fps:
        return None
    if "error" in fps:
        return {"error": fps["error"]}
    out = []
    for g in (fps.get("r") or {}).get("grillas", []):
        x = {"fuente": g.get("fuente"), "al_aire": g.get("al_aire")}
        if g.get("error"):
            x["error"] = g["error"]
        for lugar in ("entrada", "programa"):
            m = g.get(lugar)
            if not m:
                continue
            y = {"mosaico_cps": m.get("cps"), "llegada_ms": m.get("llegada_ms"), "perdidos": m.get("perdidos"),
                 "celdas": (m.get("resumen") or {}).get("celdas"), "cps_p50": (m.get("resumen") or {}).get("cps_p50"),
                 "cps_p5": (m.get("resumen") or {}).get("cps_p5"), "por_debajo_pct": (m.get("resumen") or {}).get("por_debajo_pct"),
                 # Las celdas que cambiaron de lugar o de capa mientras se medía (AnalisisCeldas::cambiar)
                 # se miden por tramos; los de menos de 2 s no entran.
                 "tramos_cortos": (m.get("resumen") or {}).get("tramos_cortos")}
            capas = {}
            for c in m.get("celdas", []):
                capas.setdefault(c.get("capa") or "?", []).append(c)
            y["por_capa"] = {k: {"n": len(v), "referencia_cps": v[0].get("referencia_cps"),
                                 "cps_p50": pct([c.get("cps") for c in v], 50), "cps_p5": pct([c.get("cps") for c in v], 5),
                                 "intervalo_p95_ms_p50": pct([(c.get("intervalo_ms") or {}).get("p95") for c in v], 50),
                                 "intervalo_max_ms": max(((c.get("intervalo_ms") or {}).get("max") or 0 for c in v), default=None),
                                 "por_debajo_pct": round(100 * sum(1 for c in v if c.get("por_debajo")) / len(v), 1)}
                            for k, v in capas.items()}
            x[lugar] = y
        out.append(x)
    return out


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
    if r.get("remoto"):
        l.append(f"- remoto {r['remoto']}: CPU de esta máquina (hinchada) {r.get('cpu_generador_pct')}; memoria remota {r.get('memoria_remota')}; "
                 f"privados de ba-app máx {r.get('privados_app_mb_max')} MB; red de los adaptadores remotos (Mbps) {r.get('red_remota_mbps')}")
    l.append(f"- hilos de ba-app: {r.get('hilos_app')}; del gateway: {r.get('hilos_gateway')}")
    l.append(f"- red (Mbps): {r.get('red_mbps')}")
    l.append(f"- sala: {r.get('sala')}")
    l.append(f"- grilla: {r.get('grilla')}")
    for g in r.get("grilla_fps") or []:
        if not isinstance(g, dict):
            continue
        for lugar in ("entrada", "programa"):
            y = g.get(lugar)
            if y:
                l.append(f"- grilla «{g.get('fuente')}» en la {lugar}: mosaico {y.get('mosaico_cps')} cps (perdidos {y.get('perdidos')}, "
                         f"llegada {y.get('llegada_ms')}); por celda cps p50 {y.get('cps_p50')} p5 {y.get('cps_p5')}, "
                         f"{y.get('por_debajo_pct')} % por debajo de su capa; por capa {y.get('por_capa')}")
    if isinstance(r.get("grilla_fps"), dict):
        l.append(f"- grilla por celda: {r['grilla_fps']}")
    l.append(f"- audio de los invitados (edad al llegar a la mezcla, entre invitados): {r.get('audio_invitados')}")
    l.append(f"- registro de la app: {r.get('registro_app')}")
    l.append(f"- conectar: {r.get('conectar')}; hinchada (unirse, ms): {r.get('hinchada_union_ms')}")
    return "\n".join(l) + "\n" + en_texto_f0(r)


# --------------------------------------------------------------------------- F0 (PLAN-GATEWAY-FRAGMENTOS)
def _prom(xs):
    xs = [x for x in xs if isinstance(x, (int, float))]
    return round(sum(xs) / len(xs), 2) if xs else None


def resumir_f0(r, a, muestras, lineas, extras, hdir, remoto, estable_t, app_info=()):
    """Lo que agrega el F0: fronteras, hilos por nombre, núcleos, el lado del invitado, la ráfaga,
    la palabra, la tendencia (resistencia) y lo de grabar y transmitir."""
    reg = [m for m in muestras if estable_t not in (None, -1) and m["s"] >= estable_t] or muestras[len(muestras) // 2:]
    r["camino"] = a.camino
    # Las fronteras entre hilos del gateway (gateway_estado cada 2 s): p50 en el régimen.
    porf = {}
    for m in reg:
        for f in ((m.get("gateway") or {}).get("fronteras") or []):
            porf.setdefault(f["id"], []).append(f)
    fr = []
    for fid, xs in porf.items():
        e = {"id": fid, "numero": xs[-1].get("numero"), "descripcion": xs[-1].get("descripcion")}
        for k in ("pases_s", "despertadas_s", "pases_por_despertada", "kbytes_s", "trabajo_us_pase", "trabajo_pct_nucleo", "espera_us"):
            e[k] = pct([x.get(k) for x in xs], 50)
        for k in ("trabajo_max_us", "espera_max_us"):
            vs = [x.get(k) for x in xs if isinstance(x.get(k), (int, float))]
            e[k] = max(vs) if vs else None
        fr.append(e)
    r["fronteras"] = sorted(fr, key=lambda e: (e["numero"] or 1000, e["id"]))
    cod = [((m.get("gateway") or {}).get("codecs")) for m in reg if (m.get("gateway") or {}).get("codecs")]
    if cod:
        r["codecs_gateway"] = cod[-1]
    # Los hilos por nombre: en Windows de muestrear-win.ps1 (cada muestra); en la Mac de xctrace.
    hil = {}
    if a.remoto:
        acc = {}
        for m in reg:
            for proc, d in (m.get("hilos_nombre") or {}).items():
                base = proc.split(":")[0]
                for nombre, v in d.items():
                    x = acc.setdefault(base, {}).setdefault(nombre, {"cpu": [], "csw": [], "n": 0})
                    x["cpu"].append(v[0])
                    x["csw"].append(v[1])
                    x["n"] = max(x["n"], v[2])
        for base, d in acc.items():
            filas = [{"nombre": k, "n": v["n"], "cpu_pct": _prom(v["cpu"]), "cambios_s": _prom(v["csw"])} for k, v in d.items()]
            hil[base] = {"mas_cpu": sorted(filas, key=lambda x: -(x["cpu_pct"] or 0))[:10],
                         "mas_cambios": sorted(filas, key=lambda x: -(x["cambios_s"] or 0))[:10]}
    for proc, x in (extras.get("xctrace") or {}).items():
        if ":" in proc or "hilos" not in x:
            continue
        hil[proc] = {"mas_cpu": x["hilos"][:10], "mas_cambios": sorted(x["hilos"], key=lambda h: -h["cambios_s"])[:10],
                     "nucleos": x.get("nucleos"), "funciones": (x.get("funciones") or [])[:10]}
    if hil:
        r["hilos_por_nombre"] = hil
    elif not a.remoto:
        r["hilos_por_nombre"] = "en la Mac los hilos por nombre salen con --xctrace S"
    if any(":contadores" in k for k in (extras.get("xctrace") or {})):
        r["contadores_cpu"] = {k: v for k, v in extras["xctrace"].items() if ":contadores" in k}
    # La carga de cada núcleo lógico (promedio del régimen).
    nuc = [m.get("nucleos") for m in reg if m.get("nucleos")]
    if nuc:
        cols = ["ocupado_pct", "dpc_pct", "irq_pct", "interrupciones_s", "dpcs_s", "mhz"] if len(nuc[-1][0]) >= 6 else ["ocupado_pct", "sistema_pct"]
        r["nucleos"] = [{c: _prom([n[i][j] for n in nuc if i < len(n)]) for j, c in enumerate(cols)} for i in range(len(nuc[-1]))]
        if not a.remoto:
            r["nucleos_nota"] = "Mac: ocupado y sistema por núcleo (host_processor_info); interrupciones por núcleo solo con sudo (powermetrics)"
    if remoto is not None and remoto.placa:
        r["placa_de_red"] = remoto.placa
    if extras.get("etw"):
        e = extras["etw"]
        r["etw"] = {"segundos": e.get("segundos"), "error": e.get("error"),
                    "nucleos": [{"nucleo": n.get("nucleo"), "ocupado_pct": n.get("ocupado_pct"), "dpc_pct": (n.get("dpc") or {}).get("pct"),
                                 "isr_pct": (n.get("isr") or {}).get("pct"), "isr_vectores": (n.get("isr") or {}).get("vectores"),
                                 "hilos": [f"{h['proceso']}/{h['nombre']} {h['pct']}%" for h in (n.get("hilos") or [])[:4]]}
                                for n in (e.get("nucleos") or [])],
                    "hilos": (e.get("hilos") or [])[:15]}
    # El lado del invitado (hinchada): lo que recibe de vuelta (el retorno) y por dónde va.
    try:
        tot = []
        t0 = reg[0]["t"] if reg else 0
        ult = None
        with open(hdir / "muestras.jsonl") as h:
            for l in h:
                try:
                    o = json.loads(l)
                except Exception:
                    continue
                ult = o
                tt = o.get("t", 0)
                tt = tt / 1000 if tt > 1e11 else tt
                if "totales" in o and tt >= t0:
                    tot.append(o["totales"])
        if tot:
            u = tot[-1]
            ret = lambda t: t.get("retorno") or {}
            ve = lambda t: max(1, t.get("videoEntradas", 0))
            pc = ret(u)
            r["invitado"] = {
                "retorno_cps_medio": _prom([t.get("fpsRecibidos", 0) / ve(t) for t in tot]),
                "retorno_cps_decodificados": _prom([t.get("fpsDecodificados", 0) / ve(t) for t in tot]),
                "retorno_cps_min": min((ret(t).get("fpsMin", -1) for t in tot if ret(t).get("fpsMin", -1) >= 0), default=None),
                "congelamientos": sum(t.get("congelamientos", 0) for t in tot),
                "resoluciones": pc.get("resoluciones"),
                "rtt_ms": _prom([t["rttSuma"] / t["rttN"] for t in tot if t.get("rttN")]),
                "rtt_max_ms": max((t.get("rttMax", 0) for t in tot), default=None),
                "jitter_ms": _prom([t["jitterSuma"] / t["jitterN"] for t in tot if t.get("jitterN")]),
                "perdida_pct": round(100 * sum(t.get("paquetesPerdidos", 0) for t in tot) /
                                     max(1, sum(t.get("paquetesRecibidos", 0) + t.get("paquetesPerdidos", 0) for t in tot)), 3),
                "nack_s": round(sum(ret(t).get("nack", 0) for t in tot) / len(tot), 1),
                "pli_s": round(sum(ret(t).get("pli", 0) for t in tot) / len(tot), 2),
                "jitter_buffer_ms": _prom([ret(t)["bufferMsSuma"] / ret(t)["bufferN"] for t in tot if ret(t).get("bufferN")]),
                "bajada_kbps_por_invitado": _prom([t.get("bajadaKbps", 0) / max(1, t.get("conectados", 0)) for t in tot]),
                "subida_kbps_por_invitado": _prom([t.get("subidaKbps", 0) / max(1, t.get("conectados", 0)) for t in tot]),
                "caminos": u.get("caminos"), "codecs_subida": u.get("codecsSubida"), "codecs_bajada": u.get("codecsBajada"),
                "primer_cuadro_ms_medio": round(pc["primerCuadroMsSuma"] / pc["primerCuadroN"]) if pc.get("primerCuadroN") else None,
                "primer_cuadro_ms_max": pc.get("primerCuadroMsMax"),
                "nota": "BWE: la de subida de cada invitado (bweSalidaKbps); la de bajada la calcula el gateway "
                        "(libwebrtc no la da del lado del receptor)"}
            # La demora de punta a punta, por tramos (todo medido, nada supuesto):
            #   subida: la edad del audio del invitado al llegar a la mezcla (estudio_latencia, la app);
            #   estudio: la latencia fijada de las salidas (estudio_latencia.latencia_ms);
            #   retorno: de la captura en el gateway a lo que suena/se ve en el invitado
            #            (estimatedPlayoutTimestamp contra el reloj de acá: vale en la misma máquina
            #            o con los relojes sincronizados; en remoto, la diferencia de relojes entra).
            ea = [ret(t)["edadAudioMsSuma"] / ret(t)["edadAudioN"] for t in tot if ret(t).get("edadAudioN")]
            ev = [ret(t)["edadVideoMsSuma"] / ret(t)["edadVideoN"] for t in tot if ret(t).get("edadVideoN")]
            sub = ((r.get("audio_invitados") or {}).get("edad_p50_ms") or {}).get("p50")
            lat = [(x.get("latencia") or {}).get("latencia_ms") for x in app_info if isinstance(x.get("latencia"), dict)]
            lat = [x for x in lat if isinstance(x, (int, float))]
            r["demora"] = {"subida_audio_ms_p50": sub, "estudio_ms": lat[-1] if lat else None,
                           "retorno_audio_ms": _prom(ea), "retorno_video_ms": _prom(ev),
                           "retorno_audio_ms_max": pc.get("edadAudioMsMax"), "retorno_video_ms_max": pc.get("edadVideoMsMax")}
            partes = [r["demora"][k] for k in ("subida_audio_ms_p50", "estudio_ms", "retorno_audio_ms")]
            r["demora"]["audio_punta_a_punta_ms"] = round(sum(partes), 1) if all(isinstance(x, (int, float)) for x in partes) else None
            if a.remoto:
                r["demora"]["nota"] = "en remoto, el retorno compara relojes de dos máquinas: sincronizarlos (NTP) o mirar solo la tendencia"
            bwe = [p.get("bweSalidaKbps") for p in (ult or {}).get("participantes", []) if (p.get("bweSalidaKbps") or -1) >= 0]
            if bwe:
                r["invitado"]["bwe_subida_kbps"] = resumen_serie(bwe)
    except Exception as ex:
        r["invitado"] = {"error": str(ex)}
    # La ráfaga: picos mientras entraban (hasta que todos tuvieron medios).
    if a.rafaga:
        hasta = r.get("todos_con_medios_s") or (muestras[-1]["s"] if muestras else 0)
        ent = [m for m in muestras if m["s"] <= hasta]
        pk = {}
        for n in ("ba-gateway", "ba-app"):
            pk[n] = {"cpu_pct_max": max((sum(e.get("cpu_pct") or 0 for k, e in m["procesos"].items() if k.startswith(n + ":")) for m in ent), default=None),
                     "hilos_max": max((sum(e.get("hilos", 0) for k, e in m["procesos"].items() if k.startswith(n + ":")) for m in ent), default=None)}
        r["rafaga"] = {"todos_con_medios_s": r.get("todos_con_medios_s"), "picos": pk,
                       "primer_cuadro_ms": {k: (r.get("invitado") or {}).get(k) for k in ("primer_cuadro_ms_medio", "primer_cuadro_ms_max")}}
    # La palabra: cuánto tardó la HD en cada pedido (lo mide el gateway, en su registro).
    if a.palabra:
        hd = []
        for l in lineas.get("gateway.log", []):
            x = re.search(r"HD\D+?(\d+) ms", l)
            if x:
                hd.append(int(x.group(1)))
        r["palabra"] = {"pedidos": len(extras.get("palabra_pedidos") or []), "hd_ms": resumen_serie(hd), "medidas": len(hd),
                        "error": extras.get("palabra") or extras.get("palabra_error")}
    # La tendencia (resistencia): por minuto, memoria, hilos y CPU de cada proceso.
    if a.duracion or (r.get("regimen_s") or 0) >= 600:
        t0 = muestras[0]["t"] if muestras else 0
        cub = {}
        for m in muestras:
            c = cub.setdefault(int((m["t"] - t0) // 60), {"rss": {}, "hilos": {}, "cpu": {}})
            for n in ("ba-gateway", "ba-app"):
                es = [e for kk, e in m["procesos"].items() if kk.startswith(n + ":")]
                c["rss"].setdefault(n, []).append(sum(e.get("rss_mb") or 0 for e in es))
                c["hilos"].setdefault(n, []).append(sum(e.get("hilos", 0) for e in es))
                c["cpu"].setdefault(n, []).append(sum(e.get("cpu_pct") or 0 for e in es))
            # Los faltantes de los anillos de retorno (el máximo de la ventana de 10 s que da el gateway).
            tr = ((m.get("sala") or {}).get("uso") or {}).get("transporte") or []
            fal = [x.get("faltantes", 0) for x in (tr if isinstance(tr, list) else tr.get("canales", []))
                   if str(x.get("nombre", "")).startswith("BA.Audio.Return")]
            if fal:
                c.setdefault("faltantes", []).append(sum(fal))
        for x in app_info:  # la edad del audio de los invitados al llegar a la mezcla
            ents = [e for e in (x.get("latencia") or {}).get("entradas", []) if e.get("audio") and str(e.get("entrada", "")).startswith("Invitado")]
            if ents:
                cub.setdefault(int(x["s"] // 60), {"rss": {}, "hilos": {}, "cpu": {}}).setdefault("edad", []).append(
                    _prom([e.get("edad_p50_ms") for e in ents]))
        r["tendencia"] = [{"min": k, **{f"{x}_{n}": _prom(v) for x in ("rss", "hilos", "cpu") for n, v in cub[k][x].items()},
                           "edad_audio_ms": _prom(cub[k].get("edad", [])), "faltantes_max": max(cub[k].get("faltantes", [0]))}
                          for k in sorted(cub)]
    if a.grabar_transmitir:
        try:
            r["transmision"] = json.loads((Path(a.salida).expanduser() / "transmision.json").read_text())
        except Exception:
            r["transmision"] = "sin datos (stream_salud)"


def en_texto_f0(r):
    l = [f"- camino: {r.get('camino')}"]
    if r.get("fronteras"):
        l += ["", "## Fronteras entre hilos (gateway, p50 del régimen)", "",
              "| # | frontera | pases/s | despertadas/s | pases/despertada | kB/s | trabajo µs/pase | % núcleo | espera µs | espera máx µs |",
              "|---|---|---|---|---|---|---|---|---|---|"]
        for f in r["fronteras"]:
            l.append(f"| {f.get('numero') or ''} | {f['id']} ({f.get('descripcion')}) | {f.get('pases_s')} | {f.get('despertadas_s')} | "
                     f"{f.get('pases_por_despertada')} | {f.get('kbytes_s')} | {f.get('trabajo_us_pase')} | {f.get('trabajo_pct_nucleo')} | "
                     f"{f.get('espera_us')} | {f.get('espera_max_us')} |")
    h = r.get("hilos_por_nombre")
    if isinstance(h, dict):
        for proc, x in h.items():
            l += ["", f"## Hilos de {proc}", ""]
            l.append("- más CPU (% de un núcleo): " + "; ".join(f"{y['nombre']} x{y.get('n')} {y.get('cpu_pct')}" for y in x["mas_cpu"]))
            l.append("- más cambios de contexto/s: " + "; ".join(f"{y['nombre']} {y.get('cambios_s')}" for y in x["mas_cambios"]))
            if x.get("nucleos"):
                l.append(f"- en qué núcleos corre (% de cada uno): {x['nucleos']}")
            if x.get("funciones"):
                l.append("- funciones propias: " + "; ".join(f"{y['funcion'][:60]} {y['pct']}%" for y in x["funciones"][:6]))
    elif h:
        l.append(f"- hilos: {h}")
    if r.get("contadores_cpu"):
        l.append(f"- contadores del procesador: {json.dumps(r['contadores_cpu'], ensure_ascii=False)[:1500]}")
    if r.get("nucleos"):
        l += ["", "## Núcleos lógicos (promedio del régimen)", ""]
        cols = list(r["nucleos"][0].keys())
        l.append("| núcleo | " + " | ".join(cols) + " |")
        l.append("|---|" + "---|" * len(cols))
        for i, n in enumerate(r["nucleos"]):
            l.append(f"| {i} | " + " | ".join(str(n[c]) for c in cols) + " |")
        if r.get("nucleos_nota"):
            l.append(f"\n{r['nucleos_nota']}")
    if r.get("placa_de_red"):
        l.append(f"- placa de red: {json.dumps(r['placa_de_red'], ensure_ascii=False)}")
    if r.get("etw"):
        e = r["etw"]
        l += ["", f"## ETW ({e.get('segundos')} s): núcleo → hilos, DPC e ISR", ""]
        if e.get("error"):
            l.append(f"- error: {e['error']}")
        for n in e.get("nucleos") or []:
            l.append(f"- núcleo {n['nucleo']}: {n['ocupado_pct']} % ocupado, DPC {n['dpc_pct']} %, ISR {n['isr_pct']} % "
                     f"(vectores {n['isr_vectores']}); {', '.join(n['hilos'])}")
        for x in e.get("hilos") or []:
            l.append(f"- {x['proceso']}/{x['nombre']}: {x['pct']} % → {x.get('nucleos')}")
    if r.get("invitado"):
        l += ["", "## Del lado del invitado (hinchada: lo que recibe de vuelta)", ""]
        for k, v in r["invitado"].items():
            l.append(f"- {k}: {v}")
    if r.get("codecs_gateway"):
        l.append(f"- códecs del gateway (placa o software): {r['codecs_gateway']}")
    for k in ("demora", "rafaga", "palabra", "transmision"):
        if r.get(k):
            l.append(f"- {k}: {json.dumps(r[k], ensure_ascii=False)[:1500]}")
    if r.get("tendencia"):
        l += ["", "## Tendencia por minuto", ""]
        cols = list(r["tendencia"][0].keys())
        l.append("| " + " | ".join(cols) + " |")
        l.append("|" + "---|" * len(cols))
        for t in r["tendencia"]:
            l.append("| " + " | ".join(str(t.get(c)) for c in cols) + " |")
    return "\n".join(l) + "\n"


if __name__ == "__main__":
    main()
