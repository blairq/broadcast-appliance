"""Lo que A le dice a B de estado vuelve a decírselo cuando B arranca de nuevo (G-7).

A es la fuente de verdad y B el borde (src/gateway/Sesion.hpp): B olvida todo al
reiniciarse. Cada ruta de la API de control (src/gateway/AdminApi.cpp y las de los
bordes) está clasificada acá:

  - un nombre: es ESTADO, y algún modelo de A tiene que registrar el republicador con
    ese nombre (ConferenceClient::registrarRepublicador) — si no, una caída de B lo
    pierde hasta que alguien lo vuelva a tocar;
  - "orden": una acción de una vez (admitir, mandar un chat…): no hay nada que repetir;
  - "lectura": solo GET;
  - "arranque": B lo recupera solo al arrancar (su configuración, el PIN del día).

Una ruta nueva sin clasificar hace fallar el test: hay que decidir de qué tipo es.
"""

import pathlib
import re

RAIZ = pathlib.Path(__file__).resolve().parent.parent

CLASIFICACION = {
    # AdminApi.cpp
    "/api/v1/acreditaciones": "lectura",
    "/api/v1/board-link": "lectura",
    "/api/v1/carruseles": "carruseles",
    "/api/v1/cast-link": "lectura",
    "/api/v1/chat": "orden",
    "/api/v1/claves": "claves",
    "/api/v1/config/": "arranque",  # B la lee de la configuración al arrancar
    "/api/v1/correccion-link": "lectura",
    "/api/v1/correccion/pendientes": "correccion",
    "/api/v1/direcciones": "lectura",
    "/api/v1/evento-web": "evento-web",
    "/api/v1/events": "lectura",
    "/api/v1/log-nivel": "orden",  # el nivel del registro de B, para mirar algo ahora
    "/api/v1/network": "lectura",
    "/api/v1/network/server-config": "orden",
    "/api/v1/participants/": "orden",
    "/api/v1/pin": "arranque",  # el PIN del día, guardado por B (CredentialStore)
    "/api/v1/pin/bloqueos": "lectura",
    "/api/v1/pin/desbloquear": "orden",
    "/api/v1/preguntas": "preguntas",
    "/api/v1/prompter-link": "lectura",
    "/api/v1/prompter": "prompter",
    "/api/v1/prompters": "prompter",
    "/api/v1/proxy": "lectura",
    "/api/v1/proxy/renovar": "orden",
    "/api/v1/publico-info": "publico",
    "/api/v1/publico-link": "lectura",
    "/api/v1/qr": "lectura",
    "/api/v1/remoto": "lectura",
    "/api/v1/room": "sala",
    "/api/v1/rotulos-link": "lectura",
    "/api/v1/rotulos": "rotulos",
    "/api/v1/salidas": "salidas",
    "/api/v1/sesion/cast": "lectura",
    "/api/v1/sesion/origen": "lectura",
    "/api/v1/shares/": "orden",
    "/api/v1/speakers": "oradores",
    "/api/v1/speakers/next": "orden",
    "/api/v1/superficies": "lectura",
    "/api/v1/superficies/mostrar": "orden",
    "/api/v1/tally": "tally",
    "/api/v1/tema-evento": "tema-evento",
    "/api/v1/tema": "tema-web",
    "/api/v1/view": "orden",
    "/api/v1/view/": "orden",
    "/api/v1/votacion": "votacion",
}

# Los bordes: su prefijo y lo que atiende cada uno (`que == "…"`).
BORDES = {
    "Botonera.cpp": {"credenciales": "botonera", "estado": "botonera", "enviar": "orden", "resolver": "orden"},
    "ControlWeb.cpp": {"credenciales": "control-web", "estado": "lectura", "enviar": "orden", "resolver": "orden",
                       "cortar": "orden"},
    "Sesion.cpp": {"credenciales": "presidencia", "medios": "presidencia", "vistas": "presidencia",
                   "estado": "lectura", "foto": "lectura", "enviar": "orden", "cortar": "orden",
                   "hablar": "orden", "atiende": "orden"},
    "Registro.cpp": {"totems": "totems", "estado": "lectura", "cuadro": "lectura", "enviar": "orden",
                     "cortar": "orden"},
}
# /api/v1/grillas (Grillas.cpp): lo que pinta cada grilla.
GRILLAS = "grillas"

NO_ESTADO = {"orden", "lectura", "arranque"}


def _registrados():
    nombres = set()
    for p in (RAIZ / "src" / "app").rglob("*.cpp"):
        nombres.update(re.findall(r'registrarRepublicador\(\s*"([a-z0-9-]+)"', p.read_text(encoding="utf-8")))
    return nombres


def test_toda_ruta_de_adminapi_esta_clasificada():
    texto = (RAIZ / "src" / "gateway" / "AdminApi.cpp").read_text(encoding="utf-8")
    rutas = set(re.findall(r'"(/api/v1/[a-z0-9/_-]*)"', texto))
    sin = sorted(rutas - set(CLASIFICACION))
    assert not sin, f"rutas sin clasificar en tests/test_republicadores.py: {sin}"


def test_toda_ruta_de_los_bordes_esta_clasificada():
    for archivo, tabla in BORDES.items():
        texto = (RAIZ / "src" / "gateway" / archivo).read_text(encoding="utf-8")
        ques = set(re.findall(r'que == "([a-z_/-]+)"', texto))
        sin = sorted(ques - set(tabla))
        assert not sin, f"{archivo}: rutas sin clasificar: {sin}"


def test_cada_estado_tiene_republicador():
    registrados = _registrados()
    estados = {v for v in CLASIFICACION.values() if v not in NO_ESTADO}
    for tabla in BORDES.values():
        estados |= {v for v in tabla.values() if v not in NO_ESTADO}
    estados.add(GRILLAS)
    faltan = sorted(estados - registrados)
    assert not faltan, f"estado que A le dice a B sin republicador (se pierde si B se reinicia): {faltan}"
