#!/usr/bin/env python3
"""Prueba de punta a punta de la asamblea por MCP: bancas, tomas, «Banca con la palabra»,
enfocar y debate, en un evento de prueba propio que al final se borra.

Qué hace, con la app corriendo (abierta con `--args --test`, así el MCP no pide aprobación):

  1. Anota el evento abierto y crea uno nuevo de cero («ZZ prueba asamblea <hora>»).
     Abrirlo reinicia la app. Si después de reconectar lo abierto NO es el de prueba,
     corta sin tocar nada (lo del operador no se pisa nunca).
  2. Una cámara virtual de barras de color, seis posiciones (una por cuadrante, presidencia,
     plano general), cinco integrantes con su banca, la toma de cada banca, el recinto y las
     escenas base.
  3. Da la palabra (la escena «Orador» se arma y la cámara NO se mueve), enfoca por banca,
     por nombre (con uno ambiguo) y por posición, y arma un debate de dos bancas de la sala
     (las dos de la misma cámara, a la vez: la escena que hacía caer la app).
  4. Vuelve al evento de antes, borra el de prueba y lo saca de la papelera para siempre.

Las fotos de cada paso quedan en la carpeta que imprime, para mirarlas: en «Orador» tiene
que verse solo el tercio derecho de las barras (magenta, rojo, azul); en «Debate sala», la
banca 1 (blanco, amarillo, cian) a la izquierda y la 4 a la derecha.

Uso:
    tools/test/asamblea-mcp.py
    tools/test/asamblea-mcp.py --ba-mcp dist/BroadcastAppliance.app/Contents/Resources/bin/64bit/ba-mcp
    tools/test/asamblea-mcp.py --cerrar      # al final cierra la app (no dejarla prendida)

Sale con 0 si todo dio lo esperado, 1 si algo falló (lo dice con «MAL»), 2 si cortó antes
de tocar nada.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import time

RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BA_MCP = os.path.join(RAIZ, "dist", "BroadcastAppliance.app", "Contents", "Resources", "bin", "64bit", "ba-mcp")
CAM = "Cámara de prueba"


class Mcp:
    """Un cliente MCP por stdio (ba-mcp hace de puente con la app)."""

    def __init__(self, ba_mcp):
        self.n = 0
        self.p = subprocess.Popen([ba_mcp], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL, text=True)
        self._arrancar()
        self._enviar({"jsonrpc": "2.0", "id": 0, "method": "initialize",
                      "params": {"capabilities": {}, "clientInfo": {"name": "asamblea-mcp", "version": "1"},
                                 "protocolVersion": "2024-11-05"}})
        self._linea()

    def _arrancar(self):
        """Para una subclase que lee de otra forma (el banco de caos: con plazo)."""

    def _linea(self):
        """La próxima línea de ba-mcp ("" si se cortó)."""
        return self.p.stdout.readline()

    def _enviar(self, o):
        self.p.stdin.write(json.dumps(o) + "\n")
        self.p.stdin.flush()

    def tool(self, nombre, args=None):
        self.n += 1
        self._enviar({"jsonrpc": "2.0", "id": self.n, "method": "tools/call",
                      "params": {"name": nombre, "arguments": args or {}}})
        while True:
            linea = self._linea()
            if not linea:
                return {"_error": "sin respuesta", "_texto": "", "_isError": True}
            r = json.loads(linea)
            if r.get("id") != self.n:
                continue
            if "error" in r:
                return {"_error": r["error"], "_texto": str(r["error"]), "_isError": True}
            res = r.get("result", {})
            txt = " ".join(c.get("text", "") for c in res.get("content", []) if c.get("type") == "text")
            d = res.get("structuredContent")
            if d is None:
                try:
                    d = json.loads(txt)
                except ValueError:
                    d = {}
            if not isinstance(d, dict):
                d = {"_valor": d}
            d["_texto"] = txt
            d["_isError"] = bool(res.get("isError"))
            return d

    def cerrar(self):
        self.p.kill()


def conectar(ba_mcp, intentos=60):
    """Espera a que la app conteste (después de un reinicio tarda) y dice qué evento está abierto."""
    for _ in range(intentos):
        try:
            m = Mcp(ba_mcp)
            r = m.tool("eventos_listar")
            if "abierto" in r:
                return m, r["abierto"]
            m.cerrar()
        except (OSError, ValueError):
            pass
        time.sleep(3)
    sys.exit("la app no contesta (¿está abierta con --args --test?)")


class Prueba:
    def __init__(self, m, evento):
        self.m = m
        self.evento = evento
        self.fallas = 0

    def guarda(self):
        """Antes de cada bloque que cambia algo: que siga abierto el de prueba."""
        ab = self.m.tool("eventos_listar").get("abierto")
        if ab != self.evento:
            print(f"CORTO: lo abierto ahora es «{ab}», no el evento de prueba")
            sys.exit(2)

    def paso(self, titulo, r, ok_esperado=True, cumple=None):
        ok = not r.get("_isError") and not r.get("_error")
        bien = ok == ok_esperado and (cumple is None or cumple(r))
        if not bien:
            self.fallas += 1
        print(f"[{'ok ' if bien else 'MAL'}] {titulo}: {(r.get('_texto') or '')[:200]}", flush=True)
        return r

    def foto(self, escena, fotos):
        r = self.m.tool("foto_fuente", {"name": escena})
        # La ruta puede tener espacios («BroadcastAppliance Datos/…»).
        mm = re.search(r"(/[^\n\"]*?\.(?:jpg|png))", r.get("_texto", ""))
        fotos[escena] = mm.group(1) if mm else None
        print(f"  foto «{escena}»: {fotos[escena] or r.get('_texto', '')[:160]}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--ba-mcp", default=BA_MCP)
    ap.add_argument("--cerrar", action="store_true", help="cerrar la app al terminar")
    a = ap.parse_args()

    m, original = conectar(a.ba_mcp)
    print("abierto al empezar:", original)
    prueba = "ZZ prueba asamblea " + time.strftime("%H%M%S")
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
    fotos = {}

    # --- La cámara virtual y sus posiciones --------------------------------------------
    t.guarda()
    t.paso("cámara virtual (barras)", m.tool("video_entrada_virtual", {"nombre": CAM, "preset": "barras", "al_aire": True}))
    time.sleep(4)
    puntos = [("Banca 1", 0.25, 0.3, 3), ("Banca 2", 0.75, 0.3, 3), ("Banca 3", 0.25, 0.75, 3),
              ("Banca 4", 0.75, 0.75, 3), ("Presidencia", 0.5, 0.5, 2), ("Plano general", 0.5, 0.5, 1)]
    for nombre, x, y, z in puntos:
        m.tool("ptz_encuadre", {"entrada": CAM, "x": x, "y": y, "zoom": z, "segundos": 0})
        time.sleep(0.6)
        t.paso("posición " + nombre, m.tool("ptz_posicion_crear", {"entrada": CAM, "nombre": nombre, "comentario": "prueba"}))
    m.tool("ptz_encuadre", {"entrada": CAM, "x": 0.5, "y": 0.5, "zoom": 1, "segundos": 0})

    # --- El cuerpo, las tomas y el recinto -------------------------------------------------
    t.guarda()
    gente = [("Pepito López", "1"), ("Ana López Ruiz", "2"), ("Marta Gómez", "3"), ("Julián Ferreyra", "4"),
             ("Presidenta Ruiz", "P")]
    for nombre, banca in gente:
        t.paso("integrante " + nombre, m.tool("integrante_editar", {"nombre": nombre, "banca": banca, "cargo_oficial": "Consejero/a"}))
    t.paso("integrante con banca repetida (tiene que fallar)", m.tool("integrante_editar", {"nombre": "Otra", "banca": "1"}), False)
    t.paso("y no queda nadie a medias",
           m.tool("cuerpo_listar"), cumple=lambda r: all(i.get("nombre") and i.get("banca") for i in r.get("integrantes", [])))
    for banca in ["1", "2", "3", "4"]:
        t.paso("toma banca " + banca, m.tool("banca_toma", {"banca": banca, "camara": CAM, "posicion": "Banca " + banca}))
    t.paso("toma presidencia", m.tool("banca_toma", {"banca": "P", "camara": CAM, "posicion": "Presidencia"}))
    t.paso("toma de una posición que no existe (tiene que fallar)",
           m.tool("banca_toma", {"banca": "9", "camara": CAM, "posicion": "Nada"}), False)
    t.paso("recinto 2x3", m.tool("recinto_editar", {"filas": 2, "columnas": 3, "auto": True}))
    t.paso("el recinto muestra la toma de cada banca", m.tool("recinto_ver"),
           cumple=lambda r: sum(1 for b in r.get("bancas", []) if (b.get("toma") or {}).get("posicion")) >= 5)

    # --- Las escenas base ---------------------------------------------------------------
    t.guarda()
    t.paso("escenas base", m.tool("asamblea_escenas_base", {"general": CAM, "crear": True}))
    time.sleep(3)
    nombres = [s.get("name") for s in m.tool("scene_list").get("scenes", [])]
    t.paso("están «Orador» y «Debate sala»", {"_texto": ", ".join(n for n in nombres if n)},
           cumple=lambda _: "Asamblea · Orador" in nombres and "Asamblea · Debate sala" in nombres)
    t.foto("Asamblea · Plano general", fotos)

    # --- Dar la palabra: la fuente recorta, la cámara no se mueve -----------------------
    t.guarda()
    pos_antes = [c.get("posicionDigital") for c in m.tool("ptz_camaras", {"entrada": CAM}).get("camaras", [])]
    t.paso("dar la palabra a la banca 2", m.tool("banca_al_aire", {"banca": "2", "modo": "armar"}))
    time.sleep(2.5)
    t.paso("se armó «Asamblea · Orador»", m.tool("scene_list"), cumple=lambda r: r.get("armada") == "Asamblea · Orador")
    pos_despues = [c.get("posicionDigital") for c in m.tool("ptz_camaras", {"entrada": CAM}).get("camaras", [])]
    t.paso("la cámara no se movió", {"_texto": f"{pos_antes} → {pos_despues}"}, cumple=lambda _: pos_antes == pos_despues)
    t.foto("Asamblea · Orador", fotos)

    # --- Enfocar ------------------------------------------------------------------------
    t.guarda()
    for que, ok in [("banca 3", True), ("Julián Ferreyra", True), ("presidencia", True), ("Plano general", True),
                    ("lopez", False), ("banca 9", False)]:
        t.paso(f"enfocar «{que}»" + ("" if ok else " (tiene que fallar)"), m.tool("enfocar", {"que": que}), ok)
        time.sleep(0.8)

    # --- Debate de dos bancas de la sala (la misma cámara dos veces) ---------------------
    t.guarda()
    t.paso("terminar la palabra", m.tool("recinto_orador_terminar"))
    t.paso("debate 1 contra 4", m.tool("debate_iniciar", {"a": "1", "b": "4", "turno": "a"}))
    time.sleep(2.5)
    t.foto("Asamblea · Debate sala", fotos)
    t.paso("la app sigue andando (antes se caía acá)", m.tool("debate_estado"))
    t.paso("terminar el debate", m.tool("debate_terminar"))
    t.paso("revisión de la asamblea", m.tool("asamblea_revisar"))

    # Las fotos, a una carpeta que no se borra con el evento.
    salida = tempfile.mkdtemp(prefix="asamblea-mcp-")
    for escena, ruta in fotos.items():
        if ruta and os.path.isfile(ruta):
            destino = os.path.join(salida, escena.replace(" · ", "-").replace(" ", "_") + os.path.splitext(ruta)[1])
            with open(ruta, "rb") as f, open(destino, "wb") as g:
                g.write(f.read())
    print("fotos para mirar:", salida)

    # --- Volver y limpiar --------------------------------------------------------------
    r = m.tool("evento_cambiar", {"nombre": original})
    print("volver a «%s»: %s" % (original, r.get("_texto", "")[:120]))
    m.cerrar()
    time.sleep(8)
    m, ab = conectar(a.ba_mcp)
    if ab != original:
        print(f"MAL: después de volver, lo abierto es «{ab}» (el de prueba queda: borralo a mano)")
        t.fallas += 1
    else:
        t.m = m
        t.paso("borrar el evento de prueba", m.tool("evento_borrar", {"nombre": prueba}))
        pap = [p for p in m.tool("evento_recuperar").get("papelera", []) if p.get("nombre") == prueba]
        if pap:
            t.paso("y sacarlo de la papelera", m.tool("evento_papelera_vaciar", {"id": pap[0]["id"]}))
    if a.cerrar:
        m.tool("app_quit")
    m.cerrar()
    print("listo:", "todo bien" if t.fallas == 0 else f"{t.fallas} cosas MAL")
    return 0 if t.fallas == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
