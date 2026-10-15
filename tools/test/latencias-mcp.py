#!/usr/bin/env python3
"""Prueba de Latencias del pipeline por MCP: se agregan módulos de a uno y se mira cómo
cambian los tiempos, en un evento de prueba propio que al final se borra.

Con la app corriendo (abierta con `--args --test`):

  1. Crea un evento de cero («ZZ prueba latencias <hora>») y lo abre. Si lo abierto no es
     ese, corta sin tocar nada.
  2. Una cámara virtual (pelota en movimiento) y, de a un paso por vez:
       superresolución → seguimiento (Seguir) → sincronía → micrófono → un compresor
       en su rack → voz a texto.
     En cada paso: marca una nota en la línea de tiempo (tiempos_evento_agregar), espera,
     anota lo que agrega cada nodo (tiempos_mapa) y saca una captura de la página.
  3. Al final, el impacto de cada nota (tiempos_evento_impacto), vuelve al evento de antes
     y borra el de prueba (y lo saca de la papelera).

Las capturas quedan en la carpeta que imprime.

    tools/test/latencias-mcp.py [--cerrar] [--espera 15]
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

CAM = "Cámara de prueba"
MIC = "Micrófono de prueba"


def resumen(mapa):
    filas = []
    for n in mapa.get("nodos", []):
        if n.get("activo") is False:
            continue
        ms = n.get("ms")
        filas.append(f'   {n["id"][:46]:46} {("—" if ms is None else str(ms) + " ms"):>10}  {n.get("fuente")}')
    return "\n".join(filas)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ba-mcp", default=BA_MCP)
    ap.add_argument("--cerrar", action="store_true")
    ap.add_argument("--espera", type=int, default=15, help="segundos entre pasos (para que se junte la historia)")
    a = ap.parse_args()

    m, original = conectar(a.ba_mcp)
    prueba = "ZZ prueba latencias " + time.strftime("%H%M%S")
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
    salida = tempfile.mkdtemp(prefix="latencias-mcp-")
    m.tool("ui_show_page", {"page": "latencias"})

    def paso(n, titulo, accion):
        t.guarda()
        print(f"\n== {n}. {titulo}")
        if accion is not None:
            accion()
        m.tool("tiempos_evento_agregar", {"texto": titulo})
        time.sleep(a.espera)
        mapa = m.tool("tiempos_mapa")
        print(resumen(mapa))
        r = m.tool("ventana_captura")
        mm = re.search(r"(/[^\n\"]*?\.(?:jpg|png))", r.get("_texto", ""))
        if mm and os.path.isfile(mm.group(1)):
            shutil.copy(mm.group(1), os.path.join(salida, f"{n:02d}-{re.sub(r'[^a-z0-9]+', '-', titulo.lower())}.jpg"))
        return mapa

    paso(1, "Cámara sola", lambda: t.paso("cámara virtual", m.tool("video_entrada_virtual", {"nombre": CAM, "preset": "pelota", "al_aire": True})))
    m.tool("tiempos_linea", {"nodos": ["entrada:" + CAM, "escena:programa"]})
    paso(2, "Superresolución", lambda: t.paso("superres", m.tool("superres_tarjeta", {"entrada": CAM, "motor": "apple"})))
    for n in m.tool("tiempos_mapa").get("nodos", []):
        if n["id"].startswith("tarjeta:" + CAM):
            m.tool("tiempos_linea", {"agregar": n["id"]})
    paso(3, "Seguimiento", lambda: t.paso("seguir", m.tool("video_tarjeta_agregar", {"entrada": CAM, "tipo": "seguir"})))
    paso(4, "Sincronía", lambda: t.paso("sincronía", m.tool("video_tarjeta_agregar", {"entrada": CAM, "tipo": "sincro"})))

    def micro():
        # El micrófono de la máquina (solo se captura: no suena nada por los parlantes).
        entradas = m.tool("audio_devices").get("inputs", [])
        texto = lambda d: " ".join(str(v) for v in d.values()) if isinstance(d, dict) else str(d)
        elegido = next((d for d in entradas if "micr" in texto(d).lower()), entradas[0] if entradas else {})
        valor = elegido.get("value", "") if isinstance(elegido, dict) else str(elegido)
        t.paso(f"micrófono ({texto(elegido)[:60]})", m.tool("audio_input_add", {"device": valor, "name": MIC}))
    paso(5, "Micrófono", micro)

    def compresor():
        mods = json.dumps(m.tool("audio_modules"), ensure_ascii=False)
        cands = re.findall(r'"(?:id|module)":\s*"([^"]*compress[^"]*)"', mods, re.I)
        t.paso(f"compresor ({cands[0] if cands else '?'})", m.tool("audio_module_add", {"source": MIC, "module": cands[0] if cands else "compressor"}))
    paso(6, "Compresor en el rack", compresor)
    paso(7, "Voz a texto", lambda: t.paso("voz a texto", m.tool("voz_texto", {"fuente": MIC, "modo": "continuo", "nombre": "Prueba"})))

    print("\n== Impacto de cada paso (lo que más cambió)")
    for e in m.tool("tiempos_eventos", {"segundos": 1800}).get("eventos", []):
        if e.get("tipo") != "nota":
            continue
        imp = m.tool("tiempos_evento_impacto", {"t_ms": e["t"], "segundos": max(3, a.espera // 2)}).get("nodos", [])
        cambios = ", ".join(f'{x["titulo"]} {x["cambio_ms"]:+.1f}' for x in imp[:3] if abs(x["cambio_ms"]) >= 0.5)
        print(f'   {e["texto"]:24} {cambios or "(sin cambios)"}')
    print("\ncapturas:", salida)

    # --- Volver y limpiar ---------------------------------------------------------------
    m.tool("voz_texto", {"fuente": MIC, "modo": ""})  # que no siga transcribiendo
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
