#!/usr/bin/env python3
"""La lista permitida del tablero web: qué funciones puede llamar y qué propiedades puede
escribir la réplica (src/app/control/ReplicaPublicador), sacada de la QML del dashboard.

Lee DashboardPage, las tarjetas, el marco web (src/tablero-web/qml) y los controles que usan
(cerrado transitivo: solo lo que de verdad se ve en el tablero) y junta:
  - las llamadas `Studio.a.b.metodo(` (y por un alias `property var x: Studio.a.b` → `x.metodo(`);
  - las escrituras `Studio.a.b.prop = …` / `+=` / `-=`.
A cada una le pone la herramienta MCP equivalente (EQUIVALENTES, abajo): con esa se decide si
el perfil del aparato la deja y si toca el aire (pide la misma confirmación que el control web).
Lo que no tiene equivalente y no está en SOLO_VISTA no entra: se avisa y el generador falla.

Escribe src/app/control/ReplicaPermitidas.inc (JSON en un literal de C++).

    tools/generar/replica-permitidas.py             # regenerar
    tools/generar/replica-permitidas.py --verificar # ¿está al día con la QML? (lo corre ctest)
"""
import json
import os
import re
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
QML = os.path.join(RAIZ, "src", "app", "qml")
SALIDA = os.path.join(RAIZ, "src", "app", "control", "ReplicaPermitidas.inc")

# --- La herramienta MCP equivalente de cada cosa -------------------------------------------------
# Clave: «objeto.metodo», «objeto.*» (todo lo de ese objeto) u «objeto.propiedad». La herramienta
# da la categoría (perfil del aparato) y si es de aire (confirmación con aire). Lo más específico gana.
EQUIVALENTES = {
    # Escenas y el TAKE
    "Studio.escenas.take": "preview_al_aire",
    "Studio.escenas.takeCon": "preview_al_aire",
    "Studio.escenas.repetirUi": "preview_al_aire",
    "Studio.escenas.takeScene": "scene_switch",
    "Studio.escenas.setTransicionEntrada": "escena_transicion",
    "Studio.escenas.setAfv": "audio_afv",
    "Studio.escenas.*": "scene_preview",
    "Studio.transitionType": "escena_transicion",
    # Fuentes y mezcla
    "Studio.sources.*": "source_update",
    "Studio.mixer.*": "audio_set",
    "Studio.mezcla.*": "audio_set",
    "Studio.mezclador.*": "audio_set",
    "Studio.emision.resetLoudnessUi": "audio_loudness_reset",
    # Emisión: grabar, transmitir, destinos, ISO, señal limpia
    "Studio.emision.toggleRecording": "record_start",
    "Studio.emision.startStreaming": "stream_start",
    "Studio.emision.stopStreaming": "stream_stop",
    "Studio.emision.iniciarDestinoUi": "stream_start",
    "Studio.emision.detenerDestinoUi": "stream_stop",
    "Studio.emision.borrarDestinoUi": "stream_destino_borrar",
    "Studio.emision.limpiaActivarUi": "salida_limpia",
    "Studio.emision.*": "record_ajustes",
    "Studio.salidas.*": "salida_editar",
    # La sala, los invitados, los oradores, la asamblea
    "Studio.conference.*": "participant_admit",
    "Studio.sala.*": "participant_admit",
    "Studio.bancas.alAire": "banca_al_aire",
    "Studio.bancas.*": "banca_cola",
    "Studio.seguirMicrofono.*": "seguir_microfono",
    "Studio.ordenDelDia.*": "orden_del_dia_punto",
    "Studio.carruseles.*": "carrusel_armar",
    "Studio.pdfEscena.*": "pdf_escena_poner",
    "Studio.dsk.*": "dsk_mostrar",
    "Studio.camaras.*": "ptz_ir_posicion",
    "Studio.seguidor.*": "ptz_seguir",
    # La línea de tiempo y la corrida del guion
    "Studio.linea.*": "linea_go",
    "Studio.evento.corridaVm.*": "linea_go",
    "Studio.evento.*": "guion_bloque",
    # Alarmas y salud
    "Studio.alarmas.*": "alert_reconocer",
    "Studio.latido.*": "supervisor_politica",
    "Studio.latidoPoliticaUi": "supervisor_politica",
}
# Lo que es solo de la vista (acomodar el tablero, el fondo, avisar): sin herramienta, sin aire.
SOLO_VISTA = {
    "Studio.dashboard.*", "Studio.notify", "Studio.focoEnTexto", "Studio.extensiones.*",
    "Studio.riel.*", "Studio.seccionConfig", "Studio.multivistas.*",
}
# Lo que el tablero no hace nunca aunque la QML lo nombre: es de la máquina de la app o de los
# objetos que la réplica no publica (StudioViewModel: replica_->negar).
NUNCA = {"Studio.ventanas", "Studio.escritorio", "Studio.copiar", "Studio.catalogo", "Studio.control",
         "Studio.botonera", "Studio.accesoRemoto", "Studio.settings", "Studio.cuentas", "Studio.controlWeb",
         "Studio.agent", "Studio.asistente"}
# Funciones de JavaScript sobre un valor (una lista, un texto, un número), no del modelo.
DE_JS = {"indexOf", "lastIndexOf", "map", "filter", "some", "every", "slice", "splice", "startsWith", "endsWith",
         "join", "toUpperCase", "toLowerCase", "toFixed", "includes", "find", "findIndex", "forEach", "reduce",
         "concat", "split", "replace", "trim", "toString", "push", "sort", "arg", "length"}

RE_ALIAS = re.compile(r"property\s+\w+\s+(\w+)\s*:\s*(Studio(?:\.\w+)+)\s*$", re.M)
RE_LLAMADA = re.compile(r"\b(Studio(?:\.\w+)+)\s*\(")
RE_ESCRITURA = re.compile(r"\b(Studio(?:\.\w+)+)\s*(?:=|\+=|-=)(?!=)")
RE_TIPO = re.compile(r"(?<![\w.])([A-Z]\w+)\s*\{")


def archivos():
    """El dashboard, las tarjetas, el marco web y los controles que usan (cerrado transitivo)."""
    controles = {os.path.splitext(f)[0]: os.path.join(QML, "controls", f)
                 for f in os.listdir(os.path.join(QML, "controls")) if f.endswith(".qml")}
    raiz = [os.path.join(QML, "pages", "DashboardPage.qml"),
            os.path.join(RAIZ, "src", "tablero-web", "qml", "TableroWeb.qml")]
    raiz += [os.path.join(QML, "cards", f) for f in sorted(os.listdir(os.path.join(QML, "cards"))) if f.endswith(".qml")]
    vistos, pila = set(), list(raiz)
    while pila:
        f = pila.pop()
        if f in vistos:
            continue
        vistos.add(f)
        texto = open(f, encoding="utf-8").read()
        for t in RE_TIPO.findall(texto):
            if t in controles and controles[t] not in vistos:
                pila.append(controles[t])
    return sorted(vistos)


def sin_comentarios(t):
    t = re.sub(r"/\*.*?\*/", "", t, flags=re.S)
    return re.sub(r"//[^\n]*", "", t)


def equivalente(objeto, nombre):
    """La herramienta de «objeto.nombre», de lo más específico a lo más general («Studio.a.*»)."""
    claves = [f"{objeto}.{nombre}"]
    partes = objeto.split(".")
    claves += [".".join(partes[:i]) + ".*" for i in range(len(partes), 0, -1)]
    for clave in claves:
        if clave in EQUIVALENTES:
            return EQUIVALENTES[clave], False
        if clave in SOLO_VISTA:
            return None, True
    return None, False


def juntar():
    entradas, sin = {}, []
    for f in archivos():
        texto = sin_comentarios(open(f, encoding="utf-8").read())
        alias = dict(RE_ALIAS.findall(texto))
        for a, camino in alias.items():
            texto = re.sub(r"\b" + re.escape(a) + r"\.", camino + ".", texto)
        rel = os.path.relpath(f, RAIZ)
        for tipo, rx in (("llamar", RE_LLAMADA), ("poner", RE_ESCRITURA)):
            for camino in rx.findall(texto):
                partes = camino.split(".")
                if len(partes) < 2:
                    continue
                objeto, nombre = ".".join(partes[:-1]), partes[-1]
                if nombre in DE_JS:
                    continue
                if objeto in NUNCA or camino in NUNCA or any(objeto.startswith(n + ".") for n in NUNCA):
                    continue
                herr, vista = equivalente(objeto, nombre)
                if herr is None and not vista:
                    sin.append(f"{rel}: {tipo} {camino}")
                    continue
                e = entradas.setdefault((objeto, nombre, tipo), {"o": objeto, "n": nombre, "tipo": tipo,
                                                                 "mcp": herr, "donde": []})
                if rel not in e["donde"]:
                    e["donde"].append(rel)
    return sorted(entradas.values(), key=lambda e: (e["o"], e["n"], e["tipo"])), sin


def inc(entradas):
    datos = json.dumps([{k: e[k] for k in ("o", "n", "tipo", "mcp")} for e in entradas], ensure_ascii=False, indent=1)
    return ("// GENERADO por tools/generar/replica-permitidas.py desde la QML del dashboard: no editar a mano.\n"
            "// Lo que el tablero web puede llamar (tipo «llamar») y escribir («poner»), con la herramienta\n"
            "// MCP equivalente (perfil del aparato y confirmación de aire). Ver control/ReplicaPublicador.\n"
            f'R"json({datos})json"\n')


def herramientas():
    """Los nombres del inventario MCP (docs/mcp.json): una equivalente que no existe es un error."""
    d = json.load(open(os.path.join(RAIZ, "docs", "mcp.json"), encoding="utf-8"))["herramientas"]
    return set(d) if isinstance(d, dict) else {h.get("name") or h.get("nombre") for h in d}


def main():
    entradas, sin = juntar()
    existen = herramientas()
    sin += [f"{e['o']}.{e['n']}: «{e['mcp']}» no es una herramienta MCP" for e in entradas
            if e["mcp"] and e["mcp"] not in existen]
    if sin:
        print("Sin herramienta equivalente (agregalas a EQUIVALENTES o SOLO_VISTA):", file=sys.stderr)
        for s in sorted(set(sin)):
            print("  " + s, file=sys.stderr)
        return 1
    nuevo = inc(entradas)
    if "--verificar" in sys.argv:
        viejo = open(SALIDA, encoding="utf-8").read() if os.path.exists(SALIDA) else ""
        if viejo != nuevo:
            print("ReplicaPermitidas.inc no está al día con la QML: tools/generar/replica-permitidas.py", file=sys.stderr)
            return 1
        print(f"al día: {len(entradas)} permitidas")
        return 0
    with open(SALIDA, "w", encoding="utf-8") as f:
        f.write(nuevo)
    print(f"{len(entradas)} permitidas → {os.path.relpath(SALIDA, RAIZ)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
