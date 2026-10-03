"""Ejemplo: desdoblar un ojo de pez (y apuntar la vista hacia quien habla).

La tarjeta «desdoblar» recibe el cuadro de la entrada (tamaño del lienzo) y lo reemplaza
por una vista "normal" (perspectiva) de una parte del ojo de pez. Los mapas de píxeles se
calculan UNA vez por combinación de parámetros (es lo caro) y por cuadro solo se indexa.

Parámetros de la tarjeta: lado ("unico" | "izquierdo" | "derecho": cuál de dos ojos de pez
lado a lado), campo_entrada (grados que cubre el ojo de pez, ~190), campo (grados de la vista,
~70), yaw y pitch (grados). Con `seguir_voz`, el yaw sale del último evento «hablando» con
`angulo` (de cualquier plugin): un canal de datos manejando el video.
"""
import math
import threading

import numpy as np

import ba_plugin as ba

plugin = ba.Plugin()
voz = {"yaw": None}
_mapas = {}
_lock = threading.Lock()


def mapas(alto, ancho, lado, campo_in, campo_out, yaw, pitch):
    clave = (alto, ancho, lado, round(campo_in, 1), round(campo_out, 1), round(yaw, 1), round(pitch, 1))
    with _lock:
        if clave in _mapas:
            return _mapas[clave]
    # El ojo de pez de origen: un círculo en el cuadro (o en una mitad).
    x0, w_in = (0, ancho) if lado == "unico" else ((0, ancho // 2) if lado == "izquierdo" else (ancho // 2, ancho // 2))
    cx, cy, radio = x0 + w_in / 2, alto / 2, min(w_in, alto) / 2
    f_in = radio / math.radians(campo_in / 2)  # equidistante: r = f * theta
    # La vista de salida (perspectiva) con su dirección.
    f_out = (ancho / 2) / math.tan(math.radians(campo_out / 2))
    u, v = np.meshgrid(np.arange(ancho, dtype=np.float32) - ancho / 2, np.arange(alto, dtype=np.float32) - alto / 2)
    d = np.stack([u, v, np.full_like(u, f_out)], axis=-1)
    d /= np.linalg.norm(d, axis=-1, keepdims=True)
    cy_, sy_ = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    cp, sp = math.cos(math.radians(pitch)), math.sin(math.radians(pitch))
    rot = np.array([[cy_, 0, sy_], [0, 1, 0], [-sy_, 0, cy_]], np.float32) @ np.array([[1, 0, 0], [0, cp, -sp], [0, sp, cp]], np.float32)
    d = d @ rot.T
    theta = np.arccos(np.clip(d[..., 2], -1, 1))
    phi = np.arctan2(d[..., 1], d[..., 0])
    sx = cx + f_in * theta * np.cos(phi)
    sy = cy + f_in * theta * np.sin(phi)
    fuera = (theta > math.radians(campo_in / 2)) | (sx < x0) | (sx >= x0 + w_in) | (sy < 0) | (sy >= alto)
    mx = np.clip(sx, 0, ancho - 1).astype(np.int32)
    my = np.clip(sy, 0, alto - 1).astype(np.int32)
    # Índice plano (un píxel RGBA = un uint32): por cuadro es UNA indexación.
    r = ((my * ancho + mx).reshape(-1), fuera.reshape(-1))
    with _lock:
        if len(_mapas) > 16:  # con seguir_voz el yaw cambia: no guardar infinitos
            _mapas.clear()
        _mapas[clave] = r
    return r


@plugin.tarjeta("desdoblar", descripcion="Ojo de pez -> vista normal (yaw/pitch/campo); sigue la voz si se pide",
                parametros={"lado": "unico", "campo_entrada": 190.0, "campo": 70.0, "yaw": 0.0, "pitch": 0.0})
def desdoblar(cuadro, params):
    a = cuadro.array()
    yaw = float(params.get("yaw", 0.0))
    if plugin.config.get("seguir_voz") and voz["yaw"] is not None:
        yaw = voz["yaw"]
    idx, fuera = mapas(cuadro.alto, cuadro.ancho, params.get("lado", "unico"), float(params.get("campo_entrada", 190)),
                       float(params.get("campo", 70)), yaw, float(params.get("pitch", 0.0)))
    px = np.ascontiguousarray(a).view(np.uint32).reshape(-1)
    salida = px[idx]
    salida[fuera] = np.array([0, 0, 0, 255], np.uint8).view(np.uint32)[0]
    a[:] = salida.view(np.uint8).reshape(a.shape)


@plugin.escuchar("*/hablando")
def hacia_la_voz(datos, de):
    if isinstance(datos, dict) and "angulo" in datos:
        objetivo = float(datos["angulo"])
        if objetivo > 180:
            objetivo -= 360
        actual = voz["yaw"] if voz["yaw"] is not None else objetivo
        k = float(plugin.config.get("suavizado", 0.25))
        # Cuantizado a 2 grados: menos mapas distintos que calcular.
        voz["yaw"] = round((actual + (objetivo - actual) * (1 - k)) / 2) * 2


@plugin.control("mirar", descripcion="Apunta la vista hacia `yaw` grados", parametros={"yaw": "grados"})
def mirar(args):
    voz["yaw"] = float(args.get("yaw", 0))
    return {"yaw": voz["yaw"]}


plugin.correr()
