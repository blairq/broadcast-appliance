"""The chaos bench's local RTMP server (tools/caos/rtmp_local.py) against the real client.

GStreamer's rtmp2sink (the vendored one: the same element the app publishes with)
publishes a few seconds of synthetic video and audio; the server must accept the
publish, count every frame, notice when the connection drops and when a new one
starts, and write a FLV that the vendored gst-discoverer can read. No system
GStreamer: without the vendored one the test is skipped.
"""
from __future__ import annotations

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


def _publicar(gst_launch: str, puerto: int, cuadros: int) -> int:
    tubo = (f"videotestsrc is-live=true num-buffers={cuadros} pattern=ball ! video/x-raw,width=320,height=240,framerate=30/1 "
            f"! x264enc tune=zerolatency key-int-max=30 speed-preset=ultrafast ! h264parse ! flvmux name=m streamable=true "
            f"! rtmp2sink location=rtmp://127.0.0.1:{puerto}/live/caos "
            f"audiotestsrc is-live=true wave=silence num-buffers={cuadros // 3} ! audioconvert ! avenc_aac ! aacparse ! m.")
    return subprocess.run([gst_launch, "-q", "-e"] + tubo.split(), capture_output=True, timeout=60).returncode


def test_rtmp_server_against_rtmp2sink(tmp_path: Path) -> None:
    gst_launch = _gst_bin("gst-launch-1.0")
    if gst_launch is None:
        pytest.skip("no vendored GStreamer (extern/ or dist/)")
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
        r = subprocess.run([discoverer, str(flv[0])], capture_output=True, text=True, timeout=60)
        assert r.returncode == 0 and "Duration" in r.stdout, r.stdout + r.stderr


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-q"]))
