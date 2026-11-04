#!/usr/bin/env python3
"""El detector de acoples, la parte ESTÁTICA (docs/arquitectura/detector-de-acoples.md).

UN ACOPLE es un camino de la interfaz que no pasa por el modelo: la vista le pregunta algo a
un objeto de C++ en vez de enlazarse a una propiedad o un rol que avisa, o el C++ de la
interfaz espera (al motor, al gateway, al disco, a la red, a otro hilo). Con 200 invitados
entrando, cada uno es un lugar donde la interfaz se puede clavar.

Lo que busca, con archivo:línea:

  QML — un BINDING (`propiedad: expresión`, no un manejador `onAlgo:` ni una función) que
        llama a una Q_INVOKABLE de C++ que DEVUELVE algo (`Studio.sala.cuantos(x)`,
        `vm.estadoDe(id)` con `vm` un alias de Studio). QML no la vuelve a evaluar cuando el
        dato cambia (la vista queda vieja) y la evalúa en cada vuelta de lo que sí cambia
        (con 200 delegados, 200 llamadas). Las Q_INVOKABLE no-void salen de los .hpp de
        src/app. También la llamada INDIRECTA: el binding llama a una función del mismo
        .qml que adentro llama a una de esas (`vía f()`).
  C++ — en el código de la interfaz (src/app/ui y los objetos que ve la QML: los que
        declaran Q_INVOKABLE o Q_PROPERTY), lo que espera o entra directo:
          bloqueante  BlockingQueuedConnection, waitFor…, QEventLoop, wait/join, sleep,
                      QFuture::result
          red         DNS o sockets sincrónicos
          proceso     QProcess::execute, system, popen
          disco       QFile, ifstream/ofstream, fopen, QDir/QDirIterator, std::filesystem,
                      persist:: sincrónico
          motor       rt.engine() / config() / gateway() / launcher() / save()
          runtime     los demás métodos de AppRuntime que no son «desde cualquier hilo»

Uso:
  tools/diag/acoples.py              todo, agrupado (lo que hay hoy)
  tools/diag/acoples.py --qml        solo los bindings
  tools/diag/acoples.py --cpp        solo el C++
  tools/diag/acoples.py --json       en JSON (lo lee tests/test_acoples.py)
  tools/diag/acoples.py --deuda      las tablas DEUDA_* de tests/test_acoples.py, recontadas

La parte DINÁMICA (BA_ACOPLE, --detectar-acoples) y el recorrido (tools/diag/recorrer-ui.py)
están en el mismo documento.
"""
from __future__ import annotations

import bisect
import json
import pathlib
import re
import sys
from dataclasses import asdict, dataclass

RAIZ = pathlib.Path(__file__).resolve().parents[2]
APP = RAIZ / "src" / "app"
QML_DIR = APP / "qml"

# Los objetos de C++ que la QML ve por nombre (GuiMode.cpp: qmlRegisterSingletonInstance).
SINGLETONS = ("Studio", "Reloj", "Fotos")

# Métodos de JS y de Qt Quick que se llaman igual que alguna Q_INVOKABLE: con un receptor que
# no se sabe qué es, no cuentan (con Studio adelante, sí).
JS_PROPIOS = set("""
filter map some every find findIndex indexOf lastIndexOf includes join slice splice concat push pop
shift unshift reduce reduceRight sort reverse keys values entries toFixed toPrecision toString
toUpperCase toLowerCase trim trimStart trimEnd split replace replaceAll startsWith endsWith substring
substr charAt charCodeAt match matchAll test exec arg max min round floor ceil abs pow sqrt log exp
now parse stringify hasOwnProperty forEach padStart padEnd repeat localeCompare toLocaleString
toLocaleDateString toLocaleTimeString qsTr qsTranslate qsTrId rgba hsla hsva lighter darker tint
alpha formatDate formatTime formatDateTime getTime getHours getMinutes getSeconds getDate getDay
getMonth getFullYear point rect size vector2d vector3d font get count at isArray from assign
contains mapToItem mapFromItem mapToGlobal childAt itemAt indexAt positionViewAtIndex
forceActiveFocus resolvedUrl binding callLater fromCharCode isNaN isFinite parseInt parseFloat
encodeURIComponent decodeURIComponent atan2 sin cos tan random sign trunc hypot add has delete set
clear
""".split())


@dataclass
class Hallazgo:
    archivo: str
    linea: int
    tipo: str      # qml: directo | indirecto | probable; cpp: la categoría
    que: str       # la llamada o el patrón
    texto: str     # la línea (recortada)

    def __str__(self) -> str:
        return f"{self.archivo}:{self.linea}: [{self.tipo}] {self.que} — {self.texto}"


def _rel(p: pathlib.Path) -> str:
    return p.relative_to(RAIZ).as_posix()


def _es_prueba(p: pathlib.Path) -> bool:
    return "tests" in p.parts


# --- las Q_INVOKABLE ---------------------------------------------------------------------
INVOCABLE = re.compile(
    r"Q_INVOKABLE\s+(?:static\s+|virtual\s+|inline\s+)*"
    r"((?:const\s+)?[\w:]+(?:\s*<[^;{()]*?>)?(?:\s*[*&])*)\s+([*&]?\s*\w+)\s*\(")


def invocables() -> dict[str, list[dict]]:
    """{nombre: [{clase, devuelve, archivo, linea}]} de las Q_INVOKABLE de src/app que no son void."""
    out: dict[str, list[dict]] = {}
    for h in sorted(APP.rglob("*.hpp")):
        if _es_prueba(h):
            continue
        txt = h.read_text(encoding="utf-8", errors="replace")
        if "Q_INVOKABLE" not in txt:
            continue
        lineas = [0] + [m.end() for m in re.finditer("\n", txt)]
        for m in INVOCABLE.finditer(txt):
            ret = re.sub(r"\s+", " ", m.group(1)).strip()
            nombre = m.group(2).lstrip("*& ")
            if ret == "void":
                continue
            clase = None
            for c in re.finditer(r"\bclass\s+(\w+)\s*(?:final\s*)?:\s*public", txt[: m.start()]):
                clase = c.group(1)
            out.setdefault(nombre, []).append({
                "clase": clase or "?", "devuelve": ret, "archivo": _rel(h),
                "linea": bisect.bisect_right(lineas, m.start())})
    return out


# --- QML: sin comentarios ni textos ---------------------------------------------------------
def limpiar_js(txt: str) -> str:
    """El texto con los comentarios y el contenido de los textos en blanco (mismo largo, mismos
    saltos de línea): lo que queda es solo código."""
    out = list(txt)
    i, n = 0, len(txt)
    while i < n:
        c = txt[i]
        if c == "/" and i + 1 < n and txt[i + 1] == "/":
            j = txt.find("\n", i)
            j = n if j < 0 else j
            for k in range(i, j):
                out[k] = " "
            i = j
        elif c == "/" and i + 1 < n and txt[i + 1] == "*":
            j = txt.find("*/", i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if out[k] != "\n":
                    out[k] = " "
            i = j
        elif c in "\"'`":
            j = i + 1
            while j < n and txt[j] != c:
                if txt[j] == "\\":
                    j += 1
                elif txt[j] == "\n" and c != "`":
                    break
                j += 1
            for k in range(i + 1, min(j, n)):
                if out[k] != "\n":
                    out[k] = " "
            i = j + 1
        else:
            i += 1
    return "".join(out)


def _cierre(t: str, i: int) -> int:
    """Desde la llave/corchete/paréntesis en `i`, el índice después del que lo cierra."""
    pares = {"{": "}", "[": "]", "(": ")"}
    pila = []
    n = len(t)
    while i < n:
        c = t[i]
        if c in pares:
            pila.append(pares[c])
        elif c in ")]}":
            if pila and pila[-1] == c:
                pila.pop()
            if not pila:
                return i + 1
        i += 1
    return n


RE_BLANCO = re.compile(r"[ \t\r\n;,]*")
RE_IMPORT = re.compile(r"(?:import|pragma)\b[^\n]*")
RE_SIGNAL = re.compile(r"signal\s+\w+\s*(?:\([^)]*\))?")
RE_ENUM = re.compile(r"enum\s+\w+\s*\{")
RE_FUNCION = re.compile(r"function\s+(\w+)\s*\(")
RE_PROPIEDAD = re.compile(r"(?:(?:default|required|readonly|final)\s+)*property\s+[\w.<>]+\s+(\w+)\s*(:)?")
RE_COMPONENTE = re.compile(r"component\s+\w+\s*:\s*")
RE_COMPORTAMIENTO = re.compile(r"[A-Z][\w.]*\s+on\s+[\w.]+\s*\{")
RE_OBJETO = re.compile(r"[A-Za-z_][\w.]*\s*\{")
RE_NOMBRE = re.compile(r"([\w.]+)\s*:")
RE_FLECHA = re.compile(r"(?:\([\w\s,=.\[\]{}]*\)|\w+)\s*=>")
SIGUE = tuple("+-*/%&|?:,.(=<>!")


class LectorQml:
    """Recorre un .qml (ya limpio) y junta los bindings y las funciones."""

    def __init__(self, t: str):
        self.t = t
        self.bindings: list[tuple[str, int, int]] = []  # (propiedad, desde, hasta)
        self.funciones: dict[str, tuple[int, int]] = {}
        self.alias: dict[str, str] = {}                  # nombre -> Studio.algo

    def leer(self) -> "LectorQml":
        i, n = 0, len(self.t)
        while i < n:
            i = self._sentencia(i, nivel_raiz=True)
        return self

    def _blanco(self, i: int) -> int:
        return RE_BLANCO.match(self.t, i).end()

    def _sentencia(self, i: int, nivel_raiz: bool = False) -> int:
        """Una sentencia en el cuerpo de un objeto (o en la raíz). Devuelve dónde sigue."""
        t = self.t
        i = self._blanco(i)
        if i >= len(t):
            return i
        if t[i] == "}":
            return i + 1
        for r in (RE_IMPORT, RE_SIGNAL):
            m = r.match(t, i)
            if m:
                return m.end()
        m = RE_ENUM.match(t, i)
        if m:
            return _cierre(t, m.end() - 1)
        m = RE_FUNCION.match(t, i)
        if m:
            fin_args = _cierre(t, m.end() - 1)
            llave = t.find("{", fin_args)
            if llave < 0:
                return len(t)
            fin = _cierre(t, llave)
            self.funciones[m.group(1)] = (llave, fin)
            return fin
        m = RE_PROPIEDAD.match(t, i)
        if m:
            if not m.group(2):
                return m.end()
            return self._valor(m.end(), m.group(1), declarada=True)
        m = RE_COMPONENTE.match(t, i)
        if m:
            return self._valor(m.end(), None)
        m = RE_COMPORTAMIENTO.match(t, i)
        if m:
            return self._cuerpo(m.end())
        m = RE_OBJETO.match(t, i)
        if m:
            return self._cuerpo(m.end())
        m = RE_NOMBRE.match(t, i)
        if m:
            nombre = m.group(1)
            ultimo = nombre.rsplit(".", 1)[-1]
            if nombre == "id":
                return self._valor(m.end(), None)
            if re.fullmatch(r"on[A-Z]\w*", ultimo):
                return self._valor(m.end(), None)  # un manejador: no es un binding
            return self._valor(m.end(), nombre)
        # Algo que no se entiende: hasta el fin de la línea.
        j = t.find("\n", i)
        return len(t) if j < 0 else j + 1

    def _cuerpo(self, i: int) -> int:
        """El cuerpo de un objeto, desde después de su «{»."""
        t = self.t
        n = len(t)
        while i < n:
            i = self._blanco(i)
            if i >= n:
                return n
            if t[i] == "}":
                return i + 1
            antes = i
            i = self._sentencia(i)
            if i <= antes:
                i = antes + 1
        return n

    def _valor(self, i: int, propiedad: str | None, declarada: bool = False) -> int:
        t = self.t
        n = len(t)
        while i < n and t[i] in " \t":
            i += 1
        if i >= n:
            return n
        m = RE_OBJETO.match(t, i)
        if m and t[i].isupper():
            return self._cuerpo(m.end())
        if t[i] == "[":
            j = self._blanco(i + 1)
            if RE_OBJETO.match(t, j) and t[j].isupper():
                # Una lista de objetos: states: [ State {…}, State {…} ]
                while j < n and t[j] != "]":
                    m = RE_OBJETO.match(t, j)
                    if m:
                        j = self._cuerpo(m.end())
                    else:
                        j += 1
                    j = self._blanco(j)
                return j + 1
        if t.startswith("function", i) or RE_FLECHA.match(t, i):
            fin = self._expresion(i)
            return fin  # una función como valor: no es un binding
        if t[i] == "{":
            fin = _cierre(t, i)
        else:
            fin = self._expresion(i)
        if propiedad is not None:
            self.bindings.append((propiedad, i, fin))
            ali = re.fullmatch(r"\s*((?:%s)(?:\.\w+)*)\s*" % "|".join(SINGLETONS), t[i:fin])
            if ali and declarada:
                self.alias[propiedad.rsplit(".", 1)[-1]] = ali.group(1)
        return fin

    def _expresion(self, i: int) -> int:
        """Una expresión hasta el fin de la línea, siguiendo si queda algo abierto o la línea
        termina (o la siguiente empieza) con un operador."""
        t = self.t
        n = len(t)
        prof = 0
        while i < n:
            c = t[i]
            if c in "([{":
                prof += 1
            elif c in ")]}":
                if prof == 0:
                    return i
                prof -= 1
            elif c == ";" and prof == 0:
                return i
            elif c == "\n" and prof == 0:
                antes = t[:i].rstrip()
                j = i + 1
                while j < n and t[j] in " \t\r\n":
                    j += 1
                if (antes and antes.endswith(SIGUE)) or (j < n and t[j] in "?:.+-*/&|" and not t.startswith("//", j)):
                    i = j
                    continue
                return i
            i += 1
        return n


RE_FUNCION_JS = re.compile(r"\bfunction\s*\w*\s*\(|(?:\([\w\s,=.\[\]{}]*\)|\b\w+)\s*=>")


def sin_funciones(expr: str) -> str:
    """La expresión con los cuerpos de las funciones que define (flechas y `function`) en blanco:
    `[{ f: () => vm.agregar(x) }]` no llama a nada al evaluarse; recién al tocar."""
    out = list(expr)
    n = len(expr)
    pos = 0
    while True:
        m = RE_FUNCION_JS.search(expr, pos)
        if not m:
            break
        i = m.end()
        if not _se_guarda(expr, m.start()):
            # Un argumento de una llamada (`lista.filter(x => vm.algo(x))`): corre ya.
            pos = m.end()
            continue
        if m.group(0).startswith("function"):
            i = _cierre(expr, i - 1)  # los argumentos
            j = expr.find("{", i)
            fin = n if j < 0 else _cierre(expr, j)
        else:
            while i < n and expr[i] in " \t\r\n":
                i += 1
            if i < n and expr[i] == "{":
                fin = _cierre(expr, i)
            else:
                # Una flecha de una expresión: hasta la coma o el cierre que la contiene.
                prof, fin = 0, i
                while fin < n:
                    c = expr[fin]
                    if c in "([{":
                        prof += 1
                    elif c in ")]}":
                        if prof == 0:
                            break
                        prof -= 1
                    elif c == "," and prof == 0:
                        break
                    fin += 1
        for k in range(m.start(), min(fin, n)):
            if out[k] != "\n":
                out[k] = " "
        pos = max(fin, m.end())
    return "".join(out)


def _se_guarda(expr: str, k: int) -> bool:
    """¿La función que empieza en `k` se guarda para después (el valor de una clave de un objeto,
    un elemento de una lista, una asignación) o se pasa a una llamada que la corre ya?"""
    j = k - 1
    while j >= 0 and expr[j] in " \t\r\n":
        j -= 1
    if j >= 0 and expr[j] in ":=":
        return True
    prof = 0
    while j >= 0:
        c = expr[j]
        if c in ")]}":
            prof += 1
        elif c in "([{":
            if prof == 0:
                return c != "("
            prof -= 1
        j -= 1
    return False


RE_LLAMADA = re.compile(r"((?:[A-Za-z_$][\w$]*\s*\.\s*)*)([A-Za-z_$][\w$]*)\s*\(")


def _llamadas_cpp(expr: str, alias: dict[str, str], nonvoid: dict, funciones_qml: set[str]):
    """Las llamadas a Q_INVOKABLE no-void de una expresión: [(tipo, receptor.metodo)]."""
    out = []
    for m in RE_LLAMADA.finditer(expr):
        receptor = re.sub(r"\s+", "", m.group(1)).rstrip(".")
        metodo = m.group(2)
        if not receptor or metodo not in nonvoid:
            continue
        partes = receptor.split(".")
        if partes[0] in SINGLETONS or partes[-1] in alias or partes[0] in alias:
            out.append(("directo", f"{receptor}.{metodo}"))
        elif metodo not in JS_PROPIOS and metodo not in funciones_qml and partes[-1] not in ("Math", "Qt", "JSON", "console", "Theme", "Number", "String", "Object", "Array", "Date"):
            out.append(("probable", f"{receptor}.{metodo}"))
    return out


def acoples_qml(nonvoid: dict | None = None) -> list[Hallazgo]:
    nonvoid = invocables() if nonvoid is None else nonvoid
    archivos = sorted(p for p in QML_DIR.rglob("*.qml"))
    lectores = {}
    funciones_qml: set[str] = set()
    for p in archivos:
        txt = p.read_text(encoding="utf-8", errors="replace")
        lec = LectorQml(limpiar_js(txt)).leer()
        lectores[p] = (txt, lec)
        funciones_qml |= set(lec.funciones)
    out: list[Hallazgo] = []
    for p, (txt, lec) in lectores.items():
        t = lec.t
        saltos = [m.start() for m in re.finditer("\n", t)]
        renglones = txt.splitlines()
        # Las funciones de este archivo que llaman a una no-void (una vuelta: f llama a g).
        llama: dict[str, str] = {}
        for _ in range(2):
            for f, (a, b) in lec.funciones.items():
                if f in llama:
                    continue
                cuerpo = t[a:b]
                ll = [q for _tipo, q in _llamadas_cpp(cuerpo, lec.alias, nonvoid, funciones_qml)]
                if ll:
                    llama[f] = ll[0]
                    continue
                for g in llama:
                    if re.search(r"(?<![\w.])(?:\w+\.)?%s\s*\(" % re.escape(g), cuerpo):
                        llama[f] = f"{g}() → {llama[g]}"
                        break
        for prop, a, b in lec.bindings:
            expr = sin_funciones(t[a:b])
            hall = _llamadas_cpp(expr, lec.alias, nonvoid, funciones_qml)
            for f, q in llama.items():
                if re.search(r"(?<![\w$])(?:[A-Za-z_]\w*\.)?%s\s*\(" % re.escape(f), expr):
                    hall.append(("indirecto", f"{f}() → {q}"))
            for tipo, q in hall:
                # La línea de la llamada (no la del binding).
                nombre = q.split("(")[0].split(" ")[0].rsplit(".", 1)[-1]
                k = t.find(nombre + "(", a, b)
                if k < 0:
                    k = re.search(re.escape(nombre) + r"\s*\(", t[a:b])
                    k = a + k.start() if k else a
                linea = bisect.bisect_right(saltos, k) + 1
                h = Hallazgo(_rel(p), linea, tipo, f"{prop}: {q}", renglones[linea - 1].strip()[:140])
                if not any(o.archivo == h.archivo and o.linea == h.linea and o.que == h.que for o in out):
                    out.append(h)
    return out


# --- C++ de la interfaz -------------------------------------------------------------------
# Los métodos de AppRuntime que se pueden llamar desde cualquier hilo (no entran al motor).
RUNTIME_SEGUROS = {
    "dataRoot", "lastError", "niveles", "latido", "turno", "motorEntreHilos", "modoPrueba",
    "haySalida", "alFallarMotor", "fotoSucia", "ultimaFoto", "operatorFeed", "ventanilla",
    "plano", "foto",
    # Registrarse para escuchar o aportar a la foto (barato; lo que aportan corre en el dueño),
    # lo pedido de la salida por omisión (desde cualquier hilo) y el reinicio de la sala, que
    # corre en un hilo aparte y avisa en el dueño.
    "observar", "dejarDeObservar", "aportarALaFoto", "dejarDeAportar", "salidasPublicadas",
    "recordStopWanted", "streamStopWanted", "reiniciarSalaEnElFondo",
}
CATEGORIAS = [
    ("bloqueante", re.compile(
        r"\bBlockingQueuedConnection\b|\bBlockingCall\b|\bblockingCall\b|\bwaitFor\w+\s*\(|\bQEventLoop\b"
        r"|\bthis_thread::sleep_for\b|\bQThread::(?:m|u)?sleep\b|\busleep\s*\(|\bSleep\s*\("
        r"|(?:\.|->)\s*(?:wait|wait_for|wait_until)\s*\(|(?:\.|->)\s*join\s*\(\s*\)"
        r"|\bQFuture\b[^;]*\.result\s*\(\)|(?:\.|->)\s*waitForFinished\s*\(")),
    ("red", re.compile(r"\bgetaddrinfo\s*\(|\bgethostbyname\s*\(|\bQHostInfo::fromName\b|\b(?:QTcpSocket|QUdpSocket|QSslSocket)\s+\w+\s*[;({]"
                       r"|\bhttp::(?:get|post|pedir)\w*\s*\(|\bHttpsClient::(?:request|download)\s*\(|\bLineSocket\s+\w+")),
    ("proceso", re.compile(r"\bQProcess::execute\b|\bstd::system\s*\(|(?<![\w.:>])system\s*\(|\bpopen\s*\(")),
    ("disco", re.compile(
        r"\bQFile\s+\w+\s*[({;]|\bQFile::(?:exists|remove|copy|rename|readLink)\b|\bstd::[io]?fstream\b|\b_?w?fopen\s*\("
        r"|\bQDir\b[^;]*\.(?:entryList|entryInfoList|mkpath|mkdir|removeRecursively|exists)\s*\(|\bQDirIterator\b"
        r"|\b(?:std::filesystem|fs)::(?:exists|directory_iterator|recursive_directory_iterator|remove|remove_all|"
        r"create_directory|create_directories|copy|copy_file|rename|file_size|last_write_time|is_directory|"
        r"is_regular_file|space|status)\b"
        r"|\bpersist::(?:escribir|agregar|leerArchivo|borrar|leerJson|leerObjetoJson|leerConfig|guardarJson|"
        r"guardarConfigYa)\s*\(|\bQSaveFile\b|\bQImage(?:Reader)?\s*\(\s*(?:QString|ruta|archivo|path)")),
    ("motor", re.compile(r"\brt_?\s*(?:\.|->)\s*(?:engine|config|gateway|launcher|save)\s*\(")),
]
RE_RUNTIME = re.compile(r"\brt_?\s*(?:\.|->)\s*(\w+)\s*\(")
ARRANQUE = ("src/app/arranque/", "src/app/mcp/", "src/app/tests/")


def archivos_interfaz(mira=None) -> list[pathlib.Path]:
    """src/app/ui y los .cpp cuyos .hpp declaran Q_INVOKABLE o Q_PROPERTY (los ve la QML).
    Sin el arranque (detrás del cartel de carga) ni el MCP (cada herramienta en su hilo).
    mira(ruta) -> bool: solo esos archivos (tests/areas.py); None: todos."""
    out = []
    for p in sorted(APP.rglob("*")):
        if p.suffix not in (".cpp", ".mm") or _es_prueba(p):
            continue
        if mira is not None and not mira(p):
            continue
        rel = _rel(p)
        if rel.startswith(ARRANQUE):
            continue
        if rel.startswith("src/app/ui/"):
            out.append(p)
            continue
        h = p.with_suffix(".hpp")
        if h.exists():
            ht = h.read_text(encoding="utf-8", errors="replace")
            if "Q_INVOKABLE" in ht or "Q_PROPERTY" in ht:
                out.append(p)
    return out


def _sin_comentarios_cpp(linea: str, en_bloque: bool) -> tuple[str, bool]:
    out = []
    i, n = 0, len(linea)
    while i < n:
        if en_bloque:
            j = linea.find("*/", i)
            if j < 0:
                return "".join(out), True
            i, en_bloque = j + 2, False
            continue
        if linea.startswith("//", i):
            break
        if linea.startswith("/*", i):
            en_bloque = True
            i += 2
            continue
        c = linea[i]
        if c == '"':
            j = i + 1
            while j < n and linea[j] != '"':
                j += 2 if linea[j] == "\\" else 1
            out.append('""')
            i = j + 1
            continue
        out.append(c)
        i += 1
    return "".join(out), en_bloque


# Lo que se encarga a otro hilo: la ventanilla del motor (corre en el dueño, el plano de
# control), un pool o un hilo propio. Lo que está adentro de esa llamada (la lambda, aunque
# siga en las líneas de abajo) no corre en la interfaz y no se cuenta. Es aproximado: la
# respuesta (`listo`) que vuelve a la interfaz dentro de la misma llamada tampoco se cuenta.
RE_OTRO_HILO = re.compile(
    r"\b(?:\w+(?:\.|->))?(?:pedir\w*|encolar\w*)\s*\(|\bfondo\w*(?:\.|->)start\s*\(|QThreadPool\b[^;]*start\s*\("
    r"|\bQtConcurrent::run\b|\bstd::(?:thread|jthread|async)\b|\bQThread::create\s*\(")


def acoples_cpp(mira=None) -> list[Hallazgo]:
    out: list[Hallazgo] = []
    for p in archivos_interfaz(mira):
        en_bloque = False
        prof = 0                # llaves y paréntesis abiertos
        otro_hilo: list[int] = []  # la profundidad donde empezó cada llamada que encarga a otro hilo
        for n, linea in enumerate(p.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            codigo, en_bloque = _sin_comentarios_cpp(linea, en_bloque)
            if codigo.lstrip().startswith("#"):
                continue
            if RE_OTRO_HILO.search(codigo):
                otro_hilo.append(prof)
            afuera = not otro_hilo
            prof += sum(codigo.count(c) for c in "{(") - sum(codigo.count(c) for c in "})")
            while otro_hilo and prof <= otro_hilo[-1]:
                otro_hilo.pop()
            if not afuera:
                continue
            for cat, r in CATEGORIAS:
                m = r.search(codigo)
                if m:
                    out.append(Hallazgo(_rel(p), n, cat, m.group(0).strip(), linea.strip()[:140]))
            for m in RE_RUNTIME.finditer(codigo):
                metodo = m.group(1)
                if metodo in RUNTIME_SEGUROS or metodo in ("engine", "config", "gateway", "launcher", "save"):
                    continue
                out.append(Hallazgo(_rel(p), n, "runtime", f"rt.{metodo}()", linea.strip()[:140]))
    return out


# --- los ganchos de la parte dinámica ------------------------------------------------------
RE_GANCHO = re.compile(r"\bBA_ACOPLE(?:_EN)?\s*\(")


def ganchos(mira=None) -> dict[str, int]:
    """{archivo: cuántos BA_ACOPLE} en src/ (sin las pruebas ni la definición). mira: como en
    archivos_interfaz."""
    out: dict[str, int] = {}
    for p in sorted((RAIZ / "src").rglob("*")):
        if p.suffix not in (".cpp", ".hpp", ".mm") or _es_prueba(p):
            continue
        if mira is not None and not mira(p):
            continue
        rel = p.relative_to(RAIZ / "src").as_posix()
        if rel.startswith("core/diag/Acoples."):
            continue
        en_bloque = False
        for linea in p.read_text(encoding="utf-8", errors="replace").splitlines():
            codigo, en_bloque = _sin_comentarios_cpp(linea, en_bloque)
            if codigo.lstrip().startswith("#"):
                continue
            out[rel] = out.get(rel, 0) + len(RE_GANCHO.findall(codigo))
    return {k: v for k, v in out.items() if v}


def metodos_llamados(qml: list[Hallazgo]) -> set[str]:
    """Los nombres de las Q_INVOKABLE que llaman los bindings (directo, indirecto o probable)."""
    out = set()
    for h in qml:
        q = h.que.split(": ", 1)[1].split(" → ")[-1]
        out.add(q.split("(")[0].rsplit(".", 1)[-1])
    return out


def cuerpo_de(clase: str, metodo: str, archivos: list[pathlib.Path]) -> list[tuple[str, int, str]]:
    """Las definiciones de Clase::metodo (o la del cuerpo en la clase, en el .hpp): [(archivo,
    línea, las primeras líneas del cuerpo)]."""
    out = []
    for f in archivos:
        if not f.exists():
            continue
        s = f.read_text(encoding="utf-8", errors="replace")
        if f.suffix == ".hpp":
            pat = re.compile(r"Q_INVOKABLE[^;{]*\b%s\s*\(" % re.escape(metodo))
        else:
            pat = re.compile(r"(?m)^[^\s/#][^\n;]*\b%s::%s\s*\(" % (re.escape(clase), re.escape(metodo)))
        for m in pat.finditer(s):
            i = _cierre(s, m.end() - 1)
            j = s.find("{", i)
            k = s.find(";", i)
            if j < 0 or (0 <= k < j):
                continue  # una declaración
            out.append((_rel(f), s.count("\n", 0, m.start()) + 1, s[j:j + 400]))
    return out


def invocables_sin_gancho(qml: list[Hallazgo], nonvoid: dict) -> list[str]:
    """Las Q_INVOKABLE no-void que llama un binding y no empiezan con BA_ACOPLE (la parte
    dinámica no las ve)."""
    faltan = []
    for n in sorted(metodos_llamados(qml)):
        for d in nonvoid.get(n, []):
            h = RAIZ / d["archivo"]
            defs = cuerpo_de(d["clase"], n, [h.with_suffix(".cpp"), h.with_suffix(".mm"), h])
            if not defs:
                faltan.append(f"{d['archivo']}:{d['linea']}: {d['clase']}::{n} (no encontré su cuerpo)")
            for archivo, linea, cuerpo in defs:
                primera = cuerpo[1:].lstrip().split(";", 1)[0]
                if not RE_GANCHO.match(primera):
                    faltan.append(f"{archivo}:{linea}: {d['clase']}::{n}")
    return faltan


# --- tablas para la deuda -----------------------------------------------------------------
def cuenta(hallazgos: list[Hallazgo], qml: bool) -> dict[str, int]:
    """{"archivo" (qml) o "archivo|categoría" (cpp): cuántos}, relativo a src/app/."""
    out: dict[str, int] = {}
    for h in hallazgos:
        a = h.archivo.removeprefix("src/app/")
        k = a if qml else f"{a}|{h.tipo}"
        out[k] = out.get(k, 0) + 1
    return dict(sorted(out.items()))


def _imprimir_tabla(nombre: str, d: dict[str, int]) -> None:
    print(f"{nombre} = {{")
    for k, v in d.items():
        print(f'    "{k}": {v},')
    print("}")


def main(argv: list[str]) -> int:
    nonvoid = invocables()
    qml = [] if "--cpp" in argv else acoples_qml(nonvoid)
    cpp = [] if "--qml" in argv else acoples_cpp()
    if "--json" in argv:
        print(json.dumps({"qml": [asdict(h) for h in qml], "cpp": [asdict(h) for h in cpp],
                          "ganchos": ganchos()}, ensure_ascii=False, indent=1))
        return 0
    if "--deuda" in argv:
        _imprimir_tabla("DEUDA_QML", cuenta(qml, True))
        _imprimir_tabla("DEUDA_CPP", cuenta(cpp, False))
        _imprimir_tabla("GANCHOS", ganchos())
        return 0
    if qml:
        print(f"== QML: bindings que llaman a una Q_INVOKABLE que devuelve algo ({len(qml)}) ==")
        for h in sorted(qml, key=lambda h: (h.tipo, h.archivo, h.linea)):
            print(h)
    if cpp:
        print(f"\n== C++ de la interfaz: esperas y entradas directas ({len(cpp)}) ==")
        for h in sorted(cpp, key=lambda h: (h.tipo, h.archivo, h.linea)):
            print(h)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
