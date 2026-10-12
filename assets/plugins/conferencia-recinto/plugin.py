"""Sistema de conferencia del recinto (ver plugin.json). Escrito para que el asistente lo adapte."""
import random
import re
import socket
import threading
import time

import ba_plugin as ba

plugin = ba.Plugin()
c = plugin.config
bancas = {}  # banca -> abierto
lock = threading.Lock()


def cambio(banca, abierto):
    banca = str(banca)
    with lock:
        if bancas.get(banca) == abierto:
            return
        bancas[banca] = abierto
    plugin.evento("conferencia/microfono", {"banca": banca, "abierto": abierto})
    plugin.mostrar("abiertos", ", ".join(sorted(b for b, a in bancas.items() if a)) or "ninguno")
    plugin.log(f"banca {banca}: {'abre' if abierto else 'cierra'}")
    if abierto and c.get("al_abrir") == "armar":
        try:
            plugin.llamar("banca_al_aire", {"banca": banca, "modo": "armar"})
        except RuntimeError as e:
            plugin.log(f"banca_al_aire: {e}")


def leer_lineas(f):
    patron = re.compile(c.get("patron") or r"MIC\s+(?P<banca>\w+)\s+(?P<estado>ON|OFF|1|0)", re.IGNORECASE)
    for linea in f:
        m = patron.search(linea.decode("utf-8", "replace") if isinstance(linea, bytes) else linea)
        if m:
            cambio(m.group("banca"), m.group("estado").upper() in ("ON", "1", "ABIERTO", "TRUE"))


def modo_tcp():
    while plugin.andando:
        try:
            with socket.create_connection((c["host"], int(c.get("puerto", 9000))), timeout=float(c.get("conectar_s", 10))) as s:
                plugin.estado(f"conectado a {c['host']}:{c.get('puerto')}")
                s.settimeout(None)
                leer_lineas(s.makefile("rb"))
        except OSError as e:
            plugin.estado(f"sin conexión con el sistema: {e}", ok=False)
        time.sleep(float(c.get("reconectar_s", 5)))


def modo_escucha():
    srv = socket.create_server(("0.0.0.0", int(c.get("puerto", 9000))))
    plugin.estado(f"esperando al sistema en el puerto {c.get('puerto')}")
    while plugin.andando:
        con, quien = srv.accept()
        plugin.log(f"se conectó {quien[0]}")
        threading.Thread(target=lambda: leer_lineas(con.makefile("rb")), daemon=True).start()


def modo_simulador():
    # Las bancas del cuerpo (si hay), o 1..8.
    try:
        lista = [b["banca"] for b in plugin.llamar("bancas_listar").get("bancas", []) if b.get("banca")]
    except (RuntimeError, AttributeError):
        lista = []
    lista = lista or [str(i) for i in range(1, 9)]
    plugin.estado(f"simulador: {len(lista)} bancas")
    while plugin.andando:
        b = random.choice(lista)
        cambio(b, True)
        time.sleep(max(3, float(c.get("simulador_cada_s", 12))))
        cambio(b, False)
        time.sleep(1)


plugin.panel([ba.seccion("Micrófonos de banca"), ba.indicador("abiertos", "Abiertos ahora")], "Conferencia del recinto")


@plugin.control("abrir", "Simula que se abre (o cierra con abierto=false) el micrófono de una `banca`.",
                {"banca": {"type": "string"}, "abierto": {"type": "boolean"}})
def control_abrir(args):
    cambio(args.get("banca", "1"), bool(args.get("abierto", True)))
    return {"ok": True}


@plugin.al_iniciar
def arrancar():
    modo = c.get("modo", "simulador")
    {"tcp_lineas": modo_tcp, "escucha_tcp": modo_escucha}.get(modo, modo_simulador)()


plugin.correr()
