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


def decir(**m) -> None:
    print(json.dumps(m, ensure_ascii=False), flush=True)


def ffmpeg() -> str:
    return os.environ.get("BA_FFMPEG") or shutil.which("ffmpeg") or "ffmpeg"


def ffprobe() -> str:
    return os.environ.get("BA_FFPROBE") or shutil.which("ffprobe") or "ffprobe"


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

# Los tamaños de cámara comunes (ancho, alto): CoreML los compila fijos (el Neural
# Engine no acepta tamaños libres). Una red x4 trabaja con la entrada a la mitad.
TAMANOS = [(768, 576), (704, 576), (720, 576), (720, 480), (640, 480), (960, 540), (1280, 720)]
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
    ml = ct.convert(tr, inputs=[ct.ImageType(name="entrada", shape=formas, color_layout=ct.colorlayout.RGB)],
                    outputs=[ct.ImageType(name="salida", color_layout=ct.colorlayout.RGB)],
                    compute_precision=ct.precision.FLOAT16, minimum_deployment_target=ct.target.macOS14)
    paquete = os.path.join(salida_dir, "modelo.mlpackage")
    shutil.rmtree(paquete, ignore_errors=True)
    ml.save(paquete)
    compilado = os.path.join(salida_dir, "modelo.mlmodelc")
    shutil.rmtree(compilado, ignore_errors=True)
    compile_model(paquete, compilado)
    shutil.rmtree(paquete, ignore_errors=True)
    # Medir: el primer tamaño, en el Neural Engine.
    from PIL import Image

    ml2 = ct.models.CompiledMLModel(compilado, compute_units=ct.ComputeUnit.CPU_AND_NE)
    img = Image.fromarray((np.random.rand(alto0, ancho0, 3) * 255).astype("uint8"))
    for _ in range(2):
        ml2.predict({"entrada": img})
    t = time.time()
    k = 6
    for _ in range(k):
        ml2.predict({"entrada": img})
    ms = round(1000 * (time.time() - t) / k, 1)
    return {"archivo": "modelo.mlmodelc", "tamanos": [[a, h] for a, h in tam], "ms": ms,
            "ms_tamano": [ancho0, alto0]}


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
        if sys.platform == "darwin" and asegurar_coremltools(args.libs):
            decir(ev="progreso", texto="Convirtiendo a CoreML (Neural Engine)…", pct=65)
            try:
                c = a_coreml(d.model, plugin["escala"], bool(receta.get("entrada_mitad")), carpeta, canales)
                plugin["archivos"]["coreml"] = c["archivo"]
                plugin["coreml_tamanos"] = c["tamanos"]
                plugin["ms_coreml"] = c["ms"]
                plugin["ms_coreml_tamano"] = c["ms_tamano"]
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
    if os.path.isdir(entrada):
        # Una carpeta de imágenes: una lista para ffmpeg (concat), una por cuadro.
        fotos = sorted(f for f in os.listdir(entrada) if f.lower().endswith((".png", ".jpg", ".jpeg")))
        if not fotos:
            raise SystemExit("la carpeta no tiene imágenes (.png, .jpg)")
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
        info = subprocess.run([ffprobe(), "-v", "error", *origen, "-select_streams", "v:0", "-show_entries",
                               "stream=width,height,sample_aspect_ratio", "-of", "json", args.uri or args.entrada],
                              capture_output=True, text=True, timeout=60) if args.uri or not os.path.isdir(args.entrada) else None
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


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("comando", choices=["chica", "span", "esrgan", "instalar"])
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
    ap.add_argument("--libs", default="", help="carpeta con PyTorch, spandrel y onnx (la baja la app una vez)")
    a = ap.parse_args()
    if a.libs:
        sys.path.insert(0, a.libs)
    os.makedirs(os.path.dirname(os.path.abspath(a.salida)), exist_ok=True)
    try:
        if a.comando == "chica":
            entrenar_chica(a)
        elif a.comando == "instalar":
            instalar(a)
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
