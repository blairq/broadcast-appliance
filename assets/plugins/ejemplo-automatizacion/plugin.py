"""Ejemplo: una automatización que escucha el estudio y actúa.

El molde de «cuando pasa X, hacé Y» que los disparadores del sistema no cubren:
  - ESCUCHAR: ``@plugin.escuchar("<tema>")`` recibe los eventos de la app (la lista completa la da la
    herramienta ``eventos_temas``; los más usados: ``aire/escena``, ``aire/tally``, ``salida/stream``,
    ``salida/grabacion``, ``audio/nivel/<fuente>``, ``audio/loudness``, ``oradores/al_aire``,
    ``alarma/#``) y los de otros plugins (``"otro-plugin/canal"``, ``"*/hablando"``).
  - ACTUAR: ``plugin.llamar("<herramienta>", {...})`` usa cualquier herramienta del sistema, las
    mismas del asistente. La lista ``"herramientas"`` del plugin.json lo limita a esas (acá, solo
    ``operator_message``): conviene ponerla siempre, así se sabe qué puede tocar. Lo que cambia el
    aire espera a una persona mientras se transmite (igual que con el asistente).
  - GUARDAR: ``plugin.datos`` es su carpeta (sobrevive a reinicios y actualizaciones).
  - MOSTRAR: ``plugin.panel`` + ``plugin.mostrar``; ``@plugin.control`` para lo que pide el asistente.

Lo que hace este: lleva la minuta de lo que salió al aire (datos/minuta.csv: cada escena con su
hora y cuánto estuvo), le recuerda al operador cada tantos minutos cuánto lleva transmitiendo y, al
cortar, le manda el resumen.
"""
import csv
import threading
import time
from datetime import datetime

import ba_plugin as ba

plugin = ba.Plugin()

# Los eventos llegan en varios hilos a la vez (uno por evento): todo el estado, con candado.
_lock = threading.Lock()
_estado = {"escena": "", "desde": time.time(), "aire_desde": None, "recordado": 0, "cambios": 0}
_por_escena: dict[str, float] = {}  # segundos al aire de cada escena (desde que arrancó la transmisión)
MINUTA = plugin.datos / "minuta.csv"


def _minutos(s: float) -> str:
    s = int(s)
    return f"{s // 3600}:{s // 60 % 60:02d}:{s % 60:02d}" if s >= 3600 else f"{s // 60}:{s % 60:02d}"


def _avisar(texto: str, prioridad: str = "info") -> None:
    """Al operador, por la barra de abajo (una herramienta del sistema)."""
    try:
        plugin.llamar("operator_message", {"text": texto, "priority": prioridad}, timeout=10)
    except RuntimeError as e:  # sin permiso, la app ocupada…: que quede en el registro y se siga
        plugin.log("no pude avisar:", e)


_simulando = threading.Event()  # el control «simular» no ensucia la minuta


def _anotar(escena: str, desde: float, hasta: float) -> None:
    if _simulando.is_set():
        return
    nuevo = not MINUTA.exists()
    with MINUTA.open("a", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        if nuevo:
            w.writerow(["desde", "hasta", "escena", "segundos", "transmitiendo"])
        w.writerow([datetime.fromtimestamp(desde).isoformat(timespec="seconds"),
                    datetime.fromtimestamp(hasta).isoformat(timespec="seconds"), escena, round(hasta - desde),
                    "si" if _estado["aire_desde"] else "no"])


def _resumen() -> str:
    if not _por_escena:
        return "Todavía no salió nada al aire."
    total = sum(_por_escena.values())
    partes = sorted(_por_escena.items(), key=lambda kv: -kv[1])[:5]
    return f"Al aire {_minutos(total)}: " + ", ".join(f"{e} {_minutos(s)}" for e, s in partes)


# --- Escuchar ---------------------------------------------------------------------------------

@plugin.escuchar("aire/escena")
def al_cambiar_escena(datos, de):
    escena = (datos or {}).get("escena", "")
    ahora = time.time()
    with _lock:
        if escena == _estado["escena"]:
            return
        anterior, desde = _estado["escena"], _estado["desde"]
        if anterior:
            _anotar(anterior, desde, ahora)
            if _estado["aire_desde"]:
                _por_escena[anterior] = _por_escena.get(anterior, 0.0) + (ahora - desde)
        _estado.update(escena=escena, desde=ahora, cambios=_estado["cambios"] + 1)
        cambios = _estado["cambios"]
    plugin.mostrar("escena", escena)
    plugin.mostrar("cambios", cambios)
    plugin.evento("escena", {"escena": escena, "anterior": anterior})


@plugin.escuchar("salida/stream")
def al_cambiar_transmision(datos, de):
    activo = bool((datos or {}).get("activo"))
    ahora = time.time()
    resumen = None
    with _lock:
        if activo and not _estado["aire_desde"]:
            _estado.update(aire_desde=ahora, recordado=0, desde=ahora)
            _por_escena.clear()
        elif not activo and _estado["aire_desde"]:
            if _estado["escena"]:
                e = _estado["escena"]
                _por_escena[e] = _por_escena.get(e, 0.0) + (ahora - _estado["desde"])
                _anotar(e, _estado["desde"], ahora)
                _estado["desde"] = ahora
            _estado["aire_desde"] = None
            resumen = _resumen()
        else:
            return  # sin cambios (este tema llega varias veces por segundo)
    plugin.mostrar("al_aire", "sí" if activo else "no")
    if resumen and plugin.valores.get("resumen_al_cortar", True):
        _avisar("Terminó la transmisión. " + resumen, "success")


# --- Lo que corre solo ------------------------------------------------------------------------

@plugin.cada(5)
def recordar():
    with _lock:
        desde = _estado["aire_desde"]
        if not desde:
            return
        cada = max(1.0, float(plugin.valores.get("cada_min", plugin.config.get("recordar_cada_min", 30))))
        transcurrido = time.time() - desde
        tocan = int(transcurrido // (cada * 60))
        if tocan <= _estado["recordado"]:
            plugin.mostrar("tiempo", _minutos(transcurrido))
            return
        _estado["recordado"] = tocan
    plugin.mostrar("tiempo", _minutos(transcurrido))
    if plugin.valores.get("recordar", True):
        _avisar(f"Llevás {_minutos(transcurrido)} al aire.")


# --- Panel del operador y control del asistente ---------------------------------------------

def mandar_resumen():
    with _lock:
        texto = _resumen()
    _avisar(texto)


plugin.panel([
    ba.seccion("Recordatorios"),
    ba.interruptor("recordar", "Recordar cuánto llevo al aire", defecto=True),
    ba.numero("cada_min", "Cada", defecto=plugin.config.get("recordar_cada_min", 30), minimo=1, maximo=240, unidad="min"),
    ba.interruptor("resumen_al_cortar", "Resumen al cortar la transmisión", defecto=True),
    ba.boton("resumen", "Mandar el resumen ahora", al_tocar=mandar_resumen),
    ba.seccion("Lo que pasa"),
    ba.indicador("escena", "Al aire"),
    ba.indicador("al_aire", "Transmitiendo"),
    ba.indicador("tiempo", "Tiempo al aire"),
    ba.indicador("cambios", "Cambios de escena"),
])


@plugin.control("resumen", descripcion="Cuánto estuvo cada escena al aire en esta transmisión y dónde está la minuta")
def control_resumen(args):
    with _lock:
        return {"resumen": _resumen(), "por_escena_s": {k: round(v) for k, v in _por_escena.items()},
                "minuta": str(MINUTA)}


@plugin.control("simular", descripcion="Prueba sin transmitir: hace como que sale al aire, pasa por unas escenas y corta "
                "(no escribe la minuta)", parametros={"escenas": ["General", "Orador", "General"]})
def control_simular(args):
    _simulando.set()
    try:
        al_cambiar_transmision({"activo": True}, "simulacion")
        for e in args.get("escenas") or ["General", "Orador"]:
            al_cambiar_escena({"escena": str(e)}, "simulacion")
            time.sleep(0.05)
        al_cambiar_transmision({"activo": False}, "simulacion")
    finally:
        _simulando.clear()
    with _lock:
        return {"resumen": _resumen()}


plugin.estado("escuchando el estudio")
plugin.correr()
