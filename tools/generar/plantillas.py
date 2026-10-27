#!/usr/bin/env python3
"""Genera las plantillas de evento del programa (assets/demo/<idioma>/<Nombre>/), una por escenario.

Cada plantilla es una configuración completa como la «Demo parlamentaria»: sus escenas
(Config/app.json, solo las escenas: lo demás lo completa la app con lo de fábrica), los
gráficos de fábrica, el guion del evento con sus bloques y cues (textos para completar
marcados ‹así›), discursos de ejemplo para el teleprompter y, donde corresponde, un
padrón de ejemplo. El asistente parte de acá y la ajusta con la entrevista de la guía
(assets/idiomas/<idioma>/guias/escenarios/<id>.md).

    python3 tools/generar/plantillas.py                # regenera las de español (borra las generadas antes)
    python3 tools/generar/plantillas.py --idioma en    # las de otro idioma (ver «Idiomas» abajo)
    python3 tools/generar/plantillas.py --textos       # la tabla de textos para traducir

Las plantillas generadas llevan en configuracion.json {"generada": true}: solo esas se
borran al regenerar (la Demo parlamentaria, hecha a mano, no se toca).

IDIOMAS. Las plantillas son de lo que ve el público (rótulos, placas, la web del evento) y
de lo que lee el operador (escenas, bloques, notas): van en un idioma. Cada idioma tiene su
carpeta, assets/demo/<código>/ (se instala en Demos/<código>/ y la app lista las del idioma
del operador, arranque/Configuraciones). Para sacar las de un idioma nuevo:
  1. Que sus guías estén traducidas: assets/idiomas/<c>/guias/escenarios/indice.json da el
     nombre de cada plantilla (su carpeta) y su descripción.
  2. `--textos` escribe assets/idiomas/es/plantillas/textos.json: cada texto de las
     plantillas (el original) con su traducción, que en español es el mismo. Se copia a
     assets/idiomas/<c>/plantillas/textos.json y se traducen los valores (las claves quedan
     en español). Los ‹…› son para completar: se traducen con sus ‹ › y todo; los %s y %d
     quedan. Lo que falte sale en español (y el script lo avisa).
  3. `--idioma <c>` genera assets/demo/<c>/.
Los nombres de escenas y fuentes se traducen con la misma tabla en todos lados (las escenas,
las cues que las nombran), así que la plantilla queda coherente. La Demo parlamentaria está
hecha a mano y es solo en español; los gráficos de fábrica (assets/graficos) se copian tal
como están.
"""
import json
import os
import shutil
import sys
import tempfile

RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
IDIOMA = "es"  # el de las plantillas que se están armando (lo pone main)
DEMOS = os.path.join(RAIZ, "assets", "demo", IDIOMA)
GRAFICOS = os.path.join(RAIZ, "assets", "graficos")
EXTENSIONES = os.path.join(RAIZ, "assets", "extensiones")
IDIOMAS = os.path.join(RAIZ, "assets", "idiomas")


def indice_de(idioma):
    """Los escenarios (id, título, ícono, resumen) en ese idioma: el índice de sus guías."""
    f = os.path.join(IDIOMAS, idioma, "guias", "escenarios", "indice.json")
    if not os.path.isfile(f):
        print("OJO: no hay guías en «%s»: los nombres de las plantillas salen en español" % idioma, file=sys.stderr)
        f = os.path.join(IDIOMAS, "es", "guias", "escenarios", "indice.json")
    return json.load(open(f))


# --- Textos ----------------------------------------------------------------------------
# Todo texto de las plantillas pasa por T: el original en español → el del idioma
# (assets/idiomas/<c>/plantillas/textos.json). Lo que no está sale como está.
TEXTOS = {}
USADOS = []      # los que pasaron por T, en orden (para --textos)
FALTAN = set()   # los que no tienen traducción en el idioma pedido


def cargar_textos(idioma):
    global TEXTOS
    TEXTOS = {}
    if idioma == "es":
        return
    f = os.path.join(IDIOMAS, idioma, "plantillas", "textos.json")
    if not os.path.isfile(f):
        sys.exit("no hay textos de las plantillas en «%s» (%s): ver --textos" % (idioma, os.path.relpath(f, RAIZ)))
    TEXTOS = {k: v for k, v in json.load(open(f)).items() if isinstance(v, str) and v}


def T(texto):
    if not isinstance(texto, str) or not texto:
        return texto
    if texto in TEXTOS.values() and texto not in TEXTOS:
        return texto  # ya traducido (un nombre que pasa dos veces)
    if texto not in USADOS:
        USADOS.append(texto)
    if IDIOMA != "es" and texto not in TEXTOS:
        FALTAN.add(texto)
    return TEXTOS.get(texto, texto)

MOLDES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "graficos-plantilla")

# --- Estilos -------------------------------------------------------------------------
# Cada plantilla con su estilo: colores, tipografía (las del bundle, data/fonts: un gráfico
# se ve igual en cualquier máquina), forma y entrada (ver graficos-plantilla/comun.qml).
# `fondo`: el de las escenas (0xAABBGGRR). Lo usan sus gráficos propios (zócalo, placa,
# espera, cierre y los de su tipo) y el color de acento de las cues del guion.
# UN nombre de familia: font.family de QML no acepta una lista con comas como CSS (toma
# todo como un nombre que no existe y cae a la letra por defecto). Son las del bundle, así
# que están siempre.
SANS, SERIF = "Rubik", "Linux Libertine G"


def estilo(nombre, forma, entrada, acento, acento2, panel, fondo, familia=SANS, titulo=None, peso="Font.DemiBold",
           radio=4, entrada_ms=600, texto="#ffffff", texto2="#c9d1da"):
    return {"estilo": nombre, "forma": forma, "entrada": entrada, "acento": acento, "acento2": acento2,
            "panel": panel, "fondo": fondo, "familia": familia, "familiaTitulo": titulo or familia, "peso": peso,
            "radio": str(radio), "entradaMs": str(entrada_ms), "texto": texto, "texto2": texto2}


INSTITUCIONAL = estilo("Institucional", "filete", "revela", "#74acdf", "#c9a227", "#0b1f3a", 0xFF2A1A0E,
                       familia="Linux Biolinum G", titulo=SERIF, peso="Font.Normal", radio=0,
                       entrada_ms=800, texto2="#e5d6a8")
ESTILOS = {
    "reunion-chica": estilo("Cálido", "pildora", "sube", "#f59e0b", "#fde68a", "#1f2937", 0xFF372B22,
                            texto2="#d1d5db"),
    "reunion-hibrida": estilo("Corporativo", "barra", "desliza", "#2563eb", "#93c5fd", "#0f172a", 0xFF2A1A11,
                              familia="Noto Sans", radio=2, texto2="#cbd5e1"),
    # Las institucionales comparten el carácter (serif, filetes, entrada que se revela) y
    # cada una tiene su color.
    "comision": dict(INSTITUCIONAL, estilo="Institucional celeste"),
    "sesion-deliberativa": dict(INSTITUCIONAL, estilo="Institucional azul", acento="#3b82f6", panel="#0a1a33"),
    "asamblea-votacion": dict(INSTITUCIONAL, estilo="Institucional bordó", acento="#9f1239", acento2="#d4af37",
                              panel="#2a0a12", fondo=0xFF140A1E, texto2="#f1d9a0"),
    "asamblea-remota": dict(INSTITUCIONAL, estilo="Institucional petróleo", acento="#0e7490", acento2="#a5f3fc",
                            panel="#062a33", fondo=0xFF2A1F08, texto2="#cffafe"),
    "audiencia-publica": dict(INSTITUCIONAL, estilo="Institucional verde", acento="#15803d", acento2="#d4af37",
                              panel="#0b2414", fondo=0xFF14200C, texto2="#e8dcae"),
    "consorcio": estilo("Vecinal", "pildora", "sube", "#16a34a", "#bbf7d0", "#102a1c", 0xFF1E2A17,
                        texto2="#d1fae5"),
    "clase-hibrida": estilo("Pizarrón", "barra", "sube", "#facc15", "#fef08a", "#1f3a2e", 0xFF2A3A24,
                            familia="Rubik", radio=10, texto2="#e7f5ec"),
    "conferencia": estilo("Académico", "diagonal", "desliza", "#8b5cf6", "#c4b5fd", "#17112b", 0xFF2B1A21,
                          texto2="#ddd6fe"),
    "panel": estilo("Debate", "barra", "revela", "#ef4444", "#60a5fa", "#111827", 0xFF271811,
                    familia="Noto Sans", titulo=SANS, peso="Font.Bold", radio=0),
    "entrevista": estilo("Podcast", "pildora", "crece", "#f43f5e", "#fda4af", "#18181b", 0xFF1E1B1B,
                         peso="Font.Bold", texto2="#e4e4e7"),
    "webinar": estilo("Tecnológico", "barra", "revela", "#06b6d4", "#67e8f9", "#0b1220", 0xFF241812,
                      familia="Noto Sans", titulo=SANS, radio=6, texto2="#cffafe"),
    "streaming-juegos": estilo("Neón", "neon", "crece", "#a855f7", "#22d3ee", "#0a0014", 0xFF1A0612,
                               peso="Font.Bold", radio=12, entrada_ms=500, texto2="#67e8f9"),
    "acto": estilo("Protocolar", "filete", "revela", "#1e3a8a", "#d4af37", "#0d1b3e", 0xFF2E1A0D,
                   familia="Linux Biolinum G", titulo=SERIF, peso="Font.Normal", radio=0,
                   entrada_ms=900, texto2="#f1e3b0"),
    "ceremonia": estilo("Sereno", "filete", "revela", "#b08d57", "#e9d5a1", "#2b2118", 0xFF18212B,
                        familia="Gentium Basic", titulo="Gentium Basic",
                        peso="Font.Normal", radio=0, entrada_ms=1100, texto2="#f3e7c9"),
    "lanzamiento": estilo("Impacto", "diagonal", "crece", "#ff3d00", "#ffd166", "#000000", 0xFF0A0A0A,
                          peso="Font.Black", radio=0, entrada_ms=500, texto2="#e5e5e5"),
    "deportivo": estilo("Deportivo", "diagonal", "desliza", "#22c55e", "#facc15", "#052e16", 0xFF162E05,
                        familia="Liberation Sans Narrow", titulo="Rubik", peso="Font.Black",
                        radio=0, entrada_ms=450, texto2="#bbf7d0"),
    "senaletica": estilo("Señalética", "barra", "sube", "#0ea5e9", "#7dd3fc", "#0f172a", 0xFF2A170F,
                         peso="Font.Bold", radio=8, texto2="#e0f2fe"),
}
EST = ESTILOS["reunion-chica"]  # el de la plantilla que se está armando (lo pone main)

FILTRO_PTZ = json.dumps({"alfa": False, "video": [{"elemento": "baptz", "id": "ptz"}]}, separators=(",", ":"))
FONDO = 0xFF2A2320   # 0xAABBGGRR (rojo en el byte bajo): un gris azulado oscuro, rgb(32, 35, 42)


# --- Fuentes -------------------------------------------------------------------------
def fuente(id, nombre, rect, settings, visible=True):
    return {"id": id, "name": T(nombre), "rect": list(rect), "settings": settings, "visible": visible,
            "volume": 1.0, "muted": False}


def fondo(color=None):
    color = EST["fondo"] if color is None else color
    return fuente("color_source_v3", "Fondo", (0, 0, 1, 1), {"color": color, "width": 1920, "height": 1080})


def texto(nombre, txt, rect, tam=72):
    return fuente("text_ft2_source_v2", nombre, rect, {"text": T(txt), "font": {"face": "Helvetica", "size": tam}})


def camara(nombre="Cámara", rect=(0, 0, 1, 1)):
    # «default»: la primera cámara de la máquina (una plantilla no sabe cuál hay).
    return fuente("ba_gst_fuente", nombre, rect, {"audio": False, "camara": "default", "camara_modo": "",
                                                   "filtros": FILTRO_PTZ, "normalizar": True, "pipeline": "",
                                                   "pipeline_audio": ""})


def sala(canal, slot, nombre, rect=(0, 0, 1, 1)):
    """Un invitado o remoto de la sala de videoconferencia (canal del gateway)."""
    tuberias = {"guest": "BA.Guest", "share": "BA.GuestShare", "remoto": "BA.Remoto",
                "remoto_contenido": "BA.RemotoContenido"}
    return fuente("ba_gst_fuente", nombre, rect, {"audio": False, "channel": canal, "estudio": False,
                                                   "filtros": FILTRO_PTZ, "normalizar": False,
                                                   "pipeline": "bavideoanillo canal=%s.%d" % (tuberias[canal], slot),
                                                   "pipeline_audio": "", "slot": slot, "video_invitado": True})


def tam_grafico(cual):
    # Los propios de la plantilla (ver PROPIOS) mandan sobre los de fábrica.
    # (El tamaño no depende del estilo: sirve el de cualquier plantilla que lo traiga.)
    for g in [PROPIOS, GRAFICOS_BASE] + list(GRAFICOS_DE.values()):
        if cual in g:
            return g[cual]["ancho"], g[cual]["alto"]
    f = json.load(open(os.path.join(GRAFICOS, cual, "grafico.json")))
    return f.get("ancho", 1920), f.get("alto", 1080)


def grafico(nombre, cual, pos=None, al_abrir=False, bucle=False, valores=None):
    """Un gráfico animado con SU tamaño (el de su ficha). `pos`: (x, y) de la esquina en
    fracciones del lienzo; sin ella, centrado. `al_abrir`: arranca solo al abrir el evento
    (con `valores`), `bucle`: se repite sin parar (GraficoSource: alAbrir, bucle, valores)."""
    ancho, alto = tam_grafico(cual)
    w, h = ancho / 1920.0, alto / 1080.0
    x, y = pos if pos else ((1 - w) / 2, (1 - h) / 2)
    aj = {"grafico": cual, "ancho": ancho, "alto": alto}
    if al_abrir:
        aj.update({"alAbrir": True, "bucle": bucle, "valores": valores or {}})
    return fuente("ba_grafico", nombre, (x, y, w, h), aj)


MEDIOS = os.path.join(RAIZ, "assets", "medios")


BANDERA = "bandera-malvinas.png"  # cuál de las de assets/medios va de mosca (ver CREDITOS.md)


def bandera():
    # La mosca de arriba a la derecha: una imagen fija con transparencia (assets/medios),
    # semitransparente por efecto. La ruta es relativa a la carpeta del evento (el motor la
    # resuelve ahí). Fija a propósito: el GIF en una fuente de video parpadeaba en pantalla.
    from PIL import Image  # solo para el tamaño
    iw, ih = Image.open(os.path.join(MEDIOS, BANDERA)).size
    w = 0.11
    h = w * 1920 / 1080 * ih / iw
    f = fuente("image_source", "Bandera", (1 - w - 0.015, 0.025, w, h), {"file": "Media/" + BANDERA})
    f["efectos"] = {"opacidad": 0.85}  # con transparencia: una mosca no tapa
    return f


def copiar_medios(dest):
    os.makedirs(os.path.join(dest, "Media"), exist_ok=True)
    for f in os.listdir(MEDIOS):
        shutil.copy(os.path.join(MEDIOS, f), os.path.join(dest, "Media", f))


SIN_PLACA = ("Espera", "Cierre", "PDF", "Pausa", "Entretiempo", "QR grande", "Placa")  # en español: T al comparar


def con_placa(escenas):
    """La «Placa» (de título o de bloque) en cada escena donde puede salir: una cue de placa
    la dispara en la escena que esté al aire (antes vivía solo en la escena «Placa» y una
    cue después de «Al aire: Conducción» se dibujaba sin verse). Es UNA fuente compartida."""
    for e in escenas:
        if e["name"] in {T(x) for x in SIN_PLACA} or any(x.get("name") == T("Placa") for x in e["sources"]):
            continue
        # En el tercio de abajo: centrada tapaba la cara de quien está en cámara.
        e["sources"].append(grafico("Placa", "placa-titulo", (0, 0.60)))
    return escenas


def revisar_guion(titulo, escenas, bloques):
    """Recorre el guion siguiendo la escena al aire: cada cue de gráfico tiene que caer en una
    fuente que esté en esa escena (si no, sale al aire sin verse). Devuelve los problemas."""
    fuentes = {e["name"]: {x["name"] for x in e["sources"]} for e in escenas}
    al_aire, mal = T("Espera"), []
    for b in bloques:
        for c in b["cues"]:
            if c["tipo"] == "escena":
                al_aire = c["datos"]["escena"]
                if al_aire not in fuentes:
                    mal.append("%s · %s: no existe la escena «%s»" % (titulo, c["texto"], al_aire))
            elif c["tipo"] == "grafico" and c["datos"].get("fuente") not in fuentes.get(al_aire, set()):
                mal.append("%s · «%s»: la fuente «%s» no está en «%s» (al aire en ese momento)"
                           % (titulo, c["texto"], c["datos"].get("fuente"), al_aire))
    return mal


def con_bandera(escenas):
    """La bandera arriba de todo en cada escena. Es UNA fuente compartida (mismo nombre):
    se carga una vez. No va en una capa DSK: una escena o fuente en el DSK se compone
    sobre negro opaco (Composicion: background=black) y tapaba el programa."""
    for e in escenas:
        e["sources"] = [x for x in e["sources"] if x.get("name") != T("Bandera")] + [bandera()]
    return escenas


def zocalo(cual="zocalo"):
    # Abajo a la izquierda, como un zócalo de verdad.
    w, h = tam_grafico(cual)
    return grafico("Zócalo", cual, (0.04, 0.92 - h / 1080.0))


def pdf():
    return fuente("ba_pdf", "PDF", (0, 0, 1, 1), {"documento": "", "pagina": 1})


def escena(nombre, fuentes, carpeta=""):
    return {"name": T(nombre), "folder": carpeta, "sources": fuentes}


def escenas_base(titulo):
    """Las que tiene todo evento."""
    return [
        escena("Espera", [fondo(), grafico("Placa de espera", "espera", al_abrir=True,
                                           valores={"titulo": T(titulo), "bajada": T("Empezamos a las ‹hora›")})]),
        escena("Conducción", [camara(), zocalo()]),
        escena("Invitado remoto", [fondo(), sala("remoto", 1, "Remoto con la palabra"), zocalo()]),
        escena("Presentación", [fondo(), sala("remoto_contenido", 1, "Remoto con la palabra — contenido"),
                                camara("Cámara", (0.74, 0.70, 0.24, 0.27))]),
        escena("Mosaico", [fondo(), sala("guest", 1, "Invitado 1", (0, 0, 0.5, 0.5)),
                           sala("guest", 2, "Invitado 2", (0.5, 0, 0.5, 0.5)),
                           sala("guest", 3, "Invitado 3", (0, 0.5, 0.5, 0.5)),
                           sala("guest", 4, "Invitado 4", (0.5, 0.5, 0.5, 0.5))]),
        escena("PDF", [fondo(0xFF000000), pdf()]),
        escena("Placa", [camara(), grafico("Placa", "placa-titulo")]),
        escena("Cierre", [fondo(), grafico("Placa de cierre", "cierre", al_abrir=True)]),
    ]


# --- Guion ---------------------------------------------------------------------------
_n = [0]


def _id(p):
    _n[0] += 1
    return "%s%d-g" % (p, _n[0])


def cue(tipo, texto_, datos=None, seg=0, auto=False):
    return {"id": _id("c"), "tipo": tipo, "texto": texto_, "datos": datos or {}, "duracionSeg": seg,
            "automatico": auto}


def c_escena(nombre, txt=None, seg=0):
    return cue("escena", T(txt) if txt else T("Al aire: %s") % T(nombre), {"escena": T(nombre)}, seg)


def c_zocalo(nombre="‹Nombre›", cargo="‹Cargo›"):
    nombre, cargo = T(nombre), T(cargo)
    return cue("grafico", T("Zócalo: %s") % nombre,
               {"fuente": T("Zócalo"), "grafico": "zocalo", "valores": {"nombre": nombre, "cargo": cargo, "acento": EST["acento"],
                                                                      "salidaEn": 0}})


def c_placa(titulo, bajada="‹Bajada›"):
    titulo, bajada = T(titulo), T(bajada)
    return cue("grafico", T("Placa: %s") % titulo,
               {"fuente": T("Placa"), "grafico": "placa-titulo",
                "valores": {"titulo": titulo, "bajada": bajada, "acento": EST["acento"]}})  # se va al avanzar (su ficha)


def c_grafico(fuente_, cual, valores, txt):
    return cue("grafico", T(txt), {"fuente": T(fuente_), "grafico": cual,
                                   "valores": {k: T(v) for k, v in valores.items()}})


def c_prompter(archivo, txt):
    return cue("teleprompter", T(txt), {"discurso": archivo, "arrancar": False})


def c_nota(txt, seg=0):
    return cue("nota", T(txt), {}, seg)


def c_espera(txt):
    return cue("espera", T(txt))


def bloque(nombre, cues):
    return {"id": _id("b"), "nombre": T(nombre), "nota": "", "cues": cues}


def apertura(titulo):
    return bloque("Apertura", [c_escena("Espera", "Placa de espera con la hora de inicio"),
                               c_espera("Confirmá que está todo listo para empezar"),
                               c_escena("Conducción"), c_placa(titulo),
                               c_prompter("0-apertura.txt", "Teleprompter: la apertura"),
                               c_zocalo("‹Quien conduce›", "‹Cargo o rol›"),
                               c_nota("Bienvenida y presentación del evento", 120)])


def cierre():
    return bloque("Cierre", [c_escena("Conducción"), c_prompter("9-cierre.txt", "Teleprompter: el cierre"),
                             c_nota("Agradecimientos y cierre", 60), c_escena("Cierre", seg=15),
                             c_grafico("Placa de cierre", "cierre", {"titulo": "¡Gracias por acompañarnos!",
                                                                     "bajada": "‹Dónde queda la grabación›"},
                                       "Placa de cierre: dónde ver la grabación")])


# --- Escenarios ----------------------------------------------------------------------
def p(titulo, extra_escenas, bloques, padron=None, discursos=None):
    return {"titulo_evento": T(titulo), "escenas": extra_escenas, "bloques": bloques, "padron": padron or [],
            "discursos": {k: T(v) for k, v in (discursos or {}).items()}}


# Los papeles del padrón (evento/Padron): orador, panelista, publico, prensa, staff.
PAPELES = {"orador": "orador", "panelista": "panelista", "invitado": "orador"}


def gente(n, papel, vota=False, prefijo="Integrante"):
    return [{"nombre": T("‹%s %d›") % (T(prefijo), i + 1), "papel": PAPELES.get(papel, "publico"), "vota": vota}
            for i in range(n)]


def escenarios():
    E = {}
    E["reunion-chica"] = p("‹Reunión›", [], [
        apertura("‹Reunión›"),
        bloque("Temas", [c_escena("Mosaico"), c_nota("‹Tema 1›", 900), c_nota("‹Tema 2›", 900),
                         c_escena("PDF", "Documento en pantalla (si hace falta)")]),
        cierre()], gente(6, "participante", prefijo="Participante"))
    E["reunion-hibrida"] = p("‹Reunión de trabajo›", [
        escena("Sala", [camara(), zocalo()])], [
        apertura("‹Reunión de trabajo›"),
        bloque("‹Tema 1›", [c_escena("Sala"), c_placa("‹Tema 1›"), c_zocalo("‹Responsable›", "‹Área›"),
                            c_escena("Presentación", "La presentación del tema"), c_nota("Exposición y discusión", 1200)]),
        bloque("‹Tema 2›", [c_escena("Invitado remoto"), c_placa("‹Tema 2›"), c_zocalo("‹Remoto›", "‹Área›"),
                            c_nota("Exposición y discusión", 1200)]),
        bloque("Decisiones y próximos pasos", [c_escena("Mosaico"), c_nota("Repaso de lo decidido", 600)]),
        cierre()], gente(8, "participante", prefijo="Participante"))
    E["comision"] = p("‹Reunión de la Comisión de ...›", [
        escena("Presidencia", [camara(), zocalo("zocalo-integrante")]),
        escena("Votación", [fondo(), grafico("Votación", "votacion")])], [
        apertura("‹Comisión›"),
        bloque("Orden del día", [c_escena("Presidencia"), c_nota("Lectura del orden del día y verificación de quórum", 180),
                                 c_escena("PDF", "El expediente en pantalla"), c_nota("‹Expediente 1›: tratamiento", 1200)]),
        bloque("Invitados", [c_escena("Invitado remoto"), c_zocalo("‹Invitado›", "‹Organismo›"),
                             c_nota("Exposición del invitado", 900)]),
        bloque("Dictamen", [c_escena("Votación"), c_nota("Votación del dictamen", 300)]),
        cierre()], gente(7, "integrante", vota=True))
    E["asamblea-votacion"] = p("‹Asamblea General›", [
        escena("Mesa", [camara(), zocalo()]),
        escena("Votación", [fondo(), grafico("Votación", "votacion")]),
        escena("Quórum", [fondo(), grafico("Quórum", "quorum", (0.64, 0.05))])], [
        apertura("‹Asamblea General›"),
        bloque("Quórum y orden del día", [c_escena("Quórum"), c_nota("Acreditación y verificación del quórum", 600),
                                          c_escena("Mesa"), c_nota("Lectura del orden del día", 300)]),
        bloque("‹Punto 1›", [c_placa("‹Punto 1›"), c_escena("PDF", "La documentación del punto"),
                             c_nota("Debate", 1200), c_escena("Votación"), c_nota("Votación del punto", 300)]),
        bloque("‹Punto 2›", [c_placa("‹Punto 2›"), c_nota("Debate", 1200), c_escena("Votación"),
                             c_nota("Votación del punto", 300)]),
        cierre()], gente(40, "integrante", vota=True, prefijo="Asociado"))
    E["asamblea-remota"] = p("‹Asamblea›", [
        escena("Mesa", [camara(), zocalo()]),
        escena("Votación", [fondo(), grafico("Votación", "votacion")]),
        escena("Remotos", [fondo(), sala("remoto", 1, "Remoto con la palabra", (0, 0, 0.5, 1)),
                           sala("remoto", 2, "Remoto con la palabra 2", (0.5, 0, 0.5, 1))])], [
        apertura("‹Asamblea›"),
        bloque("Quórum (presentes y remotos)", [c_escena("Remotos"), c_nota("Verificación de presentes y remotos", 600)]),
        bloque("‹Punto 1›", [c_placa("‹Punto 1›"), c_escena("Invitado remoto"), c_zocalo("‹Integrante remoto›", "‹Bloque›"),
                             c_nota("Debate", 1200), c_escena("Votación"), c_nota("Votación (también remotos)", 300)]),
        cierre()], gente(20, "integrante", vota=True))
    E["consorcio"] = p("‹Asamblea de propietarios›", [
        escena("Mesa", [camara(), zocalo()]),
        escena("Votación", [fondo(), grafico("Votación", "votacion")])], [
        apertura("‹Asamblea de propietarios›"),
        bloque("Constitución", [c_escena("Mesa"), c_nota("Presentes, poderes y quórum", 600)]),
        bloque("Balance", [c_placa("Balance del ejercicio"), c_escena("PDF", "El balance en pantalla"),
                           c_nota("Presentación y preguntas", 1200), c_escena("Votación"), c_nota("Aprobación", 300)]),
        bloque("‹Otros puntos›", [c_nota("‹Punto›", 900), c_escena("Votación")]),
        cierre()], gente(30, "propietario", vota=True, prefijo="Propietario UF"))
    E["clase-hibrida"] = p("‹Clase de ...›", [
        escena("Docente", [camara(), zocalo()]),
        escena("Pizarra", [camara("Cámara")]),
        escena("Pregunta remota", [fondo(), sala("remoto", 1, "Remoto con la palabra"), zocalo()])], [
        apertura("‹Clase de ...›"),
        bloque("Asistencia", [c_escena("Mosaico"), c_nota("Tomar asistencia (padrón: acreditar a cada alumno)", 300)]),
        bloque("‹Tema 1›", [c_escena("Docente"), c_placa("‹Tema 1›"),
                            c_prompter("1-tema-1.txt", "Teleprompter: el tema 1"),
                            c_escena("PDF", "La ficha del tema"), c_nota("Explicación", 900)]),
        bloque("‹Tema 2›", [c_escena("Docente"), c_placa("‹Tema 2›"),
                            c_prompter("2-tema-2.txt", "Teleprompter: el tema 2"), c_nota("Explicación", 900)]),
        bloque("Preguntas", [c_escena("Pregunta remota"), c_nota("Preguntas del aula y de los remotos", 600)]),
        cierre()], gente(30, "alumno", prefijo="Alumno"),
        {"1-tema-1.txt": "‹Lo que vas a decir en el tema 1›", "2-tema-2.txt": "‹Lo que vas a decir en el tema 2›"})
    E["conferencia"] = p("‹Conferencia›", [
        escena("Orador", [camara(), zocalo()]),
        escena("Preguntas", [fondo(), fuente("ba_qr", "QR preguntas", (0.62, 0.2, 0.32, 0.57),
                                             {"qr": "aparato:publico", "fondo": True, "tamano": 720}),
                             texto("Pregunte", "Escaneá y hacé tu pregunta", (0.06, 0.4, 0.5, 0.1), 60)])], [
        apertura("‹Conferencia›"),
        bloque("‹Orador 1›", [c_escena("Orador"), c_zocalo("‹Orador 1›", "‹Cargo›"),
                              c_escena("Presentación", "Su presentación"), c_nota("Exposición", 1500)]),
        bloque("‹Orador 2›", [c_escena("Invitado remoto"), c_zocalo("‹Orador 2›", "‹Cargo›"), c_nota("Exposición", 1500)]),
        bloque("Preguntas del público", [c_escena("Preguntas"), c_nota("Preguntas moderadas", 900)]),
        cierre()], gente(3, "orador", prefijo="Orador"))
    E["panel"] = p("‹Panel›", [
        escena("Plano general", [camara()]),
        escena("Panelistas", [fondo(), camara("Cámara", (0, 0, 0.5, 0.5)),
                              sala("guest", 1, "Invitado 1", (0.5, 0, 0.5, 0.5)),
                              sala("guest", 2, "Invitado 2", (0, 0.5, 0.5, 0.5)),
                              sala("guest", 3, "Invitado 3", (0.5, 0.5, 0.5, 0.5)), zocalo()])], [
        apertura("‹Panel›"),
        bloque("Ronda de apertura", [c_escena("Panelistas"), c_zocalo("‹Panelista 1›", "‹Cargo›"), c_nota("Cada panelista: 5 minutos", 1200)]),
        bloque("Intercambio", [c_escena("Plano general"), c_nota("Preguntas cruzadas", 1500)]),
        bloque("Preguntas del público", [c_nota("Preguntas", 900)]),
        cierre()], gente(4, "panelista", prefijo="Panelista"))
    E["entrevista"] = p("‹Entrevista›", [
        escena("Dos planos", [fondo(), camara("Cámara", (0, 0.1, 0.5, 0.8)),
                              sala("remoto", 1, "Remoto con la palabra", (0.5, 0.1, 0.5, 0.8)), zocalo()])], [
        apertura("‹Entrevista›"),
        bloque("Entrevista", [c_escena("Dos planos"), c_zocalo("‹Invitado›", "‹Quién es›"), c_nota("‹Preguntas›", 1800)]),
        cierre()], gente(1, "invitado", prefijo="Invitado"))
    E["webinar"] = p("‹Webinar›", [
        escena("Presentador", [camara(), zocalo()]),
        escena("Pantalla", [fondo(), fuente("screen_capture", "Pantalla", (0, 0, 1, 1), {"show_cursor": True}),
                            camara("Cámara", (0.76, 0.72, 0.22, 0.25))])], [
        apertura("‹Webinar›"),
        bloque("Contenido", [c_escena("Presentador"), c_zocalo("‹Presentador›", "‹Empresa›"),
                             c_escena("Pantalla", "Demostración en pantalla"), c_nota("‹Contenido›", 2400)]),
        bloque("Preguntas", [c_nota("Preguntas de los inscriptos", 900)]),
        cierre()])
    E["streaming-juegos"] = p("‹Stream›", [
        escena("Juego", [fuente("screen_capture", "Juego", (0, 0, 1, 1), {"show_cursor": False}),
                         camara("Cámara", (0.76, 0.18, 0.22, 0.25)),
                         fuente("ba_chat_overlay", "Chat", (0.76, 0.45, 0.22, 0.5), {})]),
        escena("Charla", [camara(), fuente("ba_chat_overlay", "Chat", (0.7, 0.1, 0.28, 0.8), {})]),
        escena("Pausa", [fondo(), grafico("Ya volvemos", "pausa", al_abrir=True)])], [
        bloque("Inicio", [c_escena("Espera"), c_espera("Arrancá la transmisión"), c_escena("Charla", seg=300)]),
        bloque("A jugar", [c_escena("Juego", seg=3600)]),
        bloque("Pausa", [c_escena("Pausa", seg=300)]),
        bloque("Cierre", [c_escena("Charla", seg=300), c_escena("Cierre", seg=15)])])
    E["acto"] = p("‹Acto›", [
        escena("Escenario", [camara(), zocalo(),
                             fuente("image_source", "Escudo", (0.02, 0.03, 0.1, 0.17), {"file": ""})]),
        escena("Egresados", [camara(), zocalo()])], [
        apertura("‹Acto›"),
        bloque("Himno y banderas", [c_escena("Escenario"), c_nota("Ingreso de banderas e Himno Nacional", 300)]),
        bloque("Palabras de autoridades", [c_zocalo("‹Autoridad 1›", "‹Cargo›"), c_nota("Discurso", 600),
                                           c_zocalo("‹Autoridad 2›", "‹Cargo›"), c_nota("Discurso", 600)]),
        bloque("Entrega", [c_escena("Egresados"), c_zocalo("‹Egresado 1›", "‹Carrera›"),
                           c_zocalo("‹Egresado 2›", "‹Carrera›"), c_nota("Entrega de diplomas", 1800)]),
        cierre()], gente(20, "egresado", prefijo="Egresado"))
    E["ceremonia"] = p("‹Ceremonia›", [
        escena("Altar", [camara()]),
        escena("Letra", [camara(), grafico("Letra", "letra", (0.05, 0.70))])], [
        apertura("‹Ceremonia›"),
        bloque("Celebración", [c_escena("Altar"), c_nota("‹Momento 1›", 900), c_escena("Letra", "Canto con la letra"),
                               c_grafico("Letra", "letra", {"estrofa": "‹Primer verso›\n‹Segundo verso›"}, "Letra: ‹canto›"),
                               c_nota("‹Momento 2›", 900)]),
        cierre()])
    E["audiencia-publica"] = p("‹Audiencia pública›", [
        escena("Presidencia", [camara(), zocalo()]),
        escena("Orador", [camara(), zocalo(), grafico("Próximo", "proximo-orador", (0.57, 0.04))])], [
        apertura("‹Audiencia pública›"),
        bloque("Apertura formal", [c_escena("Presidencia"), c_nota("Lectura del objeto de la audiencia y reglas", 600)]),
        bloque("Oradores inscriptos", [c_escena("Orador"), c_zocalo("‹Orador 1›", "‹Entidad›"), c_nota("5 minutos", 300),
                                       c_zocalo("‹Orador 2›", "‹Entidad›"), c_nota("5 minutos", 300)]),
        cierre()], gente(15, "orador", prefijo="Inscripto"))
    E["lanzamiento"] = p("‹Lanzamiento›", [
        escena("Escenario", [camara(), zocalo()])], [
        bloque("Espera", [c_escena("Espera", seg=600)]),
        bloque("Apertura", [c_nota("Video de apertura (cargalo en Medios)", 120), c_escena("Escenario"),
                            c_zocalo("‹Presentador›", "‹Empresa›"), c_prompter("0-apertura.txt", "Teleprompter: la apertura")]),
        bloque("Presentación", [c_escena("Presentación"), c_nota("‹Lo que se lanza›", 1200)]),
        bloque("Preguntas de la prensa", [c_nota("Preguntas", 600)]),
        cierre()])
    E["deportivo"] = p("‹Partido›", [
        escena("Cancha", [camara(), grafico("Marcador", "marcador", (0.02, 0.03), al_abrir=True,
                                            valores={"local": "‹LOC›", "visitante": "‹VIS›", "tiempo": "1T"})]),
        escena("Relatores", [fondo(), sala("remoto", 1, "Remoto con la palabra", (0, 0, 0.5, 1)),
                             sala("remoto", 2, "Remoto con la palabra 2", (0.5, 0, 0.5, 1))]),
        escena("Entretiempo", [fondo(), grafico("Entretiempo", "entretiempo", al_abrir=True)])], [
        bloque("Previa", [c_escena("Espera"), c_escena("Relatores", seg=600)]),
        bloque("Primer tiempo", [c_escena("Cancha"),
                                 c_grafico("Marcador", "marcador", {"local": "‹LOC›", "visitante": "‹VIS›", "golesLocal": 0,
                                                                    "golesVisitante": 0, "tiempo": "1T"}, "Marcador: arranca el partido"),
                                 c_nota("Primer tiempo (para un gol: la cue de abajo con los goles nuevos)", 2700),
                                 c_grafico("Marcador", "marcador", {"local": "‹LOC›", "visitante": "‹VIS›", "golesLocal": 1,
                                                                    "golesVisitante": 0, "tiempo": "1T"}, "Marcador: ‹gol›")]),
        bloque("Entretiempo", [c_escena("Entretiempo", seg=900)]),
        bloque("Segundo tiempo", [c_escena("Cancha"),
                                  c_grafico("Marcador", "marcador", {"local": "‹LOC›", "visitante": "‹VIS›", "golesLocal": 1,
                                                                     "golesVisitante": 0, "tiempo": "2T"}, "Marcador: segundo tiempo"),
                                  c_nota("Segundo tiempo", 2700)]),
        bloque("Cierre", [c_escena("Relatores", seg=300), c_escena("Cierre", seg=15)])])
    E["senaletica"] = p("‹Señalética del evento›", [
        escena("Pantalla de sala", [fondo(), texto("Agenda", "‹Agenda del día›", (0.05, 0.06, 0.55, 0.8), 48),
                                    fuente("ba_qr", "QR", (0.64, 0.2, 0.32, 0.57),
                                           {"qr": "aparato:publico", "fondo": True, "tamano": 720})]),
        escena("QR grande", [fondo(0xFFFFFFFF), fuente("ba_qr", "QR", (0.3, 0.12, 0.4, 0.71),
                                                      {"qr": "aparato:publico", "fondo": True, "tamano": 1080})])], [
        bloque("Durante el evento", [c_escena("Pantalla de sala"),
                                     c_nota("Mandar la escena a cada televisor (pantalla_mostrar)", 0)])])
    return E


# --- Gráficos propios ------------------------------------------------------------------
# Lo que cada plantilla trae con SU estilo (graficos-plantilla/<molde>.qml con los colores
# de ESTILOS). Van con el mismo nombre que los de fábrica donde existen (zocalo,
# placa-titulo): las cues y las herramientas los nombran igual, y en esa plantilla se ven
# con su estilo.
def p_texto(clave, titulo, defecto, ayuda=""):
    return {"clave": clave, "titulo": titulo, "tipo": "texto", "porDefecto": defecto, "ayuda": ayuda}


P_ACENTO = {"clave": "acento", "titulo": "Color de acento", "tipo": "color", "ayuda": "El color del evento."}
P_SALIDA = {"clave": "salidaEn", "titulo": "Cuándo se va", "tipo": "ms", "porDefecto": 0, "minimo": 0, "maximo": 60000,
            "ayuda": "Milisegundos desde que apareció. 0 = se queda hasta que lo saquen."}


def tarjeta(nombre, descripcion, etiqueta, titulo, bajada):
    return {"molde": "tarjeta", "nombre": nombre, "descripcion": descripcion, "ancho": 1600, "alto": 620,
            "duracionMs": 2000, "tokens": {"etiqueta": etiqueta, "titulo": titulo, "bajada": bajada, "nombreGrafico": nombre},
            "parametros": [p_texto("etiqueta", "Etiqueta", etiqueta, "El renglón chico de arriba, en mayúsculas."),
                           p_texto("titulo", "Título", titulo), p_texto("bajada", "Bajada", bajada), P_ACENTO, P_SALIDA]}


GRAFICOS_BASE = {
    "zocalo": {"molde": "zocalo", "nombre": "Zócalo", "descripcion": "El nombre y el cargo de quien habla, abajo a la izquierda.",
               "ancho": 900, "alto": 220, "duracionMs": 2000,
               "parametros": [p_texto("nombre", "Nombre", "‹Nombre›", "Más de 30 caracteres empieza a quedar apretado."),
                              p_texto("cargo", "Cargo o procedencia", "‹Cargo›", "El renglón chico. Puede ir vacío."),
                              P_ACENTO, {"clave": "duracionEntrada", "titulo": "Cuánto tarda en entrar", "tipo": "ms",
                                         "minimo": 150, "maximo": 1500}, P_SALIDA]},
    "placa-titulo": {"molde": "placa-titulo", "nombre": "Placa de título",
                     "descripcion": "La apertura de un bloque: una banda a media altura con el título y una bajada.",
                     "ancho": 1920, "alto": 360, "duracionMs": 2000, "seVa": "al-avanzar",
                     "parametros": [p_texto("titulo", "Título", "‹Título›", "Más de 34 caracteres se recorta."),
                                    p_texto("bajada", "Bajada", "‹Bajada›"), P_ACENTO, P_SALIDA]},
    "espera": tarjeta("Placa de espera", "Antes de empezar: el nombre del evento y a qué hora arranca.",
                      "En instantes", "‹Nombre del evento›", "Empezamos a las ‹hora›"),
    "cierre": tarjeta("Placa de cierre", "Al terminar: el agradecimiento y dónde queda la grabación.",
                      "Gracias", "¡Gracias por acompañarnos!", "‹Dónde queda la grabación›"),
}
GRAFICOS_DE = {
    "streaming-juegos": {"pausa": tarjeta("Ya volvemos", "La pausa del stream.", "Pausa", "Ya volvemos", "‹En unos minutos›")},
    "deportivo": {
        "marcador": {"molde": "marcador", "nombre": "Marcador", "descripcion": "Local, visitante, goles y tiempo, arriba a la izquierda.",
                     "ancho": 760, "alto": 110, "duracionMs": 1000,
                     "parametros": [p_texto("local", "Local", "‹LOC›", "Tres letras."), p_texto("visitante", "Visitante", "‹VIS›"),
                                    {"clave": "golesLocal", "titulo": "Goles del local", "tipo": "numero", "porDefecto": 0},
                                    {"clave": "golesVisitante", "titulo": "Goles del visitante", "tipo": "numero", "porDefecto": 0},
                                    p_texto("tiempo", "Tiempo", "1T", "1T, 2T, ET, o el minuto."), P_ACENTO, P_SALIDA]},
        "entretiempo": tarjeta("Entretiempo", "La placa del entretiempo.", "Entretiempo", "‹Local› 0 - 0 ‹Visitante›",
                               "Ya vuelve el segundo tiempo")},
    "ceremonia": {
        "letra": {"molde": "letra", "nombre": "Letra", "descripcion": "Una estrofa para cantar, centrada abajo.",
                  "ancho": 1700, "alto": 300, "duracionMs": 1500, "seVa": "al-avanzar",
                  "parametros": [p_texto("estrofa", "Estrofa", "‹Primer verso›\n‹Segundo verso›",
                                         "Hasta tres renglones (separados con un salto de línea)."), P_ACENTO, P_SALIDA]}},
}
PROPIOS = {}  # los de la plantilla que se está armando (lo pone main)
# Los textos por defecto de los moldes (graficos-plantilla/*.qml), que se traducen al escribirlos.
MOLDES_TEXTOS = ("‹Título›", "‹Bajada›", "‹LOC›", "‹VIS›", "‹Nombre›", "‹Cargo›", "‹Primer verso›", "‹Segundo verso›")
PROBLEMAS = []  # lo que revisar_guion encontró (main sale con error si hay)


def escribir_graficos(dest, meta):
    """Los de fábrica y, encima, los propios de la plantilla con su estilo."""
    import string
    shutil.copytree(GRAFICOS, os.path.join(dest, "Graficos"))
    for cual, g in PROPIOS.items():
        d = os.path.join(dest, "Graficos", cual)
        if os.path.isdir(d):
            shutil.rmtree(d)
        os.makedirs(d)
        tokens = dict(EST, plantilla=meta["titulo"], **{k: T(v) for k, v in g.get("tokens", {}).items()})
        qml = string.Template(open(os.path.join(MOLDES, g["molde"] + ".qml")).read()).substitute(tokens)
        for ph in MOLDES_TEXTOS:  # los ‹…› por defecto de los moldes
            qml = qml.replace(ph, T(ph).replace('"', '\\"'))
        open(os.path.join(d, "grafico.qml"), "w").write(qml)
        params = []
        for prm in g["parametros"]:
            prm = dict(prm)
            for k in ("titulo", "ayuda", "porDefecto"):
                if isinstance(prm.get(k), str):
                    prm[k] = T(prm[k])
            if prm["clave"] == "acento":
                prm["porDefecto"] = EST["acento"]
            if prm["clave"] == "duracionEntrada":
                prm["porDefecto"] = int(EST["entradaMs"])
            params.append(prm)
        ficha = {"nombre": T(g["nombre"]),
                 "descripcion": T(g["descripcion"]) + " " + T("Estilo «%s».") % T(EST["estilo"])}
        if g.get("seVa"):
            ficha["seVa"] = g["seVa"]  # cuándo se va una cue del guion que no lo dice (EventoViewModel)
        ficha.update({"categoria": T("De la plantilla"), "ancho": g["ancho"], "alto": g["alto"], "duracionMs": g["duracionMs"],
                      "parametros": params})
        json.dump(ficha, open(os.path.join(d, "grafico.json"), "w"), ensure_ascii=False, indent=2)


# El orden del día de ejemplo de las asambleas (Config/orden-del-dia.json, el de la Demo
# parlamentaria) y cuáles son asamblea (la web muestra integrantes, quórum, orden del día y
# los resultados de las votaciones).
ORDEN_DEL_DIA = {
    "comision": ["Lectura y aprobación del acta anterior", "‹Expediente 1›", "‹Expediente 2›", "Dictámenes"],
    "asamblea-votacion": ["Designación de dos asociados para firmar el acta", "Consideración de la memoria y el balance",
                          "‹Punto 3›", "Elección de autoridades"],
    "asamblea-remota": ["Verificación de presentes y remotos", "‹Punto 1›", "‹Punto 2›"],
    "consorcio": ["Designación de quien preside la asamblea", "Rendición de cuentas y balance del ejercicio",
                  "Presupuesto y expensas", "‹Otros puntos›"],
    "audiencia-publica": ["Lectura del objeto de la audiencia y de las reglas", "Exposición de los inscriptos",
                          "Cierre y próximos pasos"],
}
ASAMBLEAS = {"comision", "asamblea-votacion", "asamblea-remota", "consorcio"}


# --- Escritura -----------------------------------------------------------------------
def carpeta_evento(nombre):
    return nombre.replace(" ", "-").replace("/", "-")


def escribir(dest, meta, esc):
    if os.path.exists(dest):
        shutil.rmtree(dest)
    os.makedirs(os.path.join(dest, "Config"))
    titulo = esc["titulo_evento"]  # ya traducido (p)
    escenas = escenas_base(titulo)
    nombres = {e["name"] for e in escenas}
    for e in esc["escenas"]:
        if e["name"] in nombres:
            escenas = [x for x in escenas if x["name"] != e["name"]]
        escenas.append(e)
    escenas = con_bandera(con_placa(escenas))
    for m in revisar_guion(meta["titulo"], escenas, esc["bloques"]):
        print("OJO", m, file=sys.stderr)
        PROBLEMAS.append(m)
    json.dump({"scenes": escenas, "currentScene": T("Espera")}, open(os.path.join(dest, "Config", "app.json"), "w"),
              ensure_ascii=False, indent=1)
    escribir_graficos(dest, meta)
    copiar_medios(dest)
    if meta["id"] in ORDEN_DEL_DIA:
        puntos = [{"estado": "pendiente", "expediente": "", "fin_ms": 0, "inicio_ms": 0, "n": i + 1, "resultado": "",
                   "titulo": T(t)} for i, t in enumerate(ORDEN_DEL_DIA[meta["id"]])]
        json.dump({"puntos": puntos, "tramos": []}, open(os.path.join(dest, "Config", "orden-del-dia.json"), "w"),
                  ensure_ascii=False, indent=1)
    if os.path.isdir(EXTENSIONES):
        shutil.copytree(EXTENSIONES, os.path.join(dest, "Extensiones"))
    ev = os.path.join(dest, "Eventos", carpeta_evento(titulo))
    os.makedirs(os.path.join(ev, "Discursos"))
    json.dump({"nombre": titulo, "fecha": "", "inicio": "", "huso": "", "nota": meta["resumen"],
               "asamblea": meta["id"] in ASAMBLEAS, "autoAceptar": [], "bloques": esc["bloques"]},
              open(os.path.join(ev, "evento.json"), "w"), ensure_ascii=False, indent=1)
    discursos = {"0-apertura.txt": T("‹Buenas tardes y bienvenidos a ...›\n\n‹Lo que vas a decir al abrir.›\n"),
                 "9-cierre.txt": T("‹Gracias a todos por ...›\n")}
    discursos.update(esc["discursos"])
    for nom, txt in discursos.items():
        open(os.path.join(ev, "Discursos", nom), "w").write(txt)
    if esc["padron"]:
        # El formato de evento/Padron, con un código de acreditación distinto por persona.
        gente_ = [{"id": "a%d-plantilla" % (i + 1), "nombre": g["nombre"], "papel": g["papel"], "vota": g["vota"],
                   "canal": "sin_definir", "estado": "invitado", "contacto": "", "nota": "", "codigo": "%04X" % (0x1A2B + i * 37),
                   "llegoMs": 0, "porDonde": "", "dejarPasarSolo": False, "personaId": ""}
                  for i, g in enumerate(esc["padron"])]
        json.dump({"gente": gente_}, open(os.path.join(ev, "padron.json"), "w"), ensure_ascii=False, indent=1)
    # `plantilla` es sí/no (lo lee Configuraciones::listar); lo demás, en `plantillaDatos`.
    json.dump({"descripcion": meta["resumen"], "generada": True, "plantilla": True,
               "plantillaDatos": {"escenario": meta["id"], "icono": meta["icono"], "orden": meta["orden"]}},
              open(os.path.join(dest, "configuracion.json"), "w"), ensure_ascii=False, indent=1)


def main():
    import argparse
    ap = argparse.ArgumentParser(description="Genera las plantillas de evento del programa (assets/demo/<idioma>)")
    ap.add_argument("--idioma", default="es", help="el código del idioma (assets/idiomas/<c>): es, en, pt-BR…")
    ap.add_argument("--textos", action="store_true",
                    help="escribe assets/idiomas/es/plantillas/textos.json (la tabla para traducir) y no genera nada")
    args = ap.parse_args()
    # PIL hace falta para las imágenes: sin él, el script borraba lo generado y fallaba a mitad.
    if not args.textos:
        try:
            import PIL  # noqa: F401
        except ImportError:
            sys.exit("plantillas.py necesita PIL (Pillow): corrélo con .venv/bin/python3")
    global IDIOMA, DEMOS
    IDIOMA = "es" if args.textos else args.idioma
    cargar_textos(IDIOMA)
    if args.textos:
        DEMOS = tempfile.mkdtemp(prefix="plantillas-")  # se arman para juntar los textos y se tiran
        try:
            generar(retocar=False)
        finally:
            shutil.rmtree(DEMOS, ignore_errors=True)
        dest = os.path.join(IDIOMAS, "es", "plantillas", "textos.json")
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        json.dump({t: t for t in USADOS}, open(dest, "w"), ensure_ascii=False, indent=1)
        print("textos:", len(USADOS), "→", os.path.relpath(dest, RAIZ))
        return
    DEMOS = os.path.join(RAIZ, "assets", "demo", IDIOMA)
    os.makedirs(DEMOS, exist_ok=True)
    generar(retocar=True)
    if FALTAN:
        print("OJO: %d textos sin traducir en «%s» (salen en español)" % (len(FALTAN), IDIOMA), file=sys.stderr)


def generar(retocar):
    indice = indice_de(IDIOMA)
    defs = escenarios()  # solo para saber cuáles hay (se vuelven a armar con cada estilo)
    # Fuera las generadas antes (las hechas a mano quedan).
    for d in os.listdir(DEMOS):
        cj = os.path.join(DEMOS, d, "configuracion.json")
        if os.path.isfile(cj) and json.load(open(cj)).get("generada"):
            shutil.rmtree(os.path.join(DEMOS, d))
    hechas = 0
    for orden, meta in enumerate(indice):
        if meta["id"] == "sesion-deliberativa":
            continue  # es la «Demo parlamentaria» (hecha a mano, completa)
        if meta["id"] not in defs:
            print("FALTA el escenario", meta["id"], file=sys.stderr)
            continue
        meta = dict(meta, orden=orden)
        # El estilo y los gráficos propios ANTES de armar escenas y cues: los usan.
        global EST, PROPIOS
        EST = ESTILOS[meta["id"]]
        PROPIOS = dict(GRAFICOS_BASE, **GRAFICOS_DE.get(meta["id"], {}))
        _n[0] = 0
        escribir(os.path.join(DEMOS, meta["titulo"]), meta, escenarios()[meta["id"]])
        hechas += 1
    print("plantillas generadas:", hechas, "(%s)" % os.path.relpath(DEMOS, RAIZ))
    if retocar:
        retocar_demo(next(dict(m, orden=i) for i, m in enumerate(indice) if m["id"] == "sesion-deliberativa"))
    if PROBLEMAS:
        sys.exit("%d problemas en los guiones (arriba)" % len(PROBLEMAS))


def retocar_demo(meta):
    """La «Demo parlamentaria» está hecha a mano: no se regenera, solo se le suma lo que
    tienen todas (su estilo en zócalo, placa, espera y cierre; la bandera en cada escena;
    la marca de asamblea). Lo demás de ella queda como está."""
    global EST, PROPIOS
    EST, PROPIOS = ESTILOS["sesion-deliberativa"], dict(GRAFICOS_BASE)
    dest = os.path.join(DEMOS, "Demo parlamentaria")
    if not os.path.isfile(os.path.join(dest, "Config", "app.json")):
        # Está hecha a mano, en español: en otro idioma no hay (salvo que alguien la haga).
        print("FALTA la Demo parlamentaria (%s)" % os.path.relpath(dest, RAIZ), file=sys.stderr)
        return
    for cual, g in PROPIOS.items():
        d = os.path.join(dest, "Graficos", cual)
        if os.path.isdir(d):
            shutil.rmtree(d)
    # Los propios, escritos sobre su carpeta de gráficos (sin copiar de nuevo los de fábrica).
    tmp = os.path.join(dest, ".graficos-nuevos")
    if os.path.isdir(tmp):
        shutil.rmtree(tmp)
    escribir_graficos(tmp, dict(meta))
    for cual in PROPIOS:
        shutil.move(os.path.join(tmp, "Graficos", cual), os.path.join(dest, "Graficos", cual))
    shutil.rmtree(tmp)
    copiar_medios(dest)
    # «Cuándo se va» de los de fábrica, en sus copias (la demo no se regenera entera).
    for cual in os.listdir(GRAFICOS):
        fab, suya = os.path.join(GRAFICOS, cual, "grafico.json"), os.path.join(dest, "Graficos", cual, "grafico.json")
        if not (os.path.isfile(fab) and os.path.isfile(suya)):
            continue
        sv = json.load(open(fab)).get("seVa")
        j = json.load(open(suya))
        if sv and j.get("seVa") != sv:
            j["seVa"] = sv
            json.dump(j, open(suya, "w"), ensure_ascii=False, indent=2)
    app = json.load(open(os.path.join(dest, "Config", "app.json")))
    app["scenes"] = con_bandera([e for e in app["scenes"] if e.get("name") != "Logo"])
    json.dump(app, open(os.path.join(dest, "Config", "app.json"), "w"), ensure_ascii=False, indent=2)  # su formato
    for ev in os.listdir(os.path.join(dest, "Eventos")):
        f = os.path.join(dest, "Eventos", ev, "evento.json")
        if os.path.isfile(f):
            e = json.load(open(f))
            e["asamblea"] = True
            json.dump(e, open(f, "w"), ensure_ascii=False, indent=2)
    print("demo retocada:", meta["titulo"])


if __name__ == "__main__":
    main()
