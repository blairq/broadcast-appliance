# Banco de ataques contra ba-web (docs/funciones/BA-WEB.md, «Seguridad»).
# Uso: python3 src/web/tests/ataques.py <binario de ba-web> [--fuzz N]
# Mejor contra una versión con ASan/UBSan (ver el doc): un error de memoria aborta el worker
# y el banco lo cuenta. Levanta un gateway de mentira y prueba cada ataque; imprime una tabla.
import os, sys, socket, threading, time, subprocess, random, resource, signal, shutil

BIN = sys.argv[1]
FUZZ = int(sys.argv[sys.argv.index("--fuzz") + 1]) if "--fuzz" in sys.argv else 3000
AQUI = os.path.dirname(os.path.abspath(__file__))
D = os.path.join(os.environ.get("TMPDIR", "/tmp"), "ba-web-banco-%d" % os.getpid())
shutil.rmtree(D, ignore_errors=True)
os.makedirs(D + "/s/sub")
open(D + "/s/a.txt", "w").write("hola\n")
open(D + "/s/index.html", "w").write("<p>index</p>" * 50)
open(D + "/s/.secreto", "w").write("CLAVE-SECRETA")
os.symlink("/etc", D + "/s/afuera")
open(D + "/s/CON", "w").write("dispositivo")

def libre():
    s = socket.socket(); s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p

UP, PORT = libre(), libre()
vistos_up = []  # (primera línea, host, linea proxy)

def gateway():
    ls = socket.socket(); ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    ls.bind(("127.0.0.1", UP)); ls.listen(512)
    def atender(c):
        c.settimeout(5)
        try:
            b = b""
            while b"\r\n\r\n" not in b:
                k = c.recv(65536)
                if not k: break
                b += k
            proxy = ""
            if b.startswith(b"PROXY "):
                proxy, b = b.split(b"\r\n", 1); proxy = proxy.decode()
            linea = b.split(b"\r\n", 1)[0].decode(errors="replace")
            host = ""
            for l in b.split(b"\r\n"):
                if l.lower().startswith(b"host:"): host = l[5:].strip().decode(errors="replace")
            vistos_up.append((linea, host, proxy))
            cuerpo = b'{"ok":true}' if b"/api/" in b else b"gateway"
            if b"/lento" in b: time.sleep(4)
            c.sendall(b"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s" % (len(cuerpo), cuerpo))
        except Exception:
            pass
        c.close()
    while True:
        c, _ = ls.accept()
        threading.Thread(target=atender, args=(c,), daemon=True).start()

threading.Thread(target=gateway, daemon=True).start()

def conf(extra="", pp=" proxy_protocol"):
    return f"""worker_processes 2;
listen 127.0.0.1:{PORT}{pp};
upstream 127.0.0.1:{UP};
client_header_timeout 3s;
keepalive_timeout 5s;
{extra}
location /s/ {{ alias {D}/s/; index index.html; try_files $uri @upstream; }}
location = /api/v1/evento {{ proxy_pass; proxy_cache_valid 1s; proxy_cache_key $uri; ba_vistos v; }}
location / {{ proxy_pass; }}
"""

LOG = os.path.join(D, "banco.log")
proc = None
def arrancar(texto, nofile=None):
    global proc
    open(D + "/ba-web.conf", "w").write(texto)
    pre = None
    if nofile:
        def pre():
            resource.setrlimit(resource.RLIMIT_NOFILE, (nofile, nofile))
    env = dict(os.environ, BA_WEB_CLAVE="k", ASAN_OPTIONS="abort_on_error=0:halt_on_error=1:detect_leaks=0")
    proc = subprocess.Popen([BIN, "-c", D + "/ba-web.conf"], stdout=open(LOG, "a"), stderr=subprocess.STDOUT,
                            preexec_fn=pre, env=env)
    for _ in range(100):
        try:
            socket.create_connection(("127.0.0.1", PORT), timeout=0.2).close(); return
        except OSError:
            time.sleep(0.05)
def parar():
    proc.send_signal(signal.SIGTERM)
    try: proc.wait(8)
    except Exception: proc.kill()

def pedir(raw, timeout=5, leer_todo=True):
    s = socket.create_connection(("127.0.0.1", PORT), timeout=timeout)
    try:
        s.sendall(raw)
    except OSError:
        pass
    b = b""
    try:
        while True:
            k = s.recv(65536)
            if not k: break
            b += k
            if not leer_todo and b"\r\n\r\n" in b: break
    except (socket.timeout, ConnectionResetError):
        pass
    s.close()
    return b

def status(b):
    try: return int(b.split(b" ", 2)[1])
    except Exception: return 0

resultados = []
def res(nombre, ok, detalle=""):
    resultados.append((nombre, ok, detalle))
    print(("OK   " if ok else "MAL  ") + nombre + ("  — " + detalle if detalle else ""), flush=True)

def caidas():
    t = open(LOG, errors="replace").read()
    return t.count("se cayó") + t.count("ERROR: AddressSanitizer") + t.count("runtime error")

open(LOG, "w").close()
arrancar(conf())

# 1. Pipelining en cascada: miles de pedidos en un paquete
n = 20000
s = socket.create_connection(("127.0.0.1", PORT), timeout=10)
threading.Thread(target=lambda: s.sendall(b"GET /s/a.txt HTTP/1.1\r\nHost: x\r\n\r\n" * n), daemon=True).start()
b = b""; t0 = time.time()
try:
    while b.count(b"HTTP/1.1 200") < n and time.time() - t0 < 15:
        k = s.recv(1 << 20)
        if not k: break
        b += k
except (socket.timeout, ConnectionResetError):
    pass
s.close()
time.sleep(0.5)
res("pipelining de 20000 pedidos: sin caerse (atiende hasta keepalive_requests=1000 y cierra)", caidas() == 0 and b.count(b"HTTP/1.1 200") >= 990, f"{b.count(b'HTTP/1.1 200')} respuestas, caídas={caidas()}")

# 2. Slowloris: un byte por segundo de cabecera (client_header_timeout 3s)
s = socket.create_connection(("127.0.0.1", PORT), timeout=1)
s.sendall(b"GET /s/a.txt HTTP/1.1\r\n")
t0 = time.time(); cerrada = False
while time.time() - t0 < 10:
    try:
        s.sendall(b"X")
        k = s.recv(10) if False else None
    except OSError:
        cerrada = True; break
    try:
        s.settimeout(1.0)
        if s.recv(100) == b"": cerrada = True; break
    except socket.timeout:
        pass
    except OSError:
        cerrada = True; break
dur = time.time() - t0
res("slowloris: cierra al vencer la cabecera (3 s)", cerrada and dur < 5.5, f"cerrada={cerrada} a los {dur:.1f} s")

# 3. LF solos (diferencia con el gateway = contrabando)
b = pedir(b"POST /x HTTP/1.1\nHost: x\nContent-Length: 5\n\nhello")
res("LF solos: 400", status(b) == 400, f"status {status(b)}")
b = pedir(b"GET /s/a.txt HTTP/1.1\r\nHost: x\rX: y\r\n\r\n")
res("CR suelto: 400", status(b) == 400, f"status {status(b)}")
b = pedir(b"GET /s/a.txt HTTP/1.1\r\nHost: x\r\nX: a\x01b\r\n\r\n")
res("control en un valor: 400", status(b) == 400, f"status {status(b)}")
b = pedir(b"GET /s/a\x01.txt HTTP/1.1\r\nHost: x\r\n\r\n")
res("control en la ruta: 400", status(b) == 400, f"status {status(b)}")
b = pedir(b"GET /s/sub%0d%0aSet-Cookie:%20x=1 HTTP/1.1\r\nHost: x\r\n\r\n")
res("CRLF codificado en la ruta: sin cabecera inyectada", b"Set-Cookie" not in b.split(b"\r\n\r\n")[0], f"status {status(b)}")

# 4. Archivos: ocultos, enlaces que salen, nombres de Windows
b = pedir(b"GET /s/.secreto HTTP/1.1\r\nHost: x\r\n\r\n")
res("archivo oculto: no se sirve", b"CLAVE-SECRETA" not in b, f"status {status(b)}")
b = pedir(b"GET /s/afuera/passwd HTTP/1.1\r\nHost: x\r\n\r\n")
res("enlace que sale de la raíz: no se sirve", b"root:" not in b, f"status {status(b)}")
b = pedir(b"GET /s/CON HTTP/1.1\r\nHost: x\r\n\r\n")
res("nombre reservado de Windows (CON): no se sirve", b"dispositivo" not in b, f"status {status(b)}")
b = pedir(b"GET /s/a.txt::$DATA HTTP/1.1\r\nHost: x\r\n\r\n")
res("flujo alternativo de NTFS (::$DATA): no se sirve como archivo", b"hola" not in b, f"status {status(b)}")
b = pedir(b"GET /s/%2e%2e/%2e%2e/%2e%2e/etc/passwd HTTP/1.1\r\nHost: x\r\n\r\n")
res("traversal codificado: 400", status(b) == 400, f"status {status(b)}")
b = pedir(b"GET /s/..%2f..%2f..%2fetc%2fpasswd HTTP/1.1\r\nHost: x\r\n\r\n")
res("traversal con %2f: 400", status(b) == 400, f"status {status(b)}")
b = pedir(b"GET /s/sub HTTP/1.1\r\nHost: x\r\n\r\n")
loc = [l for l in b.split(b"\r\n") if l.lower().startswith(b"location:")]
res("redirección del directorio: relativa", loc == [b"Location: /s/sub/"], str(loc))
b = pedir(b"GET //evil.com/ HTTP/1.1\r\nHost: x\r\n\r\n")
res("//evil.com: no redirige afuera", b"Location: //" not in b, f"status {status(b)}")

# 5. Tamaños
b = pedir(b"GET /s/a.txt HTTP/1.1\r\nHost: x\r\nX: " + b"a" * 70000 + b"\r\n\r\n")
res("cabecera de 70 KB: 431", status(b) == 431, f"status {status(b)}")
b = pedir(b"GET /" + b"a" * 9000 + b" HTTP/1.1\r\nHost: x\r\n\r\n")
res("línea de pedido de 9 KB: 414", status(b) == 414, f"status {status(b)}")
b = pedir(b"GET /s/a.txt HTTP/1.1\r\nHost: x\r\nContent-Length: 99999999999999999999999\r\n\r\n")
res("Content-Length gigante: 400", status(b) == 400, f"status {status(b)}")
b = pedir(b"GET /s/a.txt HTTP/1.1\r\nHost: x\r\nRange: bytes=0-1,1-2,2-3,3-4,4-5\r\n\r\n")
res("muchos rangos: sin multipart (200 entero)", status(b) == 200, f"status {status(b)}")

# 6. Métodos que no van
b = pedir(b"TRACE / HTTP/1.1\r\nHost: x\r\n\r\n")
res("TRACE: 405 (no llega al gateway)", status(b) == 405, f"status {status(b)}")
b = pedir(b"CONNECT evil.com:443 HTTP/1.1\r\nHost: evil.com:443\r\n\r\n")
res("CONNECT: rechazado", status(b) in (400, 405), f"status {status(b)}")

# 7. Envenenar el microcaché con el Host
vistos_up.clear()
time.sleep(1.2)
pedir(b"GET /api/v1/evento HTTP/1.1\r\nHost: evil.example\r\nConnection: close\r\n\r\n")
time.sleep(0.2)
hosts = [h for (l, h, p) in vistos_up if "/api/v1/evento" in l]
res("microcaché: el Host del visitante no llega al gateway", hosts and all(h != "evil.example" for h in hosts), str(hosts))

# 8. La IP: PROXY falso cuando el túnel no manda PROXY, y X-Forwarded-For
parar(); arrancar(conf(pp=" real_ip_xff"))
vistos_up.clear()
pedir(b"PROXY TCP4 6.6.6.6 127.0.0.1 1 2\r\nGET /x HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
time.sleep(0.2)
res("PROXY falso (escucha sin proxy_protocol): no llega al gateway", not any("6.6.6.6" in p for (l, h, p) in vistos_up), str(vistos_up[-1:]))
vistos_up.clear()
pedir(b"GET /x HTTP/1.1\r\nHost: x\r\nX-Forwarded-For: 1.1.1.1, 203.0.113.9\r\nConnection: close\r\n\r\n")
time.sleep(0.2)
res("X-Forwarded-For: al gateway va la IP real (la última) en PROXY", any("203.0.113.9" in p for (l, h, p) in vistos_up), str(vistos_up[-1:]))

# 9. Límites por IP (todas las conexiones con la misma IP real)
parar(); arrancar(conf(extra="limit_conn_per_ip 50;\nlimit_upstream_rate 10 20;\n"))
socks = []
for i in range(120):
    try:
        s = socket.create_connection(("127.0.0.1", PORT), timeout=2)
        s.sendall(b"PROXY TCP4 198.51.100.1 127.0.0.1 1 2\r\nGET /s/a.txt HTTP/1.1\r\nHost: x\r\n\r\n")
        socks.append(s)
    except OSError:
        break
time.sleep(0.5)
rechazadas = 0
for s in socks:
    try:
        b = s.recv(4096)
        if status(b) in (429, 503) or b == b"": rechazadas += 1
    except Exception:
        rechazadas += 1
for s in socks: s.close()
res("límite de conexiones por IP (50, x2 workers)", rechazadas >= 120 - 100, f"{rechazadas} de 120 rechazadas")
dos = 0
for i in range(80):
    b = pedir(b"PROXY TCP4 198.51.100.2 127.0.0.1 1 2\r\nGET /gw%d HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n" % i)
    if status(b) == 429: dos += 1
res("límite de pedidos al gateway por IP", dos > 0, f"{dos} de 80 con 429")
ok_otro = status(pedir(b"PROXY TCP4 198.51.100.3 127.0.0.1 1 2\r\nGET /gw HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")) == 200
res("otra IP no se ve afectada", ok_otro)

# 10. Sin descriptores (ulimit -n 64): que no gire al 100 %
parar(); arrancar(conf(), nofile=64)
socks = []
for i in range(200):
    try:
        socks.append(socket.create_connection(("127.0.0.1", PORT), timeout=1))
    except OSError:
        break
time.sleep(0.5)
hijos = subprocess.run(["pgrep", "-P", str(proc.pid)], capture_output=True, text=True).stdout.split()
cpu = subprocess.run(["ps", "-o", "%cpu=", "-p", ",".join(hijos + [str(proc.pid)])], capture_output=True, text=True).stdout.split()
time.sleep(2)
cpu = subprocess.run(["ps", "-o", "%cpu=", "-p", ",".join(hijos + [str(proc.pid)])], capture_output=True, text=True).stdout.split()
tot = sum(float(x.replace(',', '.')) for x in cpu) if cpu else -1
for s in socks: s.close()
time.sleep(0.5)
b = pedir(b"GET /s/a.txt HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
res("sin descriptores: no gira al 100 % y se recupera", tot < 50 and status(b) == 200, f"CPU {tot:.0f}% con {len(socks)} conexiones; después: {status(b)}")

# 11. Fuzz: pedidos rotos al azar (ASan mira la memoria)
parar(); arrancar(conf())
semillas = [b"GET /s/a.txt HTTP/1.1\r\nHost: x\r\nAccept-Encoding: gzip;q=0.5, *\r\nRange: bytes=0-3\r\nIf-None-Match: W/\"a\", \"b\"\r\n\r\n",
            b"PROXY TCP4 1.2.3.4 127.0.0.1 1 2\r\nGET /api/v1/evento?v=abc HTTP/1.1\r\nHost: x\r\n\r\n",
            b"POST /x HTTP/1.1\r\nHost: x\r\nContent-Length: 4\r\n\r\nabcd",
            b"GET /ws HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n\r\n",
            b"HEAD /s/ HTTP/1.0\r\nIf-Modified-Since: Sun, 06 Nov 1994 08:49:37 GMT\r\n\r\n"]
random.seed(7)
alfabeto = b"\r\n :/%.;,=\"\x00\xff-*?#&GETPOSTHTPXYabcz0129"
def un_fuzz(semilla):
    rnd = random.Random(semilla)
    x = bytearray(rnd.choice(semillas))
    for _ in range(rnd.randint(1, 8)):
        op = rnd.random(); p = rnd.randrange(len(x) + 1)
        if op < 0.4 and len(x) > 1: del x[p:p + rnd.randint(1, 4)]
        elif op < 0.8: x[p:p] = bytes(rnd.choice(alfabeto) for _ in range(rnd.randint(1, 6)))
        else: x[p:p] = x[rnd.randrange(len(x)):][:rnd.randint(1, 40)]
    try:
        s = socket.create_connection(("127.0.0.1", PORT), timeout=2)
        s.sendall(bytes(x))
        s.shutdown(socket.SHUT_WR)  # que el servidor vea el fin enseguida
        try:
            while s.recv(65536): pass
        except Exception: pass
        s.close()
    except OSError:
        pass
from concurrent.futures import ThreadPoolExecutor
with ThreadPoolExecutor(8) as ex:
    list(ex.map(un_fuzz, range(FUZZ)))
time.sleep(0.5)
vivo = status(pedir(b"GET /s/a.txt HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")) == 200
res(f"fuzz de {FUZZ} pedidos rotos: sin caídas ni errores de memoria", caidas() == 0 and vivo, f"caídas/ASan={caidas()}")

parar()
mal = [r for r in resultados if not r[1]]
print(f"\n{len(resultados) - len(mal)} de {len(resultados)} bien")
