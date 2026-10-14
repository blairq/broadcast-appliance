#!/usr/bin/env python3
"""Renombrar herramientas del MCP en todo el repo, con el nombre viejo como alias.

POR QUE. Pares de herramientas con nombres que no dicen lo que las separa
(chat_history / chat_recientes, capture_people / capture_person,
bancas_listar / speakers_status...) confundian al modelo chico, al grande y a la
gente. Los nombres nuevos salen del perfil de entrenamiento (datos/perfiles/
<perfil>.json, «renombres»), donde se eligieron y midieron primero.

QUE HACE. Reemplaza cada nombre viejo por el nuevo, por palabra entera, en el
codigo (src/), las pruebas, la ayuda, los datos y las herramientas; y escribe la
tabla de alias (src/app/mcp/McpRenombres.cpp) para que lo guardado con el nombre
viejo (recetas, botoneras, disparadores, modelos ya publicados) siga andando.

NO TOCA lo historico: las tarjetas y el modelo.json de los modelos publicados
(docs/modelos/), las bitacoras, docs/mcp.json (se regenera), los perfiles con los
que se entrenaron modelos anteriores, ni git.

    tools/dev/renombrar-herramientas.py --perfil vivo6           # muestra
    tools/dev/renombrar-herramientas.py --perfil vivo6 --aplicar
"""
import argparse
import json
import re
import subprocess
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
CARPETAS = ["src", "datos", "docs", "tools", "resources", "plantillas", "Makefile", "CLAUDE.md"]
EXT = {".cpp", ".hpp", ".h", ".mm", ".c", ".qml", ".js", ".py", ".json", ".jsonl", ".md", ".txt", ".html",
       ".css", ".sh", ".yml", ".yaml", ".toml", ".csv", ""}
NO_TOCAR = [re.compile(p) for p in (
    r"^docs/modelos/", r"^docs/mcp\.json$", r"bitacora", r"^datos/perfiles/(reactivo|audio|vivo4|vivo5)\.json$",
    r"^datos/etiquetas\.json$", r"^extern/", r"^build", r"/node_modules/", r"^tools/dev/renombrar-herramientas\.py$")]
ALIAS = RAIZ / "src/app/mcp/McpRenombres.cpp"


# --solo-app: la app, su ayuda y sus guias; los datos y scripts de ENTRENAMIENTO
# se migran despues (mientras se entrena, el prompt sale de docs/mcp.json y de
# datos/ con los nombres viejos y la capa de traduccion del perfil)
SOLO_APP = [re.compile(p) for p in (r"^datos/(?!escenarios/)", r"^tools/entrenar/")]


def archivos(solo_app=False):
    lista = subprocess.run(["git", "ls-files", *CARPETAS], cwd=RAIZ, capture_output=True, text=True).stdout.split()
    for f in lista:
        p = RAIZ / f
        if p.suffix in EXT and p.is_file() and not any(r.search(f) for r in NO_TOCAR) \
                and not (solo_app and any(r.search(f) for r in SOLO_APP)):
            yield f, p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--perfil", required=True)
    ap.add_argument("--aplicar", action="store_true")
    ap.add_argument("--solo-app", action="store_true", help="sin los datos ni los scripts de entrenamiento")
    args = ap.parse_args()
    ren = json.loads((RAIZ / f"datos/perfiles/{args.perfil}.json").read_text())["renombres"]
    patron = re.compile(r"(?<![\w-])(" + "|".join(sorted(map(re.escape, ren), key=len, reverse=True)) + r")(?![\w-])")
    cambios, por = 0, {}
    for f, p in archivos(args.solo_app):
        try:
            s = p.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        n = len(patron.findall(s))
        if not n:
            continue
        cambios += n
        por[f] = n
        if args.aplicar:
            p.write_text(patron.sub(lambda m: ren[m.group(1)], s), encoding="utf-8")
    for f, n in sorted(por.items(), key=lambda x: -x[1])[:25]:
        print(f"  {n:4} {f}")
    print(f"{cambios} reemplazos en {len(por)} archivos" + ("" if args.aplicar else " (sin aplicar)"))
    if args.aplicar:
        filas = "\n".join(f'        {{"{v}", "{n}"}},' for v, n in sorted(ren.items()))
        ALIAS.write_text(f'''// Nombres VIEJOS de herramientas que siguen andando como alias del nombre de hoy:
// lo guardado con el nombre viejo (recetas, botoneras, disparadores) y los modelos
// ya publicados que lo escriben. Lo escribe tools/dev/renombrar-herramientas.py
// (perfil {args.perfil}); no aparecen en tools/list ni en el buscador.
#include "McpRenombres.hpp"

namespace ba::app::mcp {{

void registrarRenombres(McpCore& core) {{
    static const std::pair<const char*, const char*> kRenombres[] = {{
{filas}
    }};
    for (const auto& [viejo, nuevo] : kRenombres) core.addAlias(viejo, nuevo);
}}

}} // namespace ba::app::mcp
''', encoding="utf-8")
        ALIAS.with_suffix(".hpp").write_text('''#pragma once
// Los nombres viejos de herramientas como alias del de hoy (ver McpRenombres.cpp).
#include "McpCore.hpp"

namespace ba::app::mcp {

void registrarRenombres(McpCore& core);

} // namespace ba::app::mcp
''', encoding="utf-8")
        print(f"alias -> {ALIAS.relative_to(RAIZ)}")


if __name__ == "__main__":
    main()
