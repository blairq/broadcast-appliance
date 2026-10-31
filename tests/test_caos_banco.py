"""The chaos bench (tools/caos/banco.py) end to end against a fake app (tests/caos_mcp_falso.py).

The real app is never opened here. The fake bridge plays the app: events, status and a
transmission published with the vendored GStreamer to the bench's local RTMP server,
which an injected encoder failure drops and brings back. What is checked:

- the bench works on its OWN event and leaves the operator's one as it was (back to it,
  the test event deleted and purged, app_quit);
- an encoder failure on the transmission is measured: it comes back, with an RTO;
- what does not run (ba-web here) is skipped, not counted as a failure;
- the report (JSON + Markdown) has the app version and the percentiles.

Plus the pure helpers (percentiles, the gateway.log timestamps, .mkv paths in replies).
"""
from __future__ import annotations

import importlib.util
import json
import os
import stat
import subprocess
import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parent.parent
_spec = importlib.util.spec_from_file_location("banco_caos", REPO / "tools" / "caos" / "banco.py")
banco = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(banco)
_spec2 = importlib.util.spec_from_file_location("test_caos_rtmp", REPO / "tests" / "test_caos_rtmp.py")
_rtmp_test = importlib.util.module_from_spec(_spec2)
_spec2.loader.exec_module(_rtmp_test)


def test_helpers(tmp_path: Path) -> None:
    assert banco.percentil([], 50) is None
    assert banco.percentil([3.0, 1.0, 2.0, None], 50) == 2.0
    assert banco.percentil([float(x) for x in range(1, 21)], 95) == 19.0
    mk: set = set()
    banco.mkvs_en({"a": [{"archivo": "/x/rec-00001.mkv"}, "nada"], "b": {"c": "/y/z.MKV"}}, mk)
    assert mk == {"/x/rec-00001.mkv", "/y/z.MKV"}
    datos = tmp_path / "datos"
    (datos / "Logs").mkdir(parents=True)
    (datos / "Logs" / "gateway.log").write_text(
        "2026-10-31T05:00:00.000Z info | gateway | 0 | MediaPump: en marcha\n"
        "2026-10-31T05:00:05.500Z info | gateway | 0 | BA.Program: A cambio o se cerro; resuscribiendo\n"
        "2026-10-31T05:00:07.250Z info | gateway | 0 | BA.Program: suscrito (1920x1080) -> 1280x720\n", encoding="utf-8")
    app = banco.App(str(tmp_path / "bin" / "64bit" / "ba-mcp"), str(datos))
    import datetime
    t0 = datetime.datetime(2026, 10, 31, 5, 0, 6, tzinfo=datetime.timezone.utc).timestamp()
    assert app.primera_linea("gateway.log", t0, banco.ANILLO) == pytest.approx(t0 + 1.25)
    assert app.primera_linea("gateway.log", t0 + 10, banco.ANILLO) is None
    assert app.primera_linea("no-esta.log", t0, banco.ANILLO) is None


def test_bench_against_fake_app(tmp_path: Path) -> None:
    gst = _rtmp_test._gst_bin("gst-launch-1.0")
    if gst is None or os.name == "nt":
        pytest.skip("no vendored GStreamer (extern/ or dist/)")
    falso = tmp_path / "bin" / "64bit" / "ba-mcp"
    falso.parent.mkdir(parents=True)
    falso.write_text(f"#!/bin/sh\nexec '{sys.executable}' -I '{REPO / 'tests' / 'caos_mcp_falso.py'}'\n", encoding="utf-8")
    falso.chmod(falso.stat().st_mode | stat.S_IXUSR)
    (tmp_path / "manifest.json").write_text(json.dumps({"version": "9.9.9", "generated_at": "hoy"}), encoding="utf-8")
    estado = tmp_path / "estado.json"
    env = dict(os.environ, CAOS_FALSO_ESTADO=str(estado), CAOS_FALSO_GST=gst)
    salida = tmp_path / "informe"
    r = subprocess.run([sys.executable, "-I", str(REPO / "tools" / "caos" / "banco.py"), "--ba-mcp", str(falso),
                        "--datos", str(tmp_path / "datos"), "--escenarios", "codificador_tx,kill_web", "--repeticiones", "2",
                        "--plazo-s", "20", "--estable-s", "1", "--espera-app-s", "20", "--salida", str(salida), "--no-matar"],
                       env=env, capture_output=True, text=True, timeout=240)
    print(r.stdout[-4000:], r.stderr[-2000:])
    e = json.loads(estado.read_text(encoding="utf-8"))
    llamadas = [c[0] for c in e["llamadas"]]
    prueba = next(c[1]["nombre"] for c in e["llamadas"] if c[0] == "evento_nuevo")
    assert prueba.startswith("ZZ caos ")
    # Lo del operador, como estaba; lo de prueba, borrado y fuera de la papelera.
    assert e["abierto"] == "Evento del operador"
    assert ["evento_borrar", {"nombre": prueba}] in e["llamadas"]
    assert e["papelera"] == [] and prueba not in e["eventos"]
    assert llamadas[-1] == "app_quit" and e.get("salio")
    assert ["evento_cambiar", {"nombre": "Evento del operador", "aunque_este_al_aire": True}] in e["llamadas"]
    assert llamadas.index("evento_cambiar") > llamadas.index("stream_stop")
    assert e.get("gst") is None, "nothing left publishing"
    j = json.loads((salida / "caos.json").read_text(encoding="utf-8"))
    assert j["app"]["version"] == "9.9.9"
    cod = j["escenarios"]["codificador_tx"]
    assert len(cod["repeticiones"]) == 2 and cod["no_volvio"] == 0, cod
    assert all(x["disparada"] and x["volvio"] for x in cod["repeticiones"])
    assert 1.0 <= cod["rto_p50_s"] < 15  # the fake reconnects a second after the failure and cod["rto_p95_s"] >= cod["rto_p50_s"]
    assert j["escenarios"]["kill_web"]["saltado"]
    assert len(j["rtmp"]["sesiones"]) >= 3, "the fake reconnected after each injected failure"
    md = (salida / "caos.md").read_text(encoding="utf-8")
    assert "codificador_tx" in md and "RTO p50" in md and "9.9.9" in md
    assert r.returncode == 0, r.stdout[-3000:]
