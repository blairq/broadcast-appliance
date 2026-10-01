#!/usr/bin/env python3
# ============================================================================
# tools/lsp-matrix-mixer-doc.py — las capturas, íconos y configs del Matrix Mixer
#
# third_party/lsp-plugins-matrix-mixer lleva, como cada módulo oficial de LSP:
#
#   res/doc/screenshots/<plugin>.png   la interfaz (tema modern), con la
#                                      config de ejemplo puesta y audio
#   res/doc/configs/<plugin>.cfg       esa config, en el formato de LSP
#   res/main/icons/<tema>/<plugin>.xpm la interfaz entera achicada a 256x256,
#                                      en cada tema (legacy, legacy-dark,
#                                      modern, paper)
#
# Las capturas las saca src/app/tools/capturar_lsp (Mac; el último cuadro que
# dibuja la interfaz, sin leer la pantalla) con el lsp-plugins.clap compilado
# con el módulo (make deps-lsp-macos). Cada ejecución de capturar_lsp usa una
# config global de LSP propia (XDG_CONFIG_HOME) para elegir el tema: la del
# usuario no se toca.
#
# Uso: .venv/bin/python tools/lsp-matrix-mixer-doc.py [--clap <lsp-plugins.clap>]
#      (necesita Pillow; build/src/app/capturar_lsp compilado)
# ============================================================================
import argparse
import os
import subprocess
import sys
import tempfile

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MOD = os.path.join(ROOT, "third_party", "lsp-plugins-matrix-mixer")
LSP_VER = "1.2.35"
TEMAS = ("legacy", "legacy-dark", "modern", "paper")

# Los plugins, en el orden de su metadata (el LADSPA id es la base + el índice).
PLUGINS = [
    ("matrix_mixer_x4_mono", "Matrixmischer x4 Mono", "Matrix Mixer x4 Mono", "mx4m", 4, False),
    ("matrix_mixer_x8_mono", "Matrixmischer x8 Mono", "Matrix Mixer x8 Mono", "mx8m", 8, False),
    ("matrix_mixer_x4_stereo", "Matrixmischer x4 Stereo", "Matrix Mixer x4 Stereo", "mx4s", 4, True),
    ("matrix_mixer_x8_stereo", "Matrixmischer x8 Stereo", "Matrix Mixer x8 Stereo", "mx8s", 8, True),
]
LADSPA_BASE = 0x4C5350 + 300  # LSP_LADSPA_MATRIX_MIXER_BASE

GAIN_MAX = 10 ** (12 / 20)  # CHANNEL_GAIN_MAX = CELL_GAIN_MAX = +12 dB


def puertos(n, stereo):
    """Los puertos de control en el orden de la metadata: (id, nombre, tipo, defecto).

    tipo: 'bool', 'gain' (en dB; None = -inf) o 'pan' (%).
    """
    p = [("bypass", "Bypass", "bool", False)]
    for i in range(1, n + 1):
        p += [
            (f"cs_{i}", f"Input solo {i}", "bool", False),
            (f"cm_{i}", f"Input mute {i}", "bool", False),
            (f"ci_{i}", f"Input phase invert {i}", "bool", False),
            (f"cg_{i}", f"Input gain {i}", "gain", 0.0),
        ]
    for o in range(1, n + 1):
        p.append((f"om_{o}", f"Output mute {o}", "bool", False))
        if stereo:
            p.append((f"ob_{o}", f"Output balance {o}", "pan", 0.0))
        p.append((f"og_{o}", f"Output gain {o}", "gain", 0.0))
    for i in range(1, n + 1):
        for o in range(1, n + 1):
            p.append((f"mx_{i}_{o}", f"Matrix gain {i} to {o}", "gain", 0.0 if i == o else None))
    return p


def escena(n, stereo):
    """La config de ejemplo: un uso típico de transmisión.

    Entradas: 1-2 micrófonos, 3 música, 4 una llamada (x8: 5-6 un par de
    reproducciones, 7 el retorno del estudio, 8 un clic de tiempo). Salidas:
    1 el programa, 2 el monitor de estudio, 3 la mezcla para la llamada
    (sin la llamada: mix-minus), 4 la grabación de micrófonos limpios (x8:
    5-8 envíos auxiliares).
    """
    v = {}
    for i in range(1, n + 1):
        for o in range(1, n + 1):
            v[f"mx_{i}_{o}"] = None
    # Programa
    v.update({"mx_1_1": 0.0, "mx_2_1": -1.5, "mx_3_1": -9.0, "mx_4_1": -3.0})
    # Monitor de estudio: todo, la música más baja
    v.update({"mx_1_2": -6.0, "mx_2_2": -6.0, "mx_3_2": -15.0, "mx_4_2": 0.0})
    # Mix-minus para la llamada: sin la propia llamada
    v.update({"mx_1_3": 0.0, "mx_2_3": 0.0, "mx_3_3": -12.0})
    # Grabación de micrófonos limpios
    v.update({"mx_1_4": 0.0, "mx_2_4": 0.0})
    v.update({"cg_2": 2.5, "cg_3": -4.0, "og_2": -6.0})
    if n == 8:
        v.update({"mx_5_1": -6.0, "mx_6_1": -6.0, "mx_5_2": -12.0, "mx_6_2": -12.0, "mx_7_2": -3.0})
        v.update({"mx_7_5": 0.0, "mx_8_6": 0.0, "mx_1_7": -6.0, "mx_2_7": -6.0, "mx_7_7": 0.0})
        v.update({"mx_3_8": 0.0, "mx_5_8": 0.0, "mx_6_8": 0.0})
        v.update({"cm_8": True, "om_6": True, "og_8": -3.0})
    if stereo:
        v.update({"ob_2": -25.0, "ob_3": 15.0})
        if n == 8:
            v.update({"ob_7": 40.0})
    return v


def gain_db(x):
    return "-inf db" if x is None else f"{x:.2f} db"


def cfg(plugin, aleman, ingles, vst, idx, n, stereo, valores):
    uid = plugin
    lin = [
        "# " + "-" * 79,
        "# This file contains configuration of the audio plugin.",
        "#   Package:             test (Test Case)",
        "#   Package version:     0.0.0-devel",
        f"#   Plugin name:         {aleman} ({ingles})",
        "#   Plugin version:      1.0.0",
        f"#   UID:                 {uid}",
        f"#   LV2 URI:             http://lsp-plug.in/plugins/lv2/{uid}",
        f"#   VST identifier:      {vst}",
        f"#   LADSPA identifier:   {LADSPA_BASE + idx}",
        f"#   LADSPA label:        http://lsp-plug.in/plugins/ladspa/{uid}",
        "# ",
        "# (C) Test Case",
        "#   https://lsp-plug.in/",
        "# ",
        "# " + "-" * 79,
        "",
    ]
    for pid, nombre, tipo, dfl in puertos(n, stereo):
        val = valores.get(pid, dfl)
        if tipo == "bool":
            lin += [f"# {nombre} [boolean]: true/false", f"{pid} = {'true' if val else 'false'}", ""]
        elif tipo == "gain":
            lin += [f"# {nombre} [G]: 0.00000000..{GAIN_MAX:.8f}", f"{pid} = {gain_db(val)}", ""]
        else:
            lin += [f"# {nombre} [%]: -100.00000000..100.00000000", f"{pid} = {val:.5f}", ""]
    lin += ["# " + "-" * 79, "# KVT parameters", "# " + "-" * 79, "", "# " + "-" * 79, ""]
    return "\n".join(lin)


def capturar(herramienta, clap, plugin, tema, salida, n, stereo, valores, tmp):
    xdg = os.path.join(tmp, "xdg-" + tema)
    os.makedirs(os.path.join(xdg, "lsp-plugins"), exist_ok=True)
    with open(os.path.join(xdg, "lsp-plugins", "lsp-plugins.cfg"), "w") as f:
        # La versión ya vista (sin el saludo de LSP) y la escala 1x.
        f.write(f'lsp_plugins_version = "{LSP_VER}"\n')
        f.write(f'visual_schema_file = "builtin://schema/{tema}.xml"\n')
        f.write("ui_scaling_host = false\nui_scaling = 100.0\n")
    args = [herramienta, "--clap", clap, "--id", "in.lsp-plug." + plugin, "--salida", salida, "--esperar", "3000"]
    nombres = {pid: nombre for pid, nombre, _, _ in puertos(n, stereo)}
    for pid, val in valores.items():
        if val is None:
            continue  # -inf: el valor de fábrica de los cruces
        if isinstance(val, bool):
            val = 1 if val else 0
        args += ["--param", f"{nombres[pid]}={val}"]
    env = dict(os.environ, XDG_CONFIG_HOME=xdg)
    r = subprocess.run(args, env=env, capture_output=True, text=True)
    if r.returncode != 0 or not os.path.exists(salida):
        sys.exit(f"capturar_lsp fallo para {plugin} ({tema}):\n{r.stderr}")
    return Image.open(salida).convert("RGB")


def xpm(nombre, img, ruta):
    """XPM como los de LSP: 256x256, 2 caracteres por color."""
    q = img.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = q.getpalette()[: 3 * 256]
    datos = list(q.get_flattened_data() if hasattr(q, "get_flattened_data") else q.getdata())
    usados = sorted(set(datos))
    simbolos = " .XoO+@#$%&*=-;:>,<1234567890qwertyuipasdfghjklzxcvbnmMNBVCZASDFGHJKLPIUYTREWQ!~^/()_`'][{}|"
    cods = {}
    for k, c in enumerate(usados):
        cods[c] = simbolos[k % len(simbolos)] + simbolos[k // len(simbolos)]
    w, h = q.size
    lin = ["/* XPM */", f"static char *{nombre}[] = {{", "/* columns rows colors chars-per-pixel */",
           f'"{w} {h} {len(usados)} 2 ",']
    for c in usados:
        r, g, b = pal[3 * c: 3 * c + 3]
        lin.append(f'"{cods[c]} c #{r:02X}{g:02X}{b:02X}",')
    lin.append("/* pixels */")
    for y in range(h):
        fila = "".join(cods[c] for c in datos[y * w:(y + 1) * w])
        lin.append(f'"{fila}"' + ("," if y < h - 1 else ""))
    lin.append("};")
    with open(ruta, "w") as f:
        f.write("\n".join(lin) + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--clap", default=os.path.join(ROOT, "extern", "lsp", "macos-arm64", "lsp-plugins.clap"))
    ap.add_argument("--herramienta", default=os.path.join(ROOT, "build", "src", "app", "capturar_lsp"))
    a = ap.parse_args()
    if not os.path.exists(a.herramienta):
        sys.exit(f"falta {a.herramienta} (cmake --build build --target capturar_lsp)")
    with tempfile.TemporaryDirectory() as tmp:
        for idx, (plugin, aleman, ingles, vst, n, stereo) in enumerate(PLUGINS):
            valores = escena(n, stereo)
            with open(os.path.join(MOD, "res", "doc", "configs", plugin + ".cfg"), "w") as f:
                f.write(cfg(plugin, aleman, ingles, vst, idx, n, stereo, valores))
            for tema in TEMAS:
                png = os.path.join(tmp, f"{plugin}-{tema}.png")
                img = capturar(a.herramienta, a.clap, plugin, tema, png, n, stereo, valores, tmp)
                if tema == "modern":
                    img.save(os.path.join(MOD, "res", "doc", "screenshots", plugin + ".png"), optimize=True)
                d = os.path.join(MOD, "res", "main", "icons", tema)
                os.makedirs(d, exist_ok=True)
                xpm(plugin, img.resize((256, 256), Image.LANCZOS), os.path.join(d, plugin + ".xpm"))
            print(f"{plugin}: {img.size[0]}x{img.size[1]}")


if __name__ == "__main__":
    main()
