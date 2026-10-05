#!/usr/bin/env python3
"""Variantes de una red de superresolución compiladas para un tamaño fijo.

Una red instalada (Datos/Modelos/superres/<plugin>/) sirve para cualquier
tamaño, pero los aceleradores van mejor (o solo andan) con el tamaño fijo: el
Neural Engine compila CoreML para tamaños concretos, y TensorRT y DirectML
optimizan para una forma. Una VARIANTE es la misma red compilada para un
tamaño de entrada, en <plugin>/variantes/<id>/, con su `variante.json` (qué
es, para qué, entrada, red, salida, archivos, lo medido). Puede haber muchas
por plugin; el elemento de GStreamer usa la del tamaño que le llega
(propiedad `variantes`, la arma la app).

No necesita PyTorch: parte de lo que dejó la instalación (modelo.mlpackage,
modelo.onnx, modelo-nv12.onnx) y usa coremltools y onnx, que vienen en el
Python del programa.

  variantes.py crear  --plugin DIR (--ancho W --alto H | --tamanos WxH,WxH) [--id ID] [--motores coreml,onnx]
                      [--para TEXTO] [--descripcion TEXTO] [--reemplazar]
  variantes.py listar --plugin DIR
  variantes.py borrar --plugin DIR --id ID

Habla por la salida estándar con una línea JSON por evento (como el
laboratorio): {"ev": "progreso"|"aviso"|"listo"|"error", ...}.
"""

import argparse
import json
import os
import shutil
import sys
import tempfile
import time

VERSION = 1


def decir(**m) -> None:
    print(json.dumps(m, ensure_ascii=False), flush=True)


def leer_json(ruta: str) -> dict:
    with open(ruta, encoding="utf-8") as f:
        return json.load(f)


def escribir_json(ruta: str, d: dict) -> None:
    tmp = ruta + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(d, f, ensure_ascii=False, indent=1)
    os.replace(tmp, ruta)


def variantes_de(carpeta: str) -> list:
    base = os.path.join(carpeta, "variantes")
    out = []
    if not os.path.isdir(base):
        return out
    for n in sorted(os.listdir(base)):
        j = os.path.join(base, n, "variante.json")
        if os.path.isfile(j):
            try:
                out.append(leer_json(j))
            except (OSError, ValueError) as e:
                out.append({"id": n, "error": f"variante.json ilegible: {e}"})
    return out


# --- CoreML ----------------------------------------------------------------------------

def coreml_fijo(paquete: str, w: int, h: int, destino: str, precision: str) -> dict:
    """modelo.mlpackage (tamaños flexibles o enumerados) -> .mlmodelc para w x h.
    La red es la misma: solo cambia el tamaño que declara la entrada, y Core ML
    la compila (en esta Mac, sin Xcode) para ese tamaño."""
    import coremltools as ct
    from coremltools.models.utils import compile_model

    spec = ct.utils.load_spec(paquete)
    entrada = spec.description.input[0]
    if entrada.type.WhichOneof("Type") != "imageType":
        raise SystemExit("el paquete CoreML no tiene una imagen de entrada")
    it = entrada.type.imageType
    it.ClearField("enumeratedSizes")
    it.ClearField("imageSizeRange")
    t = it.enumeratedSizes.sizes.add()
    t.width, t.height = w, h
    it.width, it.height = w, h
    pesos = os.path.join(paquete, "Data", "com.apple.CoreML", "weights")
    modelo = ct.models.MLModel(spec, weights_dir=pesos if os.path.isdir(pesos) else None)
    tmp = tempfile.mkdtemp(prefix="ba-variante-")
    try:
        p = os.path.join(tmp, "v.mlpackage")
        modelo.save(p)
        shutil.rmtree(destino, ignore_errors=True)
        compile_model(p, destino)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    # Medir: unos cuadros en el acelerador que le toca (FP16: Neural Engine).
    medido = {"precision": precision}
    try:
        import numpy as np
        from PIL import Image

        unidades = ct.ComputeUnit.CPU_AND_NE if precision == "fp16" else ct.ComputeUnit.CPU_AND_GPU
        cm = ct.models.CompiledMLModel(destino, compute_units=unidades)
        img = Image.fromarray((np.random.rand(h, w, 3) * 255).astype("uint8"))
        sale = cm.predict({"entrada": img})["salida"]
        for _ in range(2):
            cm.predict({"entrada": img})
        k = 6
        t0 = time.time()
        for _ in range(k):
            cm.predict({"entrada": img})
        medido.update({
            "ms": round(1000 * (time.time() - t0) / k, 1),
            "acelerador": "neural-engine" if precision == "fp16" else "gpu",
            "salida": [sale.size[0], sale.size[1]],
        })
    except Exception as e:  # noqa: BLE001
        medido["aviso"] = f"no se pudo medir: {type(e).__name__}: {e}"
    return medido


# --- ONNX ------------------------------------------------------------------------------

def onnx_fijo(origen: str, dims: dict, destino: str) -> None:
    """Fija las dimensiones libres (alto, ancho…) de las entradas y deja que
    onnx deduzca las de adentro y las de salida: TensorRT y DirectML compilan
    una vez y para esa forma."""
    import onnx
    from onnx import shape_inference

    m = onnx.load(origen)
    sin_fijar = []
    for e in m.graph.input:
        for d in e.type.tensor_type.shape.dim:
            nombre = d.dim_param
            if nombre:
                if nombre in dims:
                    d.ClearField("dim_param")
                    d.dim_value = int(dims[nombre])
                else:
                    sin_fijar.append(f"{e.name}.{nombre}")
    if sin_fijar:
        decir(ev="aviso", texto="quedan libres: " + ", ".join(sin_fijar))
    # Las salidas: que las deduzca (se borran las formas viejas, simbólicas).
    for s in m.graph.output:
        for d in s.type.tensor_type.shape.dim:
            if d.dim_param:
                d.ClearField("dim_param")
    try:
        m = shape_inference.infer_shapes(m)
    except Exception as e:  # noqa: BLE001
        decir(ev="aviso", texto=f"no se pudieron deducir las formas: {e}")
    onnx.checker.check_model(m)
    onnx.save(m, destino)


# --- Comandos --------------------------------------------------------------------------

def crear(a) -> None:
    carpeta = os.path.abspath(a.plugin)
    pj = os.path.join(carpeta, "plugin.json")
    if not os.path.isfile(pj):
        raise SystemExit(f"no hay plugin.json en {carpeta}")
    plugin = leer_json(pj)
    if plugin.get("tipo") != "red":
        raise SystemExit("las variantes son para redes: un shader GLSL se compila solo, para cualquier tamaño, "
                         "en la placa de video")
    w, h = int(a.ancho), int(a.alto)
    if w < 16 or h < 16 or w > 4096 or h > 4096 or w % 2 or h % 2:
        raise SystemExit("ancho y alto: pares, entre 16 y 4096")
    escala = int(plugin.get("escala", 2))
    mitad = bool(plugin.get("entrada_mitad")) or escala == 4
    rw, rh = (w // 2, h // 2) if mitad else (w, h)
    vid = a.id or f"{w}x{h}"
    if not all(c.isalnum() or c in "-_." for c in vid):
        raise SystemExit("id: letras, números, - _ .")
    destino = os.path.join(carpeta, "variantes", vid)
    if os.path.exists(destino) and not a.reemplazar:
        raise SystemExit(f"ya hay una variante «{vid}» (reemplazar: true para rehacerla)")
    archivos = plugin.get("archivos", {})
    pedidos = [m.strip() for m in (a.motores or "auto").split(",") if m.strip()]
    if "auto" in pedidos:
        pedidos = [m for m in ("coreml", "onnx", "onnx_nv12") if m in archivos or (m == "coreml" and
                   os.path.isdir(os.path.join(carpeta, "modelo.mlpackage")))]
    if not pedidos:
        raise SystemExit("el plugin no tiene nada para compilar (ni CoreML ni ONNX)")
    tmp = destino + ".nueva"
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    hechos, medidos, versiones = {}, {}, {}
    try:
        for i, mot in enumerate(pedidos):
            pct = 10 + 80 * i // len(pedidos)
            if mot == "coreml":
                if sys.platform != "darwin":
                    decir(ev="aviso", texto="CoreML: solo en una Mac")
                    continue
                paquete = os.path.join(carpeta, plugin.get("archivos", {}).get("coreml_paquete", "modelo.mlpackage"))
                if not os.path.isdir(paquete):
                    decir(ev="aviso", texto="CoreML: falta modelo.mlpackage (se guarda al instalar desde esta "
                                            "versión): volvé a instalar el plugin para poder hacer variantes CoreML")
                    continue
                decir(ev="progreso", texto=f"CoreML para {rw}x{rh}…", pct=pct)
                medidos["coreml"] = coreml_fijo(paquete, rw, rh, os.path.join(tmp, "modelo.mlmodelc"),
                                                plugin.get("coreml_precision", "fp16"))
                hechos["coreml"] = "modelo.mlmodelc"
                import coremltools

                versiones["coremltools"] = coremltools.__version__
            elif mot in ("onnx", "onnx_nv12"):
                origen = archivos.get(mot)
                if not origen or not os.path.isfile(os.path.join(carpeta, origen)):
                    decir(ev="aviso", texto=f"{mot}: el plugin no lo tiene")
                    continue
                decir(ev="progreso", texto=f"ONNX {'NV12 ' if mot == 'onnx_nv12' else ''}fijo a {w}x{h}…", pct=pct)
                # La float recibe lo que entra a la red (la mitad en una x4); la
                # NV12 recibe la cámara entera (la mitad la hace adentro).
                dims = ({"alto": h, "ancho": w, "alto_c": h // 2} if mot == "onnx_nv12"
                        else {"alto": rh, "ancho": rw, "height": rh, "width": rw})
                nombre = "modelo-nv12.onnx" if mot == "onnx_nv12" else "modelo.onnx"
                onnx_fijo(os.path.join(carpeta, origen), dims, os.path.join(tmp, nombre))
                hechos[mot] = nombre
                import onnx

                versiones["onnx"] = onnx.__version__
            else:
                decir(ev="aviso", texto=f"motor desconocido: {mot} (coreml, onnx, onnx_nv12)")
        if not hechos:
            raise SystemExit("no se pudo compilar nada para esa variante (ver los avisos)")
        v = {
            "version": VERSION,
            "id": vid,
            "plugin": plugin.get("id", os.path.basename(carpeta)),
            "nombre": f"{plugin.get('nombre', plugin.get('id', ''))} · {w}x{h}",
            "descripcion": a.descripcion or (f"{plugin.get('descripcion', '')} Compilada para una entrada fija de "
                                             f"{w}x{h}.").strip(),
            "para": a.para or f"cámaras de {w}x{h}",
            "entrada": {"ancho": w, "alto": h, "formatos": {"coreml": ["NV12", "BGRA"], "onnx": ["NV12"],
                                                              "onnx_nv12": ["NV12"]},
                        "color": "la matriz que traiga (la red va y vuelve con la misma), rango limitado"},
            "red": {"ancho": rw, "alto": rh, "escala": escala, "entrada_mitad": mitad,
                    "arquitectura": plugin.get("arquitectura"), "canales": plugin.get("canales")},
            "salida": {"ancho": 2 * w, "alto": 2 * h},
            "archivos": hechos,
            "medido": medidos,
            "creada": time.strftime("%Y-%m-%d %H:%M"),
            "herramienta": dict({"nombre": "variantes.py", "version": VERSION}, **versiones),
            "licencia": plugin.get("licencia"),
        }
        escribir_json(os.path.join(tmp, "variante.json"), v)
        shutil.rmtree(destino, ignore_errors=True)
        os.replace(tmp, destino)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return v


def crear_varias(a) -> None:
    """--tamanos 1024x576,704x576 (o --ancho/--alto): una variante por tamaño; si
    una falla, se avisa y siguen las demás."""
    tamanos = []
    for t in (a.tamanos or "").split(","):
        t = t.strip().lower()
        if t:
            try:
                w, h = (int(x) for x in t.split("x"))
            except ValueError:
                raise SystemExit(f"tamaño inválido: «{t}» (ancho x alto)")
            tamanos.append((w, h))
    if not tamanos:
        tamanos = [(a.ancho, a.alto)]
    if len(tamanos) > 1 and a.id:
        raise SystemExit("--id es para una sola variante")
    hechas, errores = [], []
    for i, (w, h) in enumerate(tamanos):
        decir(ev="progreso", texto=f"Variante {i + 1} de {len(tamanos)}: {w}x{h}", pct=100 * i // len(tamanos))
        b = argparse.Namespace(**vars(a))
        b.ancho, b.alto = w, h
        try:
            hechas.append(crear(b))
        except SystemExit as e:
            if len(tamanos) == 1:
                raise
            errores.append(f"{w}x{h}: {e}")
            decir(ev="aviso", texto=f"{w}x{h}: {e}")
    if not hechas:
        raise SystemExit("; ".join(errores) or "no se hizo ninguna")
    decir(ev="listo", variantes=hechas, errores=errores, texto=f"{len(hechas)} variante(s) lista(s)")


def listar(a) -> None:
    decir(ev="listo", variantes=variantes_de(os.path.abspath(a.plugin)))


def borrar(a) -> None:
    d = os.path.join(os.path.abspath(a.plugin), "variantes", a.id)
    if not os.path.isfile(os.path.join(d, "variante.json")):
        raise SystemExit(f"no hay una variante «{a.id}»")
    shutil.rmtree(d)
    decir(ev="listo", borrada=a.id)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("comando", choices=["crear", "listar", "borrar"])
    ap.add_argument("--plugin", required=True, help="la carpeta del plugin instalado")
    ap.add_argument("--ancho", type=int, default=0)
    ap.add_argument("--alto", type=int, default=0)
    ap.add_argument("--tamanos", default="", help="varios: 1024x576,704x576 (una variante por tamaño)")
    ap.add_argument("--id", default="")
    ap.add_argument("--motores", default="auto", help="coreml,onnx,onnx_nv12 o auto (todo lo que tenga)")
    ap.add_argument("--para", default="")
    ap.add_argument("--descripcion", default="")
    ap.add_argument("--reemplazar", action="store_true")
    a = ap.parse_args()
    try:
        {"crear": crear_varias, "listar": listar, "borrar": borrar}[a.comando](a)
    except SystemExit as e:
        if e.code not in (None, 0):
            decir(ev="error", error=str(e))
            sys.exit(1)
    except Exception as e:  # noqa: BLE001
        import traceback

        traceback.print_exc(file=sys.stderr)
        decir(ev="error", error=f"{type(e).__name__}: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
