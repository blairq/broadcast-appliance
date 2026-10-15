"""El formato NUEVO del modelo local: herramienta + argumentos en el tool calling nativo de Qwen3.5.

Un solo modulo para entrenar Y para servir (como lib_prompt para el formato de
solo nombres): un desajuste entre los dos hunde el acierto (ya paso: 56 % a 16 %).
Ver docs/modelos/entrenamiento-avanzado.md.

EL FORMATO es el del chat_template de Qwen3.5 (no el JSON de Qwen2.5/3): las
herramientas van en el system dentro de <tools>…</tools> (un JSON por linea) y
el modelo contesta

    <tool_call>
    <function=audio_set>
    <parameter=name>
    Música
    </parameter>
    <parameter=volumeDb>
    -4
    </parameter>
    </function>
    </tool_call>

o, si no corresponde ninguna herramienta, texto (charla). Entrenar en EL formato
que el modelo ya conoce es lo que aprovecha lo que sabe; la gramatica queda de red.

CONVENCIONES (el contrato con la app, que valida con agent/ArgumentosLocales):
  - nombres: copiados EXACTOS del estado (mayusculas y acentos).
  - cambio relativo de un numero: con signo, «-4» / «+3» («un poco» = 3, sin
    cantidad = 6). Valor exacto: con «=» adelante, «=-10» («ponelo en -10 dB»).
    En XML no hay comillas: el «=» es lo que distingue «-10» de «bajale 10».
  - booleanos: true / false.
  - derivar: la funcion `derivar` (no es para estas herramientas: al asistente grande).
  - falta_dato: la funcion `falta_dato` con la pregunta («¿A qué escena?») cuando
    el pedido no alcanza para un argumento obligatorio.
  - charla: texto, sin tool_call.

EL PEDIDO va en el mensaje del usuario con la parte del estado del aparato que
usa esa clase de herramienta (como argumentos::estadoPara en la app):

    Estado: {"audio":[{"nombre":"Música","volumen_db":-12,"silenciada":false},…]}
    Pedido: bajale 4 a la música
"""
import hashlib
import json
import random
import re
import unicodedata
from pathlib import Path

import lib_prompt as lp

DERIVAR = "derivar"
FALTA = "falta_dato"

SISTEMA = """Sos el asistente de un estudio de televisión EN VIVO. El operador te habla apurado y corto.
Si el pedido es para una de las funciones, llamala con sus argumentos. Reglas:
- Nombres (escenas, fuentes, audio, participantes, gráficos, carruseles): copiá EXACTAMENTE uno de los del estado.
- Poné solo los argumentos que el pedido cambia. Silenciar no cambia el volumen; cambiar el volumen no silencia.
- Si no nombra la fuente de audio, es el micrófono del operador (el primero de «audio»).
- Para subir o bajar un número no hagas cuentas: escribí cuánto cambia, con signo: -4, +3. «Un poco» son 3; sin cantidad, 6. Para un valor exacto, con «=» adelante: =-10.
- Si no es para estas funciones, llamá a derivar. Si falta un dato obligatorio, llamá a falta_dato con la pregunta.
- Si es una charla o una pregunta general, contestá en texto, corto y en castellano."""

# v4 (perfil vivo4): mas cosas del estado se nombran (camaras y sus posiciones,
# pantallas, discursos, medios, preguntas, recetas). El perfil «reactivo» conserva
# su sistema tal cual: v2 y v3 se entrenaron con ese prompt byte a byte.
SISTEMA_V4 = SISTEMA.replace(
    "- Nombres (escenas, fuentes, audio, participantes, gráficos, carruseles): copiá EXACTAMENTE uno de los del estado.",
    "- Nombres (escenas, fuentes, audio, participantes, gráficos, carruseles, cámaras y sus posiciones, pantallas): copiá"
    " EXACTAMENTE uno de los del estado. Discursos y medios van por su ruta; preguntas y recetas, por su id.")
NUEVO = lp.PERFIL != "reactivo"
# v5 (perfil vivo5): el recinto (bancas), las capas del DSK, el orden del dia y el
# chat del publico tambien se nombran
V5 = getattr(lp, "ESTADO", None) in ("v5", "v6")
# v6 (perfil vivo6): suma la documentacion del evento (los documentos se nombran por su id)
V6 = getattr(lp, "ESTADO", None) == "v6"
SISTEMA_V5 = SISTEMA_V4.replace(
    "Discursos y medios van por su ruta; preguntas y recetas, por su id.",
    "Discursos y medios van por su ruta; preguntas y recetas, por su id. Una banca va por el nombre de su"
    " integrante o por su número, como la nombre el operador; las capas del DSK y los puntos del orden del día,"
    " por su número; los mensajes del chat, por su key.")


SISTEMA_V6 = SISTEMA_V5.replace("los mensajes del chat, por su key.",
                                "los mensajes del chat, por su key; los documentos, por su id.")


def sistema():
    return SISTEMA_V6 if V6 else SISTEMA_V5 if V5 else SISTEMA_V4 if NUEVO else SISTEMA


INSTRUCCION_QWEN = (
    "\n\nIf you choose to call a function ONLY reply in the following format with NO suffix:\n\n<tool_call>\n"
    "<function=example_function_name>\n<parameter=example_parameter_1>\nvalue_1\n</parameter>\n"
    "<parameter=example_parameter_2>\nThis is the value for the second parameter\nthat can span\nmultiple lines\n"
    "</parameter>\n</function>\n</tool_call>\n\n<IMPORTANT>\nReminder:\n- Function calls MUST follow the "
    "specified format: an inner <function=...></function> block must be nested within <tool_call></tool_call> "
    "XML tags\n- Required parameters MUST be specified\n- You may provide optional reasoning for your function "
    "call in natural language BEFORE the function call, but NOT after\n- If there is no function call available, "
    "answer the question like normal with your current knowledge and do not tell the user about function calls\n"
    "</IMPORTANT>")

PSEUDO = [
    {"type": "function", "function": {
        "name": DERIVAR, "description": "El pedido no es para estas funciones: pasarlo al asistente grande.",
        "parameters": {"type": "object", "properties": {}}}},
    {"type": "function", "function": {
        "name": FALTA, "description": "Falta un dato obligatorio para hacer lo pedido: preguntarlo.",
        "parameters": {"type": "object", "properties": {"pregunta": {"type": "string"}}, "required": ["pregunta"]}}},
]


def _desc(h):
    """La descripcion corta (la primera oracion, como las fichas de lib_prompt): las
    enteras son 11.000 tokens para 84 herramientas, casi todo prosa."""
    return re.split(r"(?<=[.!?])\s", h["descripcion"].replace("\n", " "))[0][:140]


def compactar(esq, prof=0):
    """El esquema sin lo que no sirve para llamarla por voz: quedan nombres, tipos,
    enum, rangos, requeridos y una descripcion corta; los objetos y listas
    anidados (layouts de scene_define, settings…) quedan como su tipo. Los
    esquemas enteros son 5.200 tokens; asi, ~4.000 (el resto es la sintaxis JSON
    de cada parametro, que es el formato nativo y no se toca)."""
    out = {}
    if "type" in esq:
        out["type"] = esq["type"]
    for k in ("enum", "minimum", "maximum"):
        if k in esq:
            out[k] = esq[k]
    if esq.get("description") and prof > 0 and "type" not in esq:
        out["description"] = esq["description"][:80]  # solo si no hay tipo que lo diga
    if esq.get("properties") and prof == 0:
        out["properties"] = {k: compactar(v, prof + 1) for k, v in esq["properties"].items()}
    if esq.get("required") and prof == 0:
        out["required"] = esq["required"]
    if prof == 0:
        out.setdefault("type", "object")
        out.setdefault("properties", {})
    return out


# LA CAPA DE TRADUCCION (v6). El modelo ve y escribe una ETIQUETA cuando el nombre
# del MCP se pisaba con el de otra herramienta (chat_history / chat_recientes,
# bancas_listar / speakers_status...: los pares con CDNV mas alta en el modelo base
# y los que el base y v4 confundian igual). Todo lo de adentro (anotaciones, banco,
# examen, esquemas, fallos) sigue con el nombre REAL: la etiqueta solo existe en el
# prompt y en la respuesta. La app traduce con modelo.json (endpoints[].etiqueta).
_A_MODELO = dict(getattr(lp, "RENOMBRES", {}))
# las etiquetas anteriores tambien se leen (fuente_crear, de la vuelta 0 de la v6)
_A_REAL = dict(getattr(lp, "ANTERIORES", {})) | {v: k for k, v in _A_MODELO.items()}
_DESCRIPCIONES = dict(getattr(lp, "DESCRIPCIONES", {}))


def a_modelo(nombre):
    return _A_MODELO.get(nombre, nombre)


def a_real(nombre):
    return _A_REAL.get(nombre, nombre)


def _en_idioma_del_modelo(texto):
    """Las descripciones nombran a otras herramientas («la `key` de chat_recientes»):
    que hablen el mismo idioma que la respuesta."""
    for real in sorted(_A_MODELO, key=len, reverse=True):
        texto = re.sub(rf"\b{re.escape(real)}\b", _A_MODELO[real], texto)
    return texto


def funciones(inv=None):
    """Lo que ve el modelo, en el orden del prompt: con las etiquetas."""
    inv = inv or lp.inventario()
    por = {h["nombre"]: h for h in inv["herramientas"]}
    out = [{"type": "function", "function": {
        "name": a_modelo(n), "description": _en_idioma_del_modelo(_DESCRIPCIONES.get(n) or _desc(por[n])),
        "parameters": compactar(por[n]["esquema"])}} for n in lp.orden_prompt(inv)]
    return out + PSEUDO


def fichas(inv=None):
    """{nombre REAL: la ficha que ve el modelo}: para todo lo de adentro."""
    return {a_real(f["function"]["name"]): f["function"] for f in funciones(inv)}


def esquemas(inv=None):
    """{nombre REAL: parametros}."""
    return {h: f["parameters"] for h, f in fichas(inv).items()}


def cabeza(inv=None):
    """La parte FIJA (se procesa una vez y se reusa entre pedidos)."""
    tools = "\n".join(json.dumps(f, ensure_ascii=False) for f in funciones(inv))
    return (f"<|im_start|>system\n# Tools\n\nYou have access to the following functions:\n\n<tools>\n{tools}\n</tools>"
            f"{INSTRUCCION_QWEN}\n\n{sistema()}<|im_end|>\n<|im_start|>user\n")


COLA = "<|im_end|>\n<|im_start|>assistant\n<think>\n\n</think>\n\n"
FIN = "<|im_end|>"


def huella(inv=None):
    return hashlib.sha256(cabeza(inv).encode()).hexdigest()[:16]


def mismo_perfil(corpus=None, modelo=None):
    """Que el prompt de ahora (BA_PERFIL) sea el del corpus y el del modelo. Sin
    BA_PERFIL la cabeza es la del perfil reactivo: la frase se recorta mal y todo
    da 0 %, o el modelo ve herramientas que nunca vio, SIN AVISAR."""
    import lib_prompt as _lp
    h = huella()
    if corpus is not None and (Path(corpus) / "huella.json").exists():
        hc = json.loads((Path(corpus) / "huella.json").read_text()).get("prompt")
        if hc and hc != h:
            raise SystemExit(f"{corpus} es del prompt {hc} y el de ahora es {h} (perfil {_lp.PERFIL}):"
                             f" ¿falta BA_PERFIL=...?")
    if modelo is not None and (Path(modelo) / "huella-prompt.json").exists():
        hm = json.loads((Path(modelo) / "huella-prompt.json").read_text()).get("prompt")
        if hm and hm != _lp.huella()["prompt"]:
            raise SystemExit(f"{modelo} se entreno con el prompt {hm} y el de ahora es {_lp.huella()['prompt']}"
                             f" (perfil {_lp.PERFIL}): ¿falta BA_PERFIL=...?")


def usuario(estado, frase):
    if estado:
        return f"Estado: {json.dumps(estado, ensure_ascii=False, separators=(',', ':'))}\nPedido: {frase}"
    return frase


# --- escribir y leer la llamada ------------------------------------------------
def valor_texto(v):
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, (dict, list)):
        return json.dumps(v, ensure_ascii=False)
    return str(v)


def llamada(nombre, args):
    """Como la escribe el chat_template de Qwen3.5 (sin texto antes)."""
    s = f"<tool_call>\n<function={a_modelo(nombre)}>\n"
    for k, v in args.items():
        s += f"<parameter={k}>\n{valor_texto(v)}\n</parameter>\n"
    return s + "</function>\n</tool_call>"


RE_LLAMADA = re.compile(r"<tool_call>\s*<function=([^>\n]+)>(.*?)</function>\s*</tool_call>", re.S)
RE_PARAM = re.compile(r"<parameter=([^>\n]+)>\n?(.*?)\n?</parameter>", re.S)


def leer(texto):
    """(herramienta, args) o (None, texto) si contesto en texto. Los valores quedan
    como texto: la app los interpreta con el esquema (ArgumentosLocales)."""
    m = RE_LLAMADA.search(texto)
    if not m:
        return None, texto.strip()
    return a_real(m.group(1).strip()), {k.strip(): v.strip() for k, v in RE_PARAM.findall(m.group(2))}


# COPIAS adentro de una herramienta: dos plantillas que comparten esta porcion de sus
# palabras con contenido y piden LO MISMO (mismos argumentos o la misma falta) son
# una sola. Con otros argumentos no es copia («activá la placa» / «quitá la placa»).
COPIA = 0.7
VACIAS = set("el la los las lo le les de del a al en con por para que y o u un una unos unas es "
             "esta este esto eso esa ese se su sus mi me te tu ya ahi aca hay como cual cuales "
             "dale eh che ojo rapido porfa bueno ahora toque".split())


def contenido(plantilla):
    """Las palabras con contenido de una plantilla (los huecos cuentan como uno)."""
    x = unicodedata.normalize("NFKD", re.sub(r"\{[a-z_]+\d?\}", " X ", plantilla).lower())
    t = set(re.sub(r"\W+", " ", x.encode("ascii", "ignore").decode()).split())
    return (t - VACIAS) or t


def sin_copias(lista, umbral=COPIA):
    """(las que quedan, las copias), con las humanas antes que las del agente."""
    quedan, copias, vistas = [], [], []
    for a in sorted(lista, key=lambda a: bool(a.get("nueva"))):
        if not a.get("plantilla"):
            quedan.append(a)
            continue
        t, pide = contenido(a["plantilla"]), json.dumps([a.get("args"), a.get("falta")], sort_keys=True)
        if any(p == pide and len(t & u) / max(len(t), len(u)) >= umbral for u, p in vistas):
            copias.append(a)
            continue
        vistas.append((t, pide))
        quedan.append(a)
    return quedan, copias


def normal(s):
    s = unicodedata.normalize("NFKD", str(s)).encode("ascii", "ignore").decode().lower()
    return re.sub(r"\s+", " ", s).strip()


# --- el estado del aparato: sintetico, variado -----------------------------------
# Cada entidad: (nombre en el aparato, formas en que lo dice el operador). Variados
# a proposito: el modelo tiene que aprender a COPIAR el nombre del estado, no a
# memorizar «Música».
ESCENAS = [("Principal", ["la principal", "principal"]), ("Cierre", ["el cierre", "la del cierre"]),
           ("Apertura", ["la apertura", "la de apertura"]), ("Entrevista", ["la entrevista", "la de la entrevista"]),
           ("Pantalla completa", ["pantalla completa", "la de pantalla completa"]), ("Pausa", ["la pausa", "la de pausa"]),
           ("Cortina", ["la cortina", "la de cortina"]), ("Dos cámaras", ["la de dos cámaras", "dos cámaras"]),
           ("Créditos", ["los créditos", "la de créditos"]), ("Placa", ["la placa", "la de la placa"]),
           ("Panel", ["el panel", "la del panel"]), ("Juego 1", ["juego uno", "la del juego 1"]),
           ("Debate", ["el debate", "la del debate"]), ("Exterior", ["exteriores", "la de exterior"]),
           ("Noticiero", ["la del noticiero", "noticiero"]), ("Invitados", ["la de invitados", "invitados"]),
           ("Tanda", ["la tanda", "la de la tanda"]), ("Saludo final", ["el saludo final", "la del saludo"])]
AUDIO = [("Música", ["la música", "la musiquita"]), ("Cortina musical", ["la cortina musical", "la cortina"]),
         ("Micro Laura", ["el micro de Laura", "Laura"]), ("Invitado 1 (audio)", ["el invitado 1", "el invitado uno"]),
         ("Invitado 2 (audio)", ["el invitado 2", "el invitado dos"]), ("Video intro", ["el video de intro", "la intro"]),
         ("Audio del escritorio", ["el audio de la compu", "el escritorio"]), ("Micro Martín", ["el micro de Martín", "Martín"]),
         ("Playlist", ["la playlist", "la lista de temas"]), ("Mic 2", ["el mic 2", "el micrófono 2"]),
         ("Teléfono", ["el teléfono", "la línea"]), ("Cámara 1 (audio)", ["el audio de la cámara 1", "la cámara uno"])]
MICROS = [("Micrófono de MacBook Pro", ["el micrófono de la MacBook", "mi micrófono"]),
          ("Mic operador", ["el mic del operador", "mi mic"]), ("Micrófono USB", ["el micrófono USB", "mi micrófono"])]
FUENTES = [("Cámara 1", ["la cámara 1", "la uno"]), ("Cámara 2", ["la cámara 2", "la dos"]),
           ("Cámara 3", ["la cámara 3", "la tres"]), ("Pantalla compartida", ["la pantalla compartida", "lo compartido"]),
           ("Logo", ["el logo", "el logo del canal"]), ("Video intro", ["el video de intro", "la intro"]),
           ("Placa de espera", ["la placa de espera", "la placa"]), ("Navegador", ["el navegador", "la web"]),
           ("Zócalo", ["el zócalo", "el zocalito"]), ("Reloj", ["el reloj", "la hora"])]
PERSONAS = [("Laura Gómez", ["Laura", "Laura Gómez"]), ("Martín", ["Martín", "Martín el invitado"]),
            ("Sofía Ruiz", ["Sofía", "Sofía Ruiz"]), ("Juan Pérez", ["Juan", "Juan Pérez"]),
            ("Invitado 3", ["el invitado 3", "el invitado tres"]), ("Carla", ["Carla"]),
            ("Diego Fernández", ["Diego", "Diego Fernández"]), ("Ana", ["Ana"])]
GRAFICOS = [("Zócalo", ["el zócalo"]), ("Logo", ["el logo"]), ("Marcador", ["el marcador", "el resultado"]),
            ("Resultados", ["los resultados"]), ("Clima", ["el clima"]), ("Reloj", ["el reloj"]),
            ("Cuenta regresiva", ["la cuenta regresiva"]), ("Sponsor", ["el sponsor", "la publicidad"])]
CARRUSELES = [("Presentación", ["la presentación"]), ("Sponsors", ["los sponsors"]),
              ("Fotos del evento", ["las fotos", "las fotos del evento"]), ("Agenda", ["la agenda"])]

# v4: lo que nombran los comandos nuevos (PTZ, teleprompter, medios, preguntas,
# pantallas, recetas). Cada entrada: (valor que va en los argumentos, como lo dice
# el operador[, lo que muestra el estado]).
CAMARAS = [("Cámara 1", ["la cámara 1", "la uno"]), ("Cámara 2", ["la cámara 2", "la dos"]),
           ("Cámara 3", ["la cámara 3", "la tres"]), ("Cámara PTZ", ["la PTZ", "la robótica"]),
           ("Cámara del atril", ["la cámara del atril", "la del atril"]), ("Cámara general", ["la general", "la cámara general"])]
POSICIONES = [("Atril", ["el atril", "al atril"]), ("Panorámica", ["la panorámica", "panorámica"]),
              ("Laura", ["Laura", "a Laura"]), ("Martín", ["Martín", "a Martín"]), ("Mesa", ["la mesa", "a la mesa"]),
              ("Público", ["el público", "al público"]), ("Escenario", ["el escenario", "al escenario"]),
              ("Plano general", ["el plano general", "general"]), ("Conductor", ["el conductor", "al conductor"]),
              ("Invitado 1", ["el invitado 1", "al invitado uno"]), ("Puerta", ["la puerta", "a la puerta"])]
DISCURSOS = [("intendente.txt", ["el discurso del intendente", "el del intendente"]),
             ("Apertura/bienvenida.txt", ["el de bienvenida", "la bienvenida"]),
             ("cierre.txt", ["el de cierre", "el discurso de cierre"]),
             ("gobernador.txt", ["el del gobernador", "el discurso del gobernador"]),
             ("Ministra/salud.txt", ["el de la ministra", "el de salud"])]
MEDIOS = [("Media/tanda1.mp4", ["la tanda", "la tanda 1"], "Tanda 1"),
          ("Media/cortina.mp3", ["la cortina", "la cortina musical"], "Cortina musical"),
          ("Media/institucional.mp4", ["el institucional", "el video institucional"], "Video institucional"),
          ("Media/espera.mp4", ["el fondo de espera", "el video de espera"], "Fondo de espera"),
          ("Media/cierre.mp3", ["la música de cierre", "el tema de cierre"], "Música de cierre"),
          ("Media/logo.mov", ["el logo animado", "la animación del logo"], "Logo animado")]
PREGUNTAS = [("q3", ["la de Juan", "la pregunta de Juan"], "¿Cuándo empieza la obra del puente?|Juan"),
             ("q7", ["la del presupuesto", "la de Ana"], "¿Cuánto se destina a salud?|Ana"),
             ("q11", ["la de las becas", "la de Pedro"], "¿Siguen las becas este año?|Pedro"),
             ("q14", ["la del transporte", "la de Marta"], "¿Qué pasa con el colectivo 5?|Marta"),
             ("q20", ["la última", "la de Diego"], "¿Hay fecha para la próxima sesión?|Diego")]
PANTALLAS = [("TV del hall", ["la tele del hall", "la del hall"]), ("Pantalla del escenario", ["la pantalla del escenario", "la del escenario"]),
             ("Monitor de prensa", ["el monitor de prensa", "la de prensa"]), ("TV de la entrada", ["la tele de la entrada", "la de la entrada"])]
RECETAS = [("r1", ["la receta de apertura", "la apertura"], "Apertura"), ("r2", ["la receta de cierre", "la de cierre"], "Cierre"),
           ("r3", ["la de la tanda", "la receta de la tanda"], "Tanda"), ("r4", ["la de entrevista", "la receta de entrevista"], "Entrevista"),
           ("r5", ["la de corte a publicidad", "el corte"], "Corte a publicidad")]

# v5: el recinto, el DSK, el orden del dia y el chat del publico. Las bancas y las
# capas reciben su numero al armar cada estado (no son fijos de una sala a otra)
INTEGRANTES = [("Pérez", "el concejal"), ("Gómez", "la concejala"), ("Fernández", "el concejal"),
               ("Rodríguez", "la concejala"), ("López", "el concejal"), ("Martínez", "la concejala"),
               ("Sosa", "el concejal"), ("Acosta", "la concejala"), ("Romero", "el concejal"),
               ("Díaz", "la concejala"), ("Benítez", "el concejal"), ("Medina", "la concejala"),
               ("Suárez", "el concejal"), ("Herrera", "la concejala"), ("Aguirre", "el concejal")]
BLOQUES = ["Frente Vecinal", "Unión Ciudadana", "Bloque Independiente", "Compromiso Local"]
CAPAS = [("Logo", ["la capa del logo", "el logo de la capa"]), ("Intérprete LSA", ["la capa del intérprete", "el intérprete de señas"]),
         ("Reloj", ["la capa del reloj", "el reloj de la capa"]), ("Zócalo fijo", ["la capa del zócalo fijo", "el zócalo fijo"]),
         ("Mosca del canal", ["la capa de la mosca", "la mosca del canal"]), ("Marca de agua", ["la capa de la marca de agua", "la marca de agua"])]
PUNTOS = [("Expte. 123/24 – Presupuesto 2025", ["el del presupuesto", "el punto del presupuesto"]),
          ("Expte. 45/24 – Transporte urbano", ["el del transporte", "el punto del transporte"]),
          ("Expte. 88/24 – Plaza Belgrano", ["el de la plaza", "el punto de la plaza"]),
          ("Expte. 12/24 – Becas estudiantiles", ["el de las becas", "el punto de las becas"]),
          ("Expte. 67/24 – Alumbrado público", ["el del alumbrado", "el punto del alumbrado"]),
          ("Expte. 31/24 – Tasa de servicios", ["el de la tasa", "el punto de la tasa"])]
MENSAJES = [("yt:8f2a", ["el mensaje de Juan", "lo que escribió Juan"], "Saludos desde Rosario|Juan|youtube"),
            ("yt:91c0", ["el mensaje de Carla", "lo que escribió Carla"], "¿Van a tratar lo de la plaza?|Carla|youtube"),
            ("tw:33b7", ["el mensaje de Nico", "lo que puso Nico"], "Se escucha bajo el micrófono|Nico|twitch"),
            ("fb:5d1e", ["el mensaje de Sofía", "lo que escribió Sofía"], "Gracias por transmitir|Sofía|facebook"),
            ("yt:a04f", ["el mensaje de Martín", "lo que puso Martín"], "¿Cuándo se vota el presupuesto?|Martín|youtube"),
            ("sala:7e", ["el mensaje de Lucía", "lo que escribió Lucía"], "Pido la palabra para una pregunta|Lucía|sala")]
# v6: la documentacion del evento (un documento por punto, mas los generales)
DOCUMENTOS = [("d1", ["el expediente del presupuesto", "el PDF del presupuesto"], "Expte. 123/24 – Presupuesto 2025"),
              ("d2", ["el expediente del transporte", "el PDF del transporte"], "Expte. 45/24 – Transporte urbano"),
              ("d3", ["el expediente de la plaza", "el PDF de la plaza"], "Expte. 88/24 – Plaza Belgrano"),
              ("d4", ["el reglamento", "el reglamento interno"], "Reglamento interno"),
              ("d5", ["el orden del día impreso", "el PDF del orden del día"], "Orden del día"),
              ("d6", ["la presentación de las becas", "el PDF de las becas"], "Expte. 12/24 – Becas estudiantiles")]
TIPOS = {"escena": ESCENAS, "audio": AUDIO, "fuente": FUENTES, "persona": PERSONAS,
         "grafico": GRAFICOS, "carrusel": CARRUSELES,
         "camara": CAMARAS, "posicion": POSICIONES, "discurso": DISCURSOS, "medio": MEDIOS,
         "pregunta": PREGUNTAS, "pantalla": PANTALLAS, "receta": RECETAS,
         "banca": INTEGRANTES, "capa": CAPAS, "punto": PUNTOS, "mensaje": MENSAJES, "documento": DOCUMENTOS}
RE_HUECO = re.compile(r"\{(escena|audio|audio_operador|fuente|persona|grafico|carrusel|camara|posicion|discurso|medio"
                      r"|pregunta|pantalla|receta|banca|capa|punto|mensaje|documento)(\d?)\}")
# las claves del estado (v4 suma las de los comandos nuevos)
ESTADO_CLAVES = ["escena_actual", "escena_armada", "escenas", "fuentes_de_la_escena_actual", "audio", "participantes",
                 "graficos", "carruseles"]
ESTADO_CLAVES_V4 = ESTADO_CLAVES + ["camaras", "discursos", "medios", "preguntas", "pantallas", "recetas"]
ESTADO_CLAVES_V5 = ESTADO_CLAVES_V4 + ["bancas", "capas", "orden_del_dia", "chat"]
ESTADO_CLAVES_V6 = ESTADO_CLAVES_V5 + ["documentos"]


def estado_sintetico(rnd, necesita=()):
    """Un estado del aparato plausible y distinto cada vez; con al menos las
    entidades que el pedido necesita (necesita: lista de tipos)."""
    def elegir(pool, k_min, k_max, obligatorias=0):
        k = rnd.randint(max(k_min, obligatorias), max(k_max, obligatorias))
        return rnd.sample(pool, min(k, len(pool)))
    cuantos = {t: sum(1 for x in necesita if x == t) for t in list(TIPOS) + ["posicion"]}
    escenas = elegir(ESCENAS, 3, 9, cuantos["escena"] + 1)
    micro = rnd.choice(MICROS)
    audio = [micro] + elegir(AUDIO, 1, 6, cuantos["audio"])
    fuentes = elegir(FUENTES, 2, 6, cuantos["fuente"])
    personas = elegir(PERSONAS, 0, 4, cuantos["persona"])
    graficos = elegir(GRAFICOS, 1, 5, cuantos["grafico"])
    carruseles = elegir(CARRUSELES, 0, 3, cuantos["carrusel"])
    actual = rnd.choice(escenas)[0]
    otra = rnd.choice([e for e in escenas if e[0] != actual] or escenas)[0]
    est = {
        "escena_actual": actual, "escena_armada": otra if rnd.random() < 0.6 else "",
        "escenas": [e[0] for e in escenas],
        "fuentes_de_la_escena_actual": [{"nombre": f[0], "visible": rnd.random() < 0.8} for f in fuentes],
        "audio": [{"nombre": a[0], "volumen_db": float(rnd.choice([0, -3, -6, -8, -10, -12, -15, -20])),
                   "silenciada": rnd.random() < 0.15} for a in audio],
        "participantes": [{"id": f"p{i + 1}", "nombre": p[0], "estado": rnd.choice(["en sala", "en espera", "al aire"])}
                          for i, p in enumerate(personas)],
        "graficos": [g[0] for g in graficos],
        "carruseles": [c[0] for c in carruseles],
    }
    formas = {"escena": escenas, "audio": audio[1:], "audio_operador": [micro], "fuente": fuentes,
              "persona": personas, "grafico": graficos, "carrusel": carruseles}
    if NUEVO:
        camaras = elegir(CAMARAS, 1, 4, cuantos.get("camara", 0))
        posiciones = {}
        for c in camaras:
            posiciones[c[0]] = rnd.sample(POSICIONES, rnd.randint(max(1, cuantos.get("posicion", 0)), 5))
        discursos = elegir(DISCURSOS, 0, 4, cuantos.get("discurso", 0))
        medios = elegir(MEDIOS, 0, 5, cuantos.get("medio", 0))
        preguntas = elegir(PREGUNTAS, 0, 4, cuantos.get("pregunta", 0))
        pantallas = elegir(PANTALLAS, 0, 3, cuantos.get("pantalla", 0))
        recetas = elegir(RECETAS, 0, 4, cuantos.get("receta", 0))
        est.update({
            "camaras": [{"nombre": c[0], "posiciones": [p[0] for p in posiciones[c[0]]]} for c in camaras],
            "discursos": [d[0] for d in discursos],
            "medios": [{"nombre": m[2], "ruta": m[0]} for m in medios],
            "preguntas": [{"id": q[0], "texto": q[2].split("|")[0], "autor": q[2].split("|")[1],
                           "estado": rnd.choice(["en cola", "elegida", "al aire"])} for q in preguntas],
            "pantallas": [x[0] for x in pantallas],
            "recetas": [{"id": r[0], "nombre": r[2]} for r in recetas]})
        formas.update({"camara": camaras, "posiciones": posiciones, "discurso": discursos, "medio": medios,
                       "pregunta": preguntas, "pantalla": pantallas, "receta": recetas})
    if V5:
        # el recinto: cada integrante en su banca (numero), por nombre o por numero
        integ = elegir(INTEGRANTES, 3, 12, cuantos.get("banca", 0))
        numeros = rnd.sample(range(1, 31), len(integ))
        al_aire = rnd.choice([None] + [i for i in range(len(integ))]) if integ else None
        bancas = [{"banca": n, "integrante": a, "bloque": rnd.choice(BLOQUES), "al_aire": i == al_aire}
                  for i, (n, (a, _)) in enumerate(zip(numeros, integ))]
        f_banca = []
        for n, (a, titulo) in zip(numeros, integ):
            f_banca.append((a, [a, f"{titulo} {a}"]))
            f_banca.append((str(n), [f"la banca {n}", f"la {n}"]))
        capas = elegir(CAPAS, 1, 4, cuantos.get("capa", 0))
        n_capas = rnd.sample(range(1, 5), len(capas))
        puntos = elegir(PUNTOS, 2, 6, cuantos.get("punto", 0))
        actual = rnd.randrange(len(puntos)) if puntos and rnd.random() < 0.7 else None
        mensajes = elegir(MENSAJES, 0, 5, cuantos.get("mensaje", 0))
        est.update({
            "bancas": bancas,
            "capas": [{"capa": n, "nombre": c[0], "arriba": rnd.random() < 0.3} for n, c in zip(n_capas, capas)],
            "orden_del_dia": [{"n": i + 1, "titulo": p[0],
                               "estado": "en_tratamiento" if i == actual else ("votado" if actual is not None and i < actual else "pendiente")}
                              for i, p in enumerate(puntos)],
            "chat": [{"key": m[0], "autor": m[2].split("|")[1], "texto": m[2].split("|")[0],
                      "plataforma": m[2].split("|")[2], "destacado": False} for m in mensajes]})
        formas.update({"banca": f_banca,
                       "capa": [(str(n), c[1] + [f"la capa {n}"]) for n, c in zip(n_capas, capas)],
                       "punto": [(str(i + 1), p[1] + [f"el punto {i + 1}"]) for i, p in enumerate(puntos)],
                       "mensaje": mensajes})
    if V6:
        docs = elegir(DOCUMENTOS, 0, 5, cuantos.get("documento", 0))
        est["documentos"] = [{"id": d[0], "titulo": d[2]} for d in docs]
        formas["documento"] = docs
    return est, formas


def empieza(s, prefijos):
    return any(s.startswith(p) for p in prefijos)


def estado_para(h, est):
    """La parte del estado que usa cada clase de herramienta: la MISMA cuenta que
    argumentos::estadoPara (src/app/agent/ArgumentosLocales.cpp)."""
    def con(*claves):
        return {k: est[k] for k in claves if k in est}
    if empieza(h, ("scene_", "transition_")):
        return con("escena_actual", "escena_armada", "escenas")
    if empieza(h, ("audio", "mixer_")):
        return con("audio")
    if empieza(h, ("source_", "capture_source")):
        return con("escena_actual", "escenas", "fuentes_de_la_escena_actual", "audio")
    if empieza(h, ("participant_", "speaker_", "chat_", "capture_person")):
        return con("participantes")
    if empieza(h, ("grafico_", "rotulo_")):
        return con("graficos", "escena_actual")
    if empieza(h, ("laminas_",)):
        return con("carruseles", "escenas", "escena_actual")
    return est


def rellenar(plantilla, args, formas, rnd):
    """Pone en la frase y en los argumentos las entidades del estado de turno:
    {audio} en la frase -> como lo dice el operador; en los args -> el nombre exacto."""
    usados, elegidos = {}, {}
    # la camara antes que la posicion: la posicion se elige entre las de ESA camara
    huecos = sorted(RE_HUECO.finditer(plantilla + " " + json.dumps(args, ensure_ascii=False)),
                    key=lambda m: m.group(1) == "posicion")
    for m in huecos:
        tipo, n = m.group(1), m.group(2)
        clave = tipo + n
        if clave in elegidos:
            continue
        if tipo == "posicion":  # una posicion de LA camara del pedido
            cam = elegidos.get("camara" + n) or elegidos.get("camara")
            pool = formas.get("posiciones", {}).get(cam[0], []) if cam else \
                [p for ps in formas.get("posiciones", {}).values() for p in ps]
        else:
            pool = formas.get(tipo, [])
        libres = [e for e in pool if e[0] not in usados.get(tipo, set())]
        if not libres:
            return None, None
        e = rnd.choice(libres)
        usados.setdefault(tipo, set()).add(e[0])
        elegidos[clave] = e
    frase = RE_HUECO.sub(lambda m: rnd.choice(elegidos[m.group(1) + m.group(2)][1]), plantilla)
    frase = re.sub(r"\b([aA]) el\b", r"\1l", frase)
    frase = re.sub(r"\b([dD]e) el\b", lambda m: m.group(1)[0] + "el", frase)
    # las formas de las posiciones traen su preposicion («al atril», «a Laura»): con
    # «a {posicion}» en la plantilla quedaba «a a Laura» / «a al atril»
    frase = re.sub(r"\b[aA] (al?)\b", r"\1", frase)
    sal = {}
    for k, v in args.items():
        if isinstance(v, str):
            v = RE_HUECO.sub(lambda m: elegidos[m.group(1) + m.group(2)][0], v)
        sal[k] = v
    return frase, sal


def tipos_de(plantilla, args):
    texto = plantilla + " " + json.dumps(args, ensure_ascii=False)
    return [m.group(1) if m.group(1) != "audio_operador" else "audio" for m in RE_HUECO.finditer(texto)]
