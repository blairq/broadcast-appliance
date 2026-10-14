#!/usr/bin/env python3
"""Genera las plantillas de evento del programa (assets/demo/<Nombre>/), una por escenario.

Cada plantilla es una configuración completa como la «Demo parlamentaria»: sus escenas
(Config/app.json, solo las escenas: lo demás lo completa la app con lo de fábrica), los
gráficos de fábrica, el guion del evento con sus bloques y cues (textos para completar
marcados ‹así›), discursos de ejemplo para el teleprompter y, donde corresponde, un
padrón de ejemplo. El asistente parte de acá y la ajusta con la entrevista de la guía
(datos/escenarios/<id>.md).

    python3 tools/generar/plantillas.py          # regenera todas (borra las generadas antes)

Las plantillas generadas llevan en configuracion.json {"generada": true}: solo esas se
borran al regenerar (la Demo parlamentaria, hecha a mano, no se toca).
"""
import json
import os
import shutil
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEMOS = os.path.join(RAIZ, "assets", "demo")
GRAFICOS = os.path.join(RAIZ, "assets", "graficos")
EXTENSIONES = os.path.join(RAIZ, "assets", "extensiones")
INDICE = os.path.join(RAIZ, "datos", "escenarios", "indice.json")

FILTRO_PTZ = json.dumps({"alfa": False, "video": [{"elemento": "baptz", "id": "ptz"}]}, separators=(",", ":"))
FONDO = 0xFF2A2320   # 0xAABBGGRR (rojo en el byte bajo): un gris azulado oscuro, rgb(32, 35, 42)
ACENTO = "#ff6a2b"


# --- Fuentes -------------------------------------------------------------------------
def fuente(id, nombre, rect, settings, visible=True):
    return {"id": id, "name": nombre, "rect": list(rect), "settings": settings, "visible": visible,
            "volume": 1.0, "muted": False}


def fondo(color=FONDO):
    return fuente("color_source_v3", "Fondo", (0, 0, 1, 1), {"color": color, "width": 1920, "height": 1080})


def texto(nombre, txt, rect, tam=72):
    return fuente("text_ft2_source_v2", nombre, rect, {"text": txt, "font": {"face": "Helvetica", "size": tam}})


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
    f = json.load(open(os.path.join(GRAFICOS, cual, "grafico.json")))
    return f.get("ancho", 1920), f.get("alto", 1080)


def grafico(nombre, cual, pos=None):
    """Un gráfico animado con SU tamaño (el de su ficha). `pos`: (x, y) de la esquina en
    fracciones del lienzo; sin ella, a pantalla completa si es de 1920×1080 o centrado."""
    ancho, alto = tam_grafico(cual)
    w, h = ancho / 1920.0, alto / 1080.0
    x, y = pos if pos else ((1 - w) / 2, (1 - h) / 2)
    return fuente("ba_grafico", nombre, (x, y, w, h), {"grafico": cual, "ancho": ancho, "alto": alto})


def zocalo(cual="zocalo"):
    # Abajo a la izquierda, como un zócalo de verdad.
    w, h = tam_grafico(cual)
    return grafico("Zócalo", cual, (0.04, 0.92 - h / 1080.0))


def pdf():
    return fuente("ba_pdf", "PDF", (0, 0, 1, 1), {"documento": "", "pagina": 1})


def escena(nombre, fuentes, carpeta=""):
    return {"name": nombre, "folder": carpeta, "sources": fuentes}


def escenas_base(titulo):
    """Las que tiene todo evento."""
    return [
        escena("Espera", [fondo(), texto("Título", titulo, (0.06, 0.34, 0.88, 0.16), 96),
                          texto("Empezamos", "Empezamos a las ‹hora›", (0.06, 0.52, 0.88, 0.08), 48)]),
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
        escena("Cierre", [fondo(), texto("Gracias", "¡Gracias por acompañarnos!", (0.06, 0.40, 0.88, 0.14), 88)]),
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
    return cue("escena", txt or "Al aire: %s" % nombre, {"escena": nombre}, seg)


def c_zocalo(nombre="‹Nombre›", cargo="‹Cargo›"):
    return cue("grafico", "Zócalo: %s" % nombre,
               {"fuente": "Zócalo", "grafico": "zocalo", "valores": {"nombre": nombre, "cargo": cargo, "acento": ACENTO,
                                                                      "salidaEn": 0}})


def c_placa(titulo, bajada="‹Bajada›"):
    return cue("grafico", "Placa: %s" % titulo,
               {"fuente": "Placa", "grafico": "placa-titulo",
                "valores": {"titulo": titulo, "bajada": bajada, "acento": ACENTO, "salidaEn": 4500}})


def c_prompter(archivo, txt):
    return cue("teleprompter", txt, {"discurso": archivo, "arrancar": False})


def c_nota(txt, seg=0):
    return cue("nota", txt, {}, seg)


def c_espera(txt):
    return cue("espera", txt)


def bloque(nombre, cues):
    return {"id": _id("b"), "nombre": nombre, "nota": "", "cues": cues}


def apertura(titulo):
    return bloque("Apertura", [c_escena("Espera", "Placa de espera con la hora de inicio"),
                               c_espera("Confirmá que está todo listo para empezar"),
                               c_escena("Conducción"), c_placa(titulo),
                               c_prompter("0-apertura.txt", "Teleprompter: la apertura"),
                               c_zocalo("‹Quien conduce›", "‹Cargo o rol›"),
                               c_nota("Bienvenida y presentación del evento", 120)])


def cierre():
    return bloque("Cierre", [c_escena("Conducción"), c_prompter("9-cierre.txt", "Teleprompter: el cierre"),
                             c_nota("Agradecimientos y cierre", 60), c_escena("Cierre", seg=15)])


# --- Escenarios ----------------------------------------------------------------------
def p(titulo, extra_escenas, bloques, padron=None, discursos=None):
    return {"titulo_evento": titulo, "escenas": extra_escenas, "bloques": bloques, "padron": padron or [],
            "discursos": discursos or {}}


# Los papeles del padrón (evento/Padron): orador, panelista, publico, prensa, staff.
PAPELES = {"orador": "orador", "panelista": "panelista", "invitado": "orador"}


def gente(n, papel, vota=False, prefijo="Integrante"):
    return [{"nombre": "‹%s %d›" % (prefijo, i + 1), "papel": PAPELES.get(papel, "publico"), "vota": vota}
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
        bloque("‹Tema 1›", [c_placa("‹Tema 1›"), c_escena("Sala"), c_zocalo("‹Responsable›", "‹Área›"),
                            c_escena("Presentación", "La presentación del tema"), c_nota("Exposición y discusión", 1200)]),
        bloque("‹Tema 2›", [c_placa("‹Tema 2›"), c_escena("Invitado remoto"), c_zocalo("‹Remoto›", "‹Área›"),
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
        bloque("‹Tema 1›", [c_placa("‹Tema 1›"), c_escena("Docente"),
                            c_prompter("1-tema-1.txt", "Teleprompter: el tema 1"),
                            c_escena("PDF", "La ficha del tema"), c_nota("Explicación", 900)]),
        bloque("‹Tema 2›", [c_placa("‹Tema 2›"), c_escena("Docente"),
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
                              sala("guest", 3, "Invitado 3", (0.5, 0.5, 0.5, 0.5))])], [
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
                         camara("Cámara", (0.76, 0.02, 0.22, 0.25)),
                         fuente("ba_chat_overlay", "Chat", (0.76, 0.3, 0.22, 0.6), {})]),
        escena("Charla", [camara(), fuente("ba_chat_overlay", "Chat", (0.7, 0.1, 0.28, 0.8), {})]),
        escena("Pausa", [fondo(), texto("Pausa", "Ya volvemos", (0.06, 0.4, 0.88, 0.14), 96)])], [
        bloque("Inicio", [c_escena("Espera"), c_espera("Arrancá la transmisión"), c_escena("Charla", seg=300)]),
        bloque("A jugar", [c_escena("Juego", seg=3600)]),
        bloque("Pausa", [c_escena("Pausa", seg=300)]),
        bloque("Cierre", [c_escena("Charla", seg=300), c_escena("Cierre", seg=15)])])
    E["acto"] = p("‹Acto›", [
        escena("Escenario", [camara(), zocalo(),
                             fuente("image_source", "Escudo", (0.88, 0.03, 0.1, 0.17), {"file": ""})]),
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
        escena("Letra", [camara(), texto("Letra", "‹Estrofa›", (0.05, 0.74, 0.9, 0.2), 54)])], [
        apertura("‹Ceremonia›"),
        bloque("Celebración", [c_escena("Altar"), c_nota("‹Momento 1›", 900), c_escena("Letra", "Canto con la letra"),
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
        escena("Cancha", [camara(), texto("Marcador", "‹Local› 0 - 0 ‹Visitante›", (0.03, 0.03, 0.4, 0.07), 44)]),
        escena("Relatores", [fondo(), sala("remoto", 1, "Remoto con la palabra", (0, 0, 0.5, 1)),
                             sala("remoto", 2, "Remoto con la palabra 2", (0.5, 0, 0.5, 1))]),
        escena("Entretiempo", [fondo(), texto("Entretiempo", "Entretiempo", (0.06, 0.4, 0.88, 0.14), 96)])], [
        bloque("Previa", [c_escena("Espera"), c_escena("Relatores", seg=600)]),
        bloque("Primer tiempo", [c_escena("Cancha", seg=2700)]),
        bloque("Entretiempo", [c_escena("Entretiempo", seg=900)]),
        bloque("Segundo tiempo", [c_escena("Cancha", seg=2700)]),
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


# --- Escritura -----------------------------------------------------------------------
def carpeta_evento(nombre):
    return nombre.replace(" ", "-").replace("/", "-")


def escribir(dest, meta, esc):
    if os.path.exists(dest):
        shutil.rmtree(dest)
    os.makedirs(os.path.join(dest, "Config"))
    titulo = esc["titulo_evento"]
    escenas = escenas_base(titulo)
    nombres = {e["name"] for e in escenas}
    for e in esc["escenas"]:
        if e["name"] in nombres:
            escenas = [x for x in escenas if x["name"] != e["name"]]
        escenas.append(e)
    json.dump({"scenes": escenas, "currentScene": "Espera"}, open(os.path.join(dest, "Config", "app.json"), "w"),
              ensure_ascii=False, indent=1)
    shutil.copytree(GRAFICOS, os.path.join(dest, "Graficos"))
    if os.path.isdir(EXTENSIONES):
        shutil.copytree(EXTENSIONES, os.path.join(dest, "Extensiones"))
    ev = os.path.join(dest, "Eventos", carpeta_evento(titulo))
    os.makedirs(os.path.join(ev, "Discursos"))
    json.dump({"nombre": titulo, "fecha": "", "inicio": "", "huso": "", "nota": meta["resumen"],
               "autoAceptar": [], "bloques": esc["bloques"]},
              open(os.path.join(ev, "evento.json"), "w"), ensure_ascii=False, indent=1)
    discursos = {"0-apertura.txt": "‹Buenas tardes y bienvenidos a ...›\n\n‹Lo que vas a decir al abrir.›\n",
                 "9-cierre.txt": "‹Gracias a todos por ...›\n"}
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
    indice = json.load(open(INDICE))
    defs = escenarios()
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
        escribir(os.path.join(DEMOS, meta["titulo"]), meta, defs[meta["id"]])
        hechas += 1
    print("plantillas generadas:", hechas)


if __name__ == "__main__":
    main()
