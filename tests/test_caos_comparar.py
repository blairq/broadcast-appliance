"""El comparador de las corridas nocturnas del banco de caos (tools/caos/comparar.py, F4-03).

Con informes falsos (nunca la app): qué es regresión (RTO p95 que sube más que la tolerancia,
un escenario que deja de medir, «no volvió»), qué es ruido, qué no cuenta (lo salteado), cuál
es «la corrida anterior» en Logs/caos y el código de salida. Y el informe del banco con
--nocturno: se compara antes de escribirse (no se encuentra a sí mismo) y lleva la sección.
Y la tarea de cada sistema para correrlo de noche (tools/caos/nocturno/instalar.py): apagada.
"""
from __future__ import annotations

import importlib.util
import json
import types
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent


def _modulo(nombre: str, ruta: Path):
    spec = importlib.util.spec_from_file_location(nombre, ruta)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


comparar = _modulo("caos_comparar_t", REPO / "tools" / "caos" / "comparar.py")


def esc(rto_p95=None, no_volvio=0, saltado=None, veces=5):
    reps = [{"saltado": saltado}] if saltado else [{"volvio": True, "rto_s": rto_p95} for _ in range(veces)]
    return {"repeticiones": reps, "rto_p95_s": rto_p95, "no_volvio": no_volvio, "saltado": saltado}


def informe(escenarios: dict, fecha="2026-10-30T03:00:00", version="1.0.0", nocturno=True, corte=None) -> dict:
    return {"fecha": fecha, "app": {"version": version}, "nocturno": nocturno, "corte": corte, "escenarios": escenarios}


def test_sin_anterior_solo_cuenta_no_volvio() -> None:
    c = comparar.comparar(None, informe({"kill_a": esc(4.0), "kill_b": esc(2.0)}))
    assert c["regresiones"] == [] and c["contra"] is None
    c = comparar.comparar(None, informe({"kill_a": esc(4.0, no_volvio=1)}))
    assert [r["que"] for r in c["regresiones"]] == ["no_volvio"]
    assert "no volvió 1 de 5" in c["regresiones"][0]["texto"]


def test_rto_p95_tolerancia_y_margen() -> None:
    antes = informe({"kill_a": esc(10.0), "kill_b": esc(0.4), "stop_a": esc(20.0), "sumidero_tx": esc(8.0),
                     "codificador_tx": esc(3.0)})
    ahora = informe({"kill_a": esc(13.0),       # +30 % y +3 s: regresión
                     "kill_b": esc(0.9),        # +125 % pero +0,5 s: ruido
                     "stop_a": esc(21.0),       # +1 s pero +5 %: ruido
                     "sumidero_tx": esc(4.0),   # bajó: mejora
                     "codificador_tx": esc(3.0)}, fecha="2026-10-31T03:00:00", version="1.0.1")
    c = comparar.comparar(antes, ahora)
    assert [(r["escenario"], r["que"]) for r in c["regresiones"]] == [("kill_a", "rto_p95")]
    assert c["regresiones"][0]["antes"] == 10.0 and c["regresiones"][0]["ahora"] == 13.0
    assert [m["escenario"] for m in c["mejoras"]] == ["sumidero_tx"]
    assert c["contra"] == {"fecha": "2026-10-30T03:00:00", "app": "1.0.0"}
    md = "\n".join(comparar.markdown(c))
    assert "**kill_a**" in md and "mejoró sumidero_tx" in md and "app 1.0.0" in md


def test_deja_de_medir_y_lo_salteado() -> None:
    antes = informe({"kill_a": esc(5.0), "kill_cf": esc(None, saltado="no corre cloudflared"), "gpu_video": esc(2.0),
                     "kill_emisor": esc(3.0)})
    ahora = informe({"kill_a": esc(None),                                  # antes volvía; ahora sin RTO
                     "kill_cf": esc(9.0),                                  # antes salteado: nada que comparar
                     "gpu_video": esc(None, saltado="sin GPU aparte"),     # ahora salteado: no cuenta
                     "kill_emisor": esc(3.0, no_volvio=2),                 # no volvió: siempre
                     "nuevo": esc(50.0)})                                  # nuevo: sin vara
    c = comparar.comparar(antes, ahora)
    assert sorted((r["escenario"], r["que"]) for r in c["regresiones"]) == [("kill_a", "sin_rto"), ("kill_emisor", "no_volvio")]


def test_cual_es_la_anterior(tmp_path: Path) -> None:
    caos = tmp_path / "Logs" / "caos"

    def poner(carpeta: str, j: dict | str) -> Path:
        d = caos / carpeta
        d.mkdir(parents=True)
        (d / "caos.json").write_text(j if isinstance(j, str) else json.dumps(j), encoding="utf-8")
        return d

    assert comparar.anterior(str(caos)) is None  # ni existe la carpeta
    poner("20261028-030000", informe({}, fecha="28"))
    buena = poner("20261029-030000", informe({}, fecha="29"))
    poner("20261030-030000", informe({}, fecha="30", corte="la app no contesta"))  # cortó: no es la vara
    poner("20261030-150000", informe({}, fecha="30b", nocturno=False))             # a pedido: tampoco
    poner("20261030-160000", "{roto")                                             # ilegible: se saltea
    actual = poner("20261031-030000", informe({}, fecha="31"))
    assert comparar.anterior(str(caos), str(actual)) == str(buena / "caos.json")
    assert comparar.anterior(str(caos)) == str(actual / "caos.json")


def test_codigo_de_salida(tmp_path: Path) -> None:
    a = tmp_path / "a.json"
    b = tmp_path / "b.json"
    a.write_text(json.dumps(informe({"kill_a": esc(10.0)})), encoding="utf-8")
    b.write_text(json.dumps(informe({"kill_a": esc(10.5)})), encoding="utf-8")
    assert comparar.main([str(a), str(b)]) == 0
    b.write_text(json.dumps(informe({"kill_a": esc(30.0)})), encoding="utf-8")
    assert comparar.main([str(a), str(b)]) == comparar.REGRESION == 3
    assert comparar.main([str(a), str(tmp_path / "no-esta.json")]) == 2
    # Sin anterior legible: se compara contra nada (solo «no volvió»).
    assert comparar.main([str(tmp_path / "no-esta.json"), str(b)]) == 0


def test_informe_nocturno_del_banco(tmp_path: Path) -> None:
    banco = _modulo("banco_caos_t", REPO / "tools" / "caos" / "banco.py")
    caos = tmp_path / "caos"

    def correr(carpeta: str, rto: float) -> dict:
        reps = [{"volvio": True, "rto_s": rto} for _ in range(5)]
        b = types.SimpleNamespace(resultados={"kill_a": reps}, a=types.SimpleNamespace(plazo_s=90.0),
                                  app=types.SimpleNamespace(version=lambda: {"version": "1.2.3", "generado": "hoy"}))
        return banco.informe(b, str(caos / carpeta), {}, [], [], None, nocturno=True)

    j1 = correr("20261030-030000", 4.0)
    assert j1["nocturno"] and j1["comparacion"]["regresiones"] == [] and j1["comparacion"]["informe_anterior"] is None
    j2 = correr("20261031-030000", 9.0)
    assert j2["comparacion"]["informe_anterior"] == str(caos / "20261030-030000" / "caos.json")
    assert [r["escenario"] for r in j2["comparacion"]["regresiones"]] == ["kill_a"]
    md = (caos / "20261031-030000" / "caos.md").read_text(encoding="utf-8")
    assert "## Contra la corrida anterior" in md and "**kill_a**" in md
    guardado = json.loads((caos / "20261031-030000" / "caos.json").read_text(encoding="utf-8"))
    assert guardado["nocturno"] is True and guardado["app"]["version"] == "1.2.3"


def test_tarea_nocturna_apagada(tmp_path: Path) -> None:
    """La tarea de cada sistema (tools/caos/nocturno/instalar.py) sale APAGADA y bien formada."""
    import plistlib
    import xml.etree.ElementTree as ET

    inst = _modulo("caos_instalar_t", REPO / "tools" / "caos" / "nocturno" / "instalar.py")
    cmd = ["/py thon", "-I", "-B", "/repo/tools/caos/banco.py", "--lanzar", '/apps/la "app"', "--nocturno"]
    # Mac: launchd con Disabled y la hora.
    _, texto, prender, _ = inst.mac(cmd, 2, 30)
    p = plistlib.loads(texto.encode("utf-8"))
    assert p["Disabled"] is True and p["ProgramArguments"] == cmd
    assert p["StartCalendarInterval"] == {"Hour": 2, "Minute": 30} and any("enable" in c for c in prender)
    # Linux: el servicio con las comillas de systemd y el timer sin habilitar.
    _, texto, prender, _ = inst.linux(cmd, 3, 0)
    escritos = inst.escribir("Linux", str(tmp_path / "systemd" / "ba-caos-nocturno.service"), texto)
    servicio = Path(escritos[0]).read_text(encoding="utf-8")
    timer = Path(escritos[1]).read_text(encoding="utf-8")
    assert 'ExecStart="/py thon" "-I"' in servicio and '"/apps/la \\"app\\""' in servicio
    assert "OnCalendar=*-*-* 03:00:00" in timer and "[Install]" in timer and any("enable --now" in c for c in prender)
    # Windows: el XML del Programador de tareas, deshabilitado y solo con la sesión abierta.
    _, texto, prender, _ = inst.windows(cmd, 3, 0)
    ns = {"t": "http://schemas.microsoft.com/windows/2004/02/mit/task"}
    raiz = ET.fromstring(texto.replace('encoding="UTF-16"', ""))
    assert raiz.find("t:Settings/t:Enabled", ns).text == "false"
    assert raiz.find("t:Principals/t:Principal/t:LogonType", ns).text == "InteractiveToken"
    assert "--nocturno" in raiz.find("t:Actions/t:Exec/t:Arguments", ns).text
    assert any("/ENABLE" in c for c in prender)
