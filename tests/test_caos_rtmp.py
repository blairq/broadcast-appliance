"""The chaos bench's local RTMP server (tools/caos/rtmp_local.py) against the real client.

GStreamer's rtmp2sink (the vendored one: the same element the app publishes with)
publishes a few seconds of synthetic video and audio; the server must accept the
publish, count every frame, notice when the connection drops and when a new one
starts, and write a FLV that the vendored gst-discoverer can read. No system
GStreamer: without the vendored one the test is skipped.
"""
from __future__ import annotations

import functools
import glob
import importlib.util
import os
import subprocess
import sys
import time
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parent.parent
_spec = importlib.util.spec_from_file_location("rtmp_local", REPO / "tools" / "caos" / "rtmp_local.py")
rtmp_local = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(rtmp_local)


def _gst_bin(nombre: str) -> str | None:
    """A vendored GStreamer tool (extern/ or the bundle in dist/), never the system's."""
    patrones = [
        "extern/gstreamer-macos/GStreamer.framework/Versions/1.0/bin/",
        "dist/BroadcastAppliance.app/Contents/Resources/bin/64bit/GStreamer.framework/Versions/1.0/bin/",
        "extern/gstreamer-linux*/bin/",
        "extern/gstreamer-windows*/bin/",
    ]
    raices = [REPO]
    # In a git worktree extern/ and dist/ live in the main tree.
    r = subprocess.run(["git", "-C", str(REPO), "worktree", "list", "--porcelain"], capture_output=True, text=True)
    raices += [Path(l[len("worktree "):]) for l in r.stdout.splitlines() if l.startswith("worktree ")]
    for raiz in raices:
        for p in patrones:
            for d in glob.glob(str(raiz / p)):
                for ext in ("", ".exe"):
                    f = os.path.join(d, nombre + ext)
                    if os.path.isfile(f):
                        return f
    return None


def _bajo_emulacion() -> str | None:
    """Por qué esta máquina no mide tiempos de verdad (como bajoRosetta/bajoWine de
    src/core/tests/Puntualidad.hpp), o None: el Linux x86_64 de Docker en una Mac ARM."""
    try:
        with open("/proc/cpuinfo", encoding="utf-8", errors="replace") as f:
            if "VirtualApple" in f.read():
                return "Rosetta (el Linux x86_64 de Docker en una Mac ARM)"
    except OSError:
        pass
    return None


def _factor_tiempo() -> float:
    """Cuánto estirar los plazos de una prueba con tiempos: 1 en una máquina de verdad, 3 bajo
    emulación (todo, desde arrancar un proceso hasta codificar, tarda varias veces más).
    BA_TEST_TIEMPO_REAL_ESTRICTO=1 lo deja en 1 (como en C++)."""
    if os.environ.get("BA_TEST_TIEMPO_REAL_ESTRICTO", "") not in ("", "0"):
        return 1.0
    return 3.0 if _bajo_emulacion() else 1.0


# Los codificadores que hay en cada GStreamer vendoreado: el de macOS trae x264enc y avenc_aac;
# el de Linux, sin x264 ni libav, openh264enc y fdkaacenc. Antes las pruebas pedían x264enc a
# secas y en Linux (Docker) gst-launch salía con «no element x264enc»: el banco lo veía como
# «la transmisión no llega al servidor RTMP local».
H264 = (("x264enc", "tune=zerolatency key-int-max=30 speed-preset=ultrafast"), ("openh264enc", "gop-size=30 complexity=low"))
AAC = ("avenc_aac", "fdkaacenc", "voaacenc")


@functools.lru_cache(maxsize=None)
def _codificadores(gst_launch: str) -> tuple[str | None, str | None]:
    """(«elemento propiedades» de H.264, elemento de AAC) del GStreamer de gst_launch, o None."""
    inspect = os.path.join(os.path.dirname(gst_launch), "gst-inspect-1.0" + (".exe" if gst_launch.endswith(".exe") else ""))

    def hay(e: str) -> bool:
        return subprocess.run([inspect, e], capture_output=True, timeout=600).returncode == 0

    h264 = next((f"{e} {props}" for e, props in H264 if hay(e)), None)
    aac = next((e for e in AAC if hay(e)), None)
    return h264, aac


def _gst_y_codificadores(nombre: str = "gst-launch-1.0") -> tuple[str, str, str]:
    """La herramienta vendoreada y sus codificadores; si falta algo, la prueba se saltea diciendo qué."""
    gst = _gst_bin(nombre)
    if gst is None:
        pytest.skip("no vendored GStreamer (extern/ or dist/)")
    h264, aac = _codificadores(gst)
    if h264 is None or aac is None:
        pytest.skip(f"the vendored GStreamer has no {'H.264' if h264 is None else 'AAC'} encoder "
                    f"({', '.join(e for e, _ in H264) if h264 is None else ', '.join(AAC)})")
    return gst, h264, aac


def _publicar(gst_launch: str, puerto: int, cuadros: int) -> int:
    h264, aac = _codificadores(gst_launch)
    tubo = (f"videotestsrc is-live=true num-buffers={cuadros} pattern=ball ! video/x-raw,width=320,height=240,framerate=30/1 "
            f"! {h264} ! h264parse ! flvmux name=m streamable=true "
            f"! rtmp2sink location=rtmp://127.0.0.1:{puerto}/live/caos "
            f"audiotestsrc is-live=true wave=silence num-buffers={cuadros // 3} ! audioconvert ! {aac} ! aacparse ! m.")
    r = subprocess.run([gst_launch, "-q", "-e"] + tubo.split(), capture_output=True, text=True, timeout=60 * _factor_tiempo())
    if r.returncode != 0:
        print(r.stdout[-2000:], r.stderr[-2000:])
    return r.returncode


def test_rtmp_server_against_rtmp2sink(tmp_path: Path) -> None:
    gst_launch, _, _ = _gst_y_codificadores()
    s = rtmp_local.ServidorRtmp(0, str(tmp_path)).iniciar()
    try:
        t0 = time.time()
        assert _publicar(gst_launch, s.puerto, 90) == 0, "rtmp2sink could not publish"
        time.sleep(0.5)
        t1 = time.time()
        assert _publicar(gst_launch, s.puerto, 60) == 0, "second publish"
        time.sleep(0.5)
        ses = s.sesiones()
        publicadas = [x for x in ses if x["publica"]]
        assert len(publicadas) == 2, ses
        assert publicadas[0]["nombre"] == "caos"
        assert publicadas[0]["videos"] >= 85 and publicadas[1]["videos"] >= 55, publicadas
        assert publicadas[0]["claves"] >= 2 and publicadas[0]["audios"] > 0
        assert all(x["fin"] for x in publicadas), "the server noticed both disconnections"
        # What the bench measures: the first frame after t1 comes from the new connection.
        assert s.primer_video_desde(t1, sesion_nueva=True) is not None
        assert s.primer_video_desde(t0) <= s.primer_video_desde(t1)
        assert s.huecos(0.3), "the gap between the two publishes shows up"
    finally:
        s.parar()
    flv = sorted(tmp_path.glob("sesion-*.flv"))
    assert len(flv) >= 2
    discoverer = _gst_bin("gst-discoverer-1.0")
    if discoverer:
        r = subprocess.run([discoverer, str(flv[0])], capture_output=True, text=True, timeout=60 * _factor_tiempo())
        assert r.returncode == 0 and "Duration" in r.stdout, r.stdout + r.stderr


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-q"]))
