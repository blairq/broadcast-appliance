"""El registro de herramientas del MCP leido del CODIGO (src/app/mcp/*.cpp), sin el aparato andando.

POR QUE. El inventario de verdad es el que da el aparato (tools/list), pero
pide compilar y levantar la app. Para saber que herramientas hay, con que clase
(F3-02: lectura / escritura / aire / ui) y entrenar el modelo con sus fichas
alcanza con leer los registros, SI se reproduce exactamente lo que el aparato
manda: el modelo se entrena con la descripcion y el esquema, y la app compara la
huella de cada esquema con la del modelo (modelo.json). Un caracter distinto y
la herramienta queda «no cubierta».

QUE LEE de cada bloque `t.name = …; … core_.addTool(…)`: lo que
McpCore::toolListEntry pone en tools/list (title, description, inputSchema,
readOnly, destructive, idempotent, openWorld) y lo que declara el registro
(category, clase, hilo, porQueAire, aireSi, reversible, porSalida, antes,
mediated, untrustedOutput).

TRES MANERAS DE REGISTRAR, y las tres se leen:
  - un bloque con el nombre literal: `t.name = "scene_list";`
  - un bucle sobre una tabla: `for (const Def& d : defs) { t.name = d.nombre; … }`
    (la tabla se evalua fila por fila con los campos de su struct);
  - una funcion auxiliar: `const auto vivo = [&](const char* nombre, …) { t.name = nombre; … };`
    y cada llamada `vivo("linea_go", …)` es una herramienta con sus argumentos.

COMO. Los literales de C++ se evaluan: cadenas pegadas ("a" "b"), crudas
(R"(...)"), concatenaciones con +, el ternario, constantes del mismo archivo y
funciones de un solo `return`; y las listas de inicializacion de nlohmann::json
con SU regla: una lista es un objeto si TODOS sus elementos son pares {cadena,
valor}; si no, es un arreglo. Los textos marcados para traducir
(`ba::i18n::traducir("mcp", "…")`, `tr`, `QCoreApplication::translate`,
`QT_TRANSLATE_NOOP`; `llenar(…, {…})` con datos literales) valen su original en
espanol, que es lo que manda el aparato sin traduccion y con lo que se entrena el
modelo. Las enumeraciones del registro (`Clase::Lectura`, `AireSi::AlActivar`)
valen su nombre en minusculas («lectura», «al_activar»), el mismo que usa la app.

NUNCA SE ADIVINA. Lo que no se puede evaluar (una funcion de la app, un bucle
sobre un catalogo, una variable que se modifica en el bloque) queda en `errores`
de ESE campo, con el motivo; los demas campos de la herramienta salen igual. Un
campo que se asigna adentro de un if/for cuenta como no evaluable salvo las
condiciones simples (`!x.empty()`).

LO QUE LO VALIDA: verificar() lo compara con un inventario sacado del aparato
(docs/mcp.json): las herramientas que estan en los dos tienen que coincidir.
"""
import json
import re
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
MCP = RAIZ / "src/app/mcp"


class NoSe(Exception):
    """Algo que no se puede evaluar sin compilar."""


class _Falla:
    """Un valor que no se pudo evaluar (un argumento, una fila): usarlo es NoSe."""

    def __init__(self, motivo):
        self.motivo = motivo


# --- tokens -------------------------------------------------------------------
_TOK = re.compile(r'''
    (?P<cruda>(?:u8|u|U|L)?R"(?P<delim>[^(\s]*)\((?P<crudo>.*?)\)(?P=delim)")
  | (?P<cad>(?:u8|u|U|L)?"(?:[^"\\\n]|\\.)*")
  | (?P<car>'(?:[^'\\\n]|\\.)')
  | (?P<num>\d+\.\d*(?:[eE][-+]?\d+)?[fFlL]?|\.\d+(?:[eE][-+]?\d+)?[fFlL]?|\d+(?:[eE][-+]?\d+)?[uUlLfF]*)
  | (?P<id>[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)
  | (?P<com>//[^\n]*|/\*.*?\*/)
  | (?P<op>::|->|==|!=|&&|\|\||[{}()\[\],;=+\-*/<>!&|?:.])
  | (?P<esp>\s+)
''', re.S | re.X)

_ESC = {"n": "\n", "t": "\t", "r": "\r", "\\": "\\", '"': '"', "'": "'", "0": "\0", "a": "\a", "b": "\b",
        "f": "\f", "v": "\v", "?": "?"}


def _desescapar(s):
    out, i = [], 0
    while i < len(s):
        c = s[i]
        if c != "\\":
            out.append(c)
            i += 1
            continue
        n = s[i + 1]
        if n in _ESC:
            out.append(_ESC[n])
            i += 2
        elif n == "x":
            m = re.match(r"[0-9a-fA-F]+", s[i + 2:])
            out.append(chr(int(m.group(0), 16)))
            i += 2 + len(m.group(0))
        elif n in "uU":
            k = 4 if n == "u" else 8
            out.append(chr(int(s[i + 2:i + 2 + k], 16)))
            i += 2 + k
        else:  # octal u otro: raro en estos archivos
            raise NoSe(f"escape \\{n}")
    # los bytes UTF-8 escritos como \xNN se rearman
    t = "".join(out)
    if any(0x80 <= ord(ch) <= 0xFF for ch in t):
        try:
            t = t.encode("latin-1").decode("utf-8")
        except (UnicodeEncodeError, UnicodeDecodeError):
            pass
    return t


def tokens(txt):
    out = []
    for m in _TOK.finditer(txt):
        k = m.lastgroup
        if k in ("esp", "com", "delim", "crudo"):
            continue
        if k == "cruda":
            out.append(("cad", m.group("crudo"), m.start()))
        elif k == "cad":
            v = m.group(0)
            v = v[v.index('"') + 1:-1]
            out.append(("cad", _desescapar(v), m.start()))
        else:
            out.append((k, m.group(0), m.start()))
    return out


def _op(tok, *ops):
    """Un operador (no una cadena que diga lo mismo: el literal "{" no abre nada)."""
    return tok[0] == "op" and tok[1] in ops


def _cierre(toks, i):
    """El indice del token que cierra el que abre en `i` ( ( [ { )."""
    par = {"(": ")", "[": "]", "{": "}"}
    abre, cierra, prof = toks[i][1], par[toks[i][1]], 0
    for k in range(i, len(toks)):
        if _op(toks[k], abre):
            prof += 1
        elif _op(toks[k], cierra):
            prof -= 1
            if prof == 0:
                return k
    raise NoSe(f"{abre} sin cerrar")


def _fin_sentencia(toks, k):
    """El `;` que termina la sentencia que empieza en `k` (saltando lo que va entre
    parentesis, corchetes y llaves: el cuerpo de una lambda no la termina)."""
    while k < len(toks) and not _op(toks[k], ";"):
        if _op(toks[k], "(", "[", "{"):
            k = _cierre(toks, k)
        k += 1
    return k


# --- evaluacion ---------------------------------------------------------------
class _Lista(list):
    """Una lista de inicializacion todavia sin decidir (objeto o arreglo)."""


def _a_json(v):
    """La regla de nlohmann::json para una lista de inicializacion."""
    if isinstance(v, _Falla):
        raise NoSe(v.motivo)
    if isinstance(v, _Lista):
        hijos = [x for x in v]
        if hijos and all(isinstance(x, _Lista) and len(x) == 2 and isinstance(x[0], str) for x in hijos):
            return {x[0]: _a_json(x[1]) for x in hijos}
        return [_a_json(x) for x in hijos]
    if isinstance(v, dict):
        return {k: _a_json(x) for k, x in v.items()}
    if isinstance(v, list):
        return [_a_json(x) for x in v]
    return v


# Las marcas de traduccion: en que argumento esta el texto original (la
# descripcion envuelta en `ba::i18n::traducir("mcp", "…")` vale "…").
_TRADUCIR = {"traducir": 1, "translate": 1, "QT_TRANSLATE_NOOP": 1, "tr": 0, "qsTr": 0, "QT_TR_NOOP": 0,
             "llenar": None}

# Las enumeraciones del registro (McpCore.hpp, McpAire.hpp): valen su nombre como
# lo escribe la app en mcp.json (nombreClase, nombreHilo, McpAire::nombre).
_ENUMS = ("Clase", "Hilo", "AireSi")


def _serpiente(x):
    return re.sub(r"(?<!^)(?=[A-Z])", "_", x).lower()


# Lo que se puede preguntar de un valor ya evaluado (`d.props.is_object()`).
_METODOS = {"is_object": lambda v: isinstance(v, dict), "is_array": lambda v: isinstance(v, list),
            "is_string": lambda v: isinstance(v, str), "empty": lambda v: len(v) == 0}


class _Eval:
    def __init__(self, archivo_txt, toks, i, resolver, ambito=None):
        self.txt, self.t, self.i, self.resolver = archivo_txt, toks, i, resolver
        self.ambito = ambito or {}  # los parametros (o la fila) que se estan evaluando

    def ver(self, k=0):
        return self.t[self.i + k] if self.i + k < len(self.t) else ("fin", "", -1)

    def comer(self, v=None):
        tok = self.ver()
        if v is not None and tok[1] != v:
            raise NoSe(f"esperaba {v!r}, hay {tok[1]!r}")
        self.i += 1
        return tok

    def saltar(self):
        """Hasta la proxima `,` (o el cierre) del mismo nivel: lo que no se pudo evaluar."""
        while self.ver()[0] != "fin" and not _op(self.ver(), ",", "}", ")", "]", ";"):
            if _op(self.ver(), "(", "[", "{"):
                self.i = _cierre(self.t, self.i)
            self.i += 1

    def tolerante(self, fin):
        """Un elemento de una lista o un argumento: si no se puede, _Falla (y se sigue)."""
        ini = self.i
        try:
            v = self.expr()
            if self.ver()[1] not in (",", fin):
                raise NoSe(f"sobra {self.ver()[1]!r}")
            return v
        except (NoSe, IndexError, KeyError, ValueError) as e:
            self.i = ini
            self.saltar()
            return _Falla(str(e))

    def expr(self):
        v = self.suma()
        if self.ver()[1] == "?":
            self.comer("?")
            a = self.tolerante(":")
            self.comer(":")
            b = self.tolerante_hasta_fin()
            if isinstance(v, _Falla):
                raise NoSe(v.motivo)
            r = a if v else b
            if isinstance(r, _Falla):
                raise NoSe(r.motivo)
            return r
        return v

    def tolerante_hasta_fin(self):
        ini = self.i
        try:
            return self.expr()
        except NoSe as e:
            self.i = ini
            self.saltar()
            return _Falla(str(e))

    def suma(self):
        v = self.termino()
        while self.ver()[1] == "+":
            self.comer("+")
            w = self.termino()
            if isinstance(v, str) and isinstance(w, str):
                v = v + w
            elif isinstance(v, (int, float)) and isinstance(w, (int, float)) and not isinstance(v, bool):
                v = v + w
            else:
                raise NoSe("suma de tipos mezclados")
        return v

    def termino(self):
        k, v, _ = self.ver()
        if v == "-":
            self.comer()
            x = self.termino()
            if not isinstance(x, (int, float)):
                raise NoSe("menos de algo que no es numero")
            return -x
        if v == "!":
            self.comer()
            x = self.termino()
            if isinstance(x, _Falla):
                raise NoSe(x.motivo)
            return not x
        if k == "cad":
            s = ""
            while self.ver()[0] == "cad":  # «"a" "b"» = «"ab"»
                s += self.comer()[1]
            return s
        if k == "num":
            self.comer()
            n = v.rstrip("uUlLfF") if not v.lower().startswith("0x") else v
            return float(n) if any(c in n for c in ".eE") and not n.lower().startswith("0x") else int(n, 0)
        if v == "{":
            return self.lista()
        if k == "id":
            return self.postfijo(self.identificador())
        if v == "(":
            self.comer("(")
            x = self.expr()
            self.comer(")")
            return x
        raise NoSe(f"no se evaluar {v!r}")

    def postfijo(self, x):
        """`x.campo` (una fila de tabla) y `x.metodo()` (los de _METODOS)."""
        while self.ver()[1] == "." and self.ver(1)[0] == "id":
            nombre = self.ver(1)[1]
            if self.ver(2)[1] == "(":
                if nombre not in _METODOS or self.ver(3)[1] != ")":
                    raise NoSe(f"llamada a .{nombre}()")
                self.i += 4
                if isinstance(x, _Falla):
                    raise NoSe(x.motivo)
                x = _a_json(x) if isinstance(x, _Lista) else x
                try:
                    x = _METODOS[nombre](x)
                except TypeError:
                    raise NoSe(f".{nombre}() de un {type(x).__name__}")
                continue
            if not isinstance(x, dict):
                raise NoSe(f"miembro .{nombre} de algo que no es una fila")
            self.i += 2
            x = x.get(nombre, _Falla(f"la fila no tiene {nombre}"))
            if isinstance(x, _Falla):
                raise NoSe(x.motivo)
        if self.ver()[1] in ("->", "["):
            raise NoSe("miembro o subindice")
        return x

    def lista(self):
        self.comer("{")
        out = _Lista()
        while self.ver()[1] != "}":
            out.append(self.tolerante("}"))
            if self.ver()[1] == ",":
                self.comer(",")
        self.comer("}")
        return out

    def args(self):
        self.comer("(")
        out = []
        while self.ver()[1] != ")":
            out.append(self.tolerante(")"))
            if self.ver()[1] == ",":
                self.comer(",")
        self.comer(")")
        return out

    def identificador(self):
        k, v, pos = self.comer()
        base = v.split("::")[-1]
        partes = v.split("::")
        if v in ("true", "false"):
            return v == "true"
        if v in ("nullptr", "NULL"):
            return None
        if len(partes) == 2 and partes[0] in _ENUMS:
            return _serpiente(partes[1])
        if base in ("json", "ordered_json") and self.ver()[1] in ("{", "("):
            if self.ver()[1] == "{":
                return self.lista()
            a = self.args()
            return a[0] if a else None
        if v.endswith("json::array") or v.endswith("json::object"):
            a = self.args()
            if v.endswith("array"):
                if not a:
                    return []
                x = a[0]
                return [_a_json(y) for y in x] if isinstance(x, _Lista) else x
            if not a:
                return {}
            return _a_json(a[0]) if isinstance(a[0], _Lista) else a[0]
        if base in ("string", "QStringLiteral", "QString", "string_view") and self.ver()[1] in ("(", "{"):
            if self.ver()[1] == "(":
                a = self.args()
            else:
                a = list(self.lista())
            if len(a) != 1 or not isinstance(a[0], str):
                raise NoSe(f"{v}(...) raro")
            return a[0]
        if self.ver()[1] == "(" and base in _TRADUCIR:
            return self.traducir(base)
        if self.ver()[1] == "(":
            return self.llamada(v)
        if self.ver()[1] == "{" and base[:1].isupper():  # una fila con su tipo: `Friendly{"audio_eq", …}`
            return self.lista()
        if v in self.ambito:
            x = self.ambito[v]
            if isinstance(x, _Falla):
                raise NoSe(f"{v}: {x.motivo}")
            return x
        if self.ver()[1] in ("->", "["):
            raise NoSe(f"miembro de {v}")
        return self.resolver(v, pos)

    def traducir(self, base):
        """Un texto marcado para traducir: vale su ORIGINAL (el literal en español),
        que es lo que el aparato manda en el idioma original. `llenar(texto, {a, b})`
        se evalua solo si los datos tambien son literales."""
        a = self.args()
        if base == "llenar":
            if len(a) != 2 or not isinstance(a[0], str) or not isinstance(a[1], list) \
                    or not all(isinstance(x, str) for x in a[1]):
                raise NoSe("llenar() con datos que no son literales")
            return re.sub(r"%([1-9])", lambda m: a[1][int(m.group(1)) - 1]
                          if int(m.group(1)) <= len(a[1]) else m.group(0), a[0])
        i = _TRADUCIR[base]
        if len(a) <= i or not isinstance(a[i], str):
            raise NoSe(f"{base}() sin texto literal")
        return a[i]

    def llamada(self, nombre):
        """Una funcion del mismo archivo que arma json: con un solo `return` (y,
        como withImageProps, a lo sumo un bucle que le copia un objeto constante a
        un parametro). Lo que depende del aparato andando no se evalua."""
        a = self.args()
        m = re.search(r"(?:static\s+)?(?:nlohmann::)?json\s+" + re.escape(nombre) + r"\s*\(([^)]*)\)\s*\{", self.txt)
        if not m:
            raise NoSe(f"llamada a {nombre}()")
        params = [p.strip().split()[-1].lstrip("*&") for p in m.group(1).split(",") if p.strip()]
        if len(params) != len(a):
            raise NoSe(f"{nombre}(): parametros")
        # el cuerpo: hasta la llave que cierra
        prof, j = 1, m.end()
        while prof and j < len(self.txt):
            prof += {"{": 1, "}": -1}.get(self.txt[j], 0)
            j += 1
        cuerpo = self.txt[m.end():j - 1]
        ambito = dict(zip(params, [_a_json(x) if isinstance(x, _Lista) else x for x in a]))
        patron_bucle = (r"for\s*\(auto\s+(\w+)\s*=\s*(\w+)\.begin\(\);.*?\)\s*"
                        r"(\w+)\[\1\.key\(\)\]\s*=\s*\1\.value\(\);")
        bucle = re.search(patron_bucle, cuerpo, re.S)
        resto = re.sub(patron_bucle, "", cuerpo, flags=re.S) if bucle else cuerpo
        if not _solo_return(resto):
            raise NoSe(f"{nombre}(): no es un solo return")
        if bucle:
            constante, destino = bucle.group(2), bucle.group(3)
            k = _a_json(self.resolver(constante, m.start()))
            if not isinstance(k, dict) or not isinstance(ambito.get(destino), dict):
                raise NoSe(f"{nombre}(): bucle raro")
            ambito[destino] = {**ambito[destino], **k}
        ini = m.end() + cuerpo.rindex("return")
        i = next(n for n, t in enumerate(self.t) if t[2] >= ini) + 1
        e = _Eval(self.txt, self.t, i, self.resolver, ambito)
        return e.expr()


def _solo_return(cuerpo):
    """Un `return` y, antes, a lo sumo constantes json (`const json campo{…};`): las
    resuelve el resolvedor, que las ve porque estan en el mismo cuerpo."""
    t = tokens(cuerpo)
    k = 0
    while k < len(t):
        fin = _fin_sentencia(t, k)
        s = [x[1] for x in t[k:fin]]
        if s[:1] == ["return"]:
            return fin >= len(t) - 1
        s = s[1:] if s[:1] == ["const"] else s
        if len(s) < 3 or s[0] not in ("json", "nlohmann::json") or s[2] not in ("=", "{"):
            return False
        k = fin + 1
    return False


_DECL = r"(?:(?:static|const|constexpr|inline)\s+)*(?:nlohmann::)?(?:json|auto|std::string|const\s+char\s*\*|char\s+const\s*\*|std::string_view|QString)\s*(?:const\s+)?\b{n}\b\s*(=|\{{)"


def _resolvedor(txt, toks):
    pos_tok = {t[2]: i for i, t in enumerate(toks)}
    # Hasta donde se ve cada token: la llave que cierra el bloque que lo contiene. Una
    # declaracion adentro del run de otra herramienta (`const std::string entrada = a.value(…)`)
    # no es la constante `entrada` de afuera.
    se_ve_hasta, pila = [len(toks)] * len(toks), []
    for i, t in enumerate(toks):
        if _op(t, "{"):
            # las llaves de un namespace no esconden nada: lo de adentro se ve despues
            ns = toks[i - 1][1] == "namespace" or (i > 1 and toks[i - 2][1] == "namespace")
            pila.append(None if ns else i)
        elif _op(t, "}") and pila:
            ini = pila.pop()
            if ini is None:
                continue
            for k in range(ini + 1, i):
                if se_ve_hasta[k] == len(toks):
                    se_ve_hasta[k] = i

    def resolver(nombre, pos, profundidad=[0]):
        if profundidad[0] > 20:
            raise NoSe("demasiadas vueltas")
        mejor = None
        aca = pos_tok.get(pos, len(toks))
        for m in re.finditer(_DECL.format(n=re.escape(nombre)), txt):
            if m.start() < pos and se_ve_hasta[pos_tok.get(m.end() - 1, 0)] >= aca:
                mejor = m
        if not mejor:
            raise NoSe(f"no encuentro {nombre}")
        # el token de «=» o «{» de la declaracion
        ini = mejor.end() - 1
        i = next((pos_tok[p] for p in range(ini, ini + 1) if p in pos_tok), None)
        if i is None:
            raise NoSe(f"declaracion rara de {nombre}")
        # Entre la declaracion y el uso, ¿se modifica? (`json tipos = json::array();` y despues
        # un for con `tipos.push_back(…)`): lo declarado no es lo que se usa.
        fin_decl = _fin_sentencia(toks, i)
        tapadas = []  # otra variable con el mismo nombre (`const std::string entrada = …` en un run): hasta donde vale
        for k in range(fin_decl + 1, min(aca, len(toks) - 2)):
            if toks[k][1] != nombre or toks[k][0] != "id" or _op(toks[k - 1], ".", "->"):
                continue
            if (toks[k - 1][0] == "id" and toks[k - 1][1] not in ("return", "case", "else", "do")) \
                    or _op(toks[k - 1], ">", "&", "*"):
                tapadas.append((k, se_ve_hasta[k]))
                continue
            if any(a <= k <= b for a, b in tapadas):
                continue
            q = k + 1
            while _op(toks[q], "["):
                q = _cierre(toks, q) + 1
            if _op(toks[q], "=") or (_op(toks[k + 1], ".") and toks[k + 2][1] in _MUTA):
                raise NoSe(f"{nombre} se modifica después de declararla")
        profundidad[0] += 1
        try:
            e = _Eval(txt, toks, i, resolver)
            if toks[i][1] == "=":
                e.comer("=")
                v = e.expr()
            else:
                v = e.lista()
            return v
        finally:
            profundidad[0] -= 1
    return resolver


# --- tablas y funciones auxiliares ---------------------------------------------
def _campos_struct(txt, toks, tipo, resolver):
    """[(campo, valor por omision o _Falla)] de `struct tipo { … };` del archivo."""
    for i in range(len(toks) - 2):
        if toks[i][1] == "struct" and toks[i + 1][1] == tipo and toks[i + 2][1] == "{":
            fin = _cierre(toks, i + 2)
            campos, ini = [], i + 3
            while ini < fin:
                k = _fin_sentencia(toks, ini)
                decl = toks[ini:min(k, fin)]
                desde, ini = ini, k + 1
                igual = next((q for q, t in enumerate(decl) if _op(t, "=")), None)
                nombre = [t for t in (decl[:igual] if igual is not None else decl) if t[0] == "id"]
                if not nombre:
                    continue
                defecto = _Falla(f"{tipo}.{nombre[-1][1]} sin valor")
                if igual is not None:
                    defecto = _Eval(txt, toks, desde + igual + 1, resolver).tolerante(";")
                campos.append((nombre[-1][1], defecto))
            return campos
    raise NoSe(f"no encuentro struct {tipo}")


def tabla(txt, toks, resolver, nombre_o_indice, tipo):
    """Las filas de una tabla `const T x[] = {…}` (o un `{T{…}, …}` en el lugar) como
    dicts con los campos del struct `tipo`. Una fila que no se pudo evaluar entera
    trae _Falla en los campos que fallaron."""
    if isinstance(nombre_o_indice, int):
        i = nombre_o_indice
    else:
        m = None
        for m in re.finditer(r"\b" + re.escape(nombre_o_indice) + r"\s*(?:\[\s*\])?\s*=?\s*\{", txt):
            break
        if not m:
            raise NoSe(f"no encuentro la tabla {nombre_o_indice}")
        i = next(k for k, t in enumerate(toks) if t[2] >= m.end() - 1)
    campos = _campos_struct(txt, toks, tipo, resolver)
    filas = _Eval(txt, toks, i, resolver).lista()
    out = []
    for f in filas:
        if not isinstance(f, _Lista):
            out.append({c: _Falla("fila sin evaluar") for c, _ in campos})
            continue
        d = {}
        for q, (c, defecto) in enumerate(campos):
            d[c] = f[q] if q < len(f) else defecto
        out.append(d)
    return out


def _parametros(toks, abre):
    """[(nombre, indice del valor por omision o None)] de la lista de parametros en `abre`."""
    cierra = _cierre(toks, abre)
    out, ini, k, angulos = [], abre + 1, abre + 1, 0
    while k <= cierra:
        t = toks[k]
        if k < cierra and _op(t, "(", "[", "{"):  # std::function<std::string(const json&)>, = {}
            k = _cierre(toks, k) + 1
            continue
        if _op(t, "<"):
            angulos += 1
        elif _op(t, ">"):
            angulos -= 1
        elif k == cierra or (_op(t, ",") and angulos == 0):
            decl = toks[ini:k]
            igual = next((q for q, x in enumerate(decl) if _op(x, "=")), None)
            nombres = [x for x in (decl[:igual] if igual is not None else decl) if x[0] == "id"]
            if nombres:
                out.append((nombres[-1][1], ini + igual + 1 if igual is not None else None))
            ini = k + 1
        k += 1
    return out, cierra


def _llamadas_de_auxiliar(txt, toks, resolver, i_nombre):
    """Si `X.name = p;` esta adentro de `const auto f = [...](... p ...) { … };`: las
    llamadas `f(…)` con sus argumentos ligados a los parametros."""
    param = toks[i_nombre + 4][1]
    for k in range(i_nombre, 0, -1):
        if not (toks[k][1] == "[" and toks[k - 1][1] == "=" and toks[k - 2][0] == "id"
                and toks[k - 3][1] in ("auto",)):
            continue
        cierra_cap = _cierre(toks, k)
        if toks[cierra_cap + 1][1] != "(":
            continue
        params, cierra_par = _parametros(toks, cierra_cap + 1)
        q = cierra_par + 1
        while toks[q][1] != "{":
            q += 1
        fin_cuerpo = _cierre(toks, q)
        if not (q < i_nombre < fin_cuerpo) or param not in [p for p, _ in params]:
            continue
        aux = toks[k - 2][1]
        llamadas = []
        for c in range(fin_cuerpo, len(toks) - 1):
            if toks[c][1] == aux and toks[c + 1][1] == "(" and toks[c - 1][1] not in (".", "->"):
                e = _Eval(txt, toks, c + 1, resolver)
                args = e.args()
                ambito = {}
                for n, (p, defecto) in enumerate(params):
                    if n < len(args):
                        ambito[p] = args[n]
                    elif defecto is not None:
                        ambito[p] = _Eval(txt, toks, defecto, resolver).tolerante(")")
                    else:
                        ambito[p] = _Falla(f"{aux}(): falta {p}")
                llamadas.append((ambito, txt.count("\n", 0, toks[c][2]) + 1))
        return llamadas, (q, fin_cuerpo)
    return None, None


def _filas_de_bucle(txt, toks, resolver, i_nombre):
    """Si `X.name = v.campo;` esta adentro de `for (const T& v : tabla) { … }`: una
    fila por herramienta."""
    var = toks[i_nombre + 4][1]
    for k in range(i_nombre, 0, -1):
        if toks[k][1] != "for" or toks[k + 1][1] != "(":
            continue
        cierra = _cierre(toks, k + 1)
        dos = next((q for q in range(k + 2, cierra) if toks[q][1] == ":"), None)
        if dos is None or toks[dos - 1][1] != var:
            continue
        q = cierra + 1
        if toks[q][1] != "{":
            continue
        fin_cuerpo = _cierre(toks, q)
        if not (q < i_nombre < fin_cuerpo):
            continue
        tipo = next(t[1] for t in reversed(toks[k + 2:dos - 1]) if t[0] == "id")
        rango = toks[dos + 1]
        filas = tabla(txt, toks, resolver, dos + 1 if rango[1] == "{" else rango[1], tipo)
        return [({var: f}, txt.count("\n", 0, toks[k][2]) + 1) for f in filas], (q, fin_cuerpo)
    return None, None


# --- bloques de herramientas -------------------------------------------------
_CAMPOS = {"title": "titulo", "description": "descripcion", "inputSchema": "esquema", "readOnly": "soloLectura",
           "destructive": "destructiva", "idempotent": "idempotente", "openWorld": "mundoAbierto",
           "category": "categoria", "clase": "clase", "hilo": "hilo", "porQueAire": "porQueAire",
           "aireSi": "aireSi", "reversible": "reversible", "porSalida": "porSalida", "antes": "antes",
           "mediated": "mediada", "untrustedOutput": "contenidoNoConfiable"}
_DEFECTO = {"titulo": "", "categoria": None, "descripcion": "", "esquema": None, "soloLectura": False,
            "destructiva": False, "idempotente": False, "mundoAbierto": False, "clase": None, "hilo": None,
            "porQueAire": None, "aireSi": "siempre", "reversible": False, "porSalida": False, "antes": [],
            "mediada": False, "contenidoNoConfiable": False}
# Lo que McpCore::addTool le suma al esquema de una herramienta de salida.
SALIDA = {"type": "string", "description": "Salida (id o nombre; `salidas_listar`). Sin ella, la por omisión "
                                           "(o la de quien llama)."}
_MUTA = ("push_back", "emplace_back", "erase", "insert", "update", "merge_patch", "emplace", "clear")


def _condicion(toks, k, ambito):
    """Para `if (cond) X.campo = …;`: la condicion si es simple (`!p.empty()`,
    `p.empty()`); None si no se sabe."""
    if toks[k - 1][1] != ")":
        return True  # no es condicional
    q = k - 1
    prof = 0
    while q > 0:
        prof += {")": 1, "(": -1}.get(toks[q][1], 0) if toks[q][0] == "op" else 0
        if prof == 0:
            break
        q -= 1
    if toks[q - 1][1] != "if":
        return None
    cond = [t[1] for t in toks[q + 1:k - 1]]
    neg = cond[:1] == ["!"]
    cond = cond[1:] if neg else cond
    if len(cond) == 5 and cond[1:] == [".", "empty", "(", ")"] and cond[0] in ambito:
        v = ambito[cond[0]]
        if isinstance(v, _Falla):
            return None
        v = _a_json(v) if isinstance(v, _Lista) else v
        return (len(v) == 0) != neg
    return None


def _bloque(txt, toks, resolver, var, ini, fin, ambito):
    """Los campos del bloque `var.name = … ; … addTool` (de `ini` a `fin`) con `ambito`
    (la fila o los argumentos). -> (campos, errores {campo: motivo})."""
    ambito = dict(ambito)
    d = {k: (list(v) if isinstance(v, list) else v) for k, v in _DEFECTO.items()}
    d["esquema"] = {"type": "object", "properties": {}}
    errores = {}
    # La profundidad de llaves desde el nombre: lo de un if/for adentro no es seguro.
    prof, base = 0, {}
    for k in range(ini, fin):
        base[k] = prof
        if toks[k][0] == "op":
            prof += {"{": 1, "}": -1}.get(toks[k][1], 0)
    k = ini
    while k < fin:
        t = toks[k]
        # Otro miembro (t.run = [...](...) {...};): se saltea entero, con su lambda.
        if t[0] == "id" and t[1] == var and _op(toks[k + 1], ".") and toks[k + 2][1] not in _CAMPOS:
            k = _fin_sentencia(toks, k) + 1
            continue
        # Una variable (parametro o local) que se modifica en el bloque: lo que la use no se sabe.
        if t[0] == "id" and t[1] != var and not _op(toks[k - 1], ".", "->") and (
                _op(toks[k + 1], "[") or (_op(toks[k + 1], ".") and toks[k + 2][1] in _MUTA)):
            fin_st = _fin_sentencia(toks, k)
            asigna = _op(toks[k + 1], ".") or any(_op(x, "=") for x in toks[k:fin_st])
            if asigna:
                ruta, q = [], k + 1
                while toks[q][1] == "[" and toks[q + 1][0] == "cad" and toks[q + 2][1] == "]":
                    ruta.append(toks[q + 1][1])
                    q += 3
                directo = base[k] == 0 and toks[k - 1][1] in (";", "{", "}") and ruta and toks[q][1] == "="
                obj = ambito.get(t[1])
                if directo and t[1] not in ambito:
                    # una local del bloque (`json props = relaciones; props["x"] = …;`): desde su declaracion
                    try:
                        obj = resolver(t[1], t[2])
                    except NoSe as x:
                        obj = _Falla(str(x))
                if directo and not isinstance(obj, _Falla):
                    try:
                        e = _Eval(txt, toks, q + 1, resolver, ambito)
                        v = _a_json(e.expr())
                        obj = _a_json(obj) if isinstance(obj, _Lista) else obj
                        if not isinstance(obj, dict):
                            raise NoSe("no es un objeto")
                        obj = json.loads(json.dumps(obj))
                        cur = obj
                        for r in ruta[:-1]:
                            cur = cur.setdefault(r, {})
                        cur[ruta[-1]] = v
                        ambito[t[1]] = obj
                    except NoSe as x:
                        ambito[t[1]] = _Falla(f"se modifica en el bloque ({x})")
                else:
                    ambito[t[1]] = _Falla("se modifica en el bloque")
            k = fin_st + 1
            continue
        if not (t[0] == "id" and t[1] == var and toks[k + 1][1] == "." and toks[k + 2][1] in _CAMPOS):
            k += 1
            continue
        clave = _CAMPOS[toks[k + 2][1]]
        try:
            cond = _condicion(toks, k, ambito)
            if cond is None or (base[k] > 0 and toks[k - 1][1] != ")"):
                raise NoSe("asignación condicional")
            if toks[k + 3][1] == "=":
                e = _Eval(txt, toks, k + 4, resolver, ambito)
                v = _a_json(e.expr())
                if e.ver()[1] != ";":
                    raise NoSe(f"sobra {e.ver()[1]!r}")
                if cond:
                    d[clave] = v
            elif toks[k + 3][1] == "[" and clave == "esquema":
                # t.inputSchema["required"] = {...};  t.inputSchema["properties"]["x"] = {...};
                ruta, q = [], k + 3
                while toks[q][1] == "[":
                    if toks[q + 1][0] != "cad" or toks[q + 2][1] != "]":
                        raise NoSe("subindice no literal en inputSchema")
                    ruta.append(toks[q + 1][1])
                    q += 3
                if toks[q][1] != "=":
                    raise NoSe("inputSchema[...] sin asignacion")
                e = _Eval(txt, toks, q + 1, resolver, ambito)
                v = _a_json(e.expr())
                if cond:
                    obj = d["esquema"]
                    for r in ruta[:-1]:
                        obj = obj.setdefault(r, {})
                    obj[ruta[-1]] = v
            else:
                raise NoSe(f"{toks[k + 3][1]!r} raro")
        except (NoSe, IndexError, KeyError, ValueError) as x:
            errores.setdefault(clave, str(x))
        k = _fin_sentencia(toks, k) + 1
    if not isinstance(d["descripcion"], str):
        errores.setdefault("descripcion", "no es texto")
    if not isinstance(d["esquema"], dict):
        errores.setdefault("esquema", "no es un objeto")
    return d, errores


def herramientas():
    """{nombre: {titulo, categoria, descripcion, esquema, soloLectura, destructiva,
    idempotente, mundoAbierto, clase, hilo, porQueAire, aireSi, reversible, porSalida,
    antes, mediada, contenidoNoConfiable, archivo, linea, errores, repetida?}}.

    `errores`: {campo: motivo} de lo que no se pudo leer (el valor queda el de
    fabrica). `clase` y `hilo` ya resueltos como McpCore::addTool (la clase sin
    declarar queda None y se anota en errores)."""
    out = {}
    for f in sorted(MCP.glob("*.cpp")):
        txt = f.read_text(encoding="utf-8")
        toks = tokens(txt)
        resolver = _resolvedor(txt, toks)
        rel = f.relative_to(RAIZ).as_posix()
        nombres = [i for i, t in enumerate(toks)
                   if t[0] == "id" and i + 4 < len(toks) and toks[i + 1][1] == "." and toks[i + 2][1] == "name"
                   and toks[i + 3][1] == "="]
        # Solo los de un `Tool var` declarado entre el nombre anterior y este: los otros son
        # `spec.name = …` de otra estructura (adentro del run de alguna herramienta).
        nombres = [i for j, i in enumerate(nombres)
                   if any(toks[q][1] == "Tool" and toks[q + 1][1] == toks[i][1]
                          for q in range(nombres[j - 1] if j else 0, i))]
        for j, i in enumerate(nombres):
            var = toks[i][1]
            sig = nombres[j + 1] if j + 1 < len(nombres) else len(toks)
            # el bloque termina en el addTool que lo registra (si viene antes del siguiente)
            fin = next((k for k in range(i, sig) if toks[k][1].endswith("addTool")), None)
            if fin is None:
                continue
            linea = txt.count("\n", 0, toks[i][2]) + 1
            rhs = toks[i + 4]
            if rhs[0] == "cad" and toks[i + 5][1] == ";":
                ligaduras = [({}, linea)]
            elif rhs[0] == "id" and toks[i + 5][1] == "." and toks[i + 7][1] == ";":
                ligaduras, _ = _filas_de_bucle(txt, toks, resolver, i)
            elif rhs[0] == "id" and toks[i + 5][1] == ";":
                ligaduras, _ = _llamadas_de_auxiliar(txt, toks, resolver, i)
            else:
                ligaduras = None
            if ligaduras is None:
                out.setdefault(f"?{rel}:{linea}", {"archivo": rel, "linea": linea,
                                                    "errores": {"nombre": "no se sabe leer este registro"}})
                continue
            for ambito, lin in ligaduras:
                try:
                    nombre = _Eval(txt, toks, i + 4, resolver, ambito).expr()
                except NoSe as e:
                    out[f"?{rel}:{lin}"] = {"archivo": rel, "linea": lin, "errores": {"nombre": str(e)}}
                    continue
                if not isinstance(nombre, str) or not re.fullmatch(r"[a-z][a-z0-9_]*", nombre):
                    continue  # «x.name = "Invitado "» de otra estructura
                d, errores = _bloque(txt, toks, resolver, var, i + 5, fin, ambito)
                d.update({"archivo": rel, "linea": lin, "errores": errores})
                _como_addtool(d)
                if nombre in out:
                    d["repetida"] = f"{out[nombre]['archivo']}:{out[nombre]['linea']}"
                out[nombre] = d
    return out


def _como_addtool(d):
    """Lo que McpCore::addTool resuelve al registrar (la clase manda sobre readOnly)."""
    if d["clase"] is None:
        d["errores"].setdefault("clase", "sin declarar (t.clase = Clase::…)")
        d["clase"] = "lectura" if d["soloLectura"] else "escritura"
    d["soloLectura"] = d["clase"] == "lectura"
    # Lo de fábrica es la interfaz (F3-03, ver Hilo en McpCore.hpp): foto o control solo
    # lo que lo declara.
    if d["hilo"] is None or d["hilo"] == "segun_clase":
        d["hilo"] = "interfaz"
    if d["porSalida"] and isinstance(d["esquema"], dict):
        props = d["esquema"].setdefault("properties", {})
        if isinstance(props, dict) and "salida" not in props:
            props["salida"] = dict(SALIDA)


def huella_esquema(esquema):
    import hashlib
    return hashlib.sha256(json.dumps(esquema, sort_keys=True, separators=(",", ":"),
                                     ensure_ascii=False).encode()).hexdigest()[:16]


_LO_DEL_PROTOCOLO = ("titulo", "descripcion", "esquema")


def verificar(inventario=RAIZ / "docs/mcp.json"):
    """Contra un inventario del aparato: cuantas coinciden campo por campo (lo que
    viaja en tools/list y se pudo leer del codigo)."""
    inv = {h["nombre"]: h for h in json.loads(Path(inventario).read_text(encoding="utf-8"))["herramientas"]}
    cod = {n: c for n, c in herramientas().items() if not n.startswith("?")}
    comunes = [n for n in inv if n in cod]
    res = {"aparato": len(inv), "codigo": len(cod), "en_los_dos": len(comunes),
           "sin_extraer": sorted(n for n in comunes if any(c in cod[n]["errores"] for c in _LO_DEL_PROTOCOLO)),
           "solo_en_aparato": sorted(n for n in inv if n not in cod), "distintas": {}}
    for n in comunes:
        c, a = cod[n], inv[n]
        dif = [campo for campo, x, y in (("titulo", a.get("titulo"), c["titulo"]),
                                           ("descripcion", a.get("descripcion"), c["descripcion"]),
                                           ("esquema", huella_esquema(a.get("esquema")), huella_esquema(c["esquema"])),
                                           ("soloLectura", a.get("soloLectura"), c["soloLectura"]),
                                           ("destructiva", a.get("destructiva"), c["destructiva"]))
               if x != y and campo not in c["errores"]]
        if dif:
            res["distintas"][n] = dif
    res["iguales"] = len(comunes) - len(res["sin_extraer"]) - len(res["distintas"])
    return res, cod, inv
