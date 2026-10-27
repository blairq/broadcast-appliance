"""Las herramientas del MCP leidas del CODIGO (src/app/mcp/*.cpp), sin el aparato andando.

POR QUE. El inventario de verdad es el que da el aparato (tools/list), pero
pide compilar y levantar la app. Para saber que herramientas hay y entrenar el
modelo con sus fichas alcanza con leer los registros, SI se reproduce
exactamente lo que el aparato manda: el modelo se entrena con la descripcion y
el esquema, y la app compara la huella de cada esquema con la del modelo
(modelo.json). Un caracter distinto y la herramienta queda «no cubierta».

QUE LEE de cada bloque `t.name = "x"; ... core_.addTool(...)`:
  title, description, inputSchema, readOnly, destructive, idempotent, openWorld
(lo que McpCore::toolListEntry pone en tools/list), mas category.

COMO. Los literales de C++ se evaluan: cadenas pegadas ("a" "b"), crudas
(R"(...)"), concatenaciones con + y constantes del mismo archivo; y las listas
de inicializacion de nlohmann::json con SU regla: una lista es un objeto si
TODOS sus elementos son pares {cadena, valor}; si no, es un arreglo. Los textos
marcados para traducir (`ba::i18n::traducir("mcp", "…")`, `tr`,
`QCoreApplication::translate`, `QT_TRANSLATE_NOOP`; `llenar(…, {…})` con datos
literales) valen su original en espanol, que es lo que manda el aparato sin
traduccion y con lo que se entrena el modelo. Lo que no
se puede evaluar (una funcion, un bucle) queda marcado «sin_extraer»: nunca se
adivina.

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


# --- tokens -------------------------------------------------------------------
_TOK = re.compile(r'''
    (?P<cruda>(?:u8|u|U|L)?R"(?P<delim>[^(\s]*)\((?P<crudo>.*?)\)(?P=delim)")
  | (?P<cad>(?:u8|u|U|L)?"(?:[^"\\\n]|\\.)*")
  | (?P<car>'(?:[^'\\\n]|\\.)')
  | (?P<num>\d+\.\d*(?:[eE][-+]?\d+)?[fFlL]?|\.\d+(?:[eE][-+]?\d+)?[fFlL]?|\d+(?:[eE][-+]?\d+)?[uUlLfF]*)
  | (?P<id>[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)
  | (?P<com>//[^\n]*|/\*.*?\*/)
  | (?P<op>::|->|[{}()\[\],;=+\-*/<>!&|?:.])
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


# --- evaluacion ---------------------------------------------------------------
class _Lista(list):
    """Una lista de inicializacion todavia sin decidir (objeto o arreglo)."""


def _a_json(v):
    """La regla de nlohmann::json para una lista de inicializacion."""
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


class _Eval:
    def __init__(self, archivo_txt, toks, i, resolver, ambito=None):
        self.txt, self.t, self.i, self.resolver = archivo_txt, toks, i, resolver
        self.ambito = ambito or {}  # los parametros de la funcion que se esta evaluando

    def ver(self, k=0):
        return self.t[self.i + k] if self.i + k < len(self.t) else ("fin", "", -1)

    def comer(self, v=None):
        tok = self.ver()
        if v is not None and tok[1] != v:
            raise NoSe(f"esperaba {v!r}, hay {tok[1]!r}")
        self.i += 1
        return tok

    def expr(self):
        v = self.termino()
        while self.ver()[1] == "+":
            self.comer("+")
            w = self.termino()
            if isinstance(v, str) and isinstance(w, str):
                v = v + w
            elif isinstance(v, (int, float)) and isinstance(w, (int, float)):
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
            return self.identificador()
        if v == "(":
            self.comer("(")
            x = self.expr()
            self.comer(")")
            return x
        raise NoSe(f"no se evaluar {v!r}")

    def lista(self):
        self.comer("{")
        out = _Lista()
        while self.ver()[1] != "}":
            out.append(self.expr())
            if self.ver()[1] == ",":
                self.comer(",")
        self.comer("}")
        return out

    def args(self):
        self.comer("(")
        out = []
        while self.ver()[1] != ")":
            out.append(self.expr())
            if self.ver()[1] == ",":
                self.comer(",")
        self.comer(")")
        return out

    def identificador(self):
        k, v, pos = self.comer()
        base = v.split("::")[-1]
        if v in ("true", "false"):
            return v == "true"
        if v in ("nullptr", "NULL"):
            return None
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
        if self.ver()[1] in (".", "->", "["):
            raise NoSe(f"miembro de {v}")
        if v in self.ambito:
            return self.ambito[v]
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
        if not re.fullmatch(r"\s*(?://[^\n]*\s*)*return\b.*;\s*", resto, re.S):
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


_DECL = r"(?:(?:static|const|constexpr|inline)\s+)*(?:nlohmann::)?(?:json|auto|std::string|const\s+char\s*\*|char\s+const\s*\*|std::string_view|QString)\s*(?:const\s+)?\b{n}\b\s*(=|\{{)"


def _resolvedor(txt, toks):
    pos_tok = {t[2]: i for i, t in enumerate(toks)}

    def resolver(nombre, pos, profundidad=[0]):
        if profundidad[0] > 20:
            raise NoSe("demasiadas vueltas")
        mejor = None
        for m in re.finditer(_DECL.format(n=re.escape(nombre)), txt):
            if m.start() < pos:
                mejor = m
        if not mejor:
            raise NoSe(f"no encuentro {nombre}")
        # el token de «=» o «{» de la declaracion
        ini = mejor.end() - 1
        i = next((pos_tok[p] for p in range(ini, ini + 1) if p in pos_tok), None)
        if i is None:
            raise NoSe(f"declaracion rara de {nombre}")
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


# --- bloques de herramientas -------------------------------------------------
_CAMPOS = ("title", "description", "inputSchema", "readOnly", "destructive", "idempotent", "openWorld", "category")


def herramientas():
    """{nombre: {titulo, categoria, descripcion, esquema, soloLectura, destructiva,
    idempotente, mundoAbierto, archivo, linea}} o {"sin_extraer": motivo}."""
    out = {}
    for f in sorted(MCP.glob("*.cpp")):
        txt = f.read_text(encoding="utf-8")
        toks = tokens(txt)
        resolver = _resolvedor(txt, toks)
        nombres = [i for i, t in enumerate(toks)
                   if t[0] == "id" and i + 3 < len(toks) and toks[i + 1][1] == "." and toks[i + 2][1] == "name"
                   and toks[i + 3][1] == "=" and i + 4 < len(toks) and toks[i + 4][0] == "cad"]
        for j, i in enumerate(nombres):
            var = toks[i][1]
            nombre = toks[i + 4][1]
            if not re.fullmatch(r"[a-z][a-z0-9_]*", nombre):  # «x.name = "Invitado "» de otra estructura
                continue
            fin = nombres[j + 1] if j + 1 < len(nombres) else len(toks)
            # el bloque termina en el addTool que lo registra (si viene antes del siguiente)
            for k in range(i, fin):
                if toks[k][1].endswith("addTool"):
                    fin = k
                    break
            d = {"titulo": "", "categoria": None, "descripcion": "",
                 "esquema": {"type": "object", "properties": {}},
                 "soloLectura": False, "destructiva": False, "idempotente": False, "mundoAbierto": False,
                 "archivo": f.relative_to(RAIZ).as_posix(), "linea": txt.count("\n", 0, toks[i][2]) + 1}
            clave = {"title": "titulo", "description": "descripcion", "inputSchema": "esquema",
                     "readOnly": "soloLectura", "destructive": "destructiva", "idempotent": "idempotente",
                     "openWorld": "mundoAbierto", "category": "categoria"}
            try:
                for k in range(i + 5, fin):
                    if not (toks[k][0] == "id" and toks[k][1] == var and toks[k + 1][1] == "."
                            and toks[k + 2][1] in _CAMPOS):
                        continue
                    if toks[k + 3][1] == "=":
                        e = _Eval(txt, toks, k + 4, resolver)
                        d[clave[toks[k + 2][1]]] = _a_json(e.expr())
                    elif toks[k + 3][1] == "[" and toks[k + 2][1] == "inputSchema":
                        # t.inputSchema["required"] = {...};  t.inputSchema["properties"]["x"] = {...};
                        ruta, q = [], k + 3
                        while toks[q][1] == "[":
                            if toks[q + 1][0] != "cad" or toks[q + 2][1] != "]":
                                raise NoSe("subindice no literal en inputSchema")
                            ruta.append(toks[q + 1][1])
                            q += 3
                        if toks[q][1] != "=":
                            raise NoSe("inputSchema[...] sin asignacion")
                        e = _Eval(txt, toks, q + 1, resolver)
                        v = _a_json(e.expr())
                        obj = d["esquema"]
                        for r in ruta[:-1]:
                            obj = obj.setdefault(r, {})
                        obj[ruta[-1]] = v
                if not isinstance(d["descripcion"], str) or not isinstance(d["esquema"], dict):
                    raise NoSe("descripcion o esquema de otro tipo")
                out[nombre] = d
            except NoSe as e:
                out[nombre] = {"sin_extraer": str(e), "archivo": d["archivo"], "linea": d["linea"]}
    return out


def huella_esquema(esquema):
    import hashlib
    return hashlib.sha256(json.dumps(esquema, sort_keys=True, separators=(",", ":"),
                                     ensure_ascii=False).encode()).hexdigest()[:16]


def verificar(inventario=RAIZ / "docs/mcp.json"):
    """Contra un inventario del aparato: cuantas coinciden campo por campo."""
    inv = {h["nombre"]: h for h in json.loads(Path(inventario).read_text(encoding="utf-8"))["herramientas"]}
    cod = herramientas()
    comunes = [n for n in inv if n in cod]
    res = {"aparato": len(inv), "codigo": len(cod), "en_los_dos": len(comunes),
           "sin_extraer": sorted(n for n in comunes if "sin_extraer" in cod[n]),
           "solo_en_aparato": sorted(n for n in inv if n not in cod), "distintas": {}}
    for n in comunes:
        c = cod[n]
        if "sin_extraer" in c:
            continue
        a = inv[n]
        dif = [campo for campo, x, y in (("titulo", a.get("titulo"), c["titulo"]),
                                           ("descripcion", a.get("descripcion"), c["descripcion"]),
                                           ("esquema", huella_esquema(a.get("esquema")), huella_esquema(c["esquema"])),
                                           ("soloLectura", a.get("soloLectura"), c["soloLectura"]),
                                           ("destructiva", a.get("destructiva"), c["destructiva"]))
               if x != y]
        if dif:
            res["distintas"][n] = dif
    res["iguales"] = len(comunes) - len(res["sin_extraer"]) - len(res["distintas"])
    return res, cod, inv
