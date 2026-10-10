"""Laboratorio de superresolución: entrena la «red chica» y convierte SPAN y Real-ESRGAN a ONNX.

Lo corre la app (agent/SuperRes.cpp) con el Python del programa. Las bibliotecas
(PyTorch, spandrel, onnx) se bajan UNA vez a una carpeta propia (portable, no se
pierde al actualizar): se la pasa la app en PYTHONPATH-equivalente (`--libs`).
Los modelos que salen (.onnx) los usa el elemento de GStreamer `baonnxsr`.

Comandos (cada uno escribe su avance como una línea JSON por stdout):

  chica    Entrena la red chica (solo luma, x2) MIRANDO UNA CÁMARA: graba unos
           segundos (`--uri`) o usa imágenes/video (`--entrada`), arma pares
           «la imagen achicada y comprimida -> la original» y aprende a deshacer
           eso. Como aprende lo que hace ESA cámara (su compresión, su ruido, lo
           que mira), el resultado se ajusta a ella. Unos minutos.
  span     Baja SPAN x2 (Apache-2.0, de los autores) y lo convierte.
  esrgan   Baja Real-ESRGAN compacto general x4v3 (BSD-3) y lo convierte.
  instalar Instala un PLUGIN de superresolución desde su receta (--receta, la de
           datos/superres-catalogo.json): baja el archivo y verifica su sha256;
           una «red» la exporta a ONNX (Windows/Linux) y, en macOS, a CoreML
           compilado (.mlmodelc: entrada y salida como imagen, los tamaños de
           cámara comunes, FP16, para el Neural Engine) y mide cuánto tarda; un
           «glsl» (shader de mpv) se guarda tal cual. Deja la carpeta del plugin
           (--salida) con su plugin.json.

Los ONNX tienen entrada `entrada` [1, C, alto, ancho] float 0..1 y salida
`salida` [1, C, alto*f, ancho*f], con alto y ancho libres (los fija baonnxsr).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request

PESOS = {
    "span": {
        "url": "https://objectstorage.us-phoenix-1.oraclecloud.com/n/ax6ygfvpvzka/b/open-modeldb-files/o/2x-spanx2-ch48.pth",
        "archivo": "2x-spanx2-ch48.pth",
        "licencia": "Apache-2.0 (SPAN, Wan et al., github.com/hongyuanyu/SPAN)",
    },
    "esrgan": {
        "url": "https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesr-general-x4v3.pth",
        "archivo": "realesr-general-x4v3.pth",
        "licencia": "BSD-3-Clause (Real-ESRGAN, xinntao)",
    },
}


# Los mensajes a la app, en UTF-8 siempre: en Windows stdout sale en la página de
# códigos del sistema (cp1252) y la app no podía leer «Convirtiendo a ONNX…» (lo
# mostraba crudo, en JSON, y el avance no se veía).
for _f in (sys.stdout, sys.stderr):
    try:
        _f.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass


def decir(**m) -> None:
    print(json.dumps(m, ensure_ascii=False), flush=True)


def ffmpeg() -> str:
    return os.environ.get("BA_FFMPEG") or shutil.which("ffmpeg") or "ffmpeg"


def ffprobe() -> str:
    return os.environ.get("BA_FFPROBE") or shutil.which("ffprobe") or "ffprobe"


def ceder_a_la_app(en_gpu: bool) -> None:
    """Que el laboratorio no le gane a la emisión: prioridad baja y pocos hilos.

    Medido en la PC con AMD (RX 5700 XT, DirectML): entrenando usaba 21,7 de 24
    núcleos (PyTorch toma todos de fábrica y algunas operaciones caen a la CPU).
    Con la app al aire en la misma máquina eso traba el programa. En la GPU,
    la CPU solo prepara datos y hace lo que la GPU no: con un cuarto alcanza."""
    import torch

    n = os.cpu_count() or 4
    torch.set_num_threads(max(2, n // 4 if en_gpu else n // 2))
    try:
        if sys.platform == "win32":
            import ctypes

            BELOW_NORMAL_PRIORITY_CLASS = 0x4000
            ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS)
        else:
            os.nice(10)
    except Exception:  # noqa: BLE001  (sin prioridad baja se sigue igual)
        pass


def dispositivo():
    import torch

    if torch.cuda.is_available():
        return torch.device("cuda"), "cuda"
    if getattr(torch.backends, "mps", None) and torch.backends.mps.is_available():
        return torch.device("mps"), "mps (GPU de la Mac)"
    try:
        import torch_directml  # noqa: F401  (Windows, si se instaló)

        return torch_directml.device(), "directml"
    except Exception:  # noqa: BLE001
        return torch.device("cpu"), "cpu"


# --- Exportar ---------------------------------------------------------------------------

def exportar(modelo, canales: int, salida: str, descripcion: dict) -> None:
    import torch

    modelo = modelo.eval().cpu()
    x = torch.rand(1, canales, 64, 64)
    tmp = salida + ".tmp"
    kw = dict(
        input_names=["entrada"],
        output_names=["salida"],
        dynamic_axes={"entrada": {2: "alto", 3: "ancho"}, "salida": {2: "alto_s", 3: "ancho_s"}},
        opset_version=17,
    )
    try:
        torch.onnx.export(modelo, (x,), tmp, dynamo=False, **kw)
    except TypeError:  # torch viejo: sin `dynamo`
        torch.onnx.export(modelo, (x,), tmp, **kw)
    os.replace(tmp, salida)
    with open(salida + ".json", "w", encoding="utf-8") as f:
        json.dump(descripcion, f, ensure_ascii=False, indent=1)


def exportar_nv12(modelo, canales: int, escala: int, salida: str) -> None:
    """La red ENVUELTA para baonnxsr: entra y sale la imagen como la tiene
    GStreamer (NV12, uint8: `y` [1,1,alto,ancho] y `uv` [1,1,alto/2,ancho]
    con U y V intercalados) y todo lo demás pasa ADENTRO, en la placa: el
    color (BT.709, rango limitado), la mitad para una red x4, el agrandado del
    color de una red de luma y la vuelta a NV12 al doble. Por la memoria viaja
    1,5 byte por píxel en vez de 12 (float RGB) y la CPU no toca un píxel."""
    import torch
    import torch.nn.functional as F

    class Envuelta(torch.nn.Module):
        def __init__(self, red):
            super().__init__()
            self.red = red

        def forward(self, y, uv):
            yf = (y.float() - 16.0) / 219.0
            h2, w = uv.shape[2], uv.shape[3]
            c = uv.float().reshape(1, 1, h2, w // 2, 2)
            u = (c[..., 0] - 128.0) / 224.0
            v = (c[..., 1] - 128.0) / 224.0
            if canales == 1:
                out = self.red(yf).clamp(0, 1)  # luma al doble
                # El color al doble, aparte (el ojo ve el detalle en la luma).
                u2 = F.interpolate(u, scale_factor=2.0, mode="bilinear", align_corners=False)
                v2 = F.interpolate(v, scale_factor=2.0, mode="bilinear", align_corners=False)
                y2 = out
            else:
                if escala == 4:
                    # x4: la red recibe la mitad (la luma promediada; el color ya está a la mitad).
                    yy = F.avg_pool2d(yf, 2)
                    uu, vv = u, v
                else:
                    yy = yf
                    uu = F.interpolate(u, scale_factor=2.0, mode="bilinear", align_corners=False)
                    vv = F.interpolate(v, scale_factor=2.0, mode="bilinear", align_corners=False)
                rgb = torch.cat([yy + 1.5748 * vv, yy - 0.1873 * uu - 0.4681 * vv, yy + 1.8556 * uu], 1).clamp(0, 1)
                o = self.red(rgb).clamp(0, 1)  # x2 de la original
                r, g, b = o[:, 0:1], o[:, 1:2], o[:, 2:3]
                y2 = 0.2126 * r + 0.7152 * g + 0.0722 * b
                med = F.avg_pool2d(o, 2)  # el color a la mitad de la salida (4:2:0)
                r, g, b = med[:, 0:1], med[:, 1:2], med[:, 2:3]
                u2 = -0.1146 * r - 0.3854 * g + 0.5 * b
                v2 = 0.5 * r - 0.4542 * g - 0.0458 * b
            yo = (y2 * 219.0 + 16.0).round().clamp(0, 255).to(torch.uint8)
            uo = (u2 * 224.0 + 128.0).round().clamp(0, 255)
            vo = (v2 * 224.0 + 128.0).round().clamp(0, 255)
            uvo = torch.stack([uo, vo], -1).reshape(1, 1, uo.shape[2], uo.shape[3] * 2).to(torch.uint8)
            return yo, uvo

    m = Envuelta(modelo.eval().cpu()).eval()
    y = torch.randint(16, 235, (1, 1, 64, 64), dtype=torch.uint8)
    uv = torch.randint(16, 240, (1, 1, 32, 64), dtype=torch.uint8)
    tmp = salida + ".tmp"
    kw = dict(
        input_names=["y", "uv"],
        output_names=["y2", "uv2"],
        dynamic_axes={"y": {2: "alto", 3: "ancho"}, "uv": {2: "alto_c", 3: "ancho"},
                      "y2": {2: "alto_s", 3: "ancho_s"}, "uv2": {2: "alto_cs", 3: "ancho_s"}},
        opset_version=17,
    )
    with torch.no_grad():
        try:
            torch.onnx.export(m, (y, uv), tmp, dynamo=False, **kw)
        except TypeError:
            torch.onnx.export(m, (y, uv), tmp, **kw)
    os.replace(tmp, salida)


# --- Modelos de terceros ----------------------------------------------------------------

def bajar(clave: str, carpeta: str) -> str:
    p = PESOS[clave]
    ruta = os.path.join(carpeta, p["archivo"])
    if not os.path.exists(ruta):
        decir(ev="progreso", texto=f"Bajando {p['archivo']}…", pct=5)
        req = urllib.request.Request(p["url"], headers={"User-Agent": "BroadcastAppliance"})
        with urllib.request.urlopen(req, timeout=120) as r, open(ruta + ".parte", "wb") as f:
            shutil.copyfileobj(r, f)
        os.replace(ruta + ".parte", ruta)
    return ruta


def convertir(clave: str, salida: str, carpeta: str) -> None:
    import spandrel

    ruta = bajar(clave, carpeta)
    sha = hashlib.sha256(open(ruta, "rb").read()).hexdigest()
    decir(ev="progreso", texto="Convirtiendo a ONNX…", pct=50)
    d = spandrel.ModelLoader().load_from_file(ruta)
    canales = d.input_channels
    exportar(d.model, canales, salida, {
        "nombre": {"span": "SPAN x2", "esrgan": "Real-ESRGAN compacto x4"}[clave],
        "arquitectura": d.architecture.name, "escala": d.scale, "canales": canales,
        "pesos": PESOS[clave]["url"], "sha256_pesos": sha, "licencia": PESOS[clave]["licencia"],
    })
    decir(ev="listo", modelo=salida, escala=d.scale, canales=canales, arquitectura=d.architecture.name)


# --- Plugins (recetas del catálogo) ---------------------------------------------------------

# Los tamaños de cámara (ancho, alto): CoreML los compila fijos (el Neural Engine
# no acepta tamaños libres). Primero los de las cámaras del estudio (--tamanos,
# los pasa la app), después los comunes. Una red x4 trabaja con la entrada a la mitad.
TAMANOS = [(768, 576), (704, 576), (720, 576), (720, 480), (640, 480), (640, 360), (352, 288),
           (800, 600), (1024, 576), (960, 540), (1280, 720)]


def sumar_tamanos(texto: str) -> None:
    """'1024x576,800x600' -> al principio de TAMANOS (pares: NV12)."""
    global TAMANOS
    propios = []
    for t in (texto or "").split(","):
        try:
            a, b = (int(x) for x in t.lower().split("x"))
        except ValueError:
            continue
        if 16 <= a <= 4096 and 16 <= b <= 4096:
            propios.append((a - a % 2, b - b % 2))
    TAMANOS = list(dict.fromkeys(propios + TAMANOS))
COREMLTOOLS = "coremltools==9.0"


def bajar_receta(origen: dict, carpeta: str) -> str:
    """Baja el archivo de la receta (verificado por sha256) y, si es un zip, saca `miembro`."""
    import zipfile

    url, sha = origen["url"], origen["sha256"]
    nombre = os.path.basename(url.split("?")[0])
    ruta = os.path.join(carpeta, nombre)
    if not (os.path.exists(ruta) and hashlib.sha256(open(ruta, "rb").read()).hexdigest() == sha):
        decir(ev="progreso", texto=f"Bajando {nombre}…", pct=5)
        req = urllib.request.Request(url, headers={"User-Agent": "BroadcastAppliance"})
        with urllib.request.urlopen(req, timeout=300) as r, open(ruta + ".parte", "wb") as f:
            shutil.copyfileobj(r, f)
        got = hashlib.sha256(open(ruta + ".parte", "rb").read()).hexdigest()
        if got != sha:
            os.remove(ruta + ".parte")
            raise SystemExit(f"el archivo bajado no es el esperado (sha256 {got[:12]}… en vez de {sha[:12]}…)")
        os.replace(ruta + ".parte", ruta)
    if origen.get("miembro"):
        with zipfile.ZipFile(ruta) as z:
            cand = [n for n in z.namelist() if n.endswith("/" + origen["miembro"]) or n == origen["miembro"]]
            if not cand:
                raise SystemExit(f"no está {origen['miembro']} adentro de {nombre}")
            destino = os.path.join(carpeta, origen["miembro"])
            with z.open(cand[0]) as src, open(destino, "wb") as dst:
                shutil.copyfileobj(src, dst)
        return destino
    return ruta


def asegurar_coremltools(libs: str) -> bool:
    try:
        import coremltools  # noqa: F401

        return True
    except ImportError:
        pass
    if not libs:
        return False
    decir(ev="progreso", texto="Bajando coremltools (una vez)…", pct=55)
    r = subprocess.run([sys.executable, "-m", "pip", "install", "-q", "--disable-pip-version-check",
                        "--no-warn-script-location", "--target", libs, COREMLTOOLS], capture_output=True, text=True)
    if r.returncode != 0:
        decir(ev="aviso", texto="no se pudo bajar coremltools: " + r.stderr[-300:])
        return False
    import importlib

    importlib.invalidate_caches()
    try:
        import coremltools  # noqa: F401

        return True
    except ImportError:
        return False


def a_coreml(modelo, escala: int, mitad: bool, salida_dir: str, canales: int) -> dict:
    """La red a CoreML: imagen RGB 0..255 de entrada y de salida (CoreML convierte el
    color en el Neural Engine o la GPU), los tamaños comunes fijos, FP16. Devuelve
    {archivo, tamanos, ms} (ms: medido con el primer tamaño en el Neural Engine)."""
    import numpy as np
    import torch
    import coremltools as ct
    from coremltools.models.utils import compile_model

    class Envuelto(torch.nn.Module):
        def __init__(self, m):
            super().__init__()
            self.m = m

        def forward(self, x):
            return (self.m(x / 255.0) * 255.0).clamp(0, 255)

    if canales != 3:
        raise SystemExit("CoreML: solo redes RGB (3 canales)")
    w = Envuelto(modelo.eval().float().cpu()).eval()
    tam = [((a // 2, b // 2) if mitad else (a, b)) for a, b in TAMANOS]
    tam = list(dict.fromkeys(tam))  # sin repetidos, en orden
    ancho0, alto0 = tam[0]
    tr = torch.jit.trace(w, torch.rand(1, 3, alto0, ancho0) * 255)
    formas = ct.EnumeratedShapes(shapes=[(1, 3, h, a) for a, h in tam], default=(1, 3, alto0, ancho0))
    from PIL import Image

    # Una imagen de prueba con algo de todo (degradés, bordes, ruido) para
    # comparar CoreML con PyTorch.
    yy, xx = np.mgrid[0:alto0, 0:ancho0]
    prueba = np.stack([xx / ancho0, yy / alto0, ((xx // 16 + yy // 16) % 2) * 0.8 + 0.1], -1)
    prueba = np.clip(prueba + np.random.RandomState(1).normal(0, 0.03, prueba.shape), 0, 1)
    prueba8 = (prueba * 255).astype("uint8")
    with torch.no_grad():
        ref = w(torch.from_numpy(prueba8).permute(2, 0, 1)[None].float()).numpy()[0].transpose(1, 2, 0)

    def convertir(precision):
        return ct.convert(tr, inputs=[ct.ImageType(name="entrada", shape=formas, color_layout=ct.colorlayout.RGB)],
                          outputs=[ct.ImageType(name="salida", color_layout=ct.colorlayout.RGB)],
                          compute_precision=precision, minimum_deployment_target=ct.target.macOS14)

    def psnr(m):
        out = np.asarray(m.predict({"entrada": Image.fromarray(prueba8)})["salida"].convert("RGB")).astype("float64")
        mse = float(np.mean((out - ref) ** 2))
        return 99.0 if mse == 0 else 10 * np.log10(255.0 ** 2 / mse)

    # FP16 (el Neural Engine). Algunas redes (SPAN: normaliza x255 adentro) se
    # desbordan en FP16 y salen con los colores rotos: si se aleja de PyTorch,
    # FP32 (corre en la GPU, más lento pero bien).
    ml = convertir(ct.precision.FLOAT16)
    precision = "fp16"
    try:
        p16 = psnr(ml)
        decir(ev="progreso", texto=f"CoreML FP16 contra PyTorch: {p16:.1f} dB", pct=75)
        if p16 < 32:
            decir(ev="aviso", texto=f"FP16 se aleja ({p16:.1f} dB): se convierte en FP32 (GPU)")
            ml = convertir(ct.precision.FLOAT32)
            precision = "fp32"
            decir(ev="progreso", texto=f"CoreML FP32 contra PyTorch: {psnr(ml):.1f} dB", pct=80)
    except Exception as e:  # noqa: BLE001
        decir(ev="aviso", texto=f"no se pudo comparar con PyTorch: {e}")
    paquete = os.path.join(salida_dir, "modelo.mlpackage")
    shutil.rmtree(paquete, ignore_errors=True)
    ml.save(paquete)
    compilado = os.path.join(salida_dir, "modelo.mlmodelc")
    shutil.rmtree(compilado, ignore_errors=True)
    compile_model(paquete, compilado)
    # El paquete queda: de él salen las variantes para otros tamaños
    # (variantes.py, sin PyTorch).
    # Medir: el primer tamaño, en el Neural Engine (FP32: en la GPU).
    ml2 = ct.models.CompiledMLModel(
        compilado, compute_units=ct.ComputeUnit.CPU_AND_NE if precision == "fp16" else ct.ComputeUnit.CPU_AND_GPU)
    img = Image.fromarray((np.random.rand(alto0, ancho0, 3) * 255).astype("uint8"))
    for _ in range(2):
        ml2.predict({"entrada": img})
    t = time.time()
    k = 6
    for _ in range(k):
        ml2.predict({"entrada": img})
    ms = round(1000 * (time.time() - t) / k, 1)
    return {"archivo": "modelo.mlmodelc", "paquete": "modelo.mlpackage", "tamanos": [[a, h] for a, h in tam], "ms": ms,
            "ms_tamano": [ancho0, alto0], "precision": precision}


def instalar(args) -> None:
    receta = json.loads(open(args.receta, encoding="utf-8").read())
    carpeta = os.path.abspath(args.salida)
    os.makedirs(carpeta, exist_ok=True)
    plugin = dict(receta)
    plugin["instalado"] = time.strftime("%Y-%m-%d %H:%M")
    archivo = bajar_receta(receta["origen"], carpeta)
    plugin["archivos"] = {}
    if receta["tipo"] == "glsl":
        destino = os.path.join(carpeta, "shader.glsl")
        if os.path.abspath(archivo) != destino:
            shutil.copyfile(archivo, destino)
        plugin["archivos"]["glsl"] = "shader.glsl"
    elif receta["tipo"] == "red":
        import spandrel

        decir(ev="progreso", texto="Cargando la red…", pct=30)
        d = spandrel.ModelLoader().load_from_file(archivo)
        canales = d.input_channels
        plugin.update({"arquitectura": d.architecture.name, "canales": canales})
        if d.scale != receta.get("escala", d.scale):
            decir(ev="aviso", texto=f"la red agranda x{d.scale} (la receta decía x{receta.get('escala')})")
            plugin["escala"] = d.scale
        decir(ev="progreso", texto="Exportando a ONNX…", pct=45)
        exportar(d.model, canales, os.path.join(carpeta, "modelo.onnx"), {"nombre": receta["nombre"]})
        os.remove(os.path.join(carpeta, "modelo.onnx.json"))
        plugin["archivos"]["onnx"] = "modelo.onnx"
        decir(ev="progreso", texto="Exportando la versión NV12 (todo en la placa)…", pct=55)
        try:
            exportar_nv12(d.model, canales, plugin.get("escala", d.scale), os.path.join(carpeta, "modelo-nv12.onnx"))
            plugin["archivos"]["onnx_nv12"] = "modelo-nv12.onnx"
        except Exception as e:  # noqa: BLE001
            decir(ev="aviso", texto=f"la versión NV12 no se pudo: {type(e).__name__}: {e}")
        if sys.platform == "darwin" and asegurar_coremltools(args.libs):
            decir(ev="progreso", texto="Convirtiendo a CoreML (Neural Engine)…", pct=65)
            try:
                c = a_coreml(d.model, plugin["escala"], bool(receta.get("entrada_mitad")), carpeta, canales)
                plugin["archivos"]["coreml"] = c["archivo"]
                plugin["archivos"]["coreml_paquete"] = c["paquete"]
                plugin["coreml_tamanos"] = c["tamanos"]
                plugin["ms_coreml"] = c["ms"]
                plugin["ms_coreml_tamano"] = c["ms_tamano"]
                plugin["coreml_precision"] = c.get("precision", "fp16")
            except Exception as e:  # noqa: BLE001
                decir(ev="aviso", texto=f"CoreML no se pudo: {type(e).__name__}: {e}")
        if os.path.abspath(archivo).startswith(carpeta):
            os.remove(archivo)  # los pesos originales ya no hacen falta
    else:
        raise SystemExit(f"tipo de plugin desconocido: {receta['tipo']}")
    with open(os.path.join(carpeta, "plugin.json"), "w", encoding="utf-8") as f:
        json.dump(plugin, f, ensure_ascii=False, indent=1)
    decir(ev="listo", plugin=receta["id"], carpeta=carpeta, archivos=plugin["archivos"], ms_coreml=plugin.get("ms_coreml"))


# --- La red chica ------------------------------------------------------------------------

def red_chica(ancho_canales: int = 24, capas: int = 2):
    """Luma x2: unas pocas convoluciones al tamaño de entrada, PixelShuffle, y se SUMA a
    la imagen agrandada bilineal (la red solo aprende el detalle que falta). ~11 mil
    operaciones por píxel de entrada: unos ms en una GPU, decenas en un procesador."""
    import torch
    from torch import nn

    class RedChica(nn.Module):
        def __init__(self):
            super().__init__()
            c = ancho_canales
            mods = [nn.Conv2d(1, c, 3, padding=1), nn.ReLU(inplace=True)]
            for _ in range(capas):
                mods += [nn.Conv2d(c, c, 3, padding=1), nn.ReLU(inplace=True)]
            mods += [nn.Conv2d(c, 4, 3, padding=1), nn.PixelShuffle(2)]
            self.cuerpo = nn.Sequential(*mods)

        def forward(self, x):
            base = torch.nn.functional.interpolate(x, scale_factor=2, mode="bilinear", align_corners=False)
            return base + self.cuerpo(x)

    return RedChica()


def leer_gris(archivo: str, w: int, h: int):
    import numpy as np

    datos = np.fromfile(archivo, dtype=np.uint8)
    n = datos.size // (w * h)
    return datos[: n * w * h].reshape(n, h, w)


def juntar_cuadros(args, tmp: str):
    """Los cuadros de la cámara (alta) y los mismos achicados y comprimidos (baja)."""
    entrada = args.uri or args.entrada
    if not entrada:
        raise SystemExit("falta --uri (una cámara) o --entrada (un video o una carpeta de imágenes)")
    origen = ["-rtsp_transport", "tcp"] if entrada.startswith("rtsp") else []
    primera = None  # con una carpeta, su primera imagen (de ahí sale el tamaño)
    if os.path.isdir(entrada):
        # Una carpeta de imágenes: una lista para ffmpeg (concat), una por cuadro.
        fotos = sorted(f for f in os.listdir(entrada) if f.lower().endswith((".png", ".jpg", ".jpeg")))
        if not fotos:
            raise SystemExit("la carpeta no tiene imágenes (.png, .jpg)")
        primera = os.path.join(os.path.abspath(entrada), fotos[0])
        lista = os.path.join(tmp, "lista.txt")
        with open(lista, "w", encoding="utf-8") as f:
            for n in fotos:
                f.write("file '" + os.path.join(os.path.abspath(entrada), n).replace("'", "'\\''") + "'\nduration 0.25\n")
        origen += ["-f", "concat", "-safe", "0"]
        entrada = lista
        args.cuadros_por_segundo = 4
        args.segundos = max(args.segundos, len(fotos) // 4 + 2)
    alta = os.path.join(tmp, "alta.gray")
    decir(ev="progreso", texto=f"Grabando {args.segundos} s de la cámara…" if args.uri else "Leyendo las imágenes…", pct=2)
    # Píxel cuadrado (como lo recibe la superresolución en la app), en gris (luma).
    vf = f"fps={args.cuadros_por_segundo},scale=trunc(iw*sar/2)*2:trunc(ih/2)*2,setsar=1,format=gray"
    cmd = [ffmpeg(), "-v", "error", *origen, "-i", entrada, "-t", str(args.segundos), "-vf", vf, "-f", "rawvideo", alta]
    subprocess.run(cmd, check=True, timeout=args.segundos + 60)
    # El tamaño: lo dice ffprobe de un cuadro.
    w, h = args.ancho, args.alto
    if not (w and h):
        # De una carpeta (una cámara local: la app guarda cuadros), el tamaño de su
        # primera imagen. Antes no se preguntaba nada en ese caso y fallaba con
        # «'NoneType' object has no attribute 'stdout'».
        objetivo = primera or args.uri or args.entrada
        info = subprocess.run([ffprobe(), "-v", "error", *([] if primera else origen), "-select_streams", "v:0",
                               "-show_entries", "stream=width,height,sample_aspect_ratio", "-of", "json", objetivo],
                              capture_output=True, text=True, timeout=60)
        if info.returncode != 0 or not info.stdout.strip():
            raise SystemExit("no se pudo saber el tamaño de los cuadros: " + (info.stderr or "ffprobe no contestó").strip()[:200])
        st = json.loads(info.stdout)["streams"][0]
        sar = st.get("sample_aspect_ratio", "1:1")
        a, b = (int(x) for x in sar.split(":")) if ":" in sar and sar != "0:1" else (1, 1)
        w, h = (int(st["width"] * a / b) // 2) * 2, (st["height"] // 2) * 2
    altas = leer_gris(alta, w, h)
    if len(altas) < 4:
        raise SystemExit("llegaron muy pocos cuadros de la cámara")
    decir(ev="progreso", texto=f"{len(altas)} cuadros de {w}x{h}; armando la versión achicada y comprimida…", pct=8)
    # Lo que la red tiene que deshacer: achicar a la mitad y comprimir con H.264
    # (como una cámara IP). Ese es el «antes» de cada «después».
    baja_mkv = os.path.join(tmp, "baja.mkv")
    subprocess.run([ffmpeg(), "-v", "error", "-f", "rawvideo", "-pix_fmt", "gray", "-s", f"{w}x{h}", "-r", "4", "-i", alta,
                    "-vf", f"scale={w // 2}:{h // 2}:flags=area,format=yuv420p", "-c:v", "libx264", "-crf", str(args.crf),
                    "-preset", "veryfast", "-y", baja_mkv], check=True, timeout=600)
    baja = os.path.join(tmp, "baja.gray")
    subprocess.run([ffmpeg(), "-v", "error", "-i", baja_mkv, "-vf", "format=gray", "-f", "rawvideo", "-y", baja], check=True, timeout=600)
    bajas = leer_gris(baja, w // 2, h // 2)
    n = min(len(altas), len(bajas))
    return altas[:n], bajas[:n], (w, h)


def entrenar_chica(args) -> None:
    import numpy as np
    import torch

    tmp = tempfile.mkdtemp(prefix="ba-superres-")
    try:
        altas, bajas, (w, h) = juntar_cuadros(args, tmp)
    except subprocess.CalledProcessError as e:
        raise SystemExit(f"ffmpeg no pudo leer la cámara ({e.returncode})")
    dev, nombre_dev = dispositivo()
    ceder_a_la_app(nombre_dev != "cpu")
    decir(ev="progreso", texto=f"Entrenando en {nombre_dev} ({len(altas)} cuadros)…", pct=10)
    torch.manual_seed(0)
    modelo = red_chica(args.canales, args.capas).to(dev)
    if args.desde and os.path.exists(args.desde + ".pt"):
        modelo.load_state_dict(torch.load(args.desde + ".pt", map_location="cpu"))
        decir(ev="progreso", texto="Sigue desde la red anterior", pct=10)
    opt = torch.optim.Adam(modelo.parameters(), lr=args.lr)
    A = torch.from_numpy(altas.astype(np.float32) / 255.0)
    B = torch.from_numpy(bajas.astype(np.float32) / 255.0)
    # Validación: los últimos cuadros, que no se usan para entrenar.
    nval = max(1, len(A) // 10)
    Av, Bv = A[-nval:], B[-nval:]
    A, B = A[:-nval] if len(A) > nval else A, B[:-nval] if len(B) > nval else B
    p = args.parche
    rng = np.random.default_rng(0)

    def lote(n):
        idx = rng.integers(0, len(A), n)
        ys = rng.integers(0, B.shape[1] - p, n)
        xs = rng.integers(0, B.shape[2] - p, n)
        lo = torch.stack([B[i, y:y + p, x:x + p] for i, y, x in zip(idx, ys, xs)])[:, None]
        hi = torch.stack([A[i, 2 * y:2 * y + 2 * p, 2 * x:2 * x + 2 * p] for i, y, x in zip(idx, ys, xs)])[:, None]
        if rng.random() < 0.5:
            lo, hi = lo.flip(3), hi.flip(3)
        return lo.to(dev), hi.to(dev)

    def psnr(a, b):
        mse = torch.mean((a - b) ** 2).item()
        return 10 * math.log10(1.0 / max(mse, 1e-10))

    def validar():
        modelo.eval()
        with torch.no_grad():
            lo = Bv[:, None].to(dev)
            hi = Av[:, None].to(dev)
            hh, ww = lo.shape[2] * 2, lo.shape[3] * 2
            out = modelo(lo)[:, :, :hh, :ww].clamp(0, 1)
            base = torch.nn.functional.interpolate(lo, scale_factor=2, mode="bicubic", align_corners=False).clamp(0, 1)
            r = psnr(out, hi[:, :, :hh, :ww]), psnr(base, hi[:, :, :hh, :ww])
        modelo.train()
        return r

    fin = time.time() + args.minutos * 60
    t0 = time.time()
    paso = 0
    ultimo = 0.0
    mejor = None
    while time.time() < fin:
        lo, hi = lote(args.lote)
        out = modelo(lo)
        loss = torch.nn.functional.l1_loss(out, hi)
        opt.zero_grad()
        loss.backward()
        opt.step()
        paso += 1
        if paso % 500 == 0:
            for g in opt.param_groups:
                g["lr"] = max(args.lr * 0.1, g["lr"] * 0.9)
        if time.time() - ultimo > 10:
            ultimo = time.time()
            red, bic = validar()
            mejor = red - bic
            pct = 10 + 85 * min(1.0, (time.time() - t0) / (args.minutos * 60))
            decir(ev="progreso", pct=round(pct), paso=paso, psnr_red=round(red, 2), psnr_bicubico=round(bic, 2),
                  texto=f"Paso {paso}: {red:.2f} dB contra {bic:.2f} dB del agrandado común ({red - bic:+.2f} dB)")
    red, bic = validar()
    torch.save(modelo.state_dict(), args.salida + ".pt")  # para seguir entrenando después
    exportar(modelo, 1, args.salida, {
        "nombre": args.nombre or "Red chica", "arquitectura": "red chica (luma x2)", "escala": 2, "canales": 1,
        "entrenada_con": (args.uri and "una cámara") or args.entrada, "cuadros": int(len(A) + len(Av)), "tamano": [w, h],
        "pasos": paso, "dispositivo": nombre_dev, "psnr_red": round(red, 2), "psnr_bicubico": round(bic, 2),
        "licencia": "propia (entrenada en este equipo)", "fecha": time.strftime("%Y-%m-%d %H:%M"),
    })
    shutil.rmtree(tmp, ignore_errors=True)
    decir(ev="listo", modelo=args.salida, psnr_red=round(red, 2), psnr_bicubico=round(bic, 2), ganancia_db=round(red - bic, 2), pasos=paso)


def destilar(args) -> None:
    """DESTILACIÓN: una red propia chica (la de `chica`, luma x2: corre en vivo
    hasta en la CPU) que aprende a imitar a una grande (el MAESTRO, p. ej.
    Real-ESRGAN, demasiado pesada para algunas máquinas) mirando lo que ve
    ESTA cámara. No hace falta la imagen «verdadera» al doble: el objetivo es lo
    que saca el maestro de cada cuadro (su x4 achicado a x2, que suaviza sus
    artefactos), y la red chica aprende a llegar a eso desde el cuadro tal cual.
    Así sale una red a medida de la cámara, con el estilo del maestro."""
    import numpy as np
    import torch
    import torch.nn.functional as F

    if not args.receta:
        raise SystemExit("destilar: falta --receta (el maestro, del catálogo)")
    receta = json.loads(open(args.receta, encoding="utf-8").read())
    tmp = tempfile.mkdtemp(prefix="ba-destilar-")
    try:
        # Los cuadros de la cámara en color (el maestro trabaja en RGB).
        entrada = args.uri or args.entrada
        if not entrada:
            raise SystemExit("falta --uri (una cámara) o --entrada (un video)")
        origen = ["-rtsp_transport", "tcp"] if entrada.startswith("rtsp") else []
        crudo = os.path.join(tmp, "cuadros.rgb")
        decir(ev="progreso", texto=f"Grabando {args.segundos} s…", pct=2)
        vf = f"fps={args.cuadros_por_segundo},scale=trunc(iw*sar/2)*2:trunc(ih/2)*2,setsar=1,format=rgb24"
        subprocess.run([ffmpeg(), "-v", "error", *origen, "-i", entrada, "-t", str(args.segundos), "-vf", vf,
                        "-f", "rawvideo", crudo], check=True, timeout=args.segundos + 120)
        info = subprocess.run([ffprobe(), "-v", "error", *origen, "-select_streams", "v:0", "-show_entries",
                               "stream=width,height,sample_aspect_ratio", "-of", "json", entrada],
                              capture_output=True, text=True, timeout=60)
        st = json.loads(info.stdout)["streams"][0]
        sar = st.get("sample_aspect_ratio", "1:1")
        a, b = (int(x) for x in sar.split(":")) if ":" in sar and sar != "0:1" else (1, 1)
        w, h = (int(st["width"] * a / b) // 2) * 2, (st["height"] // 2) * 2
        datos = np.fromfile(crudo, dtype=np.uint8)
        n = datos.size // (w * h * 3)
        if n < 4:
            raise SystemExit("llegaron muy pocos cuadros")
        cuadros = datos[: n * w * h * 3].reshape(n, h, w, 3)
        dev, nombre_dev = (torch.device("cpu"), "cpu") if args.cpu else dispositivo()
        ceder_a_la_app(nombre_dev != "cpu")
        # El maestro: su x4 de cada cuadro, achicado a x2 (promedio 2x2).
        decir(ev="progreso", texto=f"El maestro ({receta.get('nombre', receta['id'])}) mira {n} cuadros…", pct=10)
        import spandrel

        pesos = bajar_receta(receta["origen"], args.pesos or tmp)
        maestro = spandrel.ModelLoader().load_from_file(pesos).model.eval().to(dev)
        escala = int(receta.get("escala", 4))
        w709 = torch.tensor([0.2126, 0.7152, 0.0722]).view(1, 3, 1, 1)
        entradas, objetivos = [], []
        with torch.no_grad():
            for i in range(n):
                x = torch.from_numpy(cuadros[i]).permute(2, 0, 1)[None].float() / 255.0
                y = maestro(x.to(dev)).clamp(0, 1).cpu()
                if escala == 4:
                    y = F.avg_pool2d(y, 2)
                entradas.append((x * w709).sum(1, keepdim=True)[0])
                objetivos.append((y * w709).sum(1, keepdim=True)[0])
                if i % 5 == 0:
                    decir(ev="progreso", texto=f"Maestro: {i + 1}/{n}", pct=10 + round(20 * (i + 1) / n))
        del maestro
        B = torch.stack(entradas)[:, 0]   # la cámara (luma)
        A = torch.stack(objetivos)[:, 0]  # lo que saca el maestro, al doble (luma)
        nval = max(1, len(A) // 10)
        Av, Bv = A[-nval:], B[-nval:]
        A, B = (A[:-nval], B[:-nval]) if len(A) > nval else (A, B)
        torch.manual_seed(0)
        modelo = red_chica(args.canales, args.capas).to(dev)
        if args.desde and os.path.exists(args.desde + ".pt"):
            modelo.load_state_dict(torch.load(args.desde + ".pt", map_location="cpu"))
        opt = torch.optim.Adam(modelo.parameters(), lr=args.lr)
        p = args.parche
        rng = np.random.default_rng(0)

        def lote(k):
            idx = rng.integers(0, len(B), k)
            ys = rng.integers(0, B.shape[1] - p, k)
            xs = rng.integers(0, B.shape[2] - p, k)
            lo = torch.stack([B[i, y:y + p, x:x + p] for i, y, x in zip(idx, ys, xs)])[:, None]
            hi = torch.stack([A[i, 2 * y:2 * y + 2 * p, 2 * x:2 * x + 2 * p] for i, y, x in zip(idx, ys, xs)])[:, None]
            if rng.random() < 0.5:
                lo, hi = lo.flip(3), hi.flip(3)
            return lo.to(dev), hi.to(dev)

        def psnr(x, y):
            return 10 * math.log10(1.0 / max(torch.mean((x - y) ** 2).item(), 1e-10))

        def validar():
            modelo.eval()
            with torch.no_grad():
                lo, hi = Bv[:, None].to(dev), Av[:, None].to(dev)
                out = modelo(lo).clamp(0, 1)
                bic = F.interpolate(lo, scale_factor=2, mode="bicubic", align_corners=False).clamp(0, 1)
                r = psnr(out, hi), psnr(bic, hi)
            modelo.train()
            return r

        decir(ev="progreso", texto=f"Aprendiendo del maestro en {nombre_dev}…", pct=30)
        fin = time.time() + args.minutos * 60
        t0, paso, ultimo = time.time(), 0, 0.0
        while time.time() < fin:
            lo, hi = lote(args.lote)
            loss = F.l1_loss(modelo(lo), hi)
            opt.zero_grad()
            loss.backward()
            opt.step()
            paso += 1
            if paso % 500 == 0:
                for g in opt.param_groups:
                    g["lr"] = max(args.lr * 0.1, g["lr"] * 0.9)
            if time.time() - ultimo > 10:
                ultimo = time.time()
                red, bic = validar()
                pct = 30 + 65 * min(1.0, (time.time() - t0) / max(args.minutos * 60, 1))
                decir(ev="progreso", pct=round(pct), paso=paso, parecido_maestro_db=round(red, 2),
                      texto=f"Paso {paso}: {red:.2f} dB de parecido al maestro ({bic:.2f} dB el agrandado común)")
        red, bic = validar()
        torch.save(modelo.state_dict(), args.salida + ".pt")
        exportar(modelo, 1, args.salida, {
            "nombre": args.nombre or f"Destilada de {receta.get('nombre', receta['id'])}",
            "arquitectura": "red chica (luma x2), destilada", "escala": 2, "canales": 1,
            "maestro": receta["id"], "entrenada_con": (args.uri and "una cámara") or args.entrada,
            "cuadros": int(n), "tamano": [w, h], "pasos": paso, "dispositivo": nombre_dev,
            "parecido_maestro_db": round(red, 2), "parecido_bicubico_db": round(bic, 2),
            "licencia": "propia (destilada en este equipo)", "fecha": time.strftime("%Y-%m-%d %H:%M"),
        })
        try:
            exportar_nv12(modelo, 1, 2, args.salida[:-5] + "-nv12.onnx" if args.salida.endswith(".onnx") else args.salida + "-nv12.onnx")
        except Exception as e:  # noqa: BLE001
            decir(ev="aviso", texto=f"la versión NV12 no se pudo: {e}")
        decir(ev="listo", modelo=args.salida, parecido_maestro_db=round(red, 2), parecido_bicubico_db=round(bic, 2), pasos=paso)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("comando", choices=["chica", "span", "esrgan", "instalar", "destilar"])
    ap.add_argument("--receta", default="", help="instalar: el JSON de la receta (del catálogo)")
    ap.add_argument("--salida", required=True)
    ap.add_argument("--pesos", default=None, help="carpeta para los pesos bajados")
    ap.add_argument("--uri", default="")
    ap.add_argument("--entrada", default="")
    ap.add_argument("--ancho", type=int, default=0)
    ap.add_argument("--alto", type=int, default=0)
    ap.add_argument("--segundos", type=int, default=60)
    ap.add_argument("--cuadros-por-segundo", dest="cuadros_por_segundo", type=float, default=4)
    ap.add_argument("--crf", type=int, default=28)
    ap.add_argument("--minutos", type=float, default=5)
    ap.add_argument("--canales", type=int, default=24)
    ap.add_argument("--capas", type=int, default=2)
    ap.add_argument("--parche", type=int, default=48)
    ap.add_argument("--lote", type=int, default=32)
    ap.add_argument("--lr", type=float, default=2e-3)
    ap.add_argument("--desde", default="", help="seguir entrenando esta red (.onnx: usa su .pt)")
    ap.add_argument("--nombre", default="")
    ap.add_argument("--cpu", action="store_true", help="entrenar en la CPU (no calienta la GPU; más lento)")
    ap.add_argument("--tamanos", default="", help="instalar: tamaños de las cámaras (1024x576,800x600) para CoreML")
    ap.add_argument("--libs", default="", help="carpeta con PyTorch, spandrel y onnx (la baja la app una vez)")
    a = ap.parse_args()
    if a.libs:
        sys.path.insert(0, a.libs)
    os.makedirs(os.path.dirname(os.path.abspath(a.salida)), exist_ok=True)
    try:
        if a.comando == "chica":
            entrenar_chica(a)
        elif a.comando == "instalar":
            sumar_tamanos(a.tamanos)
            instalar(a)
        elif a.comando == "destilar":
            destilar(a)
        else:
            convertir(a.comando, a.salida, a.pesos or os.path.dirname(os.path.abspath(a.salida)))
    except SystemExit as e:
        decir(ev="error", error=str(e))
        sys.exit(1)
    except Exception as e:  # noqa: BLE001
        import traceback

        traceback.print_exc(file=sys.stderr)
        decir(ev="error", error=f"{type(e).__name__}: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
