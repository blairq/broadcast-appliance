"""Controlador MIDI como botonera (ver plugin.json). Escrito para que el asistente lo adapte."""
import threading

import ba_plugin as ba

plugin = ba.Plugin()
sup = plugin.superficie
SID = "midi"
estado = {"in": None, "out": None, "fase": True}


def mapa():
    c = plugin.config
    cols, filas = int(c.get("columnas", 8)), int(c.get("filas", 2))
    notas = {int(k): tuple(v) for k, v in (c.get("mapa", {}).get("notas") or {}).items()}
    ccs = {int(k): tuple(v) for k, v in (c.get("mapa", {}).get("cc") or {}).items()}
    if not notas:
        base = int(c.get("nota_base", 36))
        notas = {base + i: (i % cols, i // cols) for i in range(cols * filas)}
    if not ccs:
        base = int(c.get("cc_base", 1))
        ccs = {base + i: (i, filas - 1) for i in range(cols)}
    return cols, filas, notas, ccs


def elegir(nombres, quiero):
    if not nombres:
        return None
    if quiero:
        for n in nombres:
            if quiero.lower() in n.lower():
                return n
    return nombres[0]


@plugin.al_iniciar
def arrancar():
    import mido

    cols, filas, notas, ccs = mapa()
    ent = elegir(mido.get_input_names(), plugin.config.get("entrada", ""))
    sal = elegir(mido.get_output_names(), plugin.config.get("salida", "") or plugin.config.get("entrada", ""))
    if not ent:
        plugin.aviso("MIDI: no hay ningún controlador enchufado", "warning")
        return
    estado["out"] = mido.open_output(sal) if sal else None
    sup.registrar(SID, ent, modelo="midi", columnas=cols, filas=filas, faders=len(ccs))
    plugin.log(f"MIDI: entrada «{ent}», salida «{sal}»")

    def recibir(m):
        if m.type in ("note_on", "note_off") and m.note in notas:
            x, y = notas[m.note]
            if m.type == "note_on" and m.velocity > 0:
                sup.presionar(SID, x, y)
            else:
                sup.soltar(SID, x, y)
        elif m.type == "control_change" and m.control in ccs:
            x, y = ccs[m.control]
            if plugin.config.get("relativo"):
                # «binary offset»: 64 quieto, 65 = +1, 63 = -1.
                sup.girar(SID, x, y, m.value - 64)
            else:
                c = sup.celda_en(SID, x, y) or {}
                lo, hi = c.get("min", 0), c.get("max", 127)
                sup.valor(SID, x, y, lo + (hi - lo) * m.value / 127)

    estado["in"] = mido.open_input(ent, callback=recibir)


def encender(nota, on):
    if estado["out"] is not None:
        import mido
        estado["out"].send(mido.Message("note_on", note=nota, velocity=127 if on else 0))


def pintar(e):
    _, _, notas, ccs = mapa()
    esperando = e.get("confirmar")
    for nota, (x, y) in notas.items():
        c = sup.celda_en(SID, x, y)
        if not c:
            encender(nota, False)
            continue
        on = bool(c.get("activo")) or c.get("fondo") not in (None, "", "#2b2f3a", "#20242d")
        if esperando == f'{c["x"]},{c["y"]}':
            on = estado["fase"]           # confirmación de aire: titila aunque esté apagado
        elif c.get("titilar"):
            on = on and estado["fase"]
        encender(nota, on)
    # Faders motorizados (o perillas con anillo de LED): el valor actual por CC.
    if estado["out"] is not None:
        import mido
        for cc, (x, y) in ccs.items():
            c = sup.celda_en(SID, x, y)
            if c and c.get("tipo") in ("fader", "perilla") and c.get("max", 0) != c.get("min", 0):
                v = int(127 * (c.get("valor", 0) - c["min"]) / (c["max"] - c["min"]))
                estado["out"].send(mido.Message("control_change", control=cc, value=max(0, min(127, v))))


@sup.al_estado(SID)
def al_estado(sid, e):
    pintar(e)


@plugin.cada(0.5)
def titilar():
    estado["fase"] = not estado["fase"]
    e = sup.estado(SID)
    if e.get("confirmar") or any(c.get("titilar") for c in e.get("celdas", [])):
        pintar(e)


@plugin.al_terminar
def cerrar():
    for k in ("in", "out"):
        if estado[k] is not None:
            estado[k].close()


plugin.correr()
