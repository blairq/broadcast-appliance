#!/usr/bin/env python3
"""Prueba de Latencias del pipeline con «Sincronía y red» (página Video), por MCP, en un
evento de prueba propio que al final se borra.

Con la app corriendo (abierta con `--args --test`): dos cámaras virtuales iguales y el
micrófono, y de a un paso: el tope del estudio a 100 ms, el reloj NTP, el colchón de
las entradas de red a 400 ms, un desfase de +200 ms en una cámara, sincronizar las dos
(tiene que medir ~200 ms y corregirlo) y el retardo del audio del micrófono a +100 ms.
En cada paso: una nota en la línea de tiempo, lo que agrega cada nodo y una captura.
Al final deja todo como estaba (tope automático, reloj del sistema), el impacto de cada
paso, vuelve al evento de antes y borra el de prueba.

    tools/test/sincronia-mcp.py [--cerrar] [--espera 15]
"""
import argparse
import importlib.util
import json
import os
import re
import shutil
import sys
import tempfile
import time

AQUI = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location("asamblea_mcp", os.path.join(AQUI, "asamblea-mcp.py"))
_am = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_am)
conectar, Prueba, BA_MCP = _am.conectar, _am.Prueba, _am.BA_MCP

CAM_A, CAM_B, MIC = "Cámara A", "Cámara B", "Micrófono de prueba"


def resumen(mapa):
    filas = [f'   estudio {mapa.get("estudio", {}).get("latencia_ms")} ms (tope {mapa.get("estudio", {}).get("tope_ms")})']
    for n in mapa.get("nodos", []):
        if n.get("activo") is False or n.get("tipo") not in ("entrada", "escena", "mezcla"):
            continue
        ms, acum = n.get("ms"), n.get("acumulado_ms")
        filas.append(f'   {n["id"][:40]:40} {("—" if ms is None else str(ms)):>8} ms  acum {acum}  {n.get("estado")}')
    return "\n".join(filas)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ba-mcp", default=BA_MCP)
    ap.add_argument("--cerrar", action="store_true")
    ap.add_argument("--espera", type=int, default=15)
    a = ap.parse_args()

    m, original = conectar(a.ba_mcp)
    prueba = "ZZ prueba sincronia " + time.strftime("%H%M%S")
    r = m.tool("evento_nuevo", {"nombre": prueba, "desde": "cero", "abrir": True})
    if r.get("_isError"):
        print("no se pudo crear el evento de prueba:", r.get("_texto"))
        return 2
    m.cerrar()
    time.sleep(8)
    m, ab = conectar(a.ba_mcp)
    if ab != prueba:
        print(f"CORTO: lo abierto es «{ab}», no el evento de prueba")
        return 2
    time.sleep(5)
    t = Prueba(m, prueba)
    salida = tempfile.mkdtemp(prefix="sincronia-mcp-")
    m.tool("ui_show_page", {"page": "latencias"})

    def paso(n, titulo, accion):
        t.guarda()
        print(f"\n== {n}. {titulo}")
        if accion is not None:
            accion()
        m.tool("tiempos_evento_agregar", {"texto": titulo})
        time.sleep(a.espera)
        print(resumen(m.tool("tiempos_mapa")))
        r = m.tool("ventana_captura")
        mm = re.search(r"(/[^\n\"]*?\.(?:jpg|png))", r.get("_texto", ""))
        if mm and os.path.isfile(mm.group(1)):
            shutil.copy(mm.group(1), os.path.join(salida, f"{n:02d}-{re.sub(r'[^a-z0-9]+', '-', titulo.lower())}.jpg"))

    def armar():
        t.paso("cámara A", m.tool("video_entrada_virtual", {"nombre": CAM_A, "preset": "pelota", "al_aire": True}))
        t.paso("cámara B", m.tool("video_entrada_virtual", {"nombre": CAM_B, "preset": "pelota", "al_aire": True}))
        entradas = m.tool("audio_devices").get("inputs", [])
        texto = lambda d: " ".join(str(v) for v in d.values()) if isinstance(d, dict) else str(d)
        elegido = next((d for d in entradas if "micr" in texto(d).lower()), entradas[0] if entradas else {})
        t.paso("micrófono", m.tool("audio_input_add", {"device": elegido.get("value", "") if isinstance(elegido, dict) else "", "name": MIC}))
    paso(1, "Punto de partida", armar)
    m.tool("tiempos_linea", {"nodos": ["entrada:" + CAM_A, "entrada:" + CAM_B, "escena:programa", "mezcla:programa"]})

    paso(2, "Tope del estudio 100 ms", lambda: t.paso("tope 100", m.tool("estudio_latencia", {"tope_ms": 100})))
    paso(3, "Reloj NTP", lambda: t.paso("reloj ntp", m.tool("video_reloj", {"tipo": "ntp", "servidor": "pool.ntp.org"})))
    paso(4, "Colchón de red 400 ms", lambda: t.paso("colchón 400", m.tool("video_reloj", {"latencia_ms": 400})))
    paso(5, "Desfase +200 ms en la cámara B", lambda: t.paso("desfase B", m.tool("video_desfase", {"entrada": CAM_B, "ms": 200})))

    def sincronizar():
        r = t.paso("sincronizar A y B", m.tool("video_sincronia", {"a": CAM_A, "b": CAM_B, "aplicar": True, "segundos": 6}))
        print("   medición:", json.dumps({k: v for k, v in r.items() if not k.startswith("_")}, ensure_ascii=False)[:300])
    paso(6, "Sincronizar A y B", sincronizar)
    paso(7, "Retardo del audio +100 ms", lambda: t.paso("retardo audio", m.tool("audio_sincro", {"name": MIC, "ms": 100})))

    print("\n== Impacto de cada paso (lo que más cambió)")
    for e in m.tool("tiempos_eventos", {"segundos": 1800}).get("eventos", []):
        if e.get("tipo") != "nota":
            continue
        imp = m.tool("tiempos_evento_impacto", {"t_ms": e["t"], "segundos": max(3, a.espera // 2)}).get("nodos", [])
        cambios = ", ".join(f'{x["titulo"]} {x["cambio_ms"]:+.1f}' for x in imp[:3] if abs(x["cambio_ms"]) >= 0.5)
        print(f'   {e["texto"]:32} {cambios or "(sin cambios)"}')
    print("\n== Otros eventos")
    for e in m.tool("tiempos_eventos", {"segundos": 1800}).get("eventos", []):
        if e.get("tipo") != "nota":
            print(f'   {time.strftime("%H:%M:%S", time.localtime(e["t"] / 1000))} {e["tipo"]:12} {e["texto"]}')
    print("\ncapturas:", salida)

    # --- Dejar todo como estaba (el reloj y el tope son del evento, pero por las dudas) ---
    m.tool("estudio_latencia", {"tope_ms": -1})
    m.tool("video_reloj", {"tipo": "sistema"})
    r = m.tool("evento_cambiar", {"nombre": original})
    print("volver a «%s»: %s" % (original, r.get("_texto", "")[:100]))
    m.cerrar()
    time.sleep(8)
    m, ab = conectar(a.ba_mcp)
    if ab == original:
        t.m = m
        t.paso("borrar el evento de prueba", m.tool("evento_borrar", {"nombre": prueba}))
        pap = [p for p in m.tool("evento_recuperar").get("papelera", []) if p.get("nombre") == prueba]
        if pap:
            t.paso("sacarlo de la papelera", m.tool("evento_papelera_vaciar", {"id": pap[0]["id"]}))
    else:
        print(f"MAL: lo abierto es «{ab}» (el de prueba queda: borralo a mano)")
        t.fallas += 1
    if a.cerrar:
        m.tool("app_quit")
    m.cerrar()
    print("listo:", "todo bien" if t.fallas == 0 else f"{t.fallas} cosas MAL")
    return 0 if t.fallas == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
