"""El detector de acoples, la parte estática (docs/arquitectura/detector-de-acoples.md).

La interfaz no espera a nadie y la vista se enlaza al modelo (memory causa-de-fondo-mvc,
docs/planes/PLAN-MOTOR-SIN-ESPERAS.md). Esta prueba (con tools/diag/acoples.py, que hace la
lectura) no deja sumar caminos que no pasan por el modelo:

  - DEUDA_QML: por .qml, los bindings que llaman a una Q_INVOKABLE de C++ que devuelve algo
    (la vista no se entera cuando cambia, y la evalúa en cada vuelta). Se arregla con una
    propiedad con NOTIFY o un rol del modelo.
  - DEUDA_CPP: por «archivo|clase», en el C++ que corre en el hilo de la interfaz, lo que
    espera (bloqueante, red, proceso, disco) o entra directo (motor, runtime). Se arregla
    pidiendo por la ventanilla / la cola / un hilo propio y leyendo la foto.
  - GANCHOS: cuántos BA_ACOPLE hay por archivo (la parte dinámica, diag/Acoples.hpp): no
    pueden bajar. Y toda Q_INVOKABLE no-void que llame un binding empieza con BA_ACOPLE.

Lo nuevo falla; lo que ya estaba queda anotado con su cantidad, que SOLO PUEDE BAJAR: al
arreglar uno, se baja (o se borra) su número. `tools/diag/acoples.py` lista cada uno con
archivo:línea; `--deuda` reimprime las tablas.

Si algo nuevo de verdad no es un acople (corre en otro hilo, es el arranque detrás del
cartel), se suma a la tabla con un comentario que diga por qué.
"""
import functools
import pathlib
import sys

import areas
import pytest

RAIZ = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(RAIZ / "tools" / "diag"))
import acoples  # noqa: E402

# Bindings que llaman a una Q_INVOKABLE no-void, por .qml (relativo a src/app/). SOLO PUEDE BAJAR.
DEUDA_QML: dict[str, int] = {}

# El C++ de la interfaz que espera o entra directo, por «archivo|clase» (relativo a src/app/).
# SOLO PUEDE BAJAR. Muchos `join`/`wait` son de destructores o de hilos propios: igual se
# cuentan (un destructor que espera un hilo traba la interfaz al cerrar una página).
DEUDA_CPP = {
    "agent/AgentViewModel.cpp|bloqueante": 5,
    # 56 → 12 (2026-11-04): lo instalado, las versiones elegidas, las conversaciones guardadas
    # y la limpieza van al fondo (agent/DiscoAgentes, fondo_); la instalación, en el hilo de
    # descargas. Quedan los 12 de armar la carpeta de trabajo al lanzar el agente.
    "agent/AgentViewModel.cpp|disco": 12,
    "agent/AlarmaSonora.cpp|disco": 2,
    "agent/Despachador.cpp|bloqueante": 1,
    "agent/FlujosTexto.cpp|disco": 2,
    "agent/ModelosLocales.cpp|bloqueante": 7,
    "agent/ModelosLocales.cpp|disco": 39,
    "agent/ModelosLocales.cpp|red": 2,
    "agent/Traductor.cpp|disco": 5,
    "agent/Voz.cpp|disco": 7,
    "agent/VozPrompter.cpp|disco": 1,
    "asamblea/ConferenciaDcn.cpp|disco": 3,
    "asamblea/CuerpoViewModel.cpp|disco": 1,
    "asamblea/InformesResultado.cpp|disco": 5,
    "asamblea/SesionRemota.cpp|disco": 2,
    "audio/AudioViewModel.cpp|disco": 2,
    "audio/Calibracion.cpp|bloqueante": 2,
    "audio/Calibracion.cpp|disco": 10,
    "audio/Matriz.cpp|bloqueante": 2,
    "control/Botonera.cpp|disco": 1,
    "control/CarasBotonera.cpp|disco": 5,
    "escenas/ControlGrillas.cpp|bloqueante": 2,
    "escenas/ControlGrillas.cpp|motor": 1,
    "medios/Playlist.cpp|disco": 3,
    "medios/Playlist.cpp|motor": 10,
    "medios/Playlist.cpp|runtime": 3,
    "net/Descargas.cpp|bloqueante": 5,
    "net/Descargas.cpp|disco": 7,
    "net/Descargas.cpp|red": 2,
    "plugins/Plugins.cpp|disco": 50,
    "plugins/Plugins.cpp|motor": 7,
    "plugins/Plugins.cpp|runtime": 2,
    "red/Red.cpp|bloqueante": 1,
    "term/TerminalSession.cpp|disco": 1,
    "tiempos/TiemposModel.cpp|bloqueante": 2,
    "ui/AlarmasViewModel.cpp|runtime": 2,
    "ui/AppViewModel.cpp|disco": 3,
    "ui/AppViewModel.cpp|runtime": 1,
    "ui/AsambleaViewModel.cpp|disco": 1,
    "ui/BitacoraEvento.cpp|bloqueante": 3,
    "ui/BitacoraEvento.cpp|disco": 8,
    "ui/CapacidadViewModel.cpp|disco": 2,
    "ui/ConduccionLinea.cpp|disco": 1,
    "ui/ConduccionLinea.cpp|runtime": 4,
    "ui/ConferenceClient.cpp|bloqueante": 2,
    "ui/ConferenceClient.cpp|disco": 1,
    "ui/ControlViewModel.cpp|motor": 8,
    "ui/ControlViewModel.cpp|runtime": 1,
    "ui/CuentasViewModel.cpp|disco": 1,
    "ui/Discursos.cpp|disco": 20,
    "ui/DocumentosViewModel.cpp|disco": 4,
    "ui/EjecucionLinea.cpp|disco": 2,
    "ui/EjecucionLinea.cpp|motor": 2,
    "ui/EjecucionLinea.cpp|runtime": 5,
    "ui/EmisionViewModel.cpp|runtime": 1,
    "ui/EscenasViewModel.cpp|motor": 2,
    "ui/EscenasViewModel.cpp|runtime": 3,
    "ui/EventoAbierto.cpp|disco": 3,
    "ui/EventoViewModel.cpp|runtime": 1,
    "ui/EventosListaViewModel.cpp|bloqueante": 2,
    "ui/ExtensionesModel.cpp|disco": 3,
    "ui/FrameService.cpp|bloqueante": 2,
    "ui/GuionViewModel.cpp|bloqueante": 1,
    "ui/GuionViewModel.cpp|disco": 1,
    "ui/HistoricoConsulta.cpp|disco": 2,
    "ui/HistoricoViewModel.cpp|bloqueante": 1,
    "ui/InformeEventoViewModel.cpp|disco": 1,
    "ui/InputsViewModel.cpp|bloqueante": 1,
    "ui/InputsViewModel.cpp|motor": 1,
    "ui/LectorDocumentos.cpp|bloqueante": 1,
    "ui/ManualesViewModel.cpp|disco": 6,
    "ui/PantallasViewModel.cpp|bloqueante": 1,
    "ui/PantallasViewModel.cpp|red": 1,
    "ui/PdfEscena.cpp|motor": 4,
    "ui/PdfEscena.cpp|runtime": 2,
    "ui/PersonasViewModel.cpp|disco": 1,
    "ui/PrompterViewModel.cpp|disco": 1,
    "ui/PropertiesModel.cpp|motor": 2,
    "ui/PropertiesModel.cpp|runtime": 1,
    "ui/QrViewModel.cpp|disco": 1,
    "ui/Reconocedor.cpp|bloqueante": 2,
    "ui/RecursosViewModel.cpp|runtime": 1,
    "ui/RegistroViewModel.cpp|bloqueante": 1,
    "ui/SettingsModel.cpp|motor": 9,
    "ui/SettingsModel.cpp|runtime": 6,
    "ui/StudioViewModel.cpp|runtime": 7,
    "ui/TemaEventoViewModel.cpp|disco": 2,
    "ui/WebEventoViewModel.cpp|bloqueante": 1,
    "ui/WebEventoViewModel.cpp|disco": 1,
    "video/BancoSuperRes.cpp|bloqueante": 2,
    "video/SuperRes.cpp|disco": 56,
}

# Los BA_ACOPLE por archivo (relativo a src/). NO PUEDEN BAJAR: sacar uno deja un camino sin
# sabotaje (la parte dinámica no lo ve). Sumar uno no hace falta anotarlo, pero conviene.
# Se baja SOLO cuando desaparece el camino entero (la Q_INVOKABLE dejó de serlo porque la vista
# ahora lee una propiedad): 2026-11-03, GuionViewModel::enlaceDeCue (→ enlacesPrompter),
# RielConfig::paginaDeId/idDePagina/tituloDePagina (→ paginaPorId/idPorPagina/tituloPorPagina) y
# PrompterViewModel::avisoParaOrador (→ avisoPuesto), ConferenceViewModel::nameOf (→ el rol toName
# del chat y `nombres`) y MultiviewViewModel::de (→ porId; además `elegida` pasaba por ahí).
# 2026-11-03 (la deuda QML a cero): CuentasViewModel::accountGuide (→ guias), SettingsModel::
# seccionVisible/tieneBasicos/tieneAvanzados (→ titulosSecciones, seccionesConBasicos/
# ConAvanzados), AccesoRemoto::instruccionesDns (→ InstruccionesDns), DashboardModel::kindTitle
# (→ titulosDeKind), ExtensionesModel::ficha/vista/tituloDeKind/motivoNoCarga (→ fichas,
# titulos), PersonasViewModel::etiquetaDe (→ etiquetaFeedAuto/Puerta), PantallasViewModel::urlDe
# (→ UrlDePantalla), MediosViewModel::carpetaDeMedios (→ propiedad), InformesResultado::vistaUi
# (→ vistas + pedirVista), Botonera::buscarUi/herramientasUi (→ busquedaHerramientas/
# puntajesHerramientas, nombresHerramientas), VideoViewModel::propiedadesModuloCon (→
# EditorModulo), Vumetro::fraccion (→ marcasEscala), ConferenceViewModel::reloj (→ Bancas.reloj),
# CorridaViewModel::relojEn (→ relojVivo), MixerModel::delGrupo (→ tirasPorGrupo) y
# MonitoresModel::idDePantalla (→ idsPorSistema).
GANCHOS = {
    "app/emision/TurnoMotor.cpp": 1,
    "app/net/HttpsClient.cpp": 3,
    "app/ui/JsonListModel.cpp": 1,
    "app/ui/ListaObjetos.cpp": 1,
    "app/ui/MediosViewModel.cpp": 1,
    "core/persist/Escritor.cpp": 4,
    "motor/Candado.cpp": 1,
}


# Dos partes, cada una con su prueba en CMake (cmake/BaTestsPy.cmake): lo de la QML (los
# bindings y las Q_INVOKABLE de toda la app que llaman) y, por área (BA_AREA, tests/areas.py, la
# marca `area`), el C++ de la interfaz y los ganchos de cada archivo.
@functools.lru_cache(maxsize=1)
def _hallazgos_qml():
    """Una sola lectura de la QML y los .hpp de la app para todas las pruebas."""
    nonvoid = acoples.invocables()
    return nonvoid, acoples.acoples_qml(nonvoid)


@functools.lru_cache(maxsize=1)
def _hallazgos_cpp():
    return acoples.acoples_cpp(areas.mira)


def _nuevos(cuenta, deuda, hallazgos, qml):
    nuevos = {k: (n, deuda.get(k, 0)) for k, n in cuenta.items() if n > deuda.get(k, 0)}
    if not nuevos:
        return ""
    lineas = []
    for h in hallazgos:
        a = h.archivo.removeprefix("src/app/")
        if (a if qml else f"{a}|{h.tipo}") in nuevos:
            lineas.append(str(h))
    return "\n".join(f"{k}: {n} (deuda {d})" for k, (n, d) in nuevos.items()) + "\n\n" + "\n".join(lineas)


def test_qml_sin_bindings_nuevos_a_funciones():
    _, qml = _hallazgos_qml()
    error = _nuevos(acoples.cuenta(qml, True), DEUDA_QML, qml, True)
    assert not error, ("bindings nuevos que llaman a una Q_INVOKABLE que devuelve algo (enlazá a una propiedad con "
                       "NOTIFY o a un rol; ver la cabecera):\n" + error)


@pytest.mark.area
def test_cpp_de_la_interfaz_sin_esperas_nuevas():
    cpp = _hallazgos_cpp()
    error = _nuevos(acoples.cuenta(cpp, False), _deuda_cpp(), cpp, False)
    assert not error, ("esperas o entradas directas nuevas en el C++ de la interfaz (ventanilla/foto, la cola de "
                       "persist, un hilo propio; ver la cabecera):\n" + error)


def _deuda_cpp():
    """Las filas de DEUDA_CPP de esta corrida (claves «archivo|categoría», relativas a src/app/)."""
    return areas.deuda(DEUDA_CPP, "app/", lambda k: k.split("|", 1)[0])


def test_la_deuda_qml_solo_baja():
    _, qml = _hallazgos_qml()
    cq = acoples.cuenta(qml, True)
    sobra = {k: (n, cq.get(k, 0)) for k, n in DEUDA_QML.items() if cq.get(k, 0) < n}
    assert not sobra, f"ya se arreglaron (bajá su número en DEUDA_QML): {sobra}"


@pytest.mark.area
def test_la_deuda_cpp_solo_baja():
    cc = acoples.cuenta(_hallazgos_cpp(), False)
    sobra = {k: (n, cc.get(k, 0)) for k, n in _deuda_cpp().items() if cc.get(k, 0) < n}
    assert not sobra, f"ya se arreglaron (bajá su número en DEUDA_CPP): {sobra}"


@pytest.mark.area
def test_los_ganchos_no_bajan():
    hay = acoples.ganchos(areas.mira)
    faltan = {f: (n, hay.get(f, 0)) for f, n in areas.deuda(GANCHOS).items() if hay.get(f, 0) < n}
    assert not faltan, f"se sacaron ganchos BA_ACOPLE (el detector dinámico deja de verlos): {faltan}"


def test_cada_invocable_de_un_binding_tiene_su_gancho():
    nonvoid, qml = _hallazgos_qml()
    faltan = acoples.invocables_sin_gancho(qml, nonvoid)
    assert not faltan, ("Q_INVOKABLE que devuelven algo, llamadas desde un binding, sin BA_ACOPLE(\"invocable\", "
                        "\"Clase::metodo\") en la primera línea del cuerpo:\n" + "\n".join(faltan))


def test_el_lector_qml_ve_un_binding_y_no_un_manejador():
    t = acoples.limpiar_js('''
Item {
    property var vm: Studio.sala
    width: vm.ancho(3)              // binding: cuenta
    onClicked: vm.ancho(4)          // manejador: no
    property var f: () => vm.ancho(5)   // función guardada: no
    model: [1, 2].filter(x => Studio.sala.ancho(x) > 0)  // corre ya: cuenta
    function g() { return vm.ancho(6) }
    height: g()                     // indirecto (lo arma acoples_qml)
}
''')
    lec = acoples.LectorQml(t).leer()
    nonvoid = {"ancho": [{"clase": "X", "devuelve": "int", "archivo": "x.hpp", "linea": 1}]}
    vistos = []
    for prop, a, b in lec.bindings:
        for tipo, _q in acoples._llamadas_cpp(acoples.sin_funciones(t[a:b]), lec.alias, nonvoid, set(lec.funciones)):
            vistos.append((prop, tipo))
    assert ("width", "directo") in vistos
    assert ("model", "directo") in vistos
    assert all(p not in ("f", "onClicked") for p, _ in vistos)
    assert "g" in lec.funciones
