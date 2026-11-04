"""Los niveles del diseño físico (docs/local/PLAN-DISENO-FISICO.md; la lectura la hace
tools/diag/niveles.py, el mapa es tools/diag/niveles.txt).

  0 ba_core → 1 ba_motor, ba_gateway_* → 2 áreas de la app → 3 ba_app_ui → 4 los programas.

Un componente solo incluye y enlaza componentes de nivel MÁS BAJO; dos del mismo nivel no se
incluyen ni se enlazan entre sí. Lo que hoy no cumple queda anotado acá con su cantidad de
`#include`, por «origen -> destino», y SOLO PUEDE BAJAR: lo nuevo falla, y al arreglar uno se
baja (o se borra) su número. `tools/diag/niveles.py` lista cada uno con archivo:línea;
`--deuda` reimprime las tablas.

Se corre por partes (cmake/BaNiveles.cmake registra un test de ctest por componente, con sus
archivos como entradas: tocar un área corre solo el de esa área):

  tests/test_niveles.py --componente app/ui    los includes que salen de app/ui
  tests/test_niveles.py --enlaces              target_link_libraries / ba_biblioteca
  tests/test_niveles.py --mapa                 el mapa: que cubra todo src/ y que la deuda
                                               nombre componentes que existen
  tests/test_niveles.py                        todo (también con pytest)
"""
import pathlib
import sys

RAIZ = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(RAIZ / "tools" / "diag"))
import niveles  # noqa: E402

# Los #include que suben o cruzan entre iguales, por «componente origen -> componente destino».
# SOLO PUEDE BAJAR. Contado el 2026-11-04 sobre master b89e0247 (747 includes).
# Lo grueso: app/mcp → app/ui (206) y los modelos de área que incluyen arranque/AppRuntime.hpp
# o ui/StudioViewModel.hpp (Ola 3: interfaces Runtime*, declaraciones adelantadas); los cruces
# entre áreas (mismo nivel) se cortan con una interfaz chica abajo o por inyección.
# gateway -> app/net: src/gateway/proxy/Acme.cpp incluye ../../app/net/HttpsClient.hpp.
DEUDA_INCLUDES = {
    "app/agent -> app/asamblea": 1,
    "app/agent -> app/audio": 9,
    "app/agent -> app/carrusel": 1,
    "app/agent -> app/emision": 5,
    "app/agent -> app/plugins": 1,
    "app/agent -> app/sala": 1,
    "app/agent -> app/term": 2,
    "app/agent -> app/ui": 7,
    "app/asamblea -> app/control": 5,
    "app/asamblea -> app/emision": 1,
    "app/asamblea -> app/escenas": 1,
    "app/asamblea -> app/plugins": 1,
    "app/asamblea -> app/sala": 2,
    "app/asamblea -> app/ui": 2,
    "app/audio -> app/agent": 2,
    "app/audio -> app/dispositivos": 1,
    "app/audio -> app/emision": 17,
    "app/audio -> app/ui": 2,
    "app/audio -> app/video": 1,
    "app/control -> app/emision": 1,
    "app/control -> app/escenas": 3,
    "app/control -> app/mcp": 2,
    "app/control -> app/sala": 2,
    "app/control -> app/ui": 3,
    "app/dispositivos -> app/audio": 3,
    "app/dispositivos -> app/emision": 1,
    "app/dispositivos -> app/plugins": 1,
    "app/dispositivos -> app/ptz": 2,
    "app/dispositivos -> app/red": 1,
    "app/dispositivos -> app/ui": 5,
    "app/dispositivos -> app/video": 2,
    "app/emision -> app/agent": 1,
    "app/emision -> app/audio": 5,
    "app/emision -> app/camaras": 1,
    "app/emision -> app/escenas": 2,
    "app/emision -> app/graficos": 1,
    "app/emision -> app/ui": 3,
    "app/emision -> app/video": 2,
    "app/escenas -> app/asamblea": 2,
    "app/escenas -> app/emision": 7,
    "app/escenas -> app/sala": 1,
    "app/escenas -> app/ui": 4,
    "app/escenas -> app/video": 1,
    "app/evento -> app/carrusel": 1,
    "app/evento -> app/escenas": 2,
    "app/graficos -> app/emision": 1,
    "app/graficos -> app/ui": 1,
    "app/mcp -> app/agent": 26,
    "app/mcp -> app/asamblea": 27,
    "app/mcp -> app/audio": 15,
    "app/mcp -> app/control": 12,
    "app/mcp -> app/dispositivos": 3,
    "app/mcp -> app/emision": 24,
    "app/mcp -> app/escenas": 7,
    "app/mcp -> app/evento": 8,
    "app/mcp -> app/graficos": 6,
    "app/mcp -> app/medios": 3,
    "app/mcp -> app/net": 2,
    "app/mcp -> app/pantallas": 8,
    "app/mcp -> app/plugins": 2,
    "app/mcp -> app/ptz": 4,
    "app/mcp -> app/red": 1,
    "app/mcp -> app/resolucion": 1,
    "app/mcp -> app/rostros": 1,
    "app/mcp -> app/sala": 16,
    "app/mcp -> app/tiempos": 1,
    "app/mcp -> app/ui": 184,
    "app/mcp -> app/video": 17,
    "app/medios -> app/emision": 1,
    "app/medios -> app/ui": 1,
    "app/net -> app/emision": 1,
    "app/net -> app/evento": 1,
    "app/net -> app/plugins": 1,
    "app/net -> app/sala": 1,
    "app/net -> app/ui": 1,
    "app/pantallas -> app/ui": 3,
    "app/plugins -> app/control": 1,
    "app/plugins -> app/emision": 1,
    "app/plugins -> app/ptz": 2,
    "app/plugins -> app/ui": 4,
    "app/plugins -> app/video": 2,
    "app/ptz -> app/emision": 11,
    "app/ptz -> app/ui": 5,
    "app/red -> app/emision": 2,
    "app/red -> app/ptz": 3,
    "app/red -> app/ui": 1,
    "app/red -> app/video": 2,
    "app/sala -> app/emision": 8,
    "app/sala -> app/evento": 1,
    "app/sala -> app/ui": 21,
    "app/video -> app/emision": 7,
    "app/video -> app/plugins": 1,
    "app/video -> app/ui": 2,
    "app/vivos -> app/facebook": 1,
    "app/vivos -> app/twitch": 1,
    "app/vivos -> app/youtube": 1,
    "gateway -> app/net-base": 1,
}

# Los target_link_libraries (y DEPENDE de ba_biblioteca) que suben o cruzan entre iguales,
# por «target -> target». SOLO PUEDE BAJAR. Hoy, ninguno.
DEUDA_ENLACES: dict[str, int] = {}


# --- los chequeos (devuelven los errores; vacío = bien) ---------------------------------------
def _comparar(cuenta: dict[str, int], deuda: dict[str, int], violaciones, que: str) -> list[str]:
    errores = []
    nuevos = {k: (n, deuda.get(k, 0)) for k, n in cuenta.items() if n > deuda.get(k, 0)}
    if nuevos:
        errores.append(f"{que} nuevos hacia arriba o entre iguales (el nivel de abajo no conoce al de arriba; "
                       "si hace falta, una interfaz chica abajo o inyección por constructor):")
        errores += [f"  {k}: {n} (deuda {d}) [{next(v.tipo for v in violaciones if v.clave == k)}]"
                    for k, (n, d) in nuevos.items()]
        errores += [f"    {v}" for v in violaciones if v.clave in nuevos]
    bajaron = {k: (d, cuenta.get(k, 0)) for k, d in deuda.items() if cuenta.get(k, 0) < d}
    if bajaron:
        errores.append(f"ya se arreglaron {que} (bajá su número en la deuda de tests/test_niveles.py):")
        errores += [f"  {k}: {d} → {n}" for k, (d, n) in bajaron.items()]
    return errores


def chequear_componente(nombre: str, mapa: niveles.Mapa | None = None) -> list[str]:
    mapa = mapa or niveles.Mapa.leer()
    comp = mapa.por_nombre.get(nombre)
    if comp is None:
        return [f"no hay un componente {nombre} en tools/diag/niveles.txt"]
    archivos = niveles.archivos_del_componente(mapa, comp)
    vs = niveles.violaciones_includes(mapa, niveles.Grafo(mapa), archivos)
    deuda = {k: n for k, n in DEUDA_INCLUDES.items() if k.split(" -> ")[0] == nombre}
    return _comparar(niveles.cuenta(vs), deuda, vs, f"includes de {nombre}")


def chequear_enlaces(mapa: niveles.Mapa | None = None) -> list[str]:
    mapa = mapa or niveles.Mapa.leer()
    vs, errores = niveles.violaciones_enlaces(mapa)
    return errores + _comparar(niveles.cuenta(vs), DEUDA_ENLACES, vs, "enlaces")


def chequear_mapa() -> list[str]:
    try:
        mapa = niveles.Mapa.leer()
    except niveles.ErrorMapa as e:
        return [str(e)]
    errores = [f"src/{a}: sin componente en tools/diag/niveles.txt (sumale una línea, con su nivel)"
               for a in niveles.sin_componente(mapa, niveles.archivos_de_codigo())]
    for k in DEUDA_INCLUDES:
        for c in k.split(" -> "):
            if c not in mapa.por_nombre:
                errores.append(f"DEUDA_INCLUDES nombra {c}, que no está en el mapa: {k}")
    for c in mapa.componentes:
        for r in c.rutas:
            base = niveles.SRC / r.split("*")[0]
            if "*" not in r and not base.exists():
                errores.append(f"tools/diag/niveles.txt:{c.linea}: {c.nombre}: no existe src/{r}")
    return errores


def chequear_todo() -> list[str]:
    errores = chequear_mapa()
    if errores:
        return errores
    mapa = niveles.Mapa.leer()
    for c in mapa.componentes:
        errores += chequear_componente(c.nombre, mapa)
    return errores + chequear_enlaces(mapa)


# --- pytest ---------------------------------------------------------------------------------
def test_mapa_cubre_src():
    errores = chequear_mapa()
    assert not errores, "\n".join(errores)


def test_includes_por_nivel():
    mapa = niveles.Mapa.leer()
    errores = []
    for c in mapa.componentes:
        errores += chequear_componente(c.nombre, mapa)
    assert not errores, "\n".join(errores)


def test_enlaces_por_nivel():
    errores = chequear_enlaces()
    assert not errores, "\n".join(errores)


# El lector, con un mapa y un CMake de juguete.
MAPA_JUGUETE = """
componente core     0   core/            : ba_core
componente motor    1   motor/           : ba_motor
componente emisor   1.5 emisor/Pedido.*  : ba_emisor_pedido
componente app/a    2   app/a/
componente app/b    2   app/b/
componente app/ui   3   app/ui/ app/arranque/
componente app/main 4   app/main.cpp     : ba-app
raiz app/ app core
"""


def test_el_mapa_elige_la_ruta_mas_larga():
    m = niveles.Mapa.desde_texto(MAPA_JUGUETE)
    assert m.componente_de("app/a/X.hpp").nombre == "app/a"
    assert m.componente_de("app/arranque/AppRuntime.hpp").nombre == "app/ui"
    assert m.componente_de("app/main.cpp").nombre == "app/main"
    assert m.componente_de("emisor/Pedido.hpp").nombre == "emisor"
    assert m.componente_de("emisor/main.cpp") is None
    assert m.componente_de("app/c/Y.cpp") is None
    assert m.componente_de("emisor/Pedido.hpp").nivel == 1.5
    assert m.raices_de("app/a/X.cpp") == ["app", "core"]
    assert m.raices_de("motor/M.cpp") == ["core"]


def test_el_mapa_no_acepta_rutas_ni_targets_repetidos():
    for malo in ("componente a 0 x/\ncomponente b 1 x/\n", "componente a 0 x/ : t\ncomponente b 1 y/ : t\n",
                 "componente a cero x/\n", "otra cosa\n"):
        try:
            niveles.Mapa.desde_texto(malo)
        except niveles.ErrorMapa:
            continue
        raise AssertionError(f"aceptó: {malo!r}")


def _llamada(funcion, *args, archivo="src/app/CMakeLists.txt", linea=1):
    return niveles.Llamada(archivo, linea, funcion, list(args))


def test_enlaces_hacia_arriba_y_entre_iguales():
    m = niveles.Mapa.desde_texto(MAPA_JUGUETE)
    ll = [_llamada("target_link_libraries", "ba_motor", "PUBLIC", "ba_core", "Qt6::Core"),   # bien
          _llamada("target_link_libraries", "ba_core", "PRIVATE", "ba_motor"),               # arriba
          _llamada("target_link_libraries", "ba-app", "PRIVATE", "ba_motor", "ba_emisor_pedido")]  # bien
    vs, errores = niveles.violaciones_enlaces(m, ll)
    assert not errores
    assert [(v.clave, v.tipo) for v in vs] == [("ba_core -> ba_motor", "arriba")]


def test_ba_biblioteca_registra_su_nivel_y_se_revisa_contra_el_mapa():
    m = niveles.Mapa.desde_texto(MAPA_JUGUETE)
    ll = [_llamada("ba_biblioteca", "ba_app_a", "NIVEL", "2", "DEPENDE", "ba_core", "ba_app_b",
                   "FUENTES", "a/Uno.cpp", "a/Dos.cpp"),
          _llamada("ba_biblioteca", "ba_app_b", "NIVEL", "2", "DEPENDE", "ba_motor", "FUENTES", "b/Tres.cpp"),
          _llamada("ba_biblioteca", "ba_app_mal", "NIVEL", "2", "FUENTES", "ui/Studio.cpp")]
    bibs = niveles.bibliotecas_cmake(ll)
    assert [b.nombre for b in bibs] == ["ba_app_a", "ba_app_b", "ba_app_mal"]
    assert bibs[0].fuentes == ["app/a/Uno.cpp", "app/a/Dos.cpp"] and bibs[0].depende == ["ba_core", "ba_app_b"]
    vs, errores = niveles.violaciones_enlaces(m, ll)
    assert [(v.clave, v.tipo) for v in vs] == [("ba_app_a -> ba_app_b", "mismo nivel")]
    assert len(errores) == 1 and "ba_app_mal" in errores[0] and "app/ui" in errores[0]


def test_includes_hacia_arriba_y_entre_iguales():
    m = niveles.Mapa.desde_texto(MAPA_JUGUETE)

    class GrafoFalso:
        def includes(self, rel):
            return {"app/a/X.cpp": [niveles.Include("app/a/X.cpp", 3, "app/ui/Studio.hpp", '#include "ui/Studio.hpp"'),
                                    niveles.Include("app/a/X.cpp", 4, "app/b/Y.hpp", '#include "b/Y.hpp"'),
                                    niveles.Include("app/a/X.cpp", 5, "app/a/X.hpp", '#include "X.hpp"'),
                                    niveles.Include("app/a/X.cpp", 6, "core/C.hpp", '#include "C.hpp"')]}.get(rel, [])

    vs = niveles.violaciones_includes(m, GrafoFalso(), ["app/a/X.cpp"])
    assert [(v.clave, v.tipo) for v in vs] == [("app/a -> app/ui", "arriba"), ("app/a -> app/b", "mismo nivel")]
    assert vs[0].donde == "src/app/a/X.cpp:3"


def test_la_deuda_no_sube_ni_queda_vieja():
    vs = [niveles.Violacion("a", "b", "arriba", "x:1", "#include"), niveles.Violacion("a", "b", "arriba", "x:2", "#include")]
    assert _comparar({"a -> b": 2}, {"a -> b": 2}, vs, "x") == []
    assert "nuevos" in _comparar({"a -> b": 2}, {"a -> b": 1}, vs, "x")[0]
    assert "ya se arreglaron" in _comparar({"a -> b": 2}, {"a -> b": 3}, vs, "x")[0]


# --- por partes, desde ctest ------------------------------------------------------------------
def main(argv: list[str]) -> int:
    if "--componente" in argv:
        errores = chequear_componente(argv[argv.index("--componente") + 1])
    elif "--enlaces" in argv:
        errores = chequear_enlaces()
    elif "--mapa" in argv:
        errores = chequear_mapa()
    else:
        errores = chequear_todo()
    print("\n".join(errores) if errores else "bien")
    return 1 if errores else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
