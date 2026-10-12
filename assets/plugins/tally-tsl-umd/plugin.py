"""Tally TSL UMD v5 desde el bus de eventos (ver plugin.json)."""
import socket
import struct

import ba_plugin as ba

plugin = ba.Plugin()
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
ultimo = {"tally": {}}
orden = []  # fuentes vistas, para los índices automáticos

ROJO, VERDE, APAGADO = 1, 2, 0


def indice(fuente):
    fijos = plugin.config.get("indices") or {}
    if fuente in fijos:
        return int(fijos[fuente])
    if fuente not in orden:
        orden.append(fuente)
    return 100 + orden.index(fuente)  # lejos de los fijos


def paquete(mensajes):
    """TSL UMD v5: PBC, VER, FLAGS, SCREEN y por cada display INDEX, CONTROL, LENGTH, TEXTO."""
    cuerpo = b""
    for idx, tally, texto in mensajes:
        t = texto.encode("ascii", "replace")[:32]
        control = tally | (tally << 2) | (tally << 4) | (3 << 6)  # derecha, texto e izquierda; brillo máximo
        cuerpo += struct.pack("<HHH", idx, control, len(t)) + t
    cabeza = struct.pack("<BBH", 0, 0, int(plugin.config.get("pantalla", 0)))
    return struct.pack("<H", len(cabeza) + len(cuerpo)) + cabeza + cuerpo


def mandar():
    t = ultimo["tally"]
    if not t:
        return
    aire = set(t.get("fuentes_programa") or [])
    armada = set(t.get("fuentes_preview") or []) - aire
    fuentes = list(dict.fromkeys(list(plugin.config.get("indices") or {}) + sorted(aire | armada) + orden))
    msgs = [(indice(f), ROJO if f in aire else VERDE if f in armada else APAGADO, f) for f in fuentes]
    datos = paquete(msgs)
    for d in plugin.config.get("destinos") or []:
        try:
            h, p = d.rsplit(":", 1)
            sock.sendto(datos, (h, int(p)))
        except (OSError, ValueError) as e:
            plugin.log(f"TSL a {d}: {e}")


@plugin.escuchar("aire/tally")
def al_tally(datos, de):
    ultimo["tally"] = datos or {}
    mandar()


@plugin.cada(2)
def repetir():
    mandar()


plugin.correr()
