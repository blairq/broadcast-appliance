"""La capa de mensajes DCN (común a serie y TCP): docs/bosch-dcn-ng-open-interface.md §4.

Un mensaje es ``{tipo, fn, error, datos}``; acá se arma y se desarma el ``T_MESSAGE`` del manual
(``byType, wLength, byData``) y las estructuras de las funciones que usa el plugin. Todo con el byte
menos significativo primero (``struct`` con ``'<'``).
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field

NAK = 2
REQ = 3
RSP = 4
NTF = 5
COMM_PARAMS = 15

APP_MM, APP_VT, APP_SC, APP_MD = 0, 1, 16, 10


def fn(n: int, app: int) -> int:
    """``MKWORD(n, APP)``: en el cable va primero `n` y después `APP`."""
    return (app << 8) | n


# --- Configuración del sistema (SC) -----------------------------------------------------
SC_C_GET_CCU_VERSIONINFO = 0x1006
SC_C_START_APP = 0x1007
SC_C_STOP_APP = 0x1008
SC_C_CONNECT_UNIT = 0x1009
SC_C_DISCONNECT_UNIT = 0x100A
SC_C_GET_CCU_CONFIG = 0x100C
SC_C_CCU_REBOOT = 0x100F
SC_C_CCU_MODE_CHANGE = 0x1010
SC_C_CHECK_LINK = 0x1012
SC_C_GET_CCU_CONFIG_PROPERTY = 0x1033

# --- Gestión de micrófonos (MM) ----------------------------------------------------------
MM_C_MICRO_ON_OFF = fn(1, APP_MM)
MM_C_CHAIR_MICS_ON = fn(2, APP_MM)
MM_C_TIMER_ON_OFF = fn(3, APP_MM)
MM_C_RTS_SET_ON_PC = fn(4, APP_MM)
MM_C_RTS_CLEAR_ON_PC = fn(5, APP_MM)
MM_C_RTS_REMOVE_ON_PC = fn(6, APP_MM)
MM_C_RTS_INSERT_ON_PC = fn(7, APP_MM)
MM_C_RTS_REPLACE_ON_PC = fn(8, APP_MM)
MM_C_SPK_SET_ON_PC = fn(12, APP_MM)
MM_C_SPK_CLEAR_ON_PC = fn(13, APP_MM)
MM_C_SPK_APPEND_ON_PC = fn(14, APP_MM)
MM_C_SPK_REMOVE_ON_PC = fn(15, APP_MM)
MM_C_SPK_INSERT_ON_PC = fn(16, APP_MM)
MM_C_SPK_REPLACE_ON_PC = fn(17, APP_MM)
MM_C_SET_MIC_OPER_MODE_ON_PC = fn(18, APP_MM)
MM_C_SET_ACTIVE_MICS_ON_PC = fn(19, APP_MM)
MM_C_RTS_FIRST_ON_PC = fn(20, APP_MM)
MM_C_SET_SETTINGS_ON_PC = fn(21, APP_MM)
MM_C_START_MM = fn(30, APP_MM)
MM_C_STOP_MM = fn(31, APP_MM)
MM_C_GET_SETTINGS = fn(32, APP_MM)
MM_C_SET_SETTINGS = fn(33, APP_MM)
MM_C_SET_MICRO_ON_OFF = fn(34, APP_MM)
MM_C_SHIFT = fn(35, APP_MM)
MM_C_RTS_SET = fn(36, APP_MM)
MM_C_RTS_GET = fn(37, APP_MM)
MM_C_RTS_CLEAR = fn(38, APP_MM)
MM_C_RTS_REMOVE = fn(39, APP_MM)
MM_C_RTS_INSERT = fn(40, APP_MM)
MM_C_SPK_GET = fn(46, APP_MM)
MM_C_SPK_CLEAR = fn(47, APP_MM)
MM_C_SPK_APPEND = fn(48, APP_MM)
MM_C_SPK_REMOVE = fn(49, APP_MM)
MM_C_SET_MIC_OPER_MODE = fn(52, APP_MM)
MM_C_SET_ACTIVE_MICS = fn(53, APP_MM)
MM_C_SET_SPEECHTIME_SETTINGS = fn(59, APP_MM)
MM_C_LAST_MINUTE_WARNING = fn(60, APP_MM)
MM_C_TIME_FINISHED_WARNING = fn(61, APP_MM)
MM_C_RTS_APPEND = fn(62, APP_MM)
MM_C_START_MON_MM = fn(69, APP_MM)
MM_C_STOP_MON_MM = fn(70, APP_MM)

MM_C_PC_MIC_ON, MM_C_PC_MIC_OFF, MM_C_PC_MIC_NONE = 1, 2, 3
EMPTY_UNIT = 0xFFFF
EMPTY_DELEGATE = 0xFFFF

# Tipos de unidad (byUnitType): el nibble alto dice qué es.
TIPO_DELEGADO, TIPO_PRESIDENTE = 0x00, 0x10

# Modos de operación (wOperationMode): nombre en la app <-> valor de la CCU.
MODOS = {
    "operador": 0,            # MM_C_OPERATOR_WITH_REQ_LIST: el que queremos
    "delegado_lista": 1,      # MM_C_DELEGATE_WITH_REQ_LIST
    "reemplazo": 2,           # MM_C_DELEGATE_WITH_OVERRIDE
    "voz": 3,                 # MM_C_DELEGATE_WITH_VOICE
    "operador_replicas": 4,   # MM_C_OPERATOR_WITH_COMMENT_LIST
    "pulsar": 5,              # MM_C_DELEGATE_WITH_PUSHTOTALK
    "operador_sin_ac": 8,     # MM_C_OPERATOR_NO_AC
    "voz_sin_ac": 9,          # MM_C_VOICE_NO_AC
}
MODO_NOMBRE = {v: k for k, v in MODOS.items()}
MODOS_OPERADOR = {0, 4, 8}

# Errores (Apéndice C) en castellano. `{banca}` se completa con la banca de la unidad.
ERRORES = {
    2: "la CCU no conoce esa unidad",
    5: "la CCU no pudo abrir o cerrar el micrófono",
    6: "la unidad ya está en la lista",
    8: "no está en la lista",
    9: "la unidad no está en la lista",
    15: "no está en la lista de oradores ni entre los micrófonos especiales",
    17: "cantidad de micrófonos abiertos no válida",
    18: "la CCU no está en modo operador: cambiá el modo (Conferencia → Modo) o pedíselo al operador de sonido",
    19: "esa unidad y ese delegado no coinciden",
    21: "no se pudo sacar de la lista de pedidos",
    22: "no se pudo agregar a la lista de pedidos",
    24: "la lista de pedidos está llena",
    25: "la lista de pedidos cambió mientras tanto: probá de nuevo",
    26: "la lista de pedidos está vacía",
    27: "largo de la lista de pedidos no válido",
    28: "la lista de pedidos es demasiado larga",
    31: "no se pudo sacar de la lista de oradores",
    32: "no se pudo agregar a la lista de oradores",
    34: "ya está el máximo de micrófonos abiertos",
    47: "tipo de micrófono no válido",
    48: "la unidad no está conectada",
    49: "la unidad y el delegado no coinciden",
    50: "otro controlador tiene el control de los micrófonos",
    1: "la CCU contestó que falló",
    4106: "parámetro equivocado",
    4107: "la CCU no encuentra esa unidad",
    4109: "la unidad no está conectada",
}


def texto_error(codigo: int) -> str:
    return ERRORES.get(codigo, f"error {codigo} de la CCU (Apéndice C del manual)")


@dataclass
class Mensaje:
    tipo: int
    fn: int
    datos: bytes = b""
    error: int = 0
    extra: dict = field(default_factory=dict)

    def __repr__(self) -> str:  # para la traza
        t = {REQ: "REQ", RSP: "RSP", NTF: "NTF", NAK: "NAK", COMM_PARAMS: "COMM"}.get(self.tipo, str(self.tipo))
        e = f" err={self.error}" if self.tipo == RSP else ""
        return f"{t} {nombre_fn(self.fn)}{e} [{self.datos.hex(' ')}]"


_NOMBRES = {v: k for k, v in globals().items() if k.startswith(("MM_C_", "SC_C_")) and isinstance(v, int)}


def nombre_fn(f: int) -> str:
    return _NOMBRES.get(f, f"0x{f:04X}")


def empaquetar(m: Mensaje) -> bytes:
    """El ``T_MESSAGE`` del cable serie: ``byType, wLength, byData`` (byData = wFnId [+ wError] + datos)."""
    cuerpo = struct.pack("<H", m.fn)
    if m.tipo == RSP:
        cuerpo += struct.pack("<H", m.error)
    cuerpo += m.datos
    return struct.pack("<BH", m.tipo, len(cuerpo)) + cuerpo


def desempaquetar(b: bytes) -> Mensaje:
    if len(b) < 3:
        raise ValueError("mensaje corto")
    tipo, largo = struct.unpack_from("<BH", b, 0)
    cuerpo = b[3:3 + largo]
    if len(cuerpo) != largo or largo < 2:
        raise ValueError("largo del mensaje no coincide")
    f = struct.unpack_from("<H", cuerpo, 0)[0]
    if tipo == RSP:
        if largo < 4:
            raise ValueError("respuesta sin código de error")
        return Mensaje(tipo, f, cuerpo[4:], struct.unpack_from("<H", cuerpo, 2)[0])
    return Mensaje(tipo, f, cuerpo[2:])


# --- Estructuras ---------------------------------------------------------------------------

def micro_onoff(unidad: int, prendido: bool) -> bytes:  # MM_T_MICRO_ONOFF
    return struct.pack("<HB", unidad, 1 if prendido else 0)


def unidad(u: int) -> bytes:  # MM_T_SPK
    return struct.pack("<H", u)


def rts(u: int, delegado: int = EMPTY_DELEGATE) -> bytes:  # MM_T_RTS
    return struct.pack("<HH", u, delegado)


def rts_insertar(buscar: tuple[int, int], nuevo: tuple[int, int]) -> bytes:  # MM_T_RTS_INSERT
    return rts(*buscar) + rts(*nuevo)


def lista_rts(items: list[tuple[int, int]]) -> bytes:  # MM_T_CCU_RTSLIST
    return struct.pack("<H", len(items)) + b"".join(rts(u, d) for u, d in items)


def palabra(v: int) -> bytes:
    return struct.pack("<H", v)


def leer_palabra(d: bytes, desde: int = 0) -> int:
    return struct.unpack_from("<H", d, desde)[0]


def leer_micro_on_pc(d: bytes) -> tuple[int, int, int]:  # MM_T_MICRO_ONOFF_ON_PC
    return struct.unpack_from("<HHH", d, 0)


def leer_rts(d: bytes, desde: int = 0) -> tuple[int, int]:
    return struct.unpack_from("<HH", d, desde)


def leer_lista_rts(d: bytes) -> list[tuple[int, int]]:
    n = leer_palabra(d)
    return [leer_rts(d, 2 + 4 * i) for i in range(min(n, (len(d) - 2) // 4))]


def leer_lista_spk(d: bytes) -> list[tuple[int, bool]]:  # MM_T_CCU_SPKLIST {wNrOfSpk; {wUnitId,bMicroOn}[]}
    n = leer_palabra(d)
    out = []
    for i in range(min(n, (len(d) - 2) // 3)):
        u, on = struct.unpack_from("<HB", d, 2 + 3 * i)
        out.append((u, bool(on)))
    return out


def lista_spk(items: list[tuple[int, bool]]) -> bytes:
    return struct.pack("<H", len(items)) + b"".join(struct.pack("<HB", u, 1 if on else 0) for u, on in items)


AJUSTES = struct.Struct("<HHHBBHBBB")  # MM_T_CCU_GLOBAL_SETTINGS


def leer_ajustes(d: bytes) -> dict:
    v = AJUSTES.unpack_from(d, 0)
    return {"modo": v[0], "max_abiertos": v[1], "max_pedidos": v[2], "cancelar_pedidos": bool(v[3]),
            "apagar_micro": bool(v[4]), "tono": v[5], "ambiente": bool(v[6]), "prio_cancela_todo": bool(v[8])}


def ajustes(a: dict) -> bytes:
    return AJUSTES.pack(a["modo"], a["max_abiertos"], a["max_pedidos"], int(a.get("cancelar_pedidos", True)),
                        int(a.get("apagar_micro", True)), a.get("tono", 0), int(a.get("ambiente", False)), 0,
                        int(a.get("prio_cancela_todo", False)))


def leer_config(d: bytes) -> tuple[int, list[tuple[int, int]]]:
    """``SC_T_CCU_CONFIGURATION`` (de GET_CCU_CONFIG_PROPERTY): conectadas y [(unidad, tipo)]."""
    _esclavas, conectadas, n = struct.unpack_from("<HHH", d, 0)
    out = []
    for i in range(min(n, (len(d) - 6) // 5)):
        u, t, _prop = struct.unpack_from("<HBH", d, 6 + 5 * i)
        out.append((u, t))
    return conectadas, out


def config(unidades: list[tuple[int, int]]) -> bytes:
    return struct.pack("<HHH", 0, len(unidades), len(unidades)) + b"".join(
        struct.pack("<HBH", u, t, 0) for u, t in unidades)


def leer_unidad_sc(d: bytes) -> tuple[int, int]:
    """``SC_T_UNIT_DATA`` de CONNECT_UNIT / DISCONNECT_UNIT: {wUnitId; byUnitType}."""
    return struct.unpack_from("<HB", d, 0)


def unidad_sc(u: int, tipo: int) -> bytes:
    return struct.pack("<HB", u, tipo)
