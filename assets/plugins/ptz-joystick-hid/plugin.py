"""Joystick HID para el PTZ (ver plugin.json). Escrito para que el asistente lo adapte."""
import time

import ba_plugin as ba

plugin = ba.Plugin()
c = plugin.config


def eje(reporte, offset, centro, zona, invertir=False):
    if offset is None or offset >= len(reporte):
        return 0.0
    v = (reporte[offset] - centro) / float(centro)
    if invertir:
        v = -v
    if abs(v) < zona:
        return 0.0
    # Fuera de la zona muerta, de 0 a 1 (suave cerca del centro).
    s = (abs(v) - zona) / (1 - zona)
    return round(max(-1.0, min(1.0, (s * s) * (1 if v > 0 else -1))), 2)


@plugin.al_iniciar
def leer():
    import hid

    vid = int(str(c.get("vid") or "0"), 16)
    pid = int(str(c.get("pid") or "0"), 16)
    if not vid:
        plugin.aviso("Joystick PTZ: falta el vid/pid del aparato (dispositivos_listar)", "warning")
        return
    d = hid.device()
    d.open(vid, pid)
    d.set_nonblocking(1)
    plugin.estado(f"joystick {vid:04x}:{pid:04x} → {c.get('camara')}")
    off = c.get("offsets", {})
    centro = int(c.get("centro", 128))
    zona = float(c.get("zona_muerta", 0.12))
    botones = c.get("botones", {})
    ultimo_mov, ultimos_botones, quieto = 0.0, 0, True
    while plugin.andando:
        r = d.read(64)
        if r:
            pan = eje(r, off.get("pan"), centro, zona)
            tilt = eje(r, off.get("tilt"), centro, zona, bool(c.get("invertir_tilt", True)))
            zoom = eje(r, off.get("zoom"), centro, zona)
            ahora = time.monotonic()
            moviendo = pan or tilt or zoom
            # Cada 100 ms mientras se mueve; el movimiento se para solo a los 300 ms (seguridad).
            if moviendo and ahora - ultimo_mov >= 0.1:
                ultimo_mov = ahora
                quieto = False
                try:
                    plugin.llamar("ptz_mover", {"entrada": c.get("camara"), "pan": pan, "tilt": tilt, "zoom": zoom,
                                                "duracion_ms": 300}, timeout=2)
                except RuntimeError as e:
                    plugin.log(f"ptz_mover: {e}")
            elif not moviendo and not quieto:
                quieto = True  # no hace falta mandar nada: el último movimiento se para solo a los 300 ms
            # Botones (un bit por botón) -> presets.
            b = botones.get("byte")
            if b is not None and b < len(r):
                nuevos = r[b] & ~ultimos_botones
                for i, preset in enumerate(botones.get("presets", [])):
                    if nuevos & (1 << i):
                        try:
                            plugin.llamar("ptz_preset", {"entrada": c.get("camara"), "numero": preset}, timeout=5)
                        except RuntimeError as e:
                            plugin.log(f"preset {preset}: {e}")
                ultimos_botones = r[b]
        time.sleep(0.01)


plugin.correr()
