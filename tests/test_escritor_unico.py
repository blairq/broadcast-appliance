"""Un solo escritor para el estado (docs/arquitectura/04-plan-unificado.md, F1-01).

Todo lo que se guarda en Config/ y Runtime/ pasa por `ba::persist` (src/core/persist/):
`persist::escribir` (temporal + fsync + rename + la carpeta; F_FULLFSYNC en macOS para lo
crítico; FlushFileBuffers + MoveFileExW(WRITE_THROUGH) en Windows) o, desde el hilo de la
UI, `persist::cola()` (el hilo escritor) y `config::guardar`. Un `std::ofstream` o un
`QSaveFile` a mano no baja nada al disco: tras un corte de luz el archivo puede volver
vacío o a medias, y «todo vuelve solo como estaba» (T47) deja de ser cierto.

Esta prueba no deja sumar escritores nuevos: cuenta los `std::ofstream`/`QSaveFile`, los
que agregan al final (`std::ios::app`) y los `fopen`/`_wfopen` que escriben, de src/ (fuera
de src/core/persist/ y de las pruebas), y falla si aparece uno que no está en DEUDA. Lo que
ya estaba queda anotado con su cantidad, que solo puede bajar: al migrar un archivo, se
baja (o se borra) su número acá. Un diario (líneas que solo se suman) también va por el
escritor: `persist::agregar` o `persist::cola().agregar`.

Si lo nuevo de verdad no es estado (exportar un documento que pidió el operador, un WAV que
se graba de a poco), se suma a DEUDA con un comentario que diga por qué.
"""
import pathlib
import re

import areas
import pytest

# Por área (BA_AREA, tests/areas.py): cada archivo se cuenta en la suya.
pytestmark = pytest.mark.area

RAIZ = pathlib.Path(__file__).resolve().parent.parent
ESCRITOR = re.compile(r"\bstd::ofstream\b|\bofstream\s*[a-zA-Z_(]|\bQSaveFile\s+\w+\s*\(|\bQSaveFile\s+\w+\s*;"
                      r"|\bios::app\b|\b_?w?fopen\s*\([^;]*,\s*L?\"[wa]")

# Lo de antes del escritor único, por archivo (relativo a src/). SOLO PUEDE BAJAR.
DEUDA = {
    # No es estado: la sonda de «¿se puede escribir en esta carpeta?» antes de grabar (se borra al toque).
    "motor/Salidas.cpp": 1,
    "app/agent/AlarmaSonora.cpp": 1,
    "app/agent/Traductor.cpp": 1,
    "app/asamblea/Cuerpo.cpp": 1,
    "app/asamblea/InformesResultado.cpp": 1,
    "app/audio/Calibracion.cpp": 2,
    "app/audio/GrabadorWav.cpp": 3,  # el WAV se escribe de a poco: no es estado
    "app/audio/GrabadorWav.hpp": 1,
    "app/escritorio/control_linux.cpp": 1,
    "app/graficos/Catalogo.cpp": 1,
    "app/graficos/Medios.cpp": 1,
    "app/medios/Playlist.cpp": 1,
    "app/mcp/McpTraza.cpp": 2,  # la traza: un diario de cada llamada (sin fsync por línea)
    "app/net/Descargas.cpp": 1,
    "app/plugins/Plugins.cpp": 2,
    "app/registro/Totems.cpp": 1,
    "app/sala/ListaPrompters.cpp": 1,
    "app/video/SuperRes.cpp": 5,
    # Crea la clave una vez, exclusiva (fopen "wbx": si otro proceso ganó, se usa la suya);
    # y borra el archivo viejo pisándolo con ceros.
    "core/security/CredentialStore.cpp": 2,
    "gateway/ShareStore.cpp": 1,
    # El .json de diagnóstico al lado de un volcado o de las pilas de una traba: no es estado.
    "core/diag/Caidas.cpp": 1,
    "launcher/Integrity.cpp": 1,
    "launcher/ShadowCopy.cpp": 1,
    "launcher/Supervisor.cpp": 3,
    "modelo-llama/main.cpp": 1,
    # Los registros y diarios de diagnóstico (se agregan al final, sin fsync por línea: no son
    # estado que vuelva tras un corte). Antes no se contaban; ahora sí (solo pueden bajar).
    "app/asamblea/SesionRemota.cpp": 1,
    "app/dispositivos/SondaDrivers.cpp": 1,
    "app/escritorio/control_archivos.cpp": 1,
    "app/evento/Bitacora.cpp": 2,
    "app/net/HttpsClient.cpp": 1,  # una descarga que sigue donde quedó
    "app/term/TerminalSession.cpp": 1,
    "core/diag/LogRotator.cpp": 1,
    "core/diag/Perfil.cpp": 1,
    "launcher/ChildOutput.cpp": 1,
    "launcher/Log.cpp": 2,
}


def contar():
    cuenta = {}
    for f in sorted((RAIZ / "src").rglob("*")):
        if f.suffix not in (".cpp", ".hpp", ".h", ".mm") or "tests" in f.parts:
            continue
        rel = f.relative_to(RAIZ / "src").as_posix()
        if rel.startswith("core/persist/") or not areas.mira(f):
            continue
        for linea in f.read_text(encoding="utf-8", errors="replace").splitlines():
            codigo = linea.split("//")[0]
            if codigo.lstrip().startswith(("#include", "*", "///")):
                continue
            if ESCRITOR.search(codigo):
                cuenta[rel] = cuenta.get(rel, 0) + 1
    return cuenta


def test_sin_escritores_nuevos():
    cuenta = contar()
    deuda = areas.deuda(DEUDA)
    nuevos = {f: n for f, n in cuenta.items() if n > deuda.get(f, 0)}
    assert not nuevos, (
        "std::ofstream/QSaveFile nuevos (usá ba::persist::escribir, persist::cola() o config::guardar; "
        f"ver la cabecera de esta prueba): {nuevos}")


def test_la_deuda_solo_baja():
    cuenta = contar()
    sobra = {f: (n, cuenta.get(f, 0)) for f, n in areas.deuda(DEUDA).items() if cuenta.get(f, 0) < n}
    assert not sobra, f"ya se migraron (bajá su número en DEUDA): {sobra}"
