"""El manifest que escribe tools/deploy tiene que poder leerlo el launcher.

Durante un tiempo no: el script escribía un objeto ruta -> hash y el parser de
`src/launcher/Integrity.cpp` busca pares "path" y "sha256". No encontraba
ninguna entrada, así que la verificación de integridad pasaba siempre, con cero
archivos revisados — que es peor que no tenerla, porque parece que está.
"""
from __future__ import annotations

import json
import subprocess
from pathlib import Path

import pytest

# Lo que cambia al usar el programa no va en el manifest: si fuera, el launcher
# avisaría de "integridad rota" en cada arranque después del primero.
CAMBIANTES = ("Config", "Logs", "Recordings", "Runtime", "Credenciales", "Eventos")


@pytest.fixture(scope="module")
def bundle(tmp_path_factory, repo_root: Path) -> dict:
    """Un bundle de mentira con tools/deploy --solo-manifest corrido encima."""
    raiz = tmp_path_factory.mktemp("bundle")
    (raiz / "bin" / "64bit").mkdir(parents=True)
    (raiz / "bin" / "64bit" / "ba-app").write_text("binario", encoding="utf-8")
    (raiz / "Web" / "guest").mkdir(parents=True)
    (raiz / "Web" / "guest" / "app.js").write_text("// codigo", encoding="utf-8")
    for d in CAMBIANTES:
        (raiz / d).mkdir()
        (raiz / d / "algo.json").write_text("{}", encoding="utf-8")

    r = subprocess.run(
        [str(repo_root / "tools" / "deploy"), "--solo-manifest", str(raiz)],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, f"tools/deploy falló: {r.stderr}"
    texto = (raiz / "manifest.json").read_text(encoding="utf-8")
    return json.loads(texto)


def test_es_json_valido(bundle: dict) -> None:
    assert "files" in bundle
    assert bundle.get("app"), "el manifest nombra la app"


def test_formato_que_lee_el_launcher(bundle: dict) -> None:
    """Lista de objetos con "path" y "sha256": lo que busca Integrity.cpp."""
    files = bundle["files"]
    assert isinstance(files, list), "el launcher recorre una lista, no un objeto"
    assert files, "un manifest vacío hace que la verificación no revise nada"
    for e in files:
        assert set(e) == {"path", "sha256"}
        assert len(e["sha256"]) == 64, "SHA-256 en hexadecimal"


def test_cubre_los_binarios(bundle: dict) -> None:
    rutas = {e["path"] for e in bundle["files"]}
    assert "bin/64bit/ba-app" in rutas
    assert "Web/guest/app.js" in rutas


def test_no_cubre_lo_que_cambia_al_usarlo(bundle: dict) -> None:
    rutas = [e["path"] for e in bundle["files"]]
    metidos = [r for r in rutas if r.split("/")[0] in CAMBIANTES]
    assert not metidos, f"no deberían estar en el manifest: {metidos}"


def test_no_se_incluye_a_si_mismo(bundle: dict) -> None:
    assert "manifest.json" not in {e["path"] for e in bundle["files"]}


def test_orden_estable(bundle: dict) -> None:
    """Ordenado por ruta: dos corridas del mismo árbol dan el mismo archivo."""
    rutas = [e["path"] for e in bundle["files"]]
    assert rutas == sorted(rutas)
