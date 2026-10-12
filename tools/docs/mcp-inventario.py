#!/usr/bin/env python3
"""Inventario completo del MCP, a un .json.

QUE SALE: todas las herramientas con su titulo, descripcion, esquema de
entrada, categoria, etiquetas de busqueda, en que archivo y linea viven, y que
roles pueden usarlas. Mas las categorias, los temas de ayuda y el juego de
cada rol.

DE DONDE SALE CADA COSA, y por que no de un solo lado:
  - El APARATO ANDANDO da lo que el modelo ve de verdad (tools/list): nombre,
    titulo, descripcion y esquema, ya armados. Sacarlo del codigo obligaria a
    interpretar literales de C++ partidos en pedazos, que es como se escriben.
  - El CODIGO da lo que no viaja en el protocolo: la categoria, las etiquetas
    del buscador, y donde vive cada herramienta.

Uso:  tools/docs/mcp-inventario.py [salida.json]      (con el aparato andando)
"""
import json
import pathlib
import re
import subprocess
import sys
from datetime import datetime, timezone

RAIZ = pathlib.Path(__file__).resolve().parents[2]
# La app que está andando (la que tiene Runtime/mcp.json): release/ (Linux,
# Windows) o el paquete de macOS.
_CANDIDATAS = [RAIZ / "release", RAIZ / "dist" / "BroadcastAppliance.app" / "Contents" / "Resources"]
APP = next((c for c in _CANDIDATAS if (c / "Runtime" / "mcp.json").exists()), _CANDIDATAS[0])
MCP = APP / "bin" / "64bit" / "ba-mcp"


def del_aparato():
    """tools/list SIN rol: el juego completo, no el recortado de un rol."""
    pedidos = (
        '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05",'
        '"capabilities":{},"clientInfo":{"name":"inventario","version":"1"}}}\n'
        '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}\n'
    )
    r = subprocess.run([str(MCP)], input=pedidos, capture_output=True, text=True,
                       timeout=120, cwd=str(APP))
    for linea in r.stdout.splitlines():
        if '"id":2' not in linea:
            continue
        d = json.loads(linea)
        if "result" in d:
            return d["result"]["tools"]
        raise SystemExit("el aparato contesto un error: " + json.dumps(d.get("error", {}), ensure_ascii=False))
    raise SystemExit("el aparato no contesto. ¿Esta andando? (release/BroadcastAppliance)")


def del_codigo(nombres):
    """Categoria, archivo y linea de cada herramienta, leyendo los registros.

    :param nombres: los nombres que dio el aparato. Hacen falta porque no todas
        las herramientas se escriben `t.name = "literal"`: las del rack de
        audio salen de un bucle sobre una lista, y ahi el nombre aparece como
        literal suelto. Para esas se busca el literal y se toma la categoria
        que se asigne mas abajo en el mismo bloque.
    """
    donde, categorias = {}, {}
    for f in sorted((RAIZ / "src/app/mcp").glob("*.cpp")):
        txt = f.read_text(encoding="utf-8")
        rel = f.relative_to(RAIZ).as_posix()
        for m in re.finditer(r't\.name\s*=\s*"([a-z_0-9]+)"', txt):
            linea = txt.count("\n", 0, m.start()) + 1
            # La categoria se declara despues del nombre, en el mismo bloque.
            cuerpo = txt[m.end():txt.find("core_.addTool", m.end())]
            cat = re.search(r'\w+\.category\s*=\s*"([a-z_]+)"', cuerpo)
            donde[m.group(1)] = {"archivo": rel, "linea": linea,
                                 "categoria": cat.group(1) if cat else None}
        for m in re.finditer(r'addCategory\(\s*\n?\s*"([a-z_]+)",\s*\n?\s*"([^"]*)"', txt):
            categorias[m.group(1)] = m.group(2)

    # Segunda pasada: las que no se declaran con `t.name = "literal"`.
    # McpEtiquetas.cpp queda afuera: ahi estan TODOS los nombres (son las
    # etiquetas del buscador) y es el primero por orden alfabetico, asi que se
    # quedaba con todas las que faltaban y las daba sin categoria.
    for f in sorted(f for f in (RAIZ / "src/app/mcp").glob("*.cpp")
                    if f.name != "McpEtiquetas.cpp"):
        txt = f.read_text(encoding="utf-8")
        rel = f.relative_to(RAIZ).as_posix()
        for n in nombres:
            if n in donde:
                continue
            m = re.search(r'"' + re.escape(n) + r'"', txt)
            if not m:
                continue
            cat = re.search(r'\w+\.category\s*=\s*"([a-z_]+)"', txt[m.end():m.end() + 4000])
            donde[n] = {"archivo": rel, "linea": txt.count("\n", 0, m.start()) + 1,
                        "categoria": cat.group(1) if cat else None}
    return donde, categorias


def etiquetas():
    """Las del buscador (McpEtiquetas.cpp): por nombre exacto y por prefijo."""
    txt = (RAIZ / "src/app/mcp/McpEtiquetas.cpp").read_text(encoding="utf-8")
    exactas, prefijos = {}, {}
    # Las familias son las de la tabla kFamilias (se aplican por prefijo).
    bloque = re.search(r"kFamilias\[\]\s*=\s*\{(.*?)\n\};", txt, re.S)
    familias = set(re.findall(r'\{"([a-z_0-9]+)",', bloque.group(1))) if bloque else set()
    # Una entrada puede ocupar varias lineas: se juntan los literales pegados.
    for m in re.finditer(r'\{"([a-z_0-9]+)",\s*((?:"[^"]*"\s*)+)\}', txt):
        clave = m.group(1)
        # Tal cual estan escritas, no partidas en palabras: partirlas llenaba
        # el inventario de «a», «del» y «la», que no dicen nada de la
        # herramienta. Como frase se lee lo que alguien diria en voz alta.
        valor = " ".join(re.findall(r'"([^"]*)"', m.group(2))).strip()
        (prefijos if clave in familias else exactas)[clave] = valor
    return exactas, prefijos


def temas():
    """Los temas de ayuda, tal cual estan escritos en McpHelp.cpp."""
    txt = (RAIZ / "src/app/mcp/McpHelp.cpp").read_text(encoding="utf-8")
    out = []
    for m in re.finditer(r'\{"([a-z_]+)",\s*"([^"]*)",\s*"([^"]*)",\s*\n?\s*R"md\((.*?)\)md"\}', txt, re.S):
        out.append({"id": m.group(1), "titulo": m.group(2), "resumen": m.group(3),
                    "texto": m.group(4).strip()})
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


def main():
    salida = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else RAIZ / "docs" / "mcp.json"
    herr = del_aparato()
    donde, cats = del_codigo([t["name"] for t in herr])
    exactas, prefijos = etiquetas()
    juegos = roles()
    comunes = set(juegos.get("(comunes a todos)", []))

    fuera = []
    for t in herr:
        n = t["name"]
        d = donde.get(n, {})
        cat = d.get("categoria")
        tags = {"propias": exactas.get(n, ""),
                # Las de su familia: «pantalla_» vale para todas las de
                # pantallas. Es como el buscador las junta.
                "porPrefijo": " · ".join(v for pre, v in sorted(prefijos.items()) if n.startswith(pre))}
        puede = sorted(r for r, cs in juegos.items()
                       if r != "(comunes a todos)" and cat and (cat in cs or cat in comunes))
        a = t.get("annotations", {})
        fuera.append({
            "nombre": n,
            "titulo": t.get("title", ""),
            "categoria": cat,
            "descripcion": t.get("description", ""),
            "esquema": t.get("inputSchema", {}),
            "soloLectura": a.get("readOnlyHint", False),
            "destructiva": a.get("destructiveHint", False),
            "etiquetas": tags,
            "donde": {"archivo": d.get("archivo"), "linea": d.get("linea")},
            "roles": puede,
        })
    fuera.sort(key=lambda x: (x["categoria"] or "zzz", x["nombre"]))
    # Una herramienta publicada dos veces le llega al modelo como dos fichas
    # con el mismo nombre. No rompe nada y por eso no se nota: hay que contarlo.
    vistos, repetidas = set(), []
    for h in fuera:
        (repetidas.append(h["nombre"]) if h["nombre"] in vistos else vistos.add(h["nombre"]))

    doc = {
        "_comentario": [
            "Inventario del servidor MCP del Broadcast Appliance.",
            "GENERADO: no se edita a mano. Lo arma tools/docs/mcp-inventario.py con el aparato andando.",
            "`roles` de cada herramienta = los que la pueden usar (su categoria esta en su juego).",
            "`etiquetas` son las del buscador (McpIndice), no del protocolo: es lo que hace que",
            "«hay acople» llegue a audio_set sin que nadie nombre la herramienta.",
        ],
        "generado": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "cuantas": len(fuera),
        "repetidas": sorted(set(repetidas)),
        "categorias": [{"id": k, "titulo": v,
                        "herramientas": sorted(h["nombre"] for h in fuera if h["categoria"] == k)}
                       for k, v in sorted(cats.items())],
        "roles": {k: v for k, v in sorted(juegos.items())},
        "temas_de_ayuda": temas(),
        "herramientas": fuera,
    }
    salida.parent.mkdir(parents=True, exist_ok=True)
    salida.write_text(json.dumps(doc, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    sin_cat = [h["nombre"] for h in fuera if not h["categoria"]]
    sin_tag = [h["nombre"] for h in fuera
               if not h["etiquetas"]["propias"] and not h["etiquetas"]["porPrefijo"]]
    print(f"{len(fuera)} herramientas -> {salida.relative_to(RAIZ)}")
    print(f"  categorias: {len(cats)} · temas de ayuda: {len(doc['temas_de_ayuda'])} · roles: {len(juegos)}")
    if sin_cat:
        print(f"  SIN CATEGORIA ({len(sin_cat)}): {', '.join(sin_cat[:12])}")
    if sin_tag:
        print(f"  SIN ETIQUETAS ({len(sin_tag)}): {', '.join(sin_tag[:12])}")
    if repetidas:
        print(f"  PUBLICADAS DOS VECES ({len(repetidas)}): {', '.join(sorted(set(repetidas)))}")


if __name__ == "__main__":
    main()
