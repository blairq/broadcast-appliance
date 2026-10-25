"""Ejemplo: un equipo que se maneja por la red con líneas de texto.

Casi todo aparato de un estudio (dimmers, matrices de video, relés, tableros, sensores, centrales)
trae en el manual un «protocolo TCP» o «RS-232 / Telnet»: órdenes de texto, una por línea. Este
ejemplo es el molde para cualquiera de ellos:

  - una conexión en su hilo, con timeout en todo y reconexión con espera creciente (si el equipo se
    apaga, el plugin espera y vuelve solo; nunca traba nada);
  - órdenes con respuesta (``Equipo.pedir``) y avisos que el equipo manda solo (``BOTON 1``);
  - estado que se consulta cada tanto y se muestra en el panel del operador;
  - lo que pasa en el equipo como EVENTO (``plugin.evento``: lo escuchan otros plugins, los
    disparadores y el asistente) y como ACCIÓN del sistema (``al_boton`` → ``plugin.llamar``);
  - un SIMULADOR del aparato adentro del plugin (``modo: simulador``), que habla el mismo protocolo
    por un puerto local: el mismo código se prueba sin el equipo.

El protocolo de este aparato de mentira (cambialo por el del manual del tuyo):
    al conectar, el equipo:  HOLA <modelo>
    ESTADO?              ->  ESTADO luz1=<0..100> luz2=<0..100> temp=<grados>
    LUZ <n> <0..100>     ->  OK LUZ <n> <valor>          (o ERROR <motivo>)
    el equipo, solo:         BOTON <n>                   (alguien apretó un botón del frente)
"""
import random
import socket
import threading
import time

import ba_plugin as ba

plugin = ba.Plugin()


# --- El aparato -------------------------------------------------------------------------------

class Equipo:
    """La conexión con el aparato: una línea por orden, todo con timeout."""

    def __init__(self, host: str, puerto: int):
        self.host, self.puerto = host, puerto
        self.s: socket.socket | None = None
        self.buf = b""
        self.lock = threading.Lock()   # una orden a la vez
        self.respuestas: list[str] = []
        self.hay_respuesta = threading.Condition()
        self.modelo = ""

    def conectar(self) -> None:
        s = socket.create_connection((self.host, self.puerto), timeout=3)
        s.settimeout(0.5)
        self.s, self.buf = s, b""
        self.respuestas.clear()

    def cerrar(self) -> None:
        if self.s is not None:
            try:
                self.s.close()
            except OSError:
                pass
        self.s = None

    def leer_linea(self) -> str | None:
        """Una línea del equipo; None si en medio segundo no llegó nada. Tira OSError si se cortó."""
        while b"\n" not in self.buf:
            try:
                d = self.s.recv(4096)
            except socket.timeout:
                return None
            if not d:
                raise OSError("el equipo cerró la conexión")
            self.buf += d
            if len(self.buf) > 65536:  # un equipo que manda basura sin fin de línea
                raise OSError("línea demasiado larga")
        linea, self.buf = self.buf.split(b"\n", 1)
        return linea.decode("utf-8", "replace").strip()

    def pedir(self, orden: str, espera_s: float = 2.0) -> str:
        """Manda una orden y espera su respuesta (la lee el hilo de la conexión)."""
        with self.lock:
            if self.s is None:
                raise RuntimeError("sin conexión con el equipo")
            with self.hay_respuesta:
                self.respuestas.clear()
            self.s.sendall((orden + "\n").encode())
            with self.hay_respuesta:
                if not self.hay_respuesta.wait_for(lambda: self.respuestas, timeout=espera_s):
                    raise RuntimeError(f"el equipo no contestó «{orden}» en {espera_s} s")
                r = self.respuestas.pop(0)
        if r.startswith("ERROR"):
            raise RuntimeError(f"el equipo dijo: {r}")
        return r

    def respuesta(self, linea: str) -> None:
        with self.hay_respuesta:
            self.respuestas.append(linea)
            self.hay_respuesta.notify_all()


# --- El simulador: un aparato de mentira en un puerto local -----------------------------------

def simulador() -> int:
    """Arranca el aparato de mentira (en 127.0.0.1, un puerto libre) y devuelve el puerto."""
    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", 0))
    srv.listen(1)
    srv.settimeout(0.5)
    luces = {1: 0, 2: 0}

    def atender(c: socket.socket):
        c.settimeout(0.5)
        c.sendall(b"HOLA Simulador-2L\n")
        buf, proximo_boton = b"", time.monotonic() + random.uniform(8, 15)
        while plugin.andando:
            if time.monotonic() > proximo_boton:  # cada tanto «alguien aprieta» un botón
                c.sendall(f"BOTON {random.choice([1, 2])}\n".encode())
                proximo_boton = time.monotonic() + random.uniform(8, 15)
            try:
                d = c.recv(4096)
            except socket.timeout:
                continue
            except OSError:
                return
            if not d:
                return
            buf += d
            while b"\n" in buf:
                linea, buf = buf.split(b"\n", 1)
                p = linea.decode().split()
                if p == ["ESTADO?"]:
                    r = f"ESTADO luz1={luces[1]} luz2={luces[2]} temp={22 + random.random() * 2:.1f}"
                elif len(p) == 3 and p[0] == "LUZ" and p[1] in ("1", "2") and p[2].isdigit() and int(p[2]) <= 100:
                    luces[int(p[1])] = int(p[2])
                    r = f"OK LUZ {p[1]} {p[2]}"
                else:
                    r = "ERROR orden desconocida"
                c.sendall((r + "\n").encode())

    def aceptar():
        while plugin.andando:
            try:
                c, _ = srv.accept()
            except socket.timeout:
                continue
            except OSError:
                return
            threading.Thread(target=atender, args=(c,), daemon=True).start()
    threading.Thread(target=aceptar, name="simulador", daemon=True).start()
    return srv.getsockname()[1]


# --- La conexión, en su hilo ------------------------------------------------------------------
equipo: Equipo | None = None
_puerto_simulador = 0


def destino() -> tuple[str, int]:
    global _puerto_simulador
    if plugin.config.get("modo") == "simulador":
        if not _puerto_simulador:
            _puerto_simulador = simulador()
        return "127.0.0.1", _puerto_simulador
    return str(plugin.config.get("host", "")), int(plugin.config.get("puerto", 4001))


def al_llegar(linea: str) -> None:
    """Lo que manda el equipo: respuestas a una orden o avisos que manda solo."""
    if linea.startswith("HOLA"):
        equipo.modelo = linea[5:]
    elif linea.startswith("BOTON"):
        n = linea.split()[1] if len(linea.split()) > 1 else "?"
        plugin.evento("boton", {"boton": n})
        plugin.mostrar("ultimo_boton", f"{n} ({time.strftime('%H:%M:%S')})")
        accion = (plugin.config.get("al_boton") or {}).get(n)
        if accion:  # en otro hilo: llamar al sistema puede tardar y acá se está leyendo el equipo
            threading.Thread(target=hacer, args=(accion,), daemon=True).start()
    else:
        equipo.respuesta(linea)


def hacer(accion: dict) -> None:
    try:
        plugin.llamar(accion["herramienta"], accion.get("args") or {}, timeout=15)
    except (RuntimeError, KeyError) as e:
        plugin.log("la acción del botón falló:", e)


@plugin.al_iniciar
def conexion():
    global equipo
    espera = 1.0
    while plugin.andando:
        host, puerto = destino()
        equipo = Equipo(host, puerto)
        try:
            equipo.conectar()
        except OSError as e:
            plugin.estado(f"sin el equipo en {host}:{puerto} ({e}); reintento en {espera:.0f} s", ok=False)
            plugin.mostrar("conectado", "no")
            time.sleep(espera)
            espera = min(espera * 2, 30)  # 1, 2, 4… hasta 30 s: no martillar un equipo apagado
            continue
        espera = 1.0
        plugin.mostrar("conectado", "sí")
        plugin.estado(f"conectado a {host}:{puerto}")
        try:
            while plugin.andando:
                linea = equipo.leer_linea()
                if linea:
                    al_llegar(linea)
        except OSError as e:
            plugin.estado(f"se cortó la conexión ({e}); reconectando", ok=False)
            plugin.mostrar("conectado", "no")
        finally:
            equipo.cerrar()


@plugin.al_iniciar
def consultar():
    """Pregunta el estado cada tanto y lo muestra (y lo publica si cambió)."""
    anterior = None
    while plugin.andando:
        time.sleep(float(plugin.config.get("consultar_cada_s", 2)))
        if equipo is None or equipo.s is None:
            continue
        try:
            r = equipo.pedir("ESTADO?")
        except (RuntimeError, OSError) as e:
            plugin.log("ESTADO? falló:", e)
            continue
        est = dict(kv.split("=", 1) for kv in r.split()[1:] if "=" in kv)
        if "temp" in est:
            plugin.mostrar("temperatura", float(est["temp"]))
        for n in ("1", "2"):  # lo que se cambió en el equipo mismo vuelve al panel
            if f"luz{n}" in est:
                plugin.mostrar(f"luz{n}", int(est[f"luz{n}"]))
        luces = {k: v for k, v in est.items() if k.startswith("luz")}
        if luces != anterior:
            plugin.evento("estado", {"luces": luces, "modelo": equipo.modelo})
            anterior = luces


def conectado(espera_s: float = 3.0) -> Equipo:
    """El equipo, si hay conexión (espera un poco: recién arrancado puede estar conectando)."""
    fin = time.monotonic() + espera_s
    while equipo is None or equipo.s is None:
        if time.monotonic() > fin:
            raise RuntimeError("sin conexión con el equipo")
        time.sleep(0.05)
    return equipo


def poner_luz(n: int, nivel: int) -> dict:
    nivel = max(0, min(100, int(nivel)))
    return {"respuesta": conectado().pedir(f"LUZ {n} {nivel}")}


# --- Panel del operador y controles del asistente --------------------------------------------
plugin.panel([
    ba.seccion("Luces"),
    ba.deslizador("luz1", "Luz 1", 0, 100, paso=1, defecto=0, unidad="%", al_cambiar=lambda v: poner_luz(1, v)),
    ba.deslizador("luz2", "Luz 2", 0, 100, paso=1, defecto=0, unidad="%", al_cambiar=lambda v: poner_luz(2, v)),
    ba.seccion("Equipo"),
    ba.indicador("conectado", "Conectado"),
    ba.medidor("temperatura", "Temperatura", 0, 50, unidad="°C"),
    ba.indicador("ultimo_boton", "Último botón"),
])


@plugin.control("luz", descripcion="Pone una luz del equipo (0 a 100)", parametros={"n": 1, "nivel": 80})
def control_luz(args):
    return poner_luz(int(args.get("n", 1)), int(args.get("nivel", 0)))


@plugin.control("orden", descripcion="Manda una orden cruda al equipo y devuelve lo que contesta (para explorar)",
                parametros={"orden": "ESTADO?"})
def control_orden(args):
    return {"respuesta": conectado().pedir(str(args.get("orden", "ESTADO?")))}


plugin.estado("conectando…")
plugin.correr()
