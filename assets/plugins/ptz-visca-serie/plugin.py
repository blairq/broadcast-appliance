"""VISCA por puerto serie (ver plugin.json). Escrito para que el asistente lo adapte."""
import threading

import ba_plugin as ba

plugin = ba.Plugin()
c = plugin.config
lock = threading.Lock()
estado = {"puerto": None}


def abrir():
    import serial
    import serial.tools.list_ports

    nombre = c.get("puerto") or ""
    if not nombre:
        # El primer adaptador USB a serie que aparezca.
        for p in serial.tools.list_ports.comports():
            if "usb" in (p.device + " " + (p.description or "")).lower():
                nombre = p.device
                break
    if not nombre:
        raise RuntimeError("no encontré el puerto serie (poné «puerto» en la configuración)")
    estado["puerto"] = serial.Serial(nombre, int(c.get("baudios", 9600)), timeout=0.2)
    plugin.estado(f"VISCA por {nombre}")


def mandar(*cuerpo):
    """Un comando VISCA: 8x … FF (x = dirección de la cámara)."""
    pkt = bytes([0x80 | int(c.get("direccion", 1))] + list(cuerpo) + [0xFF])
    with lock:
        if estado["puerto"] is None:
            abrir()
        estado["puerto"].write(pkt)
        estado["puerto"].read(16)  # ACK / completion (se ignoran)


def velocidad(v, maximo):
    return max(1, min(maximo, int(round(abs(v) * maximo))))


@plugin.ptz(c.get("entrada", "Cámara 1"))
class CamaraVisca:
    capacidades = {"panTilt": True, "zoom": True, "foco": False, "autoFoco": True, "home": True,
                   "presets": int(c.get("presets", 16))}

    def mover(self, pan, tilt, zoom):
        # Pan/tilt: 81 01 06 01 VV WW XX YY FF (XX: 01 izq, 02 der, 03 quieto; YY: 01 arriba, 02 abajo, 03 quieto)
        x = 0x03 if abs(pan) < 0.02 else (0x02 if pan > 0 else 0x01)
        y = 0x03 if abs(tilt) < 0.02 else (0x01 if tilt > 0 else 0x02)
        mandar(0x01, 0x06, 0x01, velocidad(pan, 0x18), velocidad(tilt, 0x14), x, y)
        # Zoom: 81 01 04 07 2p (acercar) / 3p (alejar) / 00 (parar)
        if abs(zoom) < 0.02:
            mandar(0x01, 0x04, 0x07, 0x00)
        else:
            mandar(0x01, 0x04, 0x07, (0x20 if zoom > 0 else 0x30) | velocidad(zoom, 7))

    def parar(self):
        mandar(0x01, 0x06, 0x01, 0x01, 0x01, 0x03, 0x03)
        mandar(0x01, 0x04, 0x07, 0x00)

    def auto_foco(self, prendido):
        mandar(0x01, 0x04, 0x38, 0x02 if prendido else 0x03)

    def ir_a_preset(self, n):
        mandar(0x01, 0x04, 0x3F, 0x02, int(n) & 0x7F)

    def guardar_preset(self, n):
        mandar(0x01, 0x04, 0x3F, 0x01, int(n) & 0x7F)

    def home(self):
        mandar(0x01, 0x06, 0x04)


@plugin.al_terminar
def cerrar():
    if estado["puerto"] is not None:
        estado["puerto"].close()


plugin.correr()
