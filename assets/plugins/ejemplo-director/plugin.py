"""Ejemplo: un director que cambia de plano según quién habla.

Los equipos (otros plugins) publican ``plugin.evento("hablando", {"persona": 2, "angulo": 37,
"hablando": True})``. Este los escucha a todos (``*/hablando``), decide y maneja el estudio
con ``plugin.llamar(...)`` (las mismas herramientas que el asistente).

`planos` mapea "<plugin>/<persona>" (con * de comodín) a una escena. Reglas: un tiempo mínimo
en cada plano (no saltar a cada tos) y volver al general después de `silencio_s` sin nadie.
"""
import fnmatch
import time

import ba_plugin as ba

plugin = ba.Plugin()
cfg = plugin.config
estado = {"actual": None, "desde": 0.0, "ultimo_habla": 0.0, "propuesto": None}


def escena_de(de, datos):
    clave = f"{de}/{datos.get('persona', datos.get('id', ''))}"
    for patron, escena in cfg.get("planos", {}).items():
        if fnmatch.fnmatch(clave, patron):
            return escena
    return None


def cortar_a(escena, motivo):
    if escena is None or escena == estado["actual"]:
        return
    if time.monotonic() - estado["desde"] < float(cfg.get("minimo_s", 4.0)):
        return  # todavía no: tiempo mínimo en el plano
    if cfg.get("modo") == "proponer":
        if estado["propuesto"] != escena:
            estado["propuesto"] = escena
            plugin.llamar("operator_message", {"text": f"Director: ¿pasar a «{escena}»? ({motivo})", "priority": "info"})
        return
    try:
        plugin.llamar("scene_switch", {"name": escena})
        estado["actual"], estado["desde"] = escena, time.monotonic()
        plugin.estado(f"en «{escena}» ({motivo})")
        plugin.evento("plano", {"escena": escena, "motivo": motivo})
    except RuntimeError as e:
        plugin.estado(f"no pude cambiar a «{escena}»: {e}", ok=False)


@plugin.escuchar("*/hablando")
def al_hablar(datos, de):
    if not isinstance(datos, dict) or not datos.get("hablando", True):
        return
    # Solo los equipos que le importan (config "de": ["kandao-*", ...]; de fábrica, todos).
    if not any(fnmatch.fnmatch(de, p) for p in cfg.get("de", ["*"])):
        return
    estado["ultimo_habla"] = time.monotonic()
    cortar_a(escena_de(de, datos), f"habla {datos.get('persona', '?')} ({de})")


@plugin.cada(1.0)
def silencio():
    if estado["actual"] and time.monotonic() - estado["ultimo_habla"] > float(cfg.get("silencio_s", 6.0)):
        cortar_a(cfg.get("general"), "silencio")


@plugin.control("simular", descripcion="Simula que habla alguien (para probar sin equipos)",
                parametros={"persona": "1, 2, 3…"})
def simular(args):
    plugin.evento("hablando", {"persona": args.get("persona", 1), "hablando": True, "simulado": True})
    return {"simulado": args.get("persona", 1)}


@plugin.control("estado", descripcion="En qué plano está y por qué")
def ver_estado(args):
    return {k: v for k, v in estado.items() if k != "desde"} | {"config": cfg}


plugin.estado(f"esperando a que alguien hable (modo {cfg.get('modo')})")
plugin.correr()
