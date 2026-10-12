"""
assets/superres/runtime_gpu.py — baja el ONNX Runtime de GPU para esta placa

El programa trae un ONNX Runtime que corre en cualquier lado (CPU; DirectML en
Windows: AMD —también las integradas Ryzen/Vega/RDNA—, NVIDIA e Intel; CoreML
en la Mac). Para sacarle todo a una NVIDIA (CUDA + TensorRT, que compila la red
para ESA placa) o a una AMD en Linux (ROCm/MIGraphX) o a una Intel (OpenVINO)
hace falta otro ONNX Runtime y sus bibliotecas: pesan cientos de MB a varios
GB, así que se bajan a pedido, acá, a Datos/Herramientas/onnxruntime-gpu/lib.
baonnxsr lo carga en vez del del programa (variable BA_ORT_GPU, la pone la app).

De dónde: PyPI (las ruedas oficiales de Microsoft y NVIDIA), versiones FIJAS;
el sha256 de cada archivo se compara con el que publica PyPI. Solo se sacan
las bibliotecas (.dll / .so*) y van todas juntas a una carpeta.

  tipo     placa                                    qué baja
  nvidia   NVIDIA (CUDA 13: desde Turing hasta      onnxruntime-gpu + CUDA runtime,
           Blackwell, p. ej. RTX 5060)              cuBLAS, cuFFT, cuRAND, cuDNN 9
                                                    y TensorRT (opcional, --sin-tensorrt)
  amd      AMD en Linux (necesita ROCm 6.4 del      onnxruntime-rocm
           sistema: el controlador y su runtime)
  intel    Intel (GPU integrada / Arc / NPU)        onnxruntime-openvino + OpenVINO

Uso: python runtime_gpu.py --tipo nvidia --salida <Datos/Herramientas/onnxruntime-gpu>
Habla en JSON por línea (como el laboratorio): {"ev": "progreso"|"listo"|"error", ...}
"""

import argparse
import hashlib
import json
import os
import platform
import shutil
import sys
import tempfile
import urllib.request
import zipfile

# Versiones fijas (cambiar acá y probar en la placa).
PAQUETES = {
    "nvidia": [
        ("onnxruntime-gpu", "1.30.0"),
        ("nvidia-cuda-runtime", "13.4.92"),
        ("nvidia-cuda-nvrtc", "13.4.92"),
        ("nvidia-nvjitlink", "13.4.92"),
        ("nvidia-cublas", "13.8.1.7"),
        ("nvidia-cufft", "12.4.0.43"),
        ("nvidia-curand", "10.4.4.72"),
        ("nvidia-cudnn-cu13", "9.27.0.42"),
        ("tensorrt-cu13-libs", "11.3.0.99"),
    ],
    "amd": [("onnxruntime-rocm", "1.22.2.post3")],
    "intel": [("onnxruntime-openvino", "1.24.1"), ("openvino", "2026.0.0")],
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


def etiqueta() -> tuple[str, str]:
    """La plataforma de las ruedas: (sistema, máquina)."""
    if sys.platform.startswith("win"):
        return "win", "amd64"
    if sys.platform.startswith("linux"):
        return "linux", "x86_64" if platform.machine() in ("x86_64", "AMD64") else platform.machine()
    raise SystemExit("el ONNX Runtime de GPU es para Windows y Linux (en la Mac, CoreML ya viene)")


def elegir_rueda(paquete: str, version: str) -> dict:
    sistema, maquina = etiqueta()
    with urllib.request.urlopen(f"https://pypi.org/pypi/{paquete}/{version}/json", timeout=60) as r:
        d = json.load(r)
    py = f"cp{sys.version_info.major}{sys.version_info.minor}"
    candidatas = []
    for u in d["urls"]:
        n = u["filename"]
        if not n.endswith(".whl"):
            continue
        plat = n.rsplit("-", 1)[-1]
        if sistema == "win" and "win_amd64" not in plat:
            continue
        if sistema == "linux" and ("manylinux" not in plat or maquina not in plat):
            continue
        # La de nuestro Python, o una que sirve para cualquiera (py3-none).
        if f"-{py}-" in n or "-py3-none-" in n or "-py2.py3-none-" in n:
            candidatas.append(u)
    if not candidatas:
        raise SystemExit(f"{paquete} {version} no tiene rueda para {sistema}/{maquina} ({py})")
    return candidatas[0]


def bajar(u: dict, carpeta: str, n: int, total: int) -> str:
    destino = os.path.join(carpeta, u["filename"])
    esperado = u["digests"]["sha256"]
    if os.path.exists(destino) and sha256(destino) == esperado:
        return destino
    decir(ev="progreso", texto=f"Bajando {u['filename']} ({u['size'] // 1048576} MB)…", pct=5 + 80 * n // total)
    tmp = destino + ".part"
    with urllib.request.urlopen(u["url"], timeout=120) as r, open(tmp, "wb") as f:
        shutil.copyfileobj(r, f, 1 << 20)
    if sha256(tmp) != esperado:
        os.remove(tmp)
        raise SystemExit(f"{u['filename']}: el sha256 no coincide con el de PyPI")
    os.replace(tmp, destino)
    return destino


def sha256(archivo: str) -> str:
    h = hashlib.sha256()
    with open(archivo, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def es_biblioteca(nombre: str) -> bool:
    base = os.path.basename(nombre)
    return base.endswith(".dll") or ".so" in base and not base.endswith(".py")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--tipo", choices=sorted(PAQUETES), required=True)
    ap.add_argument("--salida", required=True)
    ap.add_argument("--sin-tensorrt", action="store_true", help="nvidia: sin TensorRT (~2 GB menos; solo CUDA)")
    a = ap.parse_args()
    try:
        paquetes = [p for p in PAQUETES[a.tipo] if not (a.sin_tensorrt and p[0].startswith("tensorrt"))]
        lib = os.path.join(a.salida, "lib")
        descargas = os.path.join(a.salida, "descargas")
        os.makedirs(descargas, exist_ok=True)
        nueva = tempfile.mkdtemp(prefix="lib-", dir=a.salida)
        for i, (p, v) in enumerate(paquetes):
            rueda = bajar(elegir_rueda(p, v), descargas, i, len(paquetes))
            decir(ev="progreso", texto=f"Sacando las bibliotecas de {p}…", pct=5 + 80 * (i + 1) // len(paquetes))
            with zipfile.ZipFile(rueda) as z:
                for m in z.namelist():
                    if es_biblioteca(m) and not m.endswith("/"):
                        destino = os.path.join(nueva, os.path.basename(m))
                        with z.open(m) as src, open(destino, "wb") as dst:
                            shutil.copyfileobj(src, dst, 1 << 20)
        # El núcleo con un nombre fijo (la app lo busca así).
        if sys.platform.startswith("linux"):
            nucleo = sorted(n for n in os.listdir(nueva) if n.startswith("libonnxruntime.so"))
            if not nucleo:
                raise SystemExit("la rueda no trae libonnxruntime.so")
            if "libonnxruntime.so" not in nucleo:
                shutil.copyfile(os.path.join(nueva, nucleo[-1]), os.path.join(nueva, "libonnxruntime.so"))
        elif not os.path.exists(os.path.join(nueva, "onnxruntime.dll")):
            raise SystemExit("la rueda no trae onnxruntime.dll")
        if os.path.isdir(lib):
            shutil.rmtree(lib)
        os.replace(nueva, lib)
        with open(os.path.join(a.salida, "runtime.json"), "w", encoding="utf-8") as f:
            json.dump({"tipo": a.tipo, "paquetes": paquetes}, f, ensure_ascii=False, indent=1)
        shutil.rmtree(descargas, ignore_errors=True)
        decir(ev="listo", tipo=a.tipo, carpeta=lib, nota="reiniciá el programa para que lo use")
    except SystemExit as e:
        decir(ev="error", error=str(e))
        sys.exit(1)
    except Exception as e:  # noqa: BLE001
        decir(ev="error", error=f"{type(e).__name__}: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
