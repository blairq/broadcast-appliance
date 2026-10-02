"""El formato NUEVO del modelo local: herramienta + argumentos en el tool calling nativo de Qwen3.5.

Un solo modulo para entrenar Y para servir (como lib_prompt para el formato de
solo nombres): un desajuste entre los dos hunde el acierto (ya paso: 56 % a 16 %).
Ver docs/entrenamiento-avanzado.md.

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


def funciones(inv=None):
    inv = inv or lp.inventario()
    por = {h["nombre"]: h for h in inv["herramientas"]}
    out = [{"type": "function", "function": {"name": n, "description": _desc(por[n]),
                                             "parameters": compactar(por[n]["esquema"])}}
           for n in lp.herramientas_reactivas(inv)]
    return out + PSEUDO


def esquemas(inv=None):
    return {f["function"]["name"]: f["function"]["parameters"] for f in funciones(inv)}


def cabeza(inv=None):
    """La parte FIJA (se procesa una vez y se reusa entre pedidos)."""
    tools = "\n".join(json.dumps(f, ensure_ascii=False) for f in funciones(inv))
    return (f"<|im_start|>system\n# Tools\n\nYou have access to the following functions:\n\n<tools>\n{tools}\n</tools>"
            f"{INSTRUCCION_QWEN}\n\n{SISTEMA}<|im_end|>\n<|im_start|>user\n")


COLA = "<|im_end|>\n<|im_start|>assistant\n<think>\n\n</think>\n\n"
FIN = "<|im_end|>"


def huella(inv=None):
    return hashlib.sha256(cabeza(inv).encode()).hexdigest()[:16]


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
    s = f"<tool_call>\n<function={nombre}>\n"
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
    return m.group(1).strip(), {k.strip(): v.strip() for k, v in RE_PARAM.findall(m.group(2))}


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

TIPOS = {"escena": ESCENAS, "audio": AUDIO, "fuente": FUENTES, "persona": PERSONAS,
         "grafico": GRAFICOS, "carrusel": CARRUSELES}
RE_HUECO = re.compile(r"\{(escena|audio|audio_operador|fuente|persona|grafico|carrusel)(\d?)\}")


def estado_sintetico(rnd, necesita=()):
    """Un estado del aparato plausible y distinto cada vez; con al menos las
    entidades que el pedido necesita (necesita: lista de tipos)."""
    def elegir(pool, k_min, k_max, obligatorias=0):
        k = rnd.randint(max(k_min, obligatorias), max(k_max, obligatorias))
        return rnd.sample(pool, min(k, len(pool)))
    cuantos = {t: sum(1 for x in necesita if x == t) for t in TIPOS}
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
    for m in RE_HUECO.finditer(plantilla + " " + json.dumps(args, ensure_ascii=False)):
        tipo, n = m.group(1), m.group(2)
        clave = tipo + n
        if clave in elegidos:
            continue
        libres = [e for e in formas.get(tipo, []) if e[0] not in usados.get(tipo, set())]
        if not libres:
            return None, None
        e = rnd.choice(libres)
        usados.setdefault(tipo, set()).add(e[0])
        elegidos[clave] = e
    frase = RE_HUECO.sub(lambda m: rnd.choice(elegidos[m.group(1) + m.group(2)][1]), plantilla)
    frase = re.sub(r"\b([aA]) el\b", r"\1l", frase)
    frase = re.sub(r"\b([dD]e) el\b", lambda m: m.group(1)[0] + "el", frase)
    sal = {}
    for k, v in args.items():
        if isinstance(v, str):
            v = RE_HUECO.sub(lambda m: elegidos[m.group(1) + m.group(2)][0], v)
        sal[k] = v
    return frase, sal


def tipos_de(plantilla, args):
    texto = plantilla + " " + json.dumps(args, ensure_ascii=False)
    return [m.group(1) if m.group(1) != "audio_operador" else "audio" for m in RE_HUECO.finditer(texto)]
