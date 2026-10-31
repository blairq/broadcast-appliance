#!/usr/bin/env python3
"""El registro de herramientas del MCP (F3-02), a docs/mcp.json y a docs/funciones/MCP.md.

QUE SALE: todas las herramientas con su titulo, descripcion, esquema de
entrada, categoria, la CLASE declarada (lectura / escritura / aire / ui), el
hilo, si es idempotente, las condiciones de aire, las etiquetas de busqueda, en
que archivo y linea viven, y que roles pueden usarlas. Mas las categorias, los
temas de ayuda y el juego de cada rol. Y en MCP.md, entre las marcas
<!-- registro -->, cuantas hay, por categoria y por clase, y la tabla de las de
aire: el numero que cita la documentacion sale de aca, no se escribe a mano.

DE DONDE SALE: del CODIGO (tools/docs/mcp_codigo.py lee cada registro, sea un
bloque, una tabla o una funcion auxiliar). Lo que el lector no puede evaluar
(un esquema armado con el catalogo de modulos, una descripcion que llama a la
app) se toma del inventario anterior, campo por campo, y queda en `origen`.
Con el aparato andando (sin --desde-codigo) titulo, descripcion y esquema salen
de tools/list, que es lo que el modelo ve de verdad, y el resto del codigo.

LA DERIVA (`--verificar`, lo corre `make probar` como test_mcp_registro): arma
todo de nuevo en memoria y falla si docs/mcp.json o la tabla de MCP.md no son lo
que sale del codigo, o si alguna herramienta no declara su clase con un literal,
una de aire no dice por que, o una no tiene categoria o etiquetas. Agregar una
herramienta sin regenerar rompe la suite; no queda para despues.

Uso:  tools/docs/mcp-inventario.py --desde-codigo     (regenera docs/mcp.json y MCP.md)
      tools/docs/mcp-inventario.py --verificar        (no escribe: dice si hay deriva)
      tools/docs/mcp-inventario.py [salida.json]      (con el aparato andando)
"""
import json
import pathlib
import re
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import mcp_codigo  # noqa: E402

RAIZ = pathlib.Path(__file__).resolve().parents[2]
INVENTARIO = RAIZ / "docs" / "mcp.json"
DOC = RAIZ / "docs" / "funciones" / "MCP.md"
MARCA_INI = "<!-- registro: lo genera tools/docs/mcp-inventario.py --desde-codigo; no se edita a mano -->"
MARCA_FIN = "<!-- /registro -->"
# La app que está andando (la que tiene Runtime/mcp.json): release/ (Linux,
# Windows) o el paquete de macOS.
_CANDIDATAS = [RAIZ / "release", RAIZ / "dist" / "BroadcastAppliance.app" / "Contents" / "Resources"]
APP = next((c for c in _CANDIDATAS if (c / "Runtime" / "mcp.json").exists()), _CANDIDATAS[0])
MCP_PUENTE = APP / "bin" / "64bit" / "ba-mcp"
CLASES = ("lectura", "escritura", "aire", "ui")
HILOS = ("foto", "control", "interfaz")


def del_aparato():
    """tools/list SIN rol: el juego completo, no el recortado de un rol."""
    pedidos = (
        '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05",'
        '"capabilities":{},"clientInfo":{"name":"inventario","version":"1"}}}\n'
        '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}\n'
    )
    r = subprocess.run([str(MCP_PUENTE)], input=pedidos, capture_output=True, text=True,
                       timeout=120, cwd=str(APP))
    for linea in r.stdout.splitlines():
        if '"id":2' not in linea:
            continue
        d = json.loads(linea)
        if "result" in d:
            return {t["name"]: t for t in d["result"]["tools"]}
        raise SystemExit("el aparato contesto un error: " + json.dumps(d.get("error", {}), ensure_ascii=False))
    raise SystemExit("el aparato no contesto. ¿Esta andando? (release/BroadcastAppliance)")


# --- lo que no es de cada herramienta ------------------------------------------
def _archivo(rel):
    txt = (RAIZ / rel).read_text(encoding="utf-8")
    toks = mcp_codigo.tokens(txt)
    return txt, toks, mcp_codigo._resolvedor(txt, toks)


def _texto(v):
    return v if isinstance(v, str) else ""


def categorias():
    """{id: titulo} de cada `addCategory(id, titulo, resumen)` (el titulo, en su original)."""
    out = {}
    for f in sorted((RAIZ / "src/app/mcp").glob("*.cpp")):
        txt, toks, resolver = _archivo(f.relative_to(RAIZ))
        for i, t in enumerate(toks):
            if t[1].endswith("addCategory") and toks[i + 1][1] == "(" and toks[i - 1][1] != "::":
                a = mcp_codigo._Eval(txt, toks, i + 1, resolver).args()
                if a and isinstance(a[0], str) and re.fullmatch(r"[a-z_]+", a[0]):
                    out[a[0]] = _texto(a[1]) if len(a) > 1 else ""
    return out


def etiquetas():
    """Las del buscador (McpEtiquetas.cpp): por nombre exacto y por prefijo de familia."""
    txt, toks, resolver = _archivo("src/app/mcp/McpEtiquetas.cpp")
    familias = mcp_codigo.tabla(txt, toks, resolver, "kFamilias", "Fila")
    propias = mcp_codigo.tabla(txt, toks, resolver, "kPropias", "Fila")
    prefijos, exactas = {}, {}
    for filas, destino in ((familias, prefijos), (propias, exactas)):
        for f in filas:
            clave, terminos = f.get("clave"), f.get("terminos")
            if isinstance(clave, str) and isinstance(terminos, str):
                # Tal cual estan escritas, no partidas en palabras: como frase se lee lo que
                # alguien diria en voz alta. Dos filas para la misma clave se suman (McpEtiquetas).
                destino[clave] = (destino.get(clave, "") + " " + " ".join(terminos.split())).strip()
    return exactas, prefijos


def temas():
    """Los temas de ayuda (McpHelp.cpp, kTopics) con su texto en el idioma original."""
    txt, toks, resolver = _archivo("src/app/mcp/McpHelp.cpp")
    out = []
    for f in mcp_codigo.tabla(txt, toks, resolver, "kTopics", "TopicDef"):
        if not isinstance(f.get("id"), str):
            continue
        cuerpo = RAIZ / "assets/idiomas/es/ayuda" / (f["id"] + ".md")
        out.append({"id": f["id"], "titulo": _texto(f.get("title")), "resumen": _texto(f.get("summary")),
                    "texto": cuerpo.read_text(encoding="utf-8").strip() if cuerpo.exists() else ""})
    return out


def roles():
    """Que categorias lleva cada rol, y cuales son comunes a todos."""
    txt = (RAIZ / "src/app/agent/Roles.cpp").read_text(encoding="utf-8")
    out = {}
    for m in re.finditer(r'x\.id = "([a-z]+)";(.*?)x\.categorias = \{(.*?)\};', txt, re.S):
        out[m.group(1)] = sorted(set(re.findall(r'"([a-z_]+)"', m.group(3))))
    for nombre, patron in (("asistente", r'juegoDelAsistente.*?std::set<std::string> c\{(.*?)\};'),
                           ("(comunes a todos)", r'comunes\(\).*?std::set<std::string> c\{(.*?)\};')):
        m = re.search(patron, txt, re.S)
        if m:
            out[nombre] = sorted(set(re.findall(r'"([a-z_]+)"', m.group(1))))
    return out


def renombres():
    """{nombre de hoy: [nombres viejos]} de la tabla en bloque de McpRenombres.cpp."""
    out = {}
    s = (RAIZ / "src/app/mcp/McpRenombres.cpp").read_text(encoding="utf-8")
    for viejo, nuevo in re.findall(r'\{"([a-z0-9_]+)",\s*"([a-z0-9_]+)"\}', s):
        out.setdefault(nuevo, []).append(viejo)
    return out


def _enum_vacio(v):
    if isinstance(v, dict):
        return v.get("enum") == [] or any(_enum_vacio(x) for x in v.values())
    if isinstance(v, list):
        return any(_enum_vacio(x) for x in v)
    return False


# --- el registro -----------------------------------------------------------------
_PROTOCOLO = {"titulo": "title", "descripcion": "description", "esquema": "inputSchema"}


def armar(aparato=None):
    """El inventario completo (el dict de docs/mcp.json) y los problemas del registro.

    :param aparato: {nombre: entrada de tools/list} si la app esta andando; sin eso,
        lo que el lector no puede evaluar sale del inventario anterior."""
    cod = mcp_codigo.herramientas()
    anterior = {}
    if INVENTARIO.exists():
        anterior = {h["nombre"]: h for h in json.loads(INVENTARIO.read_text(encoding="utf-8"))["herramientas"]}
    problemas = []
    for n, c in sorted(cod.items()):
        if n.startswith("?"):
            problemas.append(f"{c['archivo']}:{c['linea']}: {c['errores']['nombre']}")
    cod = {n: c for n, c in cod.items() if not n.startswith("?")}
    viejos = renombres()
    exactas, prefijos = etiquetas()
    juegos = roles()
    cats = categorias()

    fuera = []
    for n, c in sorted(cod.items()):
        origen = {}
        for campo, clave in _PROTOCOLO.items():
            if aparato is not None and n in aparato:
                if campo in c["errores"] or clave in aparato[n]:
                    c[campo] = aparato[n].get(clave, c[campo])
                    origen[campo] = "aparato"
            elif campo in c["errores"]:
                de_antes = anterior.get(n, {}).get("origen")
                if n in anterior and campo in anterior[n]:
                    c[campo] = anterior[n][campo]
                    # Lo que ya era «sin leer», o un enum vacío (la lista la llena la app: qr_generar
                    # con los tipos de QrViewModel), sigue sin leer aunque esté en el anterior.
                    dudoso = (isinstance(de_antes, dict) and de_antes.get(campo) == "sin_leer") or \
                        _enum_vacio(c[campo])
                    origen[campo] = "sin_leer" if dudoso else "inventario_anterior"
                else:
                    # Lo arma la app (una descripcion con los detectores de esta maquina): queda el
                    # valor de fabrica marcado, hasta que se regenere con la app andando.
                    origen[campo] = "sin_leer"
        props = c["esquema"].get("properties") if isinstance(c["esquema"], dict) else None
        if c["porSalida"] and isinstance(props, dict) and "salida" not in props:
            props["salida"] = dict(mcp_codigo.SALIDA)  # lo que suma McpCore::addTool (un esquema del anterior)
        for campo in ("clase", "categoria", "nombre"):
            if campo in c["errores"]:
                problemas.append(f"{n} ({c['archivo']}:{c['linea']}): `{campo}` {c['errores'][campo]}")
        if not c["categoria"]:
            problemas.append(f"{n} ({c['archivo']}:{c['linea']}): sin categoria")
        if c["clase"] == "aire" and not c["porQueAire"]:
            problemas.append(f"{n} ({c['archivo']}:{c['linea']}): de aire sin `porQueAire`")
        if c["clase"] != "aire" and (c["porQueAire"] or c["aireSi"] != "siempre"):
            problemas.append(f"{n} ({c['archivo']}:{c['linea']}): `porQueAire`/`aireSi` sin `Clase::Aire`")
        if c.get("repetida"):
            problemas.append(f"{n}: registrada dos veces ({c['repetida']} y {c['archivo']}:{c['linea']})")
        antes = sorted(set(c["antes"]) | set(viejos.get(n, [])))
        tags = {"propias": exactas.get(n, ""),
                # Las de su familia (y las de las familias de sus nombres viejos), como el buscador.
                "porPrefijo": " · ".join(v for pre, v in sorted(prefijos.items())
                                         if any(x.startswith(pre) for x in [n] + antes))}
        if not tags["propias"] and not tags["porPrefijo"]:
            problemas.append(f"{n}: sin etiquetas de busqueda (src/app/mcp/McpEtiquetas.cpp)")
        cat = c["categoria"]
        h = {
            "nombre": n,
            "titulo": c["titulo"],
            "categoria": cat,
            "descripcion": c["descripcion"],
            "esquema": c["esquema"],
            "clase": c["clase"],
            "hilo": c["hilo"],
            "soloLectura": c["soloLectura"],
            "destructiva": c["destructiva"],
            "idempotente": c["idempotente"],
            "etiquetas": tags,
            "donde": {"archivo": c["archivo"], "linea": c["linea"]},
            "roles": sorted(r for r, cs in juegos.items()
                            if r != "(comunes a todos)" and cat and (cat in cs or cat in juegos.get("(comunes a todos)", []))),
        }
        if c["clase"] == "aire":
            h["aire"] = {"cuando": c["aireSi"], "por_que": c["porQueAire"]}
        for clave, campo in (("reversible", "reversible"), ("porSalida", "porSalida"), ("mediada", "mediada"),
                             ("contenidoNoConfiable", "contenidoNoConfiable")):
            if c[campo]:
                h[clave] = True
        if antes:
            h["antes"] = antes
        if origen and aparato is None:
            h["origen"] = origen
        fuera.append(h)
    fuera.sort(key=lambda x: (x["categoria"] or "zzz", x["nombre"]))
    sin_cat = sorted({h["categoria"] for h in fuera if h["categoria"] and h["categoria"] not in cats})
    for c in sin_cat:
        problemas.append(f"la categoria `{c}` no tiene addCategory")
    doc = {
        "_comentario": [
            "Registro del servidor MCP del Broadcast Appliance (F3-02).",
            "GENERADO: no se edita a mano. Lo arma tools/docs/mcp-inventario.py "
            + ("LEYENDO EL CODIGO (--desde-codigo; `origen` dice los campos que el lector no pudo evaluar "
               "y salen del inventario anterior)." if aparato is None else "con el aparato andando."),
            "`clase` la declara cada herramienta (lectura, escritura, aire, ui); `hilo` es donde la despacha F3-03.",
            "`roles` de cada herramienta = los que la pueden usar (su categoria esta en su juego).",
            "`etiquetas` son las del buscador (McpIndice), no del protocolo: es lo que hace que",
            "«hay acople» llegue a audio_set sin que nadie nombre la herramienta.",
        ],
        "fuente": "codigo" if aparato is None else "aparato",
        "cuantas": len(fuera),
        "por_clase": {k: sum(1 for h in fuera if h["clase"] == k) for k in CLASES},
        "categorias": [{"id": k, "titulo": v, "herramientas": sorted(h["nombre"] for h in fuera if h["categoria"] == k)}
                       for k, v in sorted(cats.items())],
        "roles": {k: v for k, v in sorted(juegos.items())},
        "temas_de_ayuda": temas(),
        "herramientas": fuera,
    }
    return doc, problemas


def tabla_md(doc):
    """La parte de MCP.md que sale del registro (entre las marcas)."""
    pc = doc["por_clase"]
    filas = [
        MARCA_INI,
        "",
        f"**{doc['cuantas']} herramientas** en {sum(1 for c in doc['categorias'] if c['herramientas'])} categorías: "
        f"{pc['lectura']} de lectura, {pc['escritura']} de escritura, {pc['aire']} de aire y {pc['ui']} de interfaz "
        "(la clase la declara cada una al registrarse; ver «Clases» más abajo).",
        "",
        "| Categoría | Herramientas | Lectura | Escritura | Aire | Interfaz |",
        "|---|---:|---:|---:|---:|---:|",
    ]
    por_nombre = {h["nombre"]: h for h in doc["herramientas"]}
    for c in doc["categorias"]:
        hs = [por_nombre[n] for n in c["herramientas"]]
        if not hs:
            continue
        cuenta = {k: sum(1 for h in hs if h["clase"] == k) for k in CLASES}
        filas.append(f"| {c['titulo']} (`{c['id']}`) | {len(hs)} | {cuenta['lectura']} | {cuenta['escritura']} | "
                     f"{cuenta['aire']} | {cuenta['ui']} |")
    filas += ["", "Las de **aire** (con la transmisión o la grabación andando, si las pide algo automático las "
              "confirma una persona; `cuando`: con qué argumentos cuentan):", "",
              "| Herramienta | Cuándo | Por qué |", "|---|---|---|"]
    for h in sorted((h for h in doc["herramientas"] if h["clase"] == "aire"), key=lambda h: h["nombre"]):
        filas.append(f"| `{h['nombre']}` | {h['aire']['cuando'].replace('_', ' ')} | {h['aire']['por_que']} |")
    filas += ["", MARCA_FIN]
    return "\n".join(filas)


def doc_md(doc):
    """MCP.md con la parte del registro al dia (None si faltan las marcas)."""
    txt = DOC.read_text(encoding="utf-8")
    i, j = txt.find(MARCA_INI), txt.find(MARCA_FIN)
    if i < 0 or j < 0:
        return None
    return txt[:i] + tabla_md(doc) + txt[j + len(MARCA_FIN):]


def _sin_fecha(d):
    return {k: v for k, v in d.items() if k != "generado"}


def _volcar(doc):
    return json.dumps(doc, ensure_ascii=False, indent=2) + "\n"


def verificar():
    doc, problemas = armar()
    deriva = []
    actual = json.loads(INVENTARIO.read_text(encoding="utf-8")) if INVENTARIO.exists() else {}
    if _sin_fecha(actual) != _sin_fecha(doc):
        a = {h["nombre"]: h for h in actual.get("herramientas", [])}
        b = {h["nombre"]: h for h in doc["herramientas"]}
        nuevas, quitadas = sorted(set(b) - set(a)), sorted(set(a) - set(b))
        cambiadas = sorted(n for n in set(a) & set(b) if a[n] != b[n])
        deriva.append("docs/mcp.json no es lo que sale del codigo"
                      + (f"; sin registrar ahi: {', '.join(nuevas[:10])}" if nuevas else "")
                      + (f"; ya no estan: {', '.join(quitadas[:10])}" if quitadas else "")
                      + (f"; cambiaron: {', '.join(cambiadas[:10])}" if cambiadas else ""))
    md = doc_md(doc)
    if md is None:
        deriva.append(f"{DOC.relative_to(RAIZ)} no tiene las marcas del registro")
    elif md != DOC.read_text(encoding="utf-8"):
        deriva.append(f"la tabla de {DOC.relative_to(RAIZ)} no es la del registro")
    for p in problemas:
        print("REGISTRO:", p)
    for d in deriva:
        print("DERIVA:", d)
    if deriva:
        print("Regeneralo: python3 tools/docs/mcp-inventario.py --desde-codigo")
    if problemas or deriva:
        return 1
    print(f"registro MCP sin deriva: {doc['cuantas']} herramientas ({doc['por_clase']})")
    return 0


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if "--verificar" in sys.argv:
        sys.exit(verificar())
    codigo = "--desde-codigo" in sys.argv
    doc, problemas = armar(None if codigo else del_aparato())
    salida = pathlib.Path(args[0]) if args else INVENTARIO
    salida.parent.mkdir(parents=True, exist_ok=True)
    salida.write_text(_volcar(doc), encoding="utf-8")
    md = doc_md(doc)
    if md is not None and salida == INVENTARIO:
        DOC.write_text(md, encoding="utf-8")
    print(f"{doc['cuantas']} herramientas -> {salida} ({doc['por_clase']})")
    print(f"  categorias: {len(doc['categorias'])} · temas de ayuda: {len(doc['temas_de_ayuda'])} · roles: {len(doc['roles'])}")
    for de, que in (("inventario_anterior", "con campos del inventario anterior"),
                    ("sin_leer", "con campos que solo da la app andando (sin leer: regeneralo con la app)")):
        hs = [f"{h['nombre']} ({', '.join(k for k, v in h['origen'].items() if v == de)})"
              for h in doc["herramientas"] if de in h.get("origen", {}).values()]
        if hs:
            print(f"  {que} ({len(hs)}): {'; '.join(hs)}")
    for p in problemas:
        print("  REGISTRO:", p)
    if md is None:
        print(f"  {DOC.relative_to(RAIZ)}: faltan las marcas {MARCA_INI!r} … {MARCA_FIN!r}")


if __name__ == "__main__":
    main()
