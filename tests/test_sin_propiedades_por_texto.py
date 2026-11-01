"""Nada de leer las propiedades de un QObject por su nombre desde el C++.

`objeto->property("enVivo")` compila aunque la propiedad no exista: devuelve un QVariant
vacío y el código sigue callado. Así se perdió el PIN del día (T47): la propiedad se
renombró a `guionCorriendo` y la lectura por texto quedó siempre en false; y el vínculo
`{evento.nombre}` salía vacío porque `nombre` nunca fue una propiedad del evento. Desde el
C++ se llama al método tipado (`corridaVm().guionCorriendo()`), que no compila si cambia.

Esta prueba cuenta los `property("…")` de src/ (sin extern ni pruebas). Escribir una
propiedad de la QML desde el C++ (`setProperty("page", …)` en la ventana) no cuenta: es de
la vista y no tiene un método tipado.
"""
import pathlib
import re

RAIZ = pathlib.Path(__file__).resolve().parent.parent
POR_TEXTO = re.compile(r"""(?:->|\.)\s*property\(\s*"[^"]+\"""")


def test_sin_property_por_texto():
    hallados = []
    for p in sorted((RAIZ / "src").rglob("*")):
        if p.suffix not in (".cpp", ".hpp", ".h", ".mm") or "tests" in p.parts:
            continue
        for n, linea in enumerate(p.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            if POR_TEXTO.search(linea):
                hallados.append(f"{p.relative_to(RAIZ)}:{n}: {linea.strip()}")
    assert not hallados, "Lecturas de propiedades por texto (usar el método tipado):\n" + "\n".join(hallados)


def test_pin_del_dia_en_el_flanco():
    # El PIN del día se rota en el flanco de subida de guionCorriendo, con la conexión tipada.
    studio = (RAIZ / "src/app/ui/StudioViewModel.cpp").read_text(encoding="utf-8")
    assert re.search(r"alSubir\(\s*corrida,\s*&CorridaViewModel::correChanged", studio), "falta el flanco tipado"
    assert "guionCorriendo()" in studio and "alEmpezarElVivo()" in studio


def test_vinculo_evento_nombre_tipado():
    # {evento.nombre} sale del nombre del evento abierto, leído con la llamada tipada.
    studio = (RAIZ / "src/app/ui/StudioViewModel.cpp").read_text(encoding="utf-8")
    assert re.search(r'ctx\["evento"\]\s*=\s*QJsonObject\{\{"nombre",\s*evento_->eventoAbierto\(\)\.nombre\(\)\}\}', studio)
