#!/usr/bin/env python3
"""Banco de caos mínimo (F1-14): romper la app a propósito y medir cuánto tarda en volver.

Con la app abierta en modo prueba (`--args --test`, o que la abra el banco con --lanzar),
sobre un evento de prueba PROPIO (nunca el del operador):

  1. Anota el evento abierto, crea uno de cero («ZZ caos <hora>») y lo abre. Si después
     de reconectar lo abierto no es ese, corta sin tocar nada.
  2. Una cámara virtual (pelota) al aire, transmisión a un servidor RTMP LOCAL (el de
     tools/caos/rtmp_local.py, en este mismo proceso: nada del sistema) y grabación.
  3. Cada escenario, `--repeticiones` veces, con el aire andando antes de cada vez:
       kill_a, stop_a        kill -9 / SIGSTOP de la app (A): el launcher la relanza
       kill_b, stop_b        lo mismo con el gateway (B)
       kill_web, stop_web    ba-web (si corre)
       kill_cf, stop_cf      cloudflared (si corre: túnel configurado)
       al_aire_truncado      Runtime/al-aire.json a 0 bytes y kill -9 de A
       codificador_tx        el codificador de la transmisión da error (diag_falla)
       sumidero_tx           la conexión RTMP no puede escribir
       codificador_rec       el codificador de la grabación da error
       sumidero_rec          el disco de la grabación no puede escribir (a los --sumidero-s)
       gpu_video             la GPU del video se pierde (F3-09): la app rearma el motor
     y mide: cuándo contesta la app (MCP), cuándo vuelve el primer cuadro al servidor
     RTMP (RTO de la transmisión), cuándo vuelve a crecer el MKV, y la reapertura de los
     anillos A↔B (lo que dice Logs/gateway.log después de la falla). Con A relanzada,
     además cuándo sacó el motor nuevo su primer cuadro de programa (Logs/app.log,
     «primer_cuadro_s»: la parte de la retoma que es de A, F3-10, meta < 3 s); con la GPU
     perdida, cuándo terminó el rearme («rearmado_s»).
  4. Al final: los huecos de la transmisión (servidor RTMP) y de la grabación (cada
     MKV con el gst-discoverer del bundle: duración, si abre, hueco entre partes).
  5. SIEMPRE, pase lo que pase: limpia las fallas programadas, para la transmisión y la
     grabación, vuelve al evento de antes, borra el de prueba (y lo saca de la
     papelera), app_quit y mata lo que haya quedado (A, B, ba-web, cloudflared, el
     launcher). Nada queda corriendo.

El informe (JSON y Markdown, con la versión de la app) va a <datos>/Logs/caos/<fecha>/
o a --salida. RTO p50/p95 por escenario.

    python3 -I tools/caos/banco.py                       # todo, 3 veces cada uno
    python3 -I tools/caos/banco.py --escenarios kill_a,codificador_tx --repeticiones 5
    python3 -I tools/caos/banco.py --lanzar dist/BroadcastAppliance.app --nocturno

Sale con 0 si todo volvió dentro de --plazo-s, 1 si algo no volvió, 2 si cortó antes de
tocar nada. Sin sonido: la cámara virtual no tiene audio y nada sale por los parlantes.
"""
from __future__ import annotations

import argparse
import datetime
import glob
import importlib.util
import json
import math
import os
import platform
import queue
import re
import signal
import subprocess
import sys
import threading
import time
import urllib.request

AQUI = os.path.dirname(os.path.abspath(__file__))
RAIZ = os.path.dirname(os.path.dirname(AQUI))


def _modulo(nombre: str, ruta: str):
    spec = importlib.util.spec_from_file_location(nombre, ruta)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


_am = _modulo("asamblea_mcp", os.path.join(RAIZ, "tools", "test", "asamblea-mcp.py"))
rtmp_local = _modulo("rtmp_local", os.path.join(AQUI, "rtmp_local.py"))

CAM = "Cámara de caos"
WINDOWS = platform.system() == "Windows"
ESCENARIOS = ["kill_a", "stop_a", "kill_b", "stop_b", "kill_web", "stop_web", "kill_cf", "stop_cf", "al_aire_truncado",
              "codificador_tx", "sumidero_tx", "codificador_rec", "sumidero_rec", "gpu_video"]
# Los procesos, por el nombre del ejecutable (A y B pueden correr desde la copia sombra:
# la ruta no es la del bundle).
PROCESOS = {"a": "ba-app", "b": "ba-gateway", "web": "ba-web", "cf": "cloudflared", "launcher": "BroadcastAppliance"}
# Lo que escribe el gateway cuando vuelve a tomar un anillo de A (MediaPump).
ANILLO = re.compile(r"suscrito|resuscribiendo|abiert[oa]\b|\blisto\b")
# Lo que escribe A cuando su motor saca el primer cuadro de programa (LatidoAire) y cuando
# terminó de rearmarse tras perder la GPU (AppRuntime::rearmarPorGpu).
PRIMER_CUADRO = re.compile(r"arranque: el primer cuadro del programa")
REARMADO = re.compile(r"motor rearmado tras perder la GPU")


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


# --- MCP con plazo -----------------------------------------------------------------------


class McpConPlazo(_am.Mcp):
    """El cliente de las pruebas, con plazo por respuesta: con A frenada (SIGSTOP) el
    puente conecta pero nadie contesta, y sin plazo el banco quedaba colgado."""

    plazo = 10.0

    def _arrancar(self):
        self._q: queue.Queue = queue.Queue()

        def leer(p, q):
            for linea in p.stdout:
                q.put(linea)
            q.put("")

        threading.Thread(target=leer, args=(self.p, self._q), daemon=True).start()

    def _linea(self):
        try:
            return self._q.get(timeout=self.plazo)
        except queue.Empty:
            self.cerrar()
            return ""


# --- La app y sus procesos -------------------------------------------------------------


class App:
    def __init__(self, ba_mcp: str, datos: str | None):
        self.ba_mcp = os.path.abspath(ba_mcp)
        partes = self.ba_mcp.split(os.sep)
        if ".app" in self.ba_mcp and "Resources" in partes:  # macOS: <X>.app/Contents/Resources/bin/64bit/ba-mcp
            app = self.ba_mcp[: self.ba_mcp.index(".app") + 4]
            self.raiz = os.path.join(app, "Contents", "Resources")
            self.datos_def = os.path.join(os.path.dirname(app), os.path.basename(app)[:-4] + " Datos")
            self.bundle = app
        else:  # <raíz>/bin/64bit/ba-mcp
            self.raiz = os.path.dirname(os.path.dirname(os.path.dirname(self.ba_mcp)))
            self.datos_def = self.raiz
            self.bundle = self.raiz
        self.datos = datos or self.datos_def
        self.runtime = os.path.join(self.datos, "Runtime")
        self.logs = os.path.join(self.datos, "Logs")
        self.frenados: set[int] = set()
        self.dirs_procesos = [self.bundle]  # + --procesos-de

    def version(self) -> dict:
        try:
            with open(os.path.join(self.raiz, "manifest.json"), encoding="utf-8") as f:
                m = json.load(f)
            return {"version": m.get("version"), "generado": m.get("generated_at")}
        except (OSError, ValueError):
            return {"version": None}

    def discoverer(self) -> str | None:
        for f in glob.glob(os.path.join(os.path.dirname(self.ba_mcp), "**", "gst-discoverer-1.0*"), recursive=True):
            if os.path.isfile(f) and os.access(f, os.X_OK):
                return f
        return None

    # MCP: un intento con plazo. (m, abierto) o (None, None).
    def mcp(self, plazo: float = 8.0):
        McpConPlazo.plazo = plazo
        try:
            m = McpConPlazo(self.ba_mcp)
            r = m.tool("eventos_listar")
            if "abierto" in r:
                return m, r["abierto"]
            m.cerrar()
        except (OSError, ValueError):
            pass
        return None, None

    def esperar_mcp(self, hasta: float):
        """Hasta que contesta (o vence): (m, abierto, cuándo)."""
        while time.time() < hasta:
            m, ab = self.mcp(min(8.0, max(1.0, hasta - time.time())))
            if m is not None:
                return m, ab, time.time()
            time.sleep(0.3)
        return None, None, None

    def pids(self, nombre: str) -> list[int]:
        """Los procesos `nombre` de ESTE bundle (el ejecutable adentro de él, o de --procesos-de
        si A y B corren desde la copia sombra). Por nombre solo, el banco mataba lo de otra
        instancia de la app que estuviera abierta en la máquina."""
        out = []
        for pid, ruta in self._procesos(nombre):
            ruta = os.path.realpath(ruta) if ruta else ""
            if pid != os.getpid() and any(ruta.startswith(os.path.realpath(d) + os.sep) for d in self.dirs_procesos):
                out.append(pid)
        return out

    def _procesos(self, nombre: str) -> list[tuple[int, str]]:
        """(pid, ruta del ejecutable) de los que se llaman `nombre`."""
        if WINDOWS:
            r = subprocess.run(["powershell", "-NoProfile", "-Command",
                                f"Get-Process -Name '{nombre}' -ErrorAction SilentlyContinue | % {{ \"$($_.Id)|$($_.Path)\" }}"],
                               capture_output=True, text=True)
            return [(int(a), b) for a, _, b in (l.strip().partition("|") for l in r.stdout.splitlines()) if a.isdigit()]
        out = []
        r = subprocess.run(["ps", "-axo", "pid=,comm="], capture_output=True, text=True)
        for l in r.stdout.splitlines():
            pid, _, comm = l.strip().partition(" ")
            comm = comm.strip()
            if not pid.isdigit() or os.path.basename(comm) != nombre[:15] and os.path.basename(comm) != nombre:
                continue
            ruta = comm
            if os.path.exists(f"/proc/{pid}/exe"):  # Linux: comm es solo el nombre (15 letras)
                try:
                    ruta = os.readlink(f"/proc/{pid}/exe")
                except OSError:
                    continue
            if os.path.basename(ruta) == nombre:
                out.append((int(pid), ruta))
        return out

    def senal(self, pid: int, cual: str) -> bool:
        try:
            if WINDOWS:
                if cual != "kill":
                    return False
                return subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True).returncode == 0
            os.kill(pid, {"kill": signal.SIGKILL, "stop": signal.SIGSTOP, "cont": signal.SIGCONT}[cual])
            if cual == "stop":
                self.frenados.add(pid)
            if cual in ("cont", "kill"):
                self.frenados.discard(pid)
            return True
        except (OSError, ProcessLookupError):
            return False

    def vive(self, pid: int) -> bool:
        if WINDOWS:
            return pid in self.pids_todos()
        try:
            os.kill(pid, 0)
            return True
        except OSError:
            return False

    def pids_todos(self) -> list[int]:
        return [p for n in PROCESOS.values() for p in self.pids(n)]

    def gateway_contesta(self) -> bool:
        try:
            with open(os.path.join(self.runtime, "gateway-admin.json"), encoding="utf-8") as f:
                g = json.load(f)
            req = urllib.request.Request(f"http://127.0.0.1:{g['port']}/api/v1/network",
                                         headers={"Authorization": "Bearer " + g["token"]})
            with urllib.request.urlopen(req, timeout=2) as r:
                return r.status == 200
        except (OSError, ValueError, KeyError):
            return False

    def primera_linea(self, nombre_log: str, desde: float, patron: re.Pattern) -> float | None:
        """El momento (epoch) de la primera línea de Logs/<nombre_log> posterior a `desde` que
        coincide con `patron` (las líneas empiezan con la hora ISO en UTC)."""
        ruta = os.path.join(self.logs, nombre_log)
        try:
            with open(ruta, encoding="utf-8", errors="replace") as f:
                for l in f:
                    m = re.match(r"(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d+)Z", l)
                    if not m:
                        continue
                    t = datetime.datetime.fromisoformat(m.group(1)).replace(tzinfo=datetime.timezone.utc).timestamp()
                    if t >= desde and patron.search(l):
                        return t
        except OSError:
            pass
        return None


# --- El banco ----------------------------------------------------------------------------


def percentil(xs: list[float], p: float) -> float | None:
    xs = sorted(x for x in xs if x is not None)
    if not xs:
        return None
    k = max(0, min(len(xs) - 1, math.ceil(p / 100.0 * len(xs)) - 1))  # rango más cercano
    return round(xs[k], 3)


def mkvs_en(d: dict | list | str, out: set):
    """Las rutas .mkv que aparecen en una respuesta del MCP (a cualquier profundidad)."""
    if isinstance(d, dict):
        for v in d.values():
            mkvs_en(v, out)
    elif isinstance(d, list):
        for v in d:
            mkvs_en(v, out)
    elif isinstance(d, str) and d.lower().endswith(".mkv"):
        out.add(d)


class Banco:
    def __init__(self, a: argparse.Namespace, app: App, rtmp):
        self.a = a
        self.app = app
        self.rtmp = rtmp
        self.m = None
        self.prueba = None
        self.original = None
        self.carpeta_rec = None
        self.inicio = time.time()
        self.resultados: dict[str, list[dict]] = {}

    # --- Lo de siempre -------------------------------------------------------------------
    def tool(self, nombre: str, args: dict | None = None) -> dict:
        if self.m is None:
            self.reconectar(60)
        r = self.m.tool(nombre, args) if self.m else {"_isError": True, "_texto": "sin MCP"}
        if r.get("_error") == "sin respuesta":
            self.m = None
        return r

    def reconectar(self, plazo_s: float) -> bool:
        if self.m is not None:
            self.m.cerrar()
        self.m, ab, _ = self.app.esperar_mcp(time.time() + plazo_s)
        if self.m is not None and self.prueba is not None and ab != self.prueba:
            raise Corte(f"lo abierto es «{ab}», no el evento de prueba")
        return self.m is not None

    def estado(self) -> dict:
        return self.tool("status")

    def carpeta_grabacion(self) -> str | None:
        mk: set[str] = set()
        mkvs_en(self.tool("record_ajustes"), mk)
        mkvs_en(self.tool("evento_grabaciones"), mk)
        mk = {x for x in mk if os.path.isabs(x)}
        if mk:
            self.carpeta_rec = os.path.dirname(max(mk, key=lambda x: os.path.getmtime(x) if os.path.exists(x) else 0))
        return self.carpeta_rec

    def tamano_mkv(self) -> int:
        if not self.carpeta_rec:
            return 0
        return sum(os.path.getsize(f) for f in glob.glob(os.path.join(self.carpeta_rec, "*.mkv")) if os.path.getmtime(f) >= self.inicio)

    def mkv_crece_desde(self, t: float, plazo: float) -> float | None:
        """Cuándo vuelve a crecer lo grabado después de `t` (o None)."""
        if not self.carpeta_rec:
            return None
        base = self.tamano_mkv()
        while time.time() < t + plazo:
            time.sleep(0.5)
            if self.tamano_mkv() > base:
                return time.time()
        return None

    # --- Armar el evento y el aire ---------------------------------------------------------
    def preparar(self):
        self.m, self.original, _ = self.app.esperar_mcp(time.time() + self.a.espera_app_s)
        if self.m is None:
            raise Corte("la app no contesta (¿abierta con --args --test?)")
        log("abierto al empezar:", self.original)
        prueba = "ZZ caos " + time.strftime("%H%M%S")
        r = self.tool("evento_nuevo", {"nombre": prueba, "desde": "cero", "abrir": True})
        if r.get("_isError"):
            raise Corte("no se pudo crear el evento de prueba: " + r.get("_texto", ""))
        self.m.cerrar()
        self.m = None
        time.sleep(8)
        self.prueba = prueba  # desde acá, reconectar exige que lo abierto sea el de prueba
        if not self.reconectar(self.a.espera_app_s):
            raise Corte("la app no volvió después de abrir el evento de prueba")
        time.sleep(4)
        r = self.tool("video_entrada_virtual", {"nombre": CAM, "preset": "pelota", "al_aire": True})
        if r.get("_isError"):
            raise Corte("no se pudo crear la cámara virtual: " + r.get("_texto", ""))
        self.asegurar_aire()

    def asegurar_aire(self):
        """Transmisión al RTMP local y grabación andando, con cuadros llegando."""
        if self.m is None and not self.reconectar(self.a.espera_app_s):
            raise Corte("la app no contesta")
        st = self.estado()
        if not st.get("recording"):
            self.tool("record_start")
        for intento in range(2):
            if intento or not st.get("streaming"):
                if intento:
                    self.tool("stream_stop")  # andaba pero no llegaba nada: de nuevo
                    time.sleep(1)
                self.tool("stream_start", {"server": f"rtmp://127.0.0.1:{self.rtmp.puerto}/live", "key": "caos"})
            t = time.time()
            while time.time() < t + 30 and self.rtmp.primer_video_desde(t) is None:
                time.sleep(0.3)
            if self.rtmp.primer_video_desde(t) is not None:
                break
        else:
            raise Corte("la transmisión no llega al servidor RTMP local")
        if not self.carpeta_rec:
            time.sleep(2)
            self.carpeta_grabacion()
        time.sleep(self.a.estable_s)

    # --- Los escenarios ---------------------------------------------------------------------
    def correr(self, nombre: str) -> dict:
        if nombre.startswith(("kill_", "stop_")):
            cual, q = nombre.split("_", 1)
            r = self.proceso(q, cual)
        elif nombre == "al_aire_truncado":
            r = self.al_aire_truncado()
        else:
            pieza, sal = nombre.split("_")
            r = self.falla(pieza, {"tx": "transmision", "rec": "grabacion", "video": "video"}[sal])
        if not r.get("saltado"):
            r["volvio"] = r.get("rto_s") is not None
        return r

    def medir(self, t0: float, base_mkv: int, *, mcp: bool = False, video: str | None = None, grabacion: bool = False,
              gateway: bool = False, nuevo_de: tuple[str, list[int]] | None = None) -> dict:
        """Después de una falla en `t0`, cuándo vuelve cada cosa (s desde t0; None = no volvió
        dentro de --plazo-s): la app contesta (mcp), llega video al RTMP local (video:
        «nueva» = de una conexión nueva, «cualquiera»), el MKV crece de nuevo, el gateway
        contesta, hay otro proceso con ese nombre (nuevo_de). Y la reapertura de los anillos
        (la primera línea de Logs/gateway.log posterior a t0 que lo dice)."""
        r: dict = {}
        hasta = t0 + self.a.plazo_s
        if mcp:
            self.m = None
            m, ab, t = self.app.esperar_mcp(hasta)
            r["mcp_s"] = round(t - t0, 3) if t else None
            if m is not None:
                self.m = m
                if ab != self.prueba:
                    raise Corte(f"después de la falla lo abierto es «{ab}»")
        pend = {k for k, v in (("video", video), ("grabacion", grabacion), ("gateway", gateway), ("relanzado", nuevo_de)) if v}
        while pend and time.time() < hasta:
            ahora = time.time()
            if "video" in pend:
                tv = self.rtmp.primer_video_desde(t0, sesion_nueva=video == "nueva")
                if tv:
                    r["video_s"] = round(tv - t0, 3)
                    pend.discard("video")
            if "grabacion" in pend and self.tamano_mkv() > base_mkv:
                r["grabacion_s"] = round(ahora - t0, 3)
                pend.discard("grabacion")
            if "gateway" in pend and self.app.gateway_contesta():
                r["gateway_s"] = round(ahora - t0, 3)
                pend.discard("gateway")
            if "relanzado" in pend and [p for p in self.app.pids(nuevo_de[0]) if p not in nuevo_de[1]]:
                r["relanzado_s"] = round(ahora - t0, 3)
                pend.discard("relanzado")
            time.sleep(0.3)
        for k in pend:
            r[k + "_s"] = None
        if mcp:
            tp = self.app.primera_linea("app.log", t0, PRIMER_CUADRO)
            r["primer_cuadro_s"] = round(tp - t0, 3) if tp else None
        ta = self.app.primera_linea("gateway.log", t0, ANILLO)
        r["anillos_s"] = round(ta - t0, 3) if ta else None
        huecos = self.rtmp.huecos(1.0, t0 - 1.0)
        r["hueco_tx_s"] = max((h["s"] for h in huecos if h["desde"] <= hasta), default=0.0)
        return r

    def proceso(self, q: str, cual: str) -> dict:
        nombre = PROCESOS[q]
        pids = self.app.pids(nombre)
        if not pids:
            return {"saltado": f"{nombre} no corre"}
        if cual == "stop" and WINDOWS:
            return {"saltado": "SIGSTOP no existe en Windows"}
        if len(pids) > 1:
            log(f"  ojo: {len(pids)} procesos {nombre}: {pids}")
        base = self.tamano_mkv()
        t0 = time.time()
        for p in pids:
            self.app.senal(p, cual)
        r: dict = {"pids": pids}
        nueva = "nueva"
        if cual == "stop":
            # Frenado: el supervisor tiene que darse cuenta (por el latido) y relanzarlo. Si
            # no, a los --stop-s se lo suelta (SIGCONT) y se anota que nadie lo vio.
            while time.time() < t0 + self.a.stop_s and any(self.app.vive(p) for p in pids):
                time.sleep(0.3)
            vivos = [p for p in pids if self.app.vive(p)]
            r["lo_relanzaron"] = not vivos
            r["detectado_s"] = None if vivos else round(time.time() - t0, 3)
            for p in vivos:
                self.app.senal(p, "cont")
            if vivos:
                nueva = "cualquiera"  # sigue el mismo proceso: lo que vuelva por su conexión vale
        if q == "a":
            r.update(self.medir(t0, base, mcp=True, video=nueva, grabacion=True))
            r["rto_s"] = r["video_s"]
        elif q == "b":
            # La transmisión y la grabación son de A: no se tienen que enterar (hueco_tx_s).
            r.update(self.medir(t0, base, gateway=True, nuevo_de=(nombre, pids) if r.get("lo_relanzaron", True) else None))
            r["rto_s"] = r["gateway_s"]
        else:
            if r.get("lo_relanzaron", True):
                r.update(self.medir(t0, base, nuevo_de=(nombre, pids)))
                r["rto_s"] = r["relanzado_s"]
            else:
                r["rto_s"] = None  # frenado y nadie lo relanzó: no volvió solo
        return r

    def al_aire_truncado(self) -> dict:
        # El estado de aire va en dos generaciones (al-aire.A/B.json, F1-03): se trunca la más
        # nueva, como un corte a mitad de escritura; tiene que volver con la otra.
        gens = [os.path.join(self.app.runtime, n) for n in ("al-aire.A.json", "al-aire.B.json", "al-aire.json")]
        gens = [g for g in gens if os.path.exists(g)]
        if not gens:
            return {"saltado": "no hay Runtime/al-aire.A/B.json"}
        ruta = max(gens, key=os.path.getmtime)
        pids = self.app.pids(PROCESOS["a"])
        if not pids:
            return {"saltado": "ba-app no corre"}
        base = self.tamano_mkv()
        with open(ruta, "w", encoding="utf-8"):
            pass  # 0 bytes: lo que deja un corte a mitad de escritura
        t0 = time.time()
        for p in pids:
            self.app.senal(p, "kill")
        r = self.medir(t0, base, mcp=True, video="nueva", grabacion=True)
        st = self.estado() if self.m else {}
        r["retomo_transmision"] = bool(st.get("streaming"))
        r["retomo_grabacion"] = bool(st.get("recording"))
        r["rto_s"] = r["video_s"]
        return r

    def falla(self, pieza: str, salida: str) -> dict:
        en = self.a.sumidero_s if (pieza, salida) == ("sumidero", "grabacion") else 2.0
        r0 = self.tool("diag_falla", {"pieza": pieza, "salida": salida, "en_s": en})
        if r0.get("_isError"):
            return {"saltado": "diag_falla: " + r0.get("_texto", "")[:160] + " (¿la app es de antes de F1-14?)"}
        t0 = time.time() + en
        time.sleep(max(0.0, t0 - time.time()) + 1.0)
        d = self.tool("diag_falla").get("disparadas", [])
        r: dict = {"disparada": any(x.get("pieza") == pieza and x.get("salida") == salida for x in d)}
        if salida == "video":
            # La GPU perdida: la app rearma el motor y retoma la transmisión (conexión nueva).
            r.update(self.medir(t0, self.tamano_mkv(), video="nueva", grabacion=True))
            tr = self.app.primera_linea("app.log", t0, REARMADO)
            r["rearmado_s"] = round(tr - t0, 3) if tr else None
            r["rto_s"] = r["video_s"]
        elif salida == "transmision":
            r.update(self.medir(t0, 0, video="nueva"))
            r["rto_s"] = r["video_s"]
        else:
            # Lo escrito hasta la falla ya se contó; vuelve cuando crece de nuevo.
            r.update(self.medir(t0, self.tamano_mkv(), grabacion=True))
            r["rto_s"] = r["grabacion_s"]
            if r["rto_s"] is None:
                # Sin F1-05 la grabación queda muerta: se rearma a mano para seguir con el banco.
                self.tool("record_stop")
                time.sleep(1)
                self.tool("record_start")
        self.tool("diag_falla", {"limpiar": True})
        return r

    # --- Lo que queda: huecos -----------------------------------------------------------
    def huecos_mkv(self) -> dict:
        out: dict = {"carpeta": self.carpeta_rec, "archivos": [], "huecos": []}
        if not self.carpeta_rec:
            return out
        disc = self.app.discoverer()
        archivos = sorted((f for f in glob.glob(os.path.join(self.carpeta_rec, "*.mkv")) if os.path.getmtime(f) >= self.inicio),
                          key=os.path.getmtime)
        filas = []
        for f in archivos:
            st = os.stat(f)
            dur, abre = None, None
            if disc:
                try:
                    r = subprocess.run([disc, f], capture_output=True, text=True, timeout=60)
                    m = re.search(r"Duration:\s*(\d+):(\d+):([\d.]+)", r.stdout)
                    abre = r.returncode == 0 and m is not None
                    if m:
                        dur = int(m.group(1)) * 3600 + int(m.group(2)) * 60 + float(m.group(3))
                except (OSError, subprocess.TimeoutExpired):
                    abre = False
            fin = st.st_mtime
            ini = getattr(st, "st_birthtime", None) or (fin - dur if dur is not None else None)
            filas.append({"archivo": os.path.basename(f), "bytes": st.st_size, "duracion_s": dur, "abre": abre,
                          "inicio": ini, "fin": fin})
        for a, b in zip(filas, filas[1:]):
            if a["fin"] and b["inicio"]:
                h = b["inicio"] - a["fin"]
                if h > 0.5:
                    out["huecos"].append({"entre": [a["archivo"], b["archivo"]], "s": round(h, 3)})
        out["archivos"] = filas
        out["no_abren"] = [x["archivo"] for x in filas if x["abre"] is False]
        out["hueco_max_s"] = max((h["s"] for h in out["huecos"]), default=0.0)
        return out

    # --- Cerrar todo, siempre ----------------------------------------------------------------
    def cerrar(self):
        log("cerrando: fallas, aire, evento de prueba, app y procesos")
        for p in list(self.app.frenados):
            self.app.senal(p, "cont")
        try:
            if self.m is None:
                self.reconectar(30)
        except Corte as ex:
            # Lo abierto no es el de prueba: no se toca nada de eventos (puede ser el del operador).
            log(f"  {ex}: no se cambia ni se borra ningún evento" + (f" (el de prueba «{self.prueba}» queda)" if self.prueba else ""))
            self.m = None
        if self.m is not None:
            self.tool("diag_falla", {"limpiar": True})
            self.tool("stream_stop")
            self.tool("record_stop")
            if self.prueba and self.original and self.original != self.prueba:
                r = self.tool("evento_cambiar", {"nombre": self.original, "aunque_este_al_aire": True})
                log("volver a «%s»: %s" % (self.original, r.get("_texto", "")[:100]))
                if self.m is not None:
                    self.m.cerrar()
                self.m = None
                time.sleep(8)
                self.m, ab, _ = self.app.esperar_mcp(time.time() + 60)
                if self.m is not None and ab == self.original:
                    self.tool("evento_borrar", {"nombre": self.prueba})
                    pap = [p for p in self.tool("evento_recuperar").get("papelera", []) if p.get("nombre") == self.prueba]
                    if pap:
                        self.tool("evento_papelera_vaciar", {"id": pap[0]["id"]})
                else:
                    log(f"MAL: lo abierto es «{ab}»: el evento de prueba «{self.prueba}» queda (borralo a mano)")
            if self.m is not None:
                self.tool("app_quit")
                self.m.cerrar()
                self.m = None
        if self.a.no_matar:
            return
        hasta = time.time() + 15
        while time.time() < hasta and self.app.pids_todos():
            time.sleep(0.5)
        for p in self.app.pids_todos():
            log("  queda vivo", p, "(se mata)")
            self.app.senal(p, "kill")


class Corte(Exception):
    """El banco no puede seguir sin arriesgar lo del operador (o la app no contesta)."""


def informe(b: Banco, carpeta: str, mkv: dict, rtmp_ses: list, rtmp_huecos: list, corte: str | None) -> dict:
    esc = {}
    for nombre, reps in b.resultados.items():
        rto = [r.get("rto_s") for r in reps if not r.get("saltado")]
        esc[nombre] = {
            "repeticiones": reps,
            "rto_p50_s": percentil(rto, 50),
            "rto_p95_s": percentil(rto, 95),
            "mcp_p50_s": percentil([r.get("mcp_s") for r in reps], 50),
            "anillos_p50_s": percentil([r.get("anillos_s") for r in reps], 50),
            "primer_cuadro_p50_s": percentil([r.get("primer_cuadro_s") for r in reps], 50),
            "primer_cuadro_p95_s": percentil([r.get("primer_cuadro_s") for r in reps], 95),
            "no_volvio": sum(1 for r in reps if not r.get("saltado") and not r.get("volvio")),
            "saltado": next((r["saltado"] for r in reps if r.get("saltado")), None),
        }
    j = {"banco": "caos F1-14", "fecha": datetime.datetime.now().isoformat(timespec="seconds"), "app": b.app.version(),
         "maquina": {"sistema": platform.system(), "version": platform.release(), "nodo": platform.node()},
         "plazo_s": b.a.plazo_s, "corte": corte, "escenarios": esc, "mkv": mkv,
         "rtmp": {"sesiones": rtmp_ses, "huecos": rtmp_huecos}}
    os.makedirs(carpeta, exist_ok=True)
    with open(os.path.join(carpeta, "caos.json"), "w", encoding="utf-8") as f:
        json.dump(j, f, ensure_ascii=False, indent=1)
    s = lambda x: "—" if x is None else f"{x:.2f}"
    md = [f"# Banco de caos — {j['fecha']}", "",
          f"App {j['app'].get('version')} ({j['app'].get('generado')}), {j['maquina']['sistema']} {j['maquina']['version']}. "
          f"Plazo por escenario: {b.a.plazo_s:.0f} s.", ""]
    if corte:
        md += [f"**Cortó antes de terminar:** {corte}", ""]
    md += ["| Escenario | Veces | RTO p50 (s) | RTO p95 (s) | MCP p50 (s) | Anillos p50 (s) | 1.er cuadro p50/p95 (s) | No volvió |",
           "|---|---|---|---|---|---|---|---|"]
    for n, e in esc.items():
        if e["saltado"]:
            md.append(f"| {n} | — | saltado: {e['saltado']} | | | | | |")
            continue
        md.append(f"| {n} | {len(e['repeticiones'])} | {s(e['rto_p50_s'])} | {s(e['rto_p95_s'])} | {s(e['mcp_p50_s'])} | "
                  f"{s(e['anillos_p50_s'])} | {s(e['primer_cuadro_p50_s'])}/{s(e['primer_cuadro_p95_s'])} | {e['no_volvio']} |")
    md += ["", "RTO: de la falla al primer cuadro de una conexión NUEVA en el servidor RTMP local (en la grabación: "
           "a que el MKV vuelva a crecer). Anillos: primera línea de reapertura en Logs/gateway.log. 1.er cuadro: el "
           "motor de la A relanzada saca su primer cuadro de programa (Logs/app.log; meta F3-10: < 3 s).", "",
           f"## Grabación ({len(mkv.get('archivos', []))} partes)", "",
           f"Hueco máximo entre partes: {s(mkv.get('hueco_max_s'))} s. "
           f"No abren: {', '.join(mkv.get('no_abren', [])) or 'ninguna'}.", ""]
    for h in mkv.get("huecos", []):
        md.append(f"- {h['entre'][0]} → {h['entre'][1]}: {h['s']} s")
    md += ["", f"## Transmisión ({len(rtmp_ses)} conexiones al RTMP local)", ""]
    for h in rtmp_huecos:
        md.append(f"- {datetime.datetime.fromtimestamp(h['desde']).strftime('%H:%M:%S')}: {h['s']} s sin video")
    with open(os.path.join(carpeta, "caos.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(md) + "\n")
    return j


def lanzar(ruta: str) -> subprocess.Popen:
    """Abre la app en modo prueba (el launcher, que supervisa a A y B)."""
    ruta = os.path.abspath(ruta)
    if ruta.endswith(".app"):
        exe = os.path.join(ruta, "Contents", "MacOS", os.path.basename(ruta)[:-4])
    elif os.path.isdir(ruta):
        exe = os.path.join(ruta, "BroadcastAppliance" + (".exe" if WINDOWS else ""))
    else:
        exe = ruta
    log("lanzando", exe, "--test")
    # Con --test la app no retoma la grabación ni la transmisión tras una caída (una prueba no
    # sale al aire sola); el banco mide justo eso, así que la pide (BA_PRUEBA_RETOMA_SALIDAS).
    env = dict(os.environ, BA_PRUEBA_RETOMA_SALIDAS="1")
    return subprocess.Popen([exe, "--test"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=not WINDOWS, env=env)


def ba_mcp_de(lanzado: str | None) -> str:
    if lanzado:
        r = os.path.abspath(lanzado)
        base = os.path.join(r, "Contents", "Resources") if r.endswith(".app") else (r if os.path.isdir(r) else os.path.dirname(r))
        return os.path.join(base, "bin", "64bit", "ba-mcp" + (".exe" if WINDOWS else ""))
    return _am.BA_MCP


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--ba-mcp", help="el puente MCP del bundle (de fábrica, el de dist/ o el de --lanzar)")
    ap.add_argument("--lanzar", help="abrir la app en modo prueba (la .app, la carpeta del bundle o el launcher)")
    ap.add_argument("--datos", help="la carpeta de datos (de fábrica: la del bundle)")
    ap.add_argument("--procesos-de", action="append", default=[],
                    help="otra carpeta de donde pueden correr A y B (la copia sombra); solo se tocan procesos de ahí y del bundle")
    ap.add_argument("--escenarios", default=",".join(ESCENARIOS), help="separados por coma: " + ", ".join(ESCENARIOS))
    ap.add_argument("--repeticiones", type=int, default=3)
    ap.add_argument("--plazo-s", type=float, default=60.0, help="cuánto se espera que algo vuelva")
    ap.add_argument("--stop-s", type=float, default=20.0, help="cuánto queda frenado (SIGSTOP) si nadie lo relanza")
    ap.add_argument("--sumidero-s", type=float, default=10.0, help="a los cuántos segundos falla el disco de la grabación")
    ap.add_argument("--estable-s", type=float, default=5.0, help="aire andando antes de cada falla")
    ap.add_argument("--espera-app-s", type=float, default=120.0)
    ap.add_argument("--puerto-rtmp", type=int, default=0, help="0 = uno libre")
    ap.add_argument("--salida", help="carpeta del informe (de fábrica <datos>/Logs/caos/<fecha>)")
    ap.add_argument("--nocturno", action="store_true", help="5 repeticiones y plazo de 90 s")
    ap.add_argument("--no-matar", action="store_true", help="al final, app_quit pero sin matar lo que quede")
    a = ap.parse_args()
    if a.nocturno:
        a.repeticiones, a.plazo_s = max(a.repeticiones, 5), max(a.plazo_s, 90.0)
    escenarios = [e.strip() for e in a.escenarios.split(",") if e.strip()]
    malos = [e for e in escenarios if e not in ESCENARIOS]
    if malos:
        ap.error("escenarios desconocidos: " + ", ".join(malos))

    app = App(a.ba_mcp or ba_mcp_de(a.lanzar), a.datos)
    app.dirs_procesos += a.procesos_de
    if not os.path.exists(app.ba_mcp):
        print("no está ba-mcp:", app.ba_mcp)
        return 2
    carpeta = a.salida or os.path.join(app.logs, "caos", time.strftime("%Y%m%d-%H%M%S"))
    rtmp = rtmp_local.ServidorRtmp(a.puerto_rtmp, os.path.join(carpeta, "rtmp")).iniciar()
    log(f"RTMP local en rtmp://127.0.0.1:{rtmp.puerto}/live; informe en {carpeta}")
    lanzado = lanzar(a.lanzar) if a.lanzar else None
    b = Banco(a, app, rtmp)
    corte = None
    mkv: dict = {}
    try:
        b.preparar()
        for e in escenarios:
            b.resultados[e] = []
            for i in range(a.repeticiones):
                b.asegurar_aire()
                log(f"== {e} ({i + 1}/{a.repeticiones})")
                r = b.correr(e)
                log("  ", json.dumps(r, ensure_ascii=False))
                b.resultados[e].append(r)
                if r.get("saltado"):
                    break
        b.tool("record_stop")
        time.sleep(3)
    except Corte as ex:
        corte = str(ex)
        log("CORTO:", corte)
    except KeyboardInterrupt:
        corte = "interrumpido"
    finally:
        try:
            mkv = b.huecos_mkv()
        except OSError as ex:
            mkv = {"error": str(ex)}
        try:
            b.cerrar()
        finally:
            ses, huecos = rtmp.sesiones(), rtmp.huecos(1.0, b.inicio)
            rtmp.parar()
            if lanzado is not None and lanzado.poll() is None:
                lanzado.kill()
    j = informe(b, carpeta, mkv, ses, huecos, corte)
    log("informe:", os.path.join(carpeta, "caos.md"))
    if corte and not b.resultados:
        return 2
    no_volvio = sum(e["no_volvio"] for e in j["escenarios"].values())
    return 1 if (corte or no_volvio) else 0


if __name__ == "__main__":
    sys.exit(main())
