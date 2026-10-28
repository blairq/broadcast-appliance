"""Los moldes de plugins (assets/plugins/ejemplo-*) andan de verdad: cada uno con el probador del SDK.

    python3 -I -B test_ejemplos.py [--gst <raíz de GStreamer>] [--sin-gst]

Lo corre ctest (test_plugins_ejemplos, cmake/BaPython.cmake) con el Python del programa
(extern/python). Cada ejemplo se copia a una carpeta temporal (el probador y el plugin escriben
datos/ y .panel.json: nada en el árbol de fuentes) y se prueba como lo hace `plugin_probar`.

`--gst`: la raíz de GStreamer (lib/girepository-1.0, lib/gstreamer-1.0), para armar el mismo
entorno que la app le pasa a un plugin (platform/GstEntorno). `--sin-gst`: este Python no trae los
bindings (Linux sin el sitio de gstreamer-linux): salta el ejemplo de GStreamer y lo dice.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

AQUI = os.path.dirname(os.path.abspath(__file__))
SDK = os.path.dirname(AQUI)
RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(SDK)))
EJEMPLOS = os.path.join(RAIZ, "assets", "plugins")
PROBAR = os.path.join(SDK, "ba_plugin", "probar.py")

fallas: list[str] = []


def chequear(cond: bool, que: str) -> None:
    print(("ok    " if cond else "FALLA ") + que, flush=True)
    if not cond:
        fallas.append(que)


def probar(ejemplo: str, *extra: str) -> dict:
    tmp = tempfile.mkdtemp(prefix="ba-ejemplo-")
    try:
        dst = os.path.join(tmp, ejemplo)
        shutil.copytree(os.path.join(EJEMPLOS, ejemplo), dst,
                        ignore=shutil.ignore_patterns("__pycache__", "_libs", "datos", ".panel.json"))
        r = subprocess.run([sys.executable, "-I", "-B", PROBAR, dst, *extra], capture_output=True, text=True,
                           timeout=120)
        try:
            informe = json.loads(r.stdout)
        except ValueError:
            informe = {"ok": False, "error": "el probador no devolvió JSON", "registro": r.stdout[-2000:] + r.stderr[-2000:]}
        if not informe.get("ok"):
            print(json.dumps({k: informe.get(k) for k in ("error", "tarjetas", "registro")}, ensure_ascii=False,
                             indent=1)[:4000])
        return informe
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def tarjeta(inf: dict, nombre: str) -> dict:
    return next((t for t in inf.get("tarjetas", []) if t.get("tarjeta") == nombre), {})


def control(inf: dict, nombre: str) -> dict:
    return next((c["respuesta"] for c in inf.get("controles_llamados", []) if c.get("control") == nombre), {})


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--gst", default="")
    ap.add_argument("--sin-gst", action="store_true")
    a = ap.parse_args()
    if a.gst:
        g = a.gst
        sep = ";" if os.name == "nt" else ":"
        os.environ["GI_TYPELIB_PATH"] = os.path.join(g, "lib", "girepository-1.0")
        os.environ["GST_PLUGIN_SYSTEM_PATH_1_0"] = os.path.join(g, "lib", "gstreamer-1.0")
        os.environ["GST_PLUGIN_PATH_1_0"] = ""
        escaner = os.path.join(g, "libexec", "gstreamer-1.0", "gst-plugin-scanner")
        if os.path.exists(escaner):
            os.environ["GST_PLUGIN_SCANNER_1_0"] = escaner
        os.environ["GST_REGISTRY_1_0"] = os.path.join(tempfile.gettempdir(), "ba-test-ejemplos-registro.bin")
        if sys.platform.startswith("linux"):
            os.environ["LD_LIBRARY_PATH"] = os.path.join(g, "lib") + sep + os.environ.get("LD_LIBRARY_PATH", "")
        elif sys.platform == "darwin":
            # Los bindings de extern/python buscan GLib donde queda en el BUNDLE (bin/64bit/GStreamer.framework,
            # relativo al Python instalado): fuera del bundle, se les dice dónde está el framework.
            os.environ["DYLD_FALLBACK_LIBRARY_PATH"] = os.path.join(g, "lib")

    # El básico: tarjeta de video, filtro de audio, entrada propia.
    inf = probar("ejemplo-basico", "--segundos", "1")
    chequear(inf.get("ok", False), "ejemplo-basico: el probador da ok")
    chequear(tarjeta(inf, "negativo").get("bytes_cambiados_%", 0) > 50, "ejemplo-basico: «negativo» cambia el cuadro")
    chequear(any(e.get("recibidos", 0) > 0 for e in inf.get("entradas", [])), "ejemplo-basico: la entrada manda cuadros")

    # Video con GStreamer: un elemento propio en Python, una cadena de módulos y un análisis.
    if a.sin_gst:
        print("salteado: ejemplo-video-gst (este Python no trae los bindings de GStreamer)")
    else:
        inf = probar("ejemplo-video-gst")
        chequear(inf.get("ok", False), "ejemplo-video-gst: el probador da ok")
        for t in ("vineta", "efecto", "medir"):
            chequear(not tarjeta(inf, t).get("errores") and "ms_promedio" in tarjeta(inf, t),
                     f"ejemplo-video-gst: «{t}» procesa sin errores")
        chequear(tarjeta(inf, "vineta").get("bytes_cambiados_%", 0) > 20, "ejemplo-video-gst: «vineta» (elemento propio) oscurece")
        antes = tarjeta(inf, "vineta").get("antes_rgba_promedio", [0])[0]
        despues = tarjeta(inf, "vineta").get("despues_rgba_promedio", [0])[0]
        chequear(despues < antes, "ejemplo-video-gst: «vineta» baja el brillo promedio")
        chequear(tarjeta(inf, "efecto").get("bytes_cambiados_%", 0) > 20, "ejemplo-video-gst: «efecto» (cadena de GStreamer) cambia el cuadro")
        chequear(tarjeta(inf, "medir").get("bytes_cambiados_%", 1) == 0, "ejemplo-video-gst: «medir» no toca la imagen")

        # El ejemplo mínimo de la ayuda MCP (help {"topic": "plugins"}, sección «Video») también anda:
        # es lo que copia el asistente cuando le piden un efecto que el grafo no trae.
        ayuda = open(os.path.join(RAIZ, "assets", "idiomas", "es", "ayuda", "plugins.md"), encoding="utf-8").read()
        i = ayuda.find("## Video: un efecto, un filtro")
        i = ayuda.find("```python\n", i)
        j = ayuda.find("```", i + 10)
        chequear(i > 0 and j > i, "ayuda de plugins: tiene el ejemplo mínimo de video")
        if i > 0 and j > i:
            tmp = tempfile.mkdtemp(prefix="ba-ayuda-")
            try:
                carpeta = os.path.join(tmp, "ayuda-video")
                os.makedirs(carpeta)
                with open(os.path.join(carpeta, "plugin.json"), "w", encoding="utf-8") as f:
                    json.dump({"id": "ayuda-video", "nombre": "ayuda", "dependencias": ["numpy"]}, f)
                with open(os.path.join(carpeta, "plugin.py"), "w", encoding="utf-8") as f:
                    f.write(ayuda[i + 10:j])
                r = subprocess.run([sys.executable, "-I", "-B", PROBAR, carpeta], capture_output=True, text=True,
                                   timeout=120)
                inf = json.loads(r.stdout or "{}")
                tj = (inf.get("tarjetas") or [{}])[0]
                chequear(inf.get("ok", False) and tj.get("bytes_cambiados_%", 0) > 20 and not tj.get("errores"),
                         "ayuda de plugins: el ejemplo mínimo de video corre y cambia el cuadro")
                if not inf.get("ok"):
                    print(str(inf.get("registro", ""))[-2000:])
            finally:
                shutil.rmtree(tmp, ignore_errors=True)

    # Automatización: el control «simular» recorre una transmisión y avisa al operador.
    inf = probar("ejemplo-automatizacion", "--control", "simular", '{"escenas": ["General", "Orador", "General"]}')
    chequear(inf.get("ok", False), "ejemplo-automatizacion: el probador da ok")
    chequear(control(inf, "simular").get("ok", False), "ejemplo-automatizacion: «simular» contesta")
    llamadas = [x.get("herramienta") for x in inf.get("llamadas_al_sistema", [])]
    chequear("operator_message" in llamadas, "ejemplo-automatizacion: al cortar le avisa al operador")
    chequear(sum(1 for e in inf.get("eventos", []) if e.get("canal") == "escena") == 3,
             "ejemplo-automatizacion: publica cada cambio de escena")

    # Equipo por la red: contra su propio simulador.
    inf = probar("ejemplo-equipo-red", "--control", "luz", '{"n": 2, "nivel": 70}',
                 "--control", "orden", '{"orden": "ESTADO?"}')
    chequear(inf.get("ok", False), "ejemplo-equipo-red: el probador da ok")
    chequear(control(inf, "luz").get("resultado", {}).get("respuesta") == "OK LUZ 2 70",
             "ejemplo-equipo-red: «luz» le llega al equipo (simulador)")
    chequear("luz2=70" in str(control(inf, "orden").get("resultado", {}).get("respuesta", "")),
             "ejemplo-equipo-red: el estado del equipo trae la luz puesta")

    # Un tipo de documento de identidad (la licencia AAMVA): sus ejemplos SINTÉTICOS dan lo esperado
    # (lee las dos licencias, no lee un DNI argentino ni un QR).
    inf = probar("documento-aamva")
    chequear(inf.get("ok", False), "documento-aamva: el probador da ok")
    doc = next((d for d in inf.get("documentos", []) if d.get("nombre") == "licencia"), {})
    ejs = doc.get("ejemplos", [])
    chequear(len(ejs) == 4 and all("error" not in e for e in ejs), "documento-aamva: los cuatro ejemplos dan lo esperado")
    datos = (ejs[0].get("datos") or {}) if ejs else {}
    chequear(datos.get("numero") == "E1234567" and datos.get("nacimiento") == "1990-03-15" and datos.get("sexo") == "F",
             "documento-aamva: número, nacimiento (MMDDAAAA de EE.UU.) y sexo")
    chequear(len(ejs) > 1 and (ejs[1].get("datos") or {}).get("nacimiento") == "1985-07-04",
             "documento-aamva: la fecha de Canadá (AAAAMMDD)")

    print(f"\n{len(fallas)} fallas" if fallas else "\ntodo bien")
    return 1 if fallas else 0


if __name__ == "__main__":
    sys.exit(main())
