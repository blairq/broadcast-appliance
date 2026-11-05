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


def _acciones_invocables():
    """Los nombres de las Q_INVOKABLE de src/app (void o no) que son ACCIÓN según
    ReplicaPublicador::esAccion."""
    sys.path.insert(0, os.path.join(RAIZ, "tools", "diag"))
    import acoples
    accion = _es_accion()
    nombres = set()
    for h in sorted(acoples.APP.rglob("*.hpp")):
        if "tests" in h.parts:
            continue
        txt = h.read_text(encoding="utf-8", errors="replace")
        if "Q_INVOKABLE" in txt:
            nombres |= {m.group(2).lstrip("*& ") for m in acoples.INVOCABLE.finditer(txt)}
    return {n for n in nombres if accion.match(n)}


# Lo que deja una llamada como sentencia suelta: lo que viene antes (fin de la anterior, la
# llave, el «:» de un manejador, el `)` de un if, `else`, la flecha) y después (fin de línea,
# `;`, la llave, `else`). Cualquier otra cosa usa lo que devuelve.
_ANTES_LIBRE = re.compile(r"(?:^|[{;:)]|\belse|=>)\s*$")
_DESPUES_LIBRE = re.compile(r"^[ \t]*(?:$|[;}\n]|else\b)")
# Una línea que termina así sigue en la de abajo (la llamada es parte de una expresión).
_SIGUE = re.compile(r"[=(,?&|!+\-*/<>\[]\s*$")


def usos_del_resultado(texto, acciones):
    """[(línea, «receptor.nombre(»)] de las llamadas a una acción cuyo resultado se usa (se
    asigna, va en un if, es argumento de otra, se le lee un campo…). `texto`: la QML sin
    comentarios ni textos (acoples.limpiar_js)."""
    ids = set(re.findall(r"\bid:\s*(\w+)", texto))
    out = []
    for m in re.finditer(r"((?:\w+\s*\.\s*)+)(\w+)\s*\(", texto):
        receptor, nombre = m.group(1), m.group(2)
        if nombre not in acciones:
            continue
        # tarjeta.conectar(id): una función de un objeto de la QML misma (por su id), no del modelo.
        if re.sub(r"\s", "", receptor).rstrip(".") in ids:
            continue
        i, prof = m.end(), 1
        while i < len(texto) and prof:
            prof += {"(": 1, "[": 1, "{": 1, ")": -1, "]": -1, "}": -1}.get(texto[i], 0)
            i += 1
        antes = texto[:m.start()]
        linea_antes = antes[antes.rfind("\n") + 1:]
        if linea_antes.strip() == "":  # empieza la línea: suelta, salvo que siga la de arriba
            libre = not _SIGUE.search(antes.rstrip())
        else:
            libre = bool(_ANTES_LIBRE.search(linea_antes))
        if libre and _DESPUES_LIBRE.match(texto[i:]):
            continue
        out.append((texto.count("\n", 0, m.start()) + 1, f"{receptor.replace(' ', '')}{nombre}("))
    return out


def test_usos_del_resultado_ve_los_casos():
    """El criterio de abajo, con los casos que había (y los que no son)."""
    acciones = {"setCruce", "irAPosicion", "arrancarUi", "conectar", "mover", "ir"}
    usa = """
        mx.msg = mx.mz.setCruce(f, b, true)
        const r = t.cc.irAPosicion(e, id, -1); t.avisar(r.ok)
        if (acceso.conectar(id)) Studio.notify("x")
        onApretado: (px, py) => ptz.aviso(ptz.cc.mover(e, px, py, 0))
        const x = a ? t.cc.ir(1) : 0
    """
    no_usa = """
        id: tarjeta
        onClicked: { mx.msg = ""; mx.mz.setCruce(f, b, true) }
        onClicked: tarjeta.corridaVm.arrancarUi(false)
        if (tira.count === 0) return
        tarjeta.cc.ir(3)
        onMover: (px, py) => ptz.cc.mover(e, px, py, 0)
        const ir = function() { t.cc.irAPosicion(e, id, -1) }
        if (x) a.ir(1)
        else b.ir(2)
        function conectar(id) { acceso.conectar(id) }
        onTriggered: tarjeta.conectar(id)
    """
    assert len(usos_del_resultado(usa, acciones)) == 5, usos_del_resultado(usa, acciones)
    assert usos_del_resultado(no_usa, acciones) == []


def test_la_qml_del_tablero_no_usa_lo_que_devuelve_una_accion():
    """La QML que comparten la app y el tablero web no usa lo que DEVUELVE una acción
    (ReplicaPublicador::esAccion): en la web la réplica la manda sin esperar y devuelve
    `undefined` (asignárselo a un texto tira un error que corta el manejador; `r.ok`, un
    TypeError; un `if` nunca entra). La vista manda la intención; el error vuelve por el
    modelo (la señal notify/aviso del contrato de CapturaError, que el tablero muestra) y lo
    que la vista tiene que ver después (un resultado, «la revisión frenó el arranque»), por
    una propiedad con NOTIFY (ControlCamaras::avisos, CorridaViewModel::frenoArranque,
    MezclaViewModel::medicionSincro, InformeEventoViewModel::escaletaProbada).
    Lo de C++ que necesita el resultado usa la función de C++ (o errorDe)."""
    sys.path.insert(0, os.path.join(RAIZ, "tools", "diag"))
    import acoples
    rp = _generador()
    acciones = _acciones_invocables()
    hallados = []
    for f in rp.archivos():
        texto = acoples.limpiar_js(open(f, encoding="utf-8").read())
        for linea, llamada in usos_del_resultado(texto, acciones):
            hallados.append(f"{os.path.relpath(f, RAIZ)}:{linea}: {llamada}")
    assert not hallados, ("la QML del tablero usa lo que devuelve una acción (en la web no vuelve nada): que la "
                          "vista mande la intención y el error o el resultado vuelvan por el modelo:\n"
                          + "\n".join(hallados))
