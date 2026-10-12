"""Cámara virtual VISCA-over-IP (ver plugin.json). Escrito para que el asistente lo adapte.

Paquete VISCA-over-IP (Sony): 8 bytes de cabecera + el comando VISCA.
  tipo (2): 0x0100 comando, 0x0110 consulta, 0x0200 control; largo (2); secuencia (4).
Respuesta: tipo 0x0111 con la misma secuencia: ACK (90 41 FF) y completion (90 51 FF).
"""
import socket
import struct
import threading
import time

import ba_plugin as ba

plugin = ba.Plugin()
c = plugin.config

# Lo que se está moviendo en cada cámara (VISCA deja el movimiento «enganchado»
# hasta el stop; la app mueve por tramos con auto-stop, así que se refresca).
movimientos = {}  # camara -> {"pan","tilt","zoom","desde"}
lock = threading.Lock()


def responder(sock, dest, seq, cuerpo):
    sock.sendto(struct.pack(">HHI", 0x0111, len(cuerpo), seq) + bytes(cuerpo), dest)


def llamar(herr, args):
    try:
        plugin.llamar(herr, args, timeout=3)
    except RuntimeError as e:
        plugin.log(f"{herr}: {e}")


def mover(camara, pan=None, tilt=None, zoom=None):
    with lock:
        m = movimientos.setdefault(camara, {"pan": 0.0, "tilt": 0.0, "zoom": 0.0, "desde": 0.0})
        if pan is not None:
            m["pan"] = pan
        if tilt is not None:
            m["tilt"] = tilt
        if zoom is not None:
            m["zoom"] = zoom
        m["desde"] = time.monotonic()
        quieto = not (m["pan"] or m["tilt"] or m["zoom"])
    if quieto:
        llamar("ptz_parar", {"entrada": camara})  # para ya, sin esperar el auto-stop


def refrescar():
    """Mientras dure un movimiento, un tramo de 400 ms cada 200 ms (si se corta, para solo)."""
    tope = float(c.get("tope_movimiento_s", 10))
    while plugin.andando:
        ahora = time.monotonic()
        with lock:
            vivos = {k: dict(v) for k, v in movimientos.items() if v["pan"] or v["tilt"] or v["zoom"]}
        for camara, m in vivos.items():
            if ahora - m["desde"] > tope:  # nadie mandó el stop: por seguridad, se para
                mover(camara, 0.0, 0.0, 0.0)
                plugin.log(f"{camara}: movimiento sin stop por {tope:.0f} s, se paró")
                continue
            llamar("ptz_mover", {"entrada": camara, "pan": m["pan"], "tilt": m["tilt"], "zoom": m["zoom"],
                                 "duracion_ms": 400})
        time.sleep(0.2)


def atender(camara, v):
    """Un comando VISCA (sin el 8x del principio ni el FF del final ya validados)."""
    # Pan/tilt drive: 01 06 01 VV WW XX YY
    if v[:3] == [0x01, 0x06, 0x01] and len(v) >= 7:
        vv, ww, xx, yy = v[3], v[4], v[5], v[6]
        pan = {0x01: -1, 0x02: 1}.get(xx, 0) * min(1.0, vv / 0x18)
        tilt = {0x01: 1, 0x02: -1}.get(yy, 0) * min(1.0, ww / 0x17)
        mover(camara, round(pan, 2), round(tilt, 2))
        return True
    # Home: 01 06 04 -> el preset que se configure como «home» (de fábrica, el 1)
    if v[:3] == [0x01, 0x06, 0x04]:
        llamar("ptz_preset", {"entrada": camara, "numero": int(c.get("home_preset", 1)), "accion": "ir"})
        return True
    # Zoom: 01 04 07 2p (tele) / 3p (wide) / 00 (stop) / 02 / 03 (estándar)
    if v[:3] == [0x01, 0x04, 0x07] and len(v) >= 4:
        b = v[3]
        if b == 0x00:
            z = 0.0
        elif b in (0x02, 0x03):
            z = 0.5 if b == 0x02 else -0.5
        elif (b & 0xF0) in (0x20, 0x30):
            z = (1 if (b & 0xF0) == 0x20 else -1) * max(0.15, (b & 0x0F) / 7.0)
        else:
            return False
        mover(camara, zoom=round(z, 2))
        return True
    # Presets: 01 04 3F 02 pp (ir) / 01 pp (guardar)
    if v[:3] == [0x01, 0x04, 0x3F] and len(v) >= 5 and v[3] in (0x01, 0x02):
        llamar("ptz_preset", {"entrada": camara, "numero": v[4] + 1, "accion": "ir" if v[3] == 0x02 else "guardar"})
        return True
    return False


def escuchar(puerto, camara):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind((c.get("escuchar_en", "0.0.0.0"), int(puerto)))
    s.settimeout(0.5)
    plugin.log(f"VISCA-IP: {camara} en UDP {puerto}")
    while plugin.andando:
        try:
            datos, dest = s.recvfrom(1024)
        except socket.timeout:
            continue
        if len(datos) < 9:
            continue
        tipo, largo, seq = struct.unpack(">HHI", datos[:8])
        cuerpo = list(datos[8:8 + largo])
        if tipo == 0x0200:  # control (p. ej. reset de secuencia): se contesta igual
            s.sendto(struct.pack(">HHI", 0x0201, 1, seq) + b"\x01", dest)
            continue
        if not cuerpo or cuerpo[-1] != 0xFF or (cuerpo[0] & 0xF0) != 0x80:
            continue
        v = cuerpo[1:-1]
        if tipo == 0x0110:  # consultas: lo mínimo para que el controlador nos crea una cámara
            if v[:3] == [0x09, 0x04, 0x00]:  # power
                responder(s, dest, seq, [0x90, 0x50, 0x02, 0xFF])
            else:
                responder(s, dest, seq, [0x90, 0x60, 0x02, 0xFF])  # syntax error: no la sabemos
            continue
        responder(s, dest, seq, [0x90, 0x41, 0xFF])  # ACK
        ok = atender(camara, v)
        responder(s, dest, seq, [0x90, 0x51, 0xFF] if ok else [0x90, 0x61, 0x02, 0xFF])


@plugin.al_iniciar
def arrancar():
    camaras = c.get("camaras") or {}
    if not camaras:
        plugin.aviso("VISCA-IP: falta «camaras» (puerto UDP → cámara)", "warning")
        return
    for puerto, camara in camaras.items():
        threading.Thread(target=escuchar, args=(puerto, camara), daemon=True).start()
    plugin.estado("cámara virtual VISCA-IP: " + ", ".join(f"{p}→{n}" for p, n in camaras.items()))
    refrescar()


plugin.correr()
