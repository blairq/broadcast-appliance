"""La superficie de control web (assets/web/control): las cuentas de la disposición del
tablero (disposicion.js) y que la página cargue lo que necesita la multipantalla.

disposicion.js no toca el DOM: se corre en Node (vm) si está; sin Node se saltea (es una
herramienta de desarrollo, no del producto).
"""
import json
import re
import shutil
import subprocess
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[1]
CONTROL = REPO / "assets" / "web" / "control"

PRUEBA = r"""
const vm = require("vm");
const fs = require("fs");
const ctx = { window: {} };
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(process.argv[2], "utf8"), ctx);
const D = ctx.window.BA.disposicion;
const tarjetas = [{ id: "escenas", ancha: true }, { id: "audio", ancha: true }, { id: "ptz" }, { id: "estado" }];
const r = {};
r.fabrica = D.normalizar(null, tarjetas);
r.guardada = D.normalizar({ orden: ["ptz", "vieja", "ptz", "escenas"], anchos: { ptz: 3, escenas: 9, audio: "2" },
                            ocultas: ["estado", "vieja"], multipantalla: { contraida: 1 } }, tarjetas);
r.deAntes = D.desdePreferencias({ ocultas: ["ptz"] }, tarjetas);
const d = D.normalizar(null, tarjetas);
r.moverPrimeraAtras = D.mover(d, "escenas", -1);
r.moverAdelante = D.mover(d, "escenas", 1);
r.ordenMovido = d.orden.slice();
r.anchoMas = [D.ancho(d, "ptz", 1), D.ancho(d, "ptz", 1), D.ancho(d, "ptz", 1), D.ancho(d, "ptz", 1), d.anchos.ptz];
r.anchoMenos = [D.ancho(d, "estado", -1), d.anchos.estado];
r.oculta = [D.alternarOculta(d, "audio"), d.ocultas.slice(), D.alternarOculta(d, "audio"), d.ocultas.slice()];
r.reordenar = [D.reordenar(d, ["estado", "ptz"]), d.orden.slice()];
console.log(JSON.stringify(r));
"""


@pytest.fixture(scope="module")
def cuentas(tmp_path_factory):
    node = shutil.which("node")
    if not node:
        pytest.skip("sin Node")
    js = tmp_path_factory.mktemp("web") / "prueba.js"
    js.write_text(PRUEBA, encoding="utf-8")
    out = subprocess.run([node, str(js), str(CONTROL / "disposicion.js")], capture_output=True, text=True, check=True)
    return json.loads(out.stdout)


def test_de_fabrica(cuentas):
    f = cuentas["fabrica"]
    assert f["orden"] == ["escenas", "audio", "ptz", "estado"]
    assert f["anchos"] == {"escenas": 2, "audio": 2, "ptz": 1, "estado": 1}
    assert f["ocultas"] == [] and f["multipantalla"] == {"contraida": False}


def test_lo_guardado_se_limpia(cuentas):
    g = cuentas["guardada"]
    # Sin repetidas ni desconocidas; las que faltan, al final en su orden de fábrica.
    assert g["orden"] == ["ptz", "escenas", "audio", "estado"]
    # Un ancho fuera de 1…4 vuelve al de fábrica; uno en texto se toma.
    assert g["anchos"] == {"escenas": 2, "audio": 2, "ptz": 3, "estado": 1}
    assert g["ocultas"] == ["estado"]
    assert g["multipantalla"]["contraida"] is True


def test_preferencias_de_antes(cuentas):
    assert cuentas["deAntes"]["ocultas"] == ["ptz"]
    assert cuentas["deAntes"]["orden"] == ["escenas", "audio", "ptz", "estado"]


def test_mover_ancho_ocultar_reordenar(cuentas):
    assert cuentas["moverPrimeraAtras"] is False  # la primera no va más adelante
    assert cuentas["moverAdelante"] is True
    assert cuentas["ordenMovido"] == ["audio", "escenas", "ptz", "estado"]
    assert cuentas["anchoMas"] == [True, True, True, False, 4]  # tope 4 columnas
    assert cuentas["anchoMenos"] == [False, 1]  # mínimo 1
    assert cuentas["oculta"] == [True, ["audio"], True, []]
    cambio, orden = cuentas["reordenar"]
    assert cambio is True and orden == ["estado", "ptz", "audio", "escenas"]


def test_la_pagina_carga_la_multipantalla():
    html = (CONTROL / "index.html").read_text(encoding="utf-8")
    scripts = re.findall(r'<script src="([^"]+)"', html)
    for s in ("disposicion.js", "multipantalla.js", "control.js"):
        assert s in scripts, s
    # En orden: lo común, la disposición y la multipantalla antes del tablero que las usa.
    assert scripts.index("tarjetas/comun.js") < scripts.index("multipantalla.js") < scripts.index("control.js")
    assert scripts.index("disposicion.js") < scripts.index("control.js")
    for id_ in ("multipantalla", "barraEdicion", "botonDisposicion", "botonDeFabrica", "tablero"):
        assert f'id="{id_}"' in html, id_
    for f in scripts:
        if not f.startswith("/"):
            assert (CONTROL / f).is_file(), f


def test_las_tarjetas_usan_nombres_de_hoy():
    """Los nombres viejos de herramientas (McpRenombres) siguen andando al llamar, pero el
    catálogo del aparato trae los de hoy: con uno viejo en `herramientas`, la tarjeta no se ve."""
    renombres = (REPO / "src" / "app" / "mcp" / "McpRenombres.cpp").read_text(encoding="utf-8")
    viejos = set(re.findall(r'\{"([a-z_]+)", "[a-z_]+"\}', renombres))
    assert viejos
    for js in sorted((CONTROL / "tarjetas").glob("*.js")) + [CONTROL / "multipantalla.js"]:
        usados = set(re.findall(r'"([a-z]+_[a-z_]+)"', js.read_text(encoding="utf-8")))
        assert not (usados & viejos), f"{js.name}: {sorted(usados & viejos)}"


def test_el_tema_monitores_existe():
    """La multipantalla mira «monitores»: tiene que estar en la lista de la app."""
    cpp = (REPO / "src" / "app" / "control" / "ControlWeb.cpp").read_text(encoding="utf-8")
    assert re.search(r'\{"monitores", \{"monitores_ver"\}, \d+\}', cpp)
    assert '"monitores"' in (CONTROL / "multipantalla.js").read_text(encoding="utf-8")
