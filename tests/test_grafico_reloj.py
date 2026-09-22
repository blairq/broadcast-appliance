"""El reloj del renderizador tiene que ser exacto y reproducible.

Es la propiedad de la que cuelga todo lo demás: la vista previa de la LLM, la
barra de tiempo con arrastre del IDE y dibujar al framerate del programa. Si
«dibujá el cuadro de los 400 ms» no da siempre lo mismo, nada de eso sirve.

La prueba mira una caja que se desplaza 300 px en 1 s y comprueba dónde está
en cada instante. Costó llegar acá (ver el encabezado de Renderizador.cpp),
así que conviene que quede cubierto.
"""
from __future__ import annotations

import base64
import io
import json
import shutil
import subprocess
from pathlib import Path

import pytest

# El QML de prueba vive al lado del renderizador, no acá.
QML_REL = "src/grafico/tests/reloj.qml"


def _renderizador(repo_root: Path) -> Path:
    for c in (repo_root / "build/src/grafico/ba-grafico", repo_root / "release/bin/64bit/ba-grafico"):
        if c.is_file():
            return c
    pytest.skip("ba-grafico no está compilado")


def _tira(binario: Path, qml: Path, momentos: list[int]) -> dict[int, "Image.Image"]:
    Image = pytest.importorskip("PIL.Image", reason="hace falta Pillow para mirar los cuadros")
    pedido = {
        "op": "tira",
        "qml": str(qml),
        "parametros": {},
        "ancho": 400,
        "alto": 100,
        "momentos": momentos,
    }
    r = subprocess.run(
        [str(binario)],
        input=json.dumps(pedido) + '\n{"op":"salir"}\n',
        capture_output=True,
        text=True,
        timeout=180,
    )
    linea = next((x for x in r.stdout.splitlines() if x.strip()), "")
    assert linea, f"el renderizador no respondió nada. stderr: {r.stderr[:400]}"
    d = json.loads(linea)
    assert d.get("ok"), d.get("error")
    return {
        c["ms"]: Image.open(io.BytesIO(base64.b64decode(c["png"]))).convert("RGBA")
        for c in d["cuadros"]
    }


def _borde_izquierdo(img) -> int:
    """Dónde empieza la caja: la primera columna con algo dibujado."""
    for x in range(img.width):
        if img.getpixel((x, 50))[3] > 0:
            return x
    return -1


@pytest.fixture(scope="module")
def cuadros(repo_root: Path):
    return _tira(_renderizador(repo_root), repo_root / QML_REL, [0, 250, 500, 750, 1000])


def test_la_animacion_avanza_donde_corresponde(cuadros) -> None:
    """La caja va de 0 a 300 px en 1 s: en cada instante tiene que estar donde
    dice la cuenta, no donde quiera el reloj de pared."""
    for ms, esperado in ((0, 0), (250, 75), (500, 150), (750, 225), (1000, 300)):
        assert abs(_borde_izquierdo(cuadros[ms]) - esperado) <= 2, (
            f"a los {ms} ms la caja tendría que estar en x={esperado}"
        )


def test_dos_corridas_dan_lo_mismo(repo_root: Path) -> None:
    """Reproducible: sin esto la LLM estaría persiguiendo fantasmas."""
    qml = repo_root / QML_REL
    a = _tira(_renderizador(repo_root), qml, [0, 400, 800])
    b = _tira(_renderizador(repo_root), qml, [0, 400, 800])
    for ms in (0, 400, 800):
        assert a[ms].tobytes() == b[ms].tobytes(), f"el cuadro de {ms} ms salió distinto"


@pytest.mark.xfail(
    reason="TODAVIA NO: pedir 800 y despues 400 no da igual que al reves. El reloj es "
           "monotono (no se puede reiniciar sin que Qt vea el tiempo ir para atras), asi "
           "que el instante en que nace cada grafico depende de lo que se pidio antes. "
           "Para la vista previa alcanza -cada tira se pide de una y en orden- pero la "
           "barra de tiempo con arrastre del IDE lo va a necesitar.",
    strict=False,
)
def test_el_orden_en_que_se_piden_no_importa(repo_root: Path) -> None:
    """Pedir 800 antes que 400 tendría que dar lo mismo que al revés."""
    q = repo_root / QML_REL
    derecho = _tira(_renderizador(repo_root), q, [400, 800])
    revés = _tira(_renderizador(repo_root), q, [800, 400])
    assert derecho[400].tobytes() == revés[400].tobytes()
    assert derecho[800].tobytes() == revés[800].tobytes()


def test_un_qml_roto_se_explica(repo_root: Path, tmp_path: Path) -> None:
    """Y el error tiene que servirle a quien lo escribió."""
    roto = tmp_path / "roto.qml"
    roto.write_text("import QtGraphicalEffects 1.0\nItem { }\n", encoding="utf-8")
    r = subprocess.run(
        [str(_renderizador(repo_root))],
        input=json.dumps({"op": "validar", "qml": str(roto), "parametros": {}}) + '\n{"op":"salir"}\n',
        capture_output=True,
        text=True,
        timeout=120,
    )
    d = json.loads(next(x for x in r.stdout.splitlines() if x.strip()))
    assert not d["ok"]
    # El consejo va pegado al error: el módulo de Qt 5 es el que la LLM va a
    # escribir por inercia, y el error de Qt solo no ayuda a nadie.
    assert "QtQuick.Effects" in d["error"], d["error"]


def test_un_parametro_que_no_existe_es_un_error(repo_root: Path, tmp_path: Path) -> None:
    """Un nombre mal escrito tiene que saltar acá, no al aire."""
    r = subprocess.run(
        [str(_renderizador(repo_root))],
        input=json.dumps(
            {"op": "validar", "qml": str(repo_root / QML_REL), "parametros": {"noExiste": 1}}
        ) + '\n{"op":"salir"}\n',
        capture_output=True,
        text=True,
        timeout=120,
    )
    d = json.loads(next(x for x in r.stdout.splitlines() if x.strip()))
    assert not d["ok"]
    assert "noExiste" in d["error"]


def test_los_parametros_llegan_al_qml_escrito_a_mano(repo_root: Path) -> None:
    """Un gráfico QML mandado como TEXTO tiene que recibir sus parámetros.

    Un gráfico viaja de tres formas: archivo (`qml`), texto suelto (`fuente`) o
    descripción (`declarativo`). Al generado desde una descripción no hay que
    pasarle los parámetros —ya vienen metidos en el texto y no los declara como
    propiedades—, pero al escrito a mano SÍ, y también viaja como texto: es lo
    que manda el editor mientras se escribe y el emisor al sacarlo al aire.

    La condición estaba puesta sobre «vino como texto» en vez de sobre «lo
    generamos nosotros», así que un zócalo salía al aire siempre con el nombre
    de ejemplo y sin decir nada. Se comprueba por el lado del error: un
    parámetro que el gráfico NO declara tiene que fallar, y solo puede fallar
    si los parámetros de verdad llegaron.
    """
    binario = _renderizador(repo_root)
    fuente = 'import QtQuick\nItem { property string nombre: "x"\n Text { text: parent.nombre } }'

    def validar(parametros: dict) -> dict:
        pedido = {"op": "validar", "fuente": fuente, "parametros": parametros}
        p = subprocess.run(
            [str(binario)],
            input=json.dumps(pedido) + "\n" + json.dumps({"op": "salir"}) + "\n",
            capture_output=True,
            text=True,
            timeout=120,
        )
        return json.loads(p.stdout.strip().splitlines()[0])

    assert validar({"nombre": "Fulano"})["ok"], "un parámetro que el gráfico declara tiene que andar"

    malo = validar({"noExiste": "x"})
    assert not malo["ok"], "un parámetro que el gráfico no declara tiene que fallar, no perderse"
    assert "noExiste" in malo["error"]
