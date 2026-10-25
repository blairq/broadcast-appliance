"""Ejemplo: un plugin que provee gráficos animados (docs/funciones/GRAFICOS.md, «Gráficos de un plugin»).

Dos formas de hacer un gráfico desde Python:

1. DESCRIBIRLO (``def f(valores) -> dict``): Python arma la descripción con las capas del
   repertorio (texto, panel, barras, torta, medio…) y la dibuja el renderizador de gráficos,
   con sus animaciones de entrada y de cambio. Es lo que conviene casi siempre: las cuentas,
   los datos de afuera o cuántas cosas mostrar los decide Python, y se ve igual de bien que
   un gráfico de fábrica.
2. DIBUJARLO (``def f(lienzo, ms, valores)``): cada cuadro con numpy, para lo que el
   repertorio no tiene. Los cuadros se guardan en datos/.graficos y no se vuelven a dibujar
   con los mismos valores.

Se prueban con ``plugin_probar`` y, ya activo, con ``grafico_probar {"nombre":
"plugin-ejemplo-grafico-marcador"}`` (el nombre que le da la app: plugin-<id>-<nombre>).
"""

import math

import ba_plugin as ba

plugin = ba.Plugin()


@plugin.grafico(
    "marcador",
    titulo="Marcador (de un plugin)",
    descripcion="Barras con los puntos de cada uno y quién va ganando. Al cambiar los puntos al aire, "
                "las barras se mueven sin volver a entrar.",
    parametros=[
        {"clave": "titulo", "titulo": "Título", "tipo": "texto", "porDefecto": "Votación en vivo",
         "ayuda": "Arriba de las barras."},
        {"clave": "puntos", "titulo": "Puntos", "tipo": "datos", "porDefecto": "Sí: 21; No: 9 #c62828; Abstenciones: 3",
         "ayuda": "«Nombre: número» separados por punto y coma; el color al final es opcional."},
    ],
    ancho=1000, alto=420, duracion_ms=1500,
)
def marcador(valores):
    puntos = ba_datos(valores.get("puntos"))
    # Lo que se calcula en Python: quién va ganando y por cuánto.
    if len(puntos) >= 2:
        orden = sorted(puntos, key=lambda p: p[1], reverse=True)
        dif = orden[0][1] - orden[1][1]
        bajada = "Empate" if dif == 0 else f"Gana «{orden[0][0]}» por {dif:g}"
    else:
        bajada = ""
    return {"declarativo": {
        "ancho": 1000, "alto": 420,
        "entrada": {"efecto": "desliza-izquierda", "duracion": 450},
        "capas": [
            {"tipo": "panel", "izquierda": 0, "arriba": 0, "ancho": 1000, "alto": 420, "color": "#0d1014",
             "opacidad": 0.92},
            {"tipo": "barra", "izquierda": 0, "arriba": 0, "ancho": 8, "alto": 420, "color": "#ff6a2b"},
            {"tipo": "texto", "id": "titulo", "valor": "{titulo}", "tamano": 36, "peso": "semi",
             "izquierda": 40, "arriba": 24},
            {"tipo": "texto", "id": "bajada", "valor": bajada, "tamano": 22, "color": "#b9c0c6",
             "izquierda": 40, "arriba": 74, "aparece": 300},
            # «{puntos}» (y no la lista ya armada): así, cuando el valor cambia al aire, las barras
            # se mueven de los números de antes a los nuevos.
            {"tipo": "barras", "datos": "{puntos}", "orientacion": "horizontal", "mostrar": "ambos",
             "izquierda": 40, "arriba": 130, "ancho": 920, "alto": 260, "aparece": 250, "tamanoTexto": 24},
        ],
    }}


def ba_datos(v):
    """Los puntos como [(nombre, número)] (lo mínimo para las cuentas; el gráfico lee el texto entero)."""
    if isinstance(v, list):
        return [(str(x.get("etiqueta", "")), float(x.get("valor", 0))) for x in v if isinstance(x, dict)]
    out = []
    for parte in str(v or "").replace("\n", ";").split(";"):
        if ":" in parte:
            nombre, resto = parte.split(":", 1)
            try:
                out.append((nombre.strip(), float(resto.split()[0].replace(",", "."))))
            except (ValueError, IndexError):
                pass
    return out


@plugin.grafico(
    "ondas",
    titulo="Ondas (dibujado en Python)",
    descripcion="Un ecualizador que sube y baja en bucle, con transparencia. Dibujado cuadro a cuadro con numpy.",
    parametros=[
        {"clave": "color", "titulo": "Color", "tipo": "color", "porDefecto": "#ff6a2b"},
        {"clave": "barras", "titulo": "Barras", "tipo": "numero", "porDefecto": 24, "ayuda": "Entre 4 y 64."},
    ],
    ancho=600, alto=160, duracion_ms=3000, bucle=True,
)
def ondas(lienzo, ms, valores):
    alto, ancho = lienzo.shape[:2]
    n = max(4, min(64, int(float(valores.get("barras", 24)))))
    c = str(valores.get("color", "#ff6a2b")).lstrip("#")
    r, g, b = (int(c[i:i + 2], 16) for i in (0, 2, 4)) if len(c) >= 6 else (255, 106, 43)
    t = ms / 3000.0  # una vuelta entera en los 3 s: el bucle no salta
    paso = ancho / n
    for i in range(n):
        fase = i / n
        h = 0.15 + 0.85 * (0.5 + 0.5 * math.sin(2 * math.pi * (2 * t + 1.5 * fase))) * \
            (0.6 + 0.4 * math.sin(2 * math.pi * (t + fase)) ** 2)
        y0 = int(alto * (1 - h))
        x0, x1 = int(i * paso + paso * 0.15), int((i + 1) * paso - paso * 0.15)
        lienzo[y0:, x0:x1] = (r, g, b, 235)
        lienzo[y0:y0 + 3, x0:x1] = (255, 255, 255, 255)  # la tapa, más clara
    return lienzo


plugin.correr()
