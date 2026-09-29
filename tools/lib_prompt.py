"""El prompt del modelo local. UNA sola vez, para todos.

POR QUE VIVE ACA Y NO EN CADA SCRIPT. Si el conjunto de entrenamiento se arma
con una forma de prompt y despues se sirve con otra, el modelo se degrada y no
hay como darse cuenta mirando: parece que "no aprendio". Se midio en este
mismo proyecto, por accidente: con el sistema pidiendo JSON y la gramatica
dejando escribir solo el nombre, el acierto se cayo de 56% a 16%.

Asi que el banco, el generador del corpus y —el dia que exista— el servidor
llaman todos a estas funciones. Si el prompt cambia, cambia para los tres.
"""
import json
import re
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
INVENTARIO = RAIZ / "docs" / "mcp.json"

# Lo que se usa cuando algo se rompe en vivo. NO son las 291: el repertorio
# reactivo es chico y FIJO, y por eso se puede cachear entero.
REACTIVAS = ["audio", "escenas", "fuentes", "sala", "oradores", "imagenes", "estado", "salidas"]

SISTEMA_NOMBRE = """Sos el asistente de un estudio de television EN VIVO. El operador te habla apurado y corto.
Contestas UNICAMENTE el nombre de la herramienta que corresponde. Una palabra, nada mas.
Si te piden HACER algo, elegi la que lo HACE. Los listados son para cuando preguntan QUE HAY.
Ante un problema de audio o de imagen, MEDIR primero tambien vale.

Herramientas:
"""


def inventario():
    return json.loads(INVENTARIO.read_text(encoding="utf-8"))


def herramientas_reactivas(inv=None):
    inv = inv or inventario()
    return sorted(h["nombre"] for h in inv["herramientas"] if h["categoria"] in REACTIVAS)


def fichas(inv=None):
    """Cortas: que hace y que pide. Enteras son 7.200 tokens y la mayor parte
    es prosa que no hace falta para ELEGIR."""
    inv = inv or inventario()
    out = []
    for h in inv["herramientas"]:
        if h["categoria"] not in REACTIVAS:
            continue
        desc = re.split(r"(?<=[.!?])\s", h["descripcion"].replace("\n", " "))[0][:180]
        req = h["esquema"].get("required", [])
        props = list((h["esquema"].get("properties") or {}).keys())
        args = ", ".join(f"{p}*" if p in req else p for p in props[:6])
        out.append(f"- {h['nombre']}({args}): {desc}")
    return sorted(out)


def prefijo(inv=None):
    """La parte FIJA: sistema + fichas. Es lo unico que se cachea, asi que
    tiene que ser identica byte a byte en cada pedido."""
    return SISTEMA_NOMBRE + "\n".join(fichas(inv))


def partes(inv=None):
    """(cabeza, cola) alrededor de la frase del operador.

    Lo variable va DESPUES de lo fijo: el cache es de prefijo. Y `<think>`
    vacio es el modo no-thinking de Qwen3, escrito en el propio prompt para
    que no dependa de una opcion del servidor.
    """
    cabeza = f"<|im_start|>system\n{prefijo(inv)}<|im_end|>\n<|im_start|>user\n"
    cola = "<|im_end|>\n<|im_start|>assistant\n<think>\n\n</think>\n\n"
    return cabeza, cola


def gramatica_nombre(nombres):
    """Solo un nombre que exista. No es un pedido: es imposible desviarse."""
    return "root ::= " + " | ".join('"%s"' % n for n in nombres) + "\n"
