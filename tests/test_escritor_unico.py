"""Un solo escritor para el estado (docs/arquitectura/04-plan-unificado.md, F1-01).

Todo lo que se guarda en Config/ y Runtime/ pasa por `ba::persist` (src/core/persist/):
`persist::escribir` (temporal + fsync + rename + la carpeta; F_FULLFSYNC en macOS para lo
crítico; FlushFileBuffers + MoveFileExW(WRITE_THROUGH) en Windows) o, desde el hilo de la
UI, `persist::cola()` (el hilo escritor) y `config::guardar`. Un `std::ofstream` o un
`QSaveFile` a mano no baja nada al disco: tras un corte de luz el archivo puede volver
vacío o a medias, y «todo vuelve solo como estaba» (T47) deja de ser cierto.

Esta prueba no deja sumar escritores nuevos: cuenta los `std::ofstream`/`QSaveFile` de
src/ (fuera de src/core/persist/ y de las pruebas) y falla si aparece uno que no está en
DEUDA. Lo que ya estaba queda anotado con su cantidad, que solo puede bajar: al migrar un
archivo, se baja (o se borra) su número acá. Los agregados al final (`std::ios::app`, los
registros) no cuentan: son diarios, no estado que se reemplaza.

Si lo nuevo de verdad no es estado (exportar un documento que pidió el operador, un WAV que
se graba de a poco), se suma a DEUDA con un comentario que diga por qué.
"""
import pathlib
import re

RAIZ = pathlib.Path(__file__).resolve().parent.parent
ESCRITOR = re.compile(r"\bstd::ofstream\b|\bofstream\s*[a-zA-Z_(]|\bQSaveFile\s+\w+\s*\(|\bQSaveFile\s+\w+\s*;")

# Lo de antes del escritor único, por archivo (relativo a src/). SOLO PUEDE BAJAR.
DEUDA = {
    # No es estado: la sonda de «¿se puede escribir en esta carpeta?» antes de grabar (se borra al toque).
    "motor/Salidas.cpp": 1,
    "app/agent/AgentViewModel.cpp": 1,
    "app/agent/AlarmaSonora.cpp": 2,
    "app/agent/AlertEngine.cpp": 1,
    "app/agent/ControlSistema.cpp": 1,
    "app/agent/FlujosTexto.cpp": 1,
    "app/agent/ModeloPropio.cpp": 1,
    "app/agent/Notificador.cpp": 1,
    "app/agent/Reconocedores.cpp": 1,
    "app/agent/Roles.cpp": 1,
    "app/agent/SubtitulosCC.cpp": 1,
    "app/agent/Traductor.cpp": 2,
    "app/agent/Vocabulario.cpp": 1,
    "app/arranque/Configuraciones.cpp": 1,
    "app/arranque/InicioConElSistema.cpp": 1,
    "app/asamblea/ConferenciaDcn.cpp": 1,
    "app/asamblea/Cuerpo.cpp": 2,
    "app/asamblea/Debate.cpp": 1,
    "app/asamblea/InformesResultado.cpp": 1,
    "app/asamblea/OrdenDelDia.cpp": 1,
    "app/asamblea/Quorum.cpp": 2,
    "app/asamblea/SeguirMicrofono.cpp": 1,
    "app/asamblea/SesionRemota.cpp": 1,
    "app/audio/Calibracion.cpp": 2,
    "app/audio/GrabadorWav.cpp": 3,  # el WAV se escribe de a poco: no es estado
    "app/audio/GrabadorWav.hpp": 1,
    "app/audio/Matriz.cpp": 1,
    "app/audio/RackManager.cpp": 3,
    "app/carrusel/CarruselStore.cpp": 1,
    "app/control/Acciones.cpp": 1,
    "app/control/Botonera.cpp": 2,
    "app/control/ControlWeb.cpp": 1,
    "app/control/Dsk.cpp": 1,
    "app/control/SatelliteServidor.cpp": 1,
    "app/dispositivos/Conocidos.cpp": 1,
    "app/emision/BroadcastEngine.cpp": 1,
    "app/escenas/Conmutador.cpp": 1,
    "app/escenas/RespaldoEntradas.cpp": 1,
    "app/escritorio/control_linux.cpp": 1,
    "app/evento/CierreEvento.cpp": 3,
    "app/evento/Documentos.cpp": 1,
    "app/evento/EventoStore.cpp": 2,
    "app/evento/EventoWeb.cpp": 1,
    "app/evento/FotosConfig.cpp": 3,
    "app/evento/Historico.cpp": 2,
    "app/evento/Padron.cpp": 1,
    "app/evento/TemaEvento.cpp": 1,
    "app/graficos/Catalogo.cpp": 1,
    "app/graficos/Medios.cpp": 1,
    "app/medios/Playlist.cpp": 2,
    "app/mcp/McpAire.cpp": 1,
    "app/mcp/McpTraduccionTools.cpp": 1,
    "app/mcp/McpTraza.cpp": 1,
    "app/net/Descargas.cpp": 1,
    "app/plugins/Plugins.cpp": 3,
    "app/ptz/ControlCamaras.cpp": 2,
    "app/registro/Totems.cpp": 1,
    "app/sala/ListaPrompters.cpp": 1,
    "app/tiempos/TiemposModel.cpp": 1,
    "app/ui/EventoViewModel.cpp": 3,
    "app/ui/HistoricoViewModel.cpp": 1,
    "app/ui/ListModels.cpp": 1,
    "app/ui/PantallasViewModel.cpp": 1,
    "app/ui/QrViewModel.cpp": 1,
    "app/ui/StudioViewModel.cpp": 1,
    "app/video/BancoSuperRes.cpp": 1,
    "app/video/SuperRes.cpp": 5,
    "app/video/VideoGrafos.cpp": 3,
    "core/diag/DiagExport.cpp": 1,
    "core/fault/FaultInjector.cpp": 1,
    "core/hw/SaludDrivers.cpp": 1,
    "core/platform/GstEntorno.cpp": 1,
    "core/rostros/Personas.cpp": 1,
    "core/security/CredentialStore.cpp": 1,  # borra el archivo viejo pisándolo con ceros
    "gateway/ShareStore.cpp": 1,
    "launcher/Datos.cpp": 3,
    "launcher/Integrity.cpp": 1,
    "launcher/ShadowCopy.cpp": 1,
    "launcher/Supervisor.cpp": 2,
    "modelo-llama/main.cpp": 1,
}


def contar():
    cuenta = {}
    for f in sorted((RAIZ / "src").rglob("*")):
        if f.suffix not in (".cpp", ".hpp", ".h", ".mm") or "tests" in f.parts:
            continue
        rel = f.relative_to(RAIZ / "src").as_posix()
        if rel.startswith("core/persist/"):
            continue
        for linea in f.read_text(encoding="utf-8", errors="replace").splitlines():
            codigo = linea.split("//")[0]
            if codigo.lstrip().startswith(("#include", "*", "///")) or "ios::app" in codigo:
                continue
            if ESCRITOR.search(codigo):
                cuenta[rel] = cuenta.get(rel, 0) + 1
    return cuenta


def test_sin_escritores_nuevos():
    cuenta = contar()
    nuevos = {f: n for f, n in cuenta.items() if n > DEUDA.get(f, 0)}
    assert not nuevos, (
        "std::ofstream/QSaveFile nuevos (usá ba::persist::escribir, persist::cola() o config::guardar; "
        f"ver la cabecera de esta prueba): {nuevos}")


def test_la_deuda_solo_baja():
    cuenta = contar()
    sobra = {f: (n, cuenta.get(f, 0)) for f, n in DEUDA.items() if cuenta.get(f, 0) < n}
    assert not sobra, f"ya se migraron (bajá su número en DEUDA): {sobra}"
