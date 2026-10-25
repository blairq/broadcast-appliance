"""Probar un plugin SIN la app: ``python3 -I probar.py <carpeta> [--segundos 3] [--control nombre '{json}'] [--ptz]``.

Hace de app: arranca el plugin, junta lo que declara, le pasa cuadros y audio
de prueba a cada tarjeta (y mide cuánto tarda y qué cambió), recibe unos
segundos de cada entrada que manda cuadros, llama los controles que se pidan y
anota los eventos y las llamadas al sistema que hace (no las ejecuta).
Imprime un informe JSON. Es lo que usa ``plugin_probar`` del asistente.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import socket
import subprocess
import sys
import threading
import time
from multiprocessing import resource_tracker, shared_memory

AQUI = os.path.dirname(os.path.abspath(__file__))


def _abrir_shm(nombre: str) -> shared_memory.SharedMemory:
    m = shared_memory.SharedMemory(name=nombre.lstrip("/"))
    try:
        resource_tracker.unregister(m._name, "shared_memory")  # type: ignore[attr-defined]
    except Exception:  # noqa: BLE001
        pass
    return m


class Linea:
    def __init__(self, s):
        self.s, self.buf = s, b""

    def enviar(self, m):
        self.s.sendall((json.dumps(m) + "\n").encode())

    def leer(self, timeout):
        fin = time.monotonic() + timeout
        while b"\n" not in self.buf:
            resta = fin - time.monotonic()
            if resta <= 0:
                return None
            self.s.settimeout(resta)
            try:
                d = self.s.recv(65536)
            except socket.timeout:
                return None
            if not d:
                return None
            self.buf += d
        l, self.buf = self.buf.split(b"\n", 1)
        return json.loads(l)


def _gradiente(ancho, alto):
    b = bytearray(ancho * alto * 4)
    for y in range(alto):
        for x in range(ancho):
            i = (y * ancho + x) * 4
            b[i], b[i + 1], b[i + 2], b[i + 3] = x * 255 // ancho, y * 255 // alto, 128, 255
    return bytes(b)


def _promedio_rgba(buf, n):
    s = [0, 0, 0, 0]
    paso = max(1, n // 4 // 4096)
    cuenta = 0
    for i in range(0, n - 3, 4 * paso):
        for k in range(4):
            s[k] += buf[i + k]
        cuenta += 1
    return [round(v / max(cuenta, 1), 1) for v in s]


def probar_tarjeta(puerto, nombre, medio, params, informe):
    s = socket.create_connection(("127.0.0.1", puerto), timeout=5)
    l = Linea(s)
    r = {"tarjeta": nombre, "medio": medio}
    try:
        if medio == "video":
            ancho, alto = 320, 180
            tam = ancho * alto * 4
            l.enviar({"rol": "tarjeta", "nombre": nombre, "medio": "video", "formato": "RGBA", "ancho": ancho,
                      "alto": alto, "stride": ancho * 4, "tam": tam, "parametros": params})
        else:
            tasa, canales, n = 48000, 2, 480
            tam = n * canales * 4
            l.enviar({"rol": "tarjeta", "nombre": nombre, "medio": "audio", "tasa": tasa, "canales": canales, "tam": tam,
                      "parametros": params})
        h = l.leer(10)
        if not h or not h.get("ok"):
            r["error"] = (h or {}).get("error", "no contestó al conectar")
            return r
        mem = _abrir_shm(h["shm"])
        tiempos, errores = [], []
        if medio == "video":
            orig = _gradiente(ancho, alto)
            r["antes_rgba_promedio"] = _promedio_rgba(orig, tam)
        for c in range(1, 11):
            if medio == "video":
                mem.buf[:tam] = orig
            else:
                import struct
                vals = [0.5 * math.sin(2 * math.pi * 440 * (i // 2) / 48000) for i in range(n * canales)]
                mem.buf[:tam] = struct.pack(f"<{n * canales}f", *vals)
            t0 = time.perf_counter()
            l.enviar({"c": c, "pts": c * 33_333_333, **({"muestras": n} if medio == "audio" else {})})
            resp = l.leer(5)
            if resp is None:
                r["error"] = f"el cuadro {c} no volvió en 5 s (la tarjeta se colgó o tarda demasiado)"
                break
            tiempos.append((time.perf_counter() - t0) * 1000)
            if resp.get("error"):
                errores.append(resp["error"])
        if medio == "video" and tiempos:
            despues = bytes(mem.buf[:tam])
            r["despues_rgba_promedio"] = _promedio_rgba(despues, tam)
            r["bytes_cambiados_%"] = round(100 * sum(1 for a, b in zip(orig[::97], despues[::97]) if a != b) /
                                           len(orig[::97]), 1)
        elif tiempos:
            import struct
            vals = struct.unpack(f"<{n * canales}f", bytes(mem.buf[:tam]))
            r["rms_antes"] = round(0.5 / math.sqrt(2), 4)
            r["rms_despues"] = round(math.sqrt(sum(v * v for v in vals) / len(vals)), 4)
        if tiempos:
            r["ms_promedio"] = round(sum(tiempos) / len(tiempos), 2)
            r["ms_max"] = round(max(tiempos), 2)
            if medio == "video":
                r["aviso_tiempo"] = ("ojo: a 320x180 ya tarda más de 10 ms; en 1080p (27 veces más píxeles) no va "
                                     "a llegar a 30 fps: usar numpy vectorizado, no bucles por píxel") if r["ms_promedio"] > 10 else ""
        if errores:
            r["errores"] = sorted(set(errores))[:3]
        del mem
    finally:
        s.close()
    informe["tarjetas"].append(r)


def probar_entrada(puerto, nombre, medio, segundos, informe):
    s = socket.create_connection(("127.0.0.1", puerto), timeout=5)
    l = Linea(s)
    r = {"entrada": nombre, "medio": medio, "recibidos": 0}
    mem, cab, t0 = None, None, time.monotonic()
    try:
        l.enviar({"rol": "entrada", "nombre": nombre, "medio": medio})
        while time.monotonic() - t0 < segundos:
            m = l.leer(max(0.1, segundos - (time.monotonic() - t0)))
            if m is None:
                break
            if "error" in m:
                r["error"] = m["error"]
                break
            if "shm" in m:
                cab = m
                mem = _abrir_shm(m["shm"])
                r["formato"] = {k: m[k] for k in ("formato", "ancho", "alto", "fps", "tasa", "canales") if k in m}
                continue
            if "c" in m and mem is not None:
                r["recibidos"] += 1
                if r["recibidos"] == 1 and medio == "video":
                    r["primer_cuadro_rgba_promedio"] = (_promedio_rgba(bytes(mem.buf[: m["tam"]]), m["tam"])
                                                       if cab.get("formato") in ("RGBA", "BGRA", "RGBx", "BGRx") else None)
                l.enviar({"c": m["c"]})
        dur = time.monotonic() - t0
        r["por_segundo"] = round(r["recibidos"] / dur, 1) if dur > 0 else 0
        if r["recibidos"] == 0 and "error" not in r:
            r["error"] = f"no mandó nada en {segundos} s (¿enviar_video/enviar_audio en un hilo de al_iniciar?)"
    finally:
        s.close()
        del mem
    informe["entradas"].append(r)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("carpeta")
    ap.add_argument("--segundos", type=float, default=3.0)
    ap.add_argument("--control", nargs=2, action="append", default=[], metavar=("NOMBRE", "ARGS_JSON"))
    ap.add_argument("--ptz", action="store_true", help="mover el PTZ un instante (mueve el equipo de verdad)")
    a = ap.parse_args()
    carpeta = os.path.abspath(a.carpeta)
    informe = {"plugin": os.path.basename(carpeta), "declarado": [], "tarjetas": [], "entradas": [], "controles": [],
               "ptz": [], "graficos": [], "eventos": [], "llamadas_al_sistema": [], "avisos": [], "estado": None}

    srv = socket.socket()
    srv.bind(("127.0.0.1", 0))
    srv.listen(1)
    env = {k: v for k, v in os.environ.items() if not k.startswith("PYTHON")}
    env.update({"BA_PLUGIN_DIR": carpeta, "BA_PLUGIN_ID": os.path.basename(carpeta),
                "BA_PLUGIN_CANAL": f"127.0.0.1:{srv.getsockname()[1]}", "BA_PLUGIN_PRUEBA": "1"})
    p = subprocess.Popen([sys.executable, "-I", "-B", "-u", os.path.join(os.path.dirname(AQUI), "correr.py"), carpeta],
                         env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    errlog: list[str] = []
    threading.Thread(target=lambda: errlog.extend(x.decode(errors="replace") for x in p.stderr), daemon=True).start()
    srv.settimeout(20)
    try:
        c, _ = srv.accept()
    except socket.timeout:
        p.kill()
        informe["error"] = "el plugin no arrancó en 20 s"
        informe["registro"] = "".join(errlog)[-3000:]
        print(json.dumps(informe, ensure_ascii=False, indent=1))
        return 1
    ctl = Linea(c)
    medios, listo = None, False
    n = 0
    pedidos: dict[int, dict] = {}
    t0 = time.monotonic()

    def atender(m):
        t = m.get("t")
        if t == "hola":
            informe["sdk"] = m.get("sdk")
            informe["python"] = m.get("python")
        elif t in ("entrada", "tarjeta", "ptz", "control", "escuchar", "grafico"):
            informe["declarado"].append(m)
        elif t == "evento":
            informe["eventos"].append({k: m.get(k) for k in ("canal", "datos")})
        elif t == "llamar":
            informe["llamadas_al_sistema"].append({"herramienta": m.get("herramienta"), "args": m.get("args")})
            ctl.enviar({"t": "respuesta", "n": m.get("n"), "ok": True, "resultado": {"prueba": True}})
        elif t == "secreto":
            informe.setdefault("claves_pedidas", []).append(m.get("nombre"))
            ctl.enviar({"t": "respuesta", "n": m.get("n"), "ok": True, "resultado": None})
        elif t == "aviso":
            informe["avisos"].append(m.get("texto"))
        elif t == "estado":
            informe["estado"] = {"texto": m.get("texto"), "ok": m.get("ok")}
        elif t == "respuesta":
            pedidos[m.get("n")] = m

    while time.monotonic() - t0 < 30:
        m = ctl.leer(30)
        if m is None:
            break
        if m.get("t") == "hola":
            medios = m.get("medios")
        atender(m)
        if m.get("t") == "listo":
            listo = True
            break
    if not listo:
        informe["error"] = "el plugin no terminó de declararse (¿se cayó? mirá `registro`)"
    else:
        lector_vivo = [True]

        def lector():
            while lector_vivo[0]:
                m = ctl.leer(0.5)
                if m is not None:
                    atender(m)
                elif p.poll() is not None:
                    return
        threading.Thread(target=lector, daemon=True).start()
        hilos = []
        for d in informe["declarado"]:
            if d["t"] == "tarjeta":
                hilos.append(threading.Thread(target=probar_tarjeta,
                                              args=(medios, d["nombre"], d.get("medio", "video"), d.get("parametros", {}), informe)))
            elif d["t"] == "entrada":
                if d.get("gst") or d.get("gst_audio"):
                    informe["entradas"].append({"entrada": d["nombre"], "gst": d.get("gst"), "gst_audio": d.get("gst_audio"),
                                                "nota": "pipeline de GStreamer: lo arma la app al activar el plugin "
                                                        "(plugin_activar dice si anda)"})
                else:
                    for medio in ("video", "audio"):
                        if d.get(medio):
                            hilos.append(threading.Thread(target=probar_entrada,
                                                          args=(medios, d["nombre"], medio, a.segundos, informe)))
            elif d["t"] == "ptz":
                informe["ptz"].append({"entrada": d["entrada"], "capacidades": d.get("capacidades")})
            elif d["t"] == "control":
                informe["controles"].append({"nombre": d["nombre"], "descripcion": d.get("descripcion", "")})
            elif d["t"] == "grafico":
                informe["graficos"].append({"nombre": d["nombre"], "modo": d.get("modo"), "ancho": d.get("ancho"),
                                            "alto": d.get("alto"), "duracion_ms": d.get("duracion_ms")})
        for h in hilos:
            h.start()

        def pedir(accion, args, espera=10):
            nonlocal n
            n += 1
            ctl.enviar({"t": "pedido", "n": n, "accion": accion, "args": args})
            fin = time.monotonic() + espera
            while time.monotonic() < fin and n not in pedidos:
                time.sleep(0.02)
            return pedidos.get(n, {"ok": False, "error": f"no contestó en {espera} s"})

        # Los gráficos, con sus valores por defecto, chicos y cortos (lo que se prueba es que dibuje).
        for g in informe["graficos"]:
            d = next(x for x in informe["declarado"] if x["t"] == "grafico" and x["nombre"] == g["nombre"])
            valores = {q["clave"]: q["porDefecto"] for q in d.get("parametros", []) if "porDefecto" in q}
            esc = min(1.0, 480 / max(1, d.get("ancho", 900)))
            ancho, alto = max(2, int(d.get("ancho", 900) * esc)), max(2, int(d.get("alto", 220) * esc))
            t1 = time.monotonic()
            r = pedir("grafico", {"nombre": g["nombre"], "valores": valores, "ancho": ancho, "alto": alto, "fps": 10,
                                  "duracion_ms": min(d.get("duracion_ms", 2000), 1000)}, espera=60)
            g["ms"] = round((time.monotonic() - t1) * 1000)
            if not r.get("ok"):
                g["error"] = r.get("error")
                continue
            res = r.get("resultado") or {}
            g["devolvio"] = sorted(res)
            if "secuencia" in res:
                try:
                    meta = json.load(open(os.path.join(res["secuencia"], "secuencia.json")))
                    tam = os.path.getsize(os.path.join(res["secuencia"], meta.get("archivo", "cuadros.bgra")))
                    g["cuadros"] = meta.get("cuadros")
                    if tam != meta["ancho"] * meta["alto"] * 4 * meta["cuadros"]:
                        g["error"] = "los cuadros quedaron incompletos"
                except (OSError, ValueError, KeyError) as e:
                    g["error"] = f"la secuencia no se puede leer: {e}"
            elif "declarativo" in res:
                g["capas"] = [c.get("tipo") for c in res["declarativo"].get("capas", [])]
            elif "qml" not in res:
                g["error"] = "no devolvió ni declarativo, ni qml, ni secuencia"

        for nombre, args in a.control:
            informe.setdefault("controles_llamados", []).append(
                {"control": nombre, "respuesta": pedir("control", {"nombre": nombre, "args": json.loads(args or "{}")})})
        if a.ptz:
            for z in informe["ptz"]:
                z["mover"] = pedir("ptz.mover", {"entrada": z["entrada"], "pan": 0.3, "tilt": 0.0, "zoom": 0.0})
                time.sleep(0.4)
                z["parar"] = pedir("ptz.parar", {"entrada": z["entrada"]})
        for h in hilos:
            h.join(a.segundos + 15)
        informe["info"] = pedir("info", {}).get("resultado")
        time.sleep(0.3)
        lector_vivo[0] = False
    try:
        ctl.enviar({"t": "chau"})
    except OSError:
        pass
    try:
        p.wait(4)
    except subprocess.TimeoutExpired:
        p.kill()
        informe["aviso_cierre"] = "no se cerró en 4 s al pedírselo (la app lo mata igual, pero revisá al_terminar)"
    time.sleep(0.2)
    reg = "".join(errlog)
    informe["registro"] = reg[-4000:]
    informe["salio_con"] = p.returncode
    informe["ok"] = listo and "error" not in informe and not any(
        "error" in x for x in informe["tarjetas"] + informe["entradas"] + informe["graficos"])
    print(json.dumps(informe, ensure_ascii=False, indent=1, default=str))
    return 0 if informe["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
