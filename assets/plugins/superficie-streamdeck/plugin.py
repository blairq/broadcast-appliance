"""Stream Deck como botonera (ver plugin.json). Escrito para que el asistente lo adapte."""
import threading
import time

import ba_plugin as ba

plugin = ba.Plugin()
sup = plugin.superficie
abiertos = {}  # id -> {"deck", "cols", "filas", "tam", "perillas"}
fase = {"on": True}


def id_de(deck):
    try:
        return "sd-" + deck.get_serial_number().strip().replace(" ", "")
    except Exception:  # noqa: BLE001 — algunos modelos no la dan sin abrir
        return "sd-" + str(deck.id())[-8:]


def abrir(deck):
    from StreamDeck.Devices.StreamDeck import DialEventType

    deck.open()
    deck.reset()
    deck.set_brightness(int(plugin.config.get("brillo", 60)))
    filas, cols = deck.key_layout()
    tam = deck.key_image_format()["size"][0] if deck.is_visual() else 0
    sid = id_de(deck)
    perillas = deck.dial_count() if hasattr(deck, "dial_count") else 0
    abiertos[sid] = {"deck": deck, "cols": cols, "filas": filas, "tam": tam, "perillas": perillas}

    def tecla(d, k, apretada):
        x, y = k % cols, k // cols
        (sup.presionar if apretada else sup.soltar)(sid, x, y)

    deck.set_key_callback(tecla)
    if perillas:
        def perilla(d, n, evento, valor):
            # Las perillas manejan las celdas de la fila de abajo de la página.
            y = sup.estado(sid).get("pagina", {}).get("filas", filas + 1) - 1
            if evento == DialEventType.TURN:
                sup.girar(sid, n, y, valor)
            elif evento == DialEventType.PUSH:
                (sup.presionar if valor else sup.soltar)(sid, n, y)
        deck.set_dial_callback(perilla)
    sup.registrar(sid, deck.deck_type(), modelo=deck.deck_type(), columnas=cols, filas=filas,
                  teclas_lcd=deck.is_visual(), tamano_px=tam, perillas=perillas)
    plugin.log(f"abierto {deck.deck_type()} ({sid}), {cols}×{filas}")


def dibujar(sid, estado=None):
    a = abiertos.get(sid)
    if not a or not a["tam"]:
        return
    from StreamDeck.ImageHelpers import PILHelper

    deck, estado = a["deck"], estado or sup.estado(sid)
    esperando = estado.get("confirmar")
    with deck:
        for k in range(a["cols"] * a["filas"]):
            x, y = k % a["cols"], k // a["cols"]
            c = sup.celda_en(sid, x, y)
            img = sup.render_tecla(c, a["tam"], fase_titilar=fase["on"],
                                   esperando=bool(c) and esperando == f'{c["x"]},{c["y"]}')
            deck.set_key_image(k, PILHelper.to_native_key_format(deck, img))


@sup.al_estado("*")
def al_estado(sid, estado):
    dibujar(sid, estado)


@sup.al_aviso
def al_aviso(sid, texto, error):
    plugin.log(f"{sid}: {texto}")


@plugin.al_iniciar
def buscar():
    from ba_plugin.dispositivos import streamdeck_gestor

    gestor = streamdeck_gestor()  # por el hidapi del programa, no el del sistema
    while plugin.andando:
        try:
            vistos = set()
            for deck in gestor.enumerate():
                sid = id_de(deck)
                vistos.add(sid)
                if sid not in abiertos:
                    try:
                        abrir(deck)
                    except Exception as e:  # noqa: BLE001
                        plugin.log(f"no pude abrir un Stream Deck: {e}")
            for sid in list(abiertos):
                d = abiertos[sid]["deck"]
                if sid not in vistos or not d.connected():
                    abiertos.pop(sid, None)
                    sup.quitar(sid)
                    plugin.log(f"se desenchufó {sid}")
        except Exception as e:  # noqa: BLE001 — sin hidapi no hay nada que buscar
            plugin.aviso(f"Stream Deck: {e}", "warning")
            time.sleep(float(plugin.config.get("sin_aparato_s", 30)))
        time.sleep(float(plugin.config.get("buscar_cada_s", 3)))


@plugin.cada(0.5)
def titilar():
    fase["on"] = not fase["on"]
    for sid in list(abiertos):
        e = sup.estado(sid)
        if e.get("confirmar") or any(c.get("titilar") or c.get("borde_titilar") for c in e.get("celdas", [])):
            dibujar(sid, e)


@plugin.al_terminar
def cerrar():
    for a in abiertos.values():
        try:
            with a["deck"]:
                a["deck"].reset()
                a["deck"].close()
        except Exception:  # noqa: BLE001
            pass


plugin.correr()
