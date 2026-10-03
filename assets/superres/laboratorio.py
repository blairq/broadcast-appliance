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
    ap.add_argument("comando", choices=["chica", "span", "esrgan"])
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
