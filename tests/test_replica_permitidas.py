"""La lista permitida del tablero web (src/app/control/ReplicaPermitidas.inc) está al día con la
QML del dashboard: si una tarjeta llama algo nuevo de Studio, hay que regenerarla (y decir con
qué herramienta MCP se autoriza). tools/generar/replica-permitidas.py

Y esa QML no lee por funciones (test_la_qml_del_tablero_no_lee_por_funciones)."""
import importlib.util
import json
import os
import re
import subprocess
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def test_lista_permitida_al_dia():
    r = subprocess.run([sys.executable, os.path.join(RAIZ, "tools", "generar", "replica-permitidas.py"), "--verificar"],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr


def test_herramientas_equivalentes_existen():
    """Cada cosa de la lista se autoriza con una herramienta MCP que EXISTE (docs/mcp.json, el
    registro sacado del código): si no, ControlWeb::autorizar la rechaza siempre («No existe la
    herramienta») y el botón del tablero web no anda aunque el perfil lo deje."""
    with open(os.path.join(RAIZ, "docs", "mcp.json"), encoding="utf-8") as f:
        registro = json.load(f)
    todas = {h for c in registro["categorias"] for h in c["herramientas"]}
    with open(os.path.join(RAIZ, "src", "app", "control", "ReplicaPermitidas.inc"), encoding="utf-8") as f:
        texto = f.read()
    lista = json.loads(texto[texto.index('R"json(') + 7:texto.rindex(')json"')])
    faltan = sorted({e["mcp"] for e in lista if e.get("mcp") and e["mcp"] not in todas})
    assert not faltan, f"herramientas de la lista permitida que no están en el registro MCP: {faltan}"


def _generador():
    spec = importlib.util.spec_from_file_location("replica_permitidas",
                                                  os.path.join(RAIZ, "tools", "generar", "replica-permitidas.py"))
    rp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(rp)
    return rp


def _es_accion():
    """La regla de ReplicaPublicador::esAccion (el texto de su expresión regular, leído del .cpp)."""
    with open(os.path.join(RAIZ, "src", "app", "control", "ReplicaPublicador.cpp"), encoding="utf-8") as f:
        texto = f.read()
    cuerpo = texto[texto.index("bool ReplicaPublicador::esAccion"):]
    cuerpo = cuerpo[cuerpo.index("QStringLiteral("):cuerpo.index(");")]
    return re.compile("".join(re.findall(r'"((?:[^"\\]|\\.)*)"', cuerpo)))


def test_la_qml_del_tablero_no_lee_por_funciones():
    """La QML que comparten la app y el tablero web (DashboardPage, las tarjetas y sus controles:
    replica-permitidas.py) no usa lo que devuelve una Q_INVOKABLE de LECTURA (no void y que no es
    acción según ReplicaPublicador::esAccion), tampoco en un manejador ni en una función: en la
    web la réplica la contesta por la red (la primera vez, nada: `undefined` y un TypeError) y
    es la vista decidiendo con datos que no le avisan. Lo que muestra o decide sale de una
    propiedad con NOTIFY o de un rol; lo que hace, de una intención del modelo
    (MixerModel::alternarMudoGrupo, SourcesModel::alternarVisible, ControlCamaras::motivosInterlock…).
    Los bindings ya los vigila tests/test_acoples.py (en toda la app)."""
    sys.path.insert(0, os.path.join(RAIZ, "tools", "diag"))
    import acoples
    rp = _generador()
    accion = _es_accion()
    lecturas = {n for n in acoples.invocables() if not accion.match(n)}
    hallados = []
    for f in rp.archivos():
        crudo = rp.sin_comentarios(open(f, encoding="utf-8").read())
        propias = set(re.findall(r"\bfunction\s+(\w+)\s*\(", crudo))  # las funciones de la QML misma
        texto = crudo
        for a, camino in dict(rp.RE_ALIAS.findall(crudo)).items():
            texto = re.sub(r"\b" + re.escape(a) + r"\.", camino + ".", texto)
        for m in re.finditer(r"([\w.]*)\.(\w+)\s*\(", texto):
            receptor, nombre = m.group(1), m.group(2)
            if nombre not in lecturas or nombre in propias or nombre in rp.DE_JS:
                continue
            # Lo que el tablero no publica o tiene propio (la disposición, el catálogo de la app…).
            if receptor in rp.NUNCA or any(receptor.startswith(n + ".") for n in rp.NUNCA):
                continue
            linea = texto.count("\n", 0, m.start()) + 1
            hallados.append(f"{os.path.relpath(f, RAIZ)}:~{linea}: {receptor}.{nombre}(")
    assert not hallados, ("la QML del tablero lee por funciones (pasalo a una propiedad, un rol o una intención "
                          "del modelo):\n" + "\n".join(hallados))
