"""`capas_del`, compartida por el que arma el vector y el que lo mide.

Existe porque unsloth envuelve el modelo en varias capas de adaptadores y la
ruta hasta los bloques del transformer cambia segun como se cargo. Buscarla en
dos lugares distintos es como se termina aplicando el vector en un lado y
midiendolo en otro.
"""


def capas_del(modelo):
    m = modelo
    for _ in range(6):
        if hasattr(m, "layers"):
            return m.layers
        for att in ("model", "base_model", "transformer"):
            if hasattr(m, att):
                m = getattr(m, att)
                break
        else:
            break
    raise SystemExit("no encontre las capas del modelo")
