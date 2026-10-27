"""La capa de mensajes DCN (común a serie y TCP): docs/bosch/open-interface.md §4.

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


# MDSC_COMMUNICATION_PARAMS (tipo 15, solo por serie): RSMT_COMMUNICATION_PARAMS
# {wFnId; wHeartbeatTime; wNrRetries; wRetryTime}; la CCU contesta con el mismo tipo y wFnId RSP.
RSMC_SET_COMMUNICATION_PARAMS, RSMC_RSP_COMMUNICATION_PARAMS = 0x0001, 0x0002


def parametros_enlace(latido_s: int = 5, reintentos: int = 2, reintento_s: int = 2) -> bytes:
    return struct.pack("<HHH", max(0, min(1000, latido_s)), max(0, min(10, reintentos)), max(1, min(100, reintento_s)))


def leer_parametros_enlace(d: bytes) -> dict:
    hb, rt, rts = struct.unpack_from("<HHH", d, 0)
    return {"latido_s": hb, "reintentos": rt, "reintento_s": rts}


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
    if tipo == NAK and largo == 0:  # protocolo Simple: la CCU recibió algo con checksum mal
        return Mensaje(NAK, 0)
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
            "apagar_micro": bool(v[4]), "tono": v[5], "ambiente": bool(v[6]), "prio_cancela_todo": bool(v[8]),
            # El byte sin nombre del manual: se devuelve tal cual (antes se escribía 0 y se pisaba lo de la CCU).
            "_reservado": v[7]}


def ajustes(a: dict) -> bytes:
    return AJUSTES.pack(a["modo"], a["max_abiertos"], a["max_pedidos"], int(a.get("cancelar_pedidos", True)),
                        int(a.get("apagar_micro", True)), a.get("tono", 0), int(a.get("ambiente", False)),
                        int(a.get("_reservado", 0)) & 0xFF,
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


# --- Votación (VT, APP 1) — fase 2 -------------------------------------------------------
VT_C_START_APP = fn(1, APP_VT)
VT_C_STOP_APP = fn(2, APP_VT)
VT_C_START_VOTING = fn(3, APP_VT)
VT_C_STOP_VOTING = fn(4, APP_VT)
VT_C_HOLD_VOTING = fn(5, APP_VT)
VT_C_RESTART_VOTING = fn(6, APP_VT)
VT_C_DOWNLOAD_SUBJECT = fn(7, APP_VT)
VT_C_SET_GLOBAL_SETTINGS = fn(9, APP_VT)
VT_C_SET_VOTINGPARAMS = fn(10, APP_VT)
VT_C_GET_RESULTS = fn(12, APP_VT)
VT_C_RESULTSNOTIFY = fn(23, APP_VT)

VT_MAX_SUBJECT, VT_MAX_LEGEND, VT_MAX_OPCIONES = 142, 11, 25
VT_NO_VOTO, VT_SIN_ASIGNAR = 0xFE, 0xFF
VT_INT_RES_INDIV = 2
# Menús de votación: nombre en la app -> (wVotingMenu, cantidad de opciones, nombres de las opciones)
MENUS = {
    "si_no": (1, 2, ["afirmativo", "negativo"]),
    "si_no_abstencion": (2, 3, ["afirmativo", "negativo", "abstencion"]),
    "a_favor_en_contra": (3, 2, ["afirmativo", "negativo"]),
    "si_no_abstencion_nppv": (8, 4, ["afirmativo", "negativo", "abstencion", "no_participa"]),
}
MENU_NOMBRE = {v[0]: k for k, v in MENUS.items()}

ERRORES.update({
    276: "ya hay una votación abierta en la central",
    277: "no hay una votación abierta",
    278: "la votación no está en pausa",
    286: "la votación de la central no está arrancada (¿licencia o «votación» apagada en la conexión?)",
    287: "parámetro de votación equivocado",
    288: "otro controlador maneja la votación",
    290: "no tenés el control de la votación",
    291: "la central todavía no tiene resultados",
    2305: "la asistencia de la central no está arrancada",
    2316: "otro controlador maneja la asistencia",
    2322: "la central no deja activar la asistencia ahora",
})


def comcontrol(notificar: bool = True, segundos_despues: int = 0) -> bytes:  # VT_T_COMCONTROL
    return struct.pack("<BBH", 1 if notificar else 0, 0, segundos_despues)


def _texto_fijo(t: str, largo: int) -> bytes:
    b = (t or "").encode("cp1252", "replace")[: largo - 1]
    return b + b"\x00" * (largo - len(b))


def tema(numero: int, texto: str, leyenda: str = "") -> bytes:  # VT_T_SUBJECT_REC
    return struct.pack("<H", numero) + _texto_fijo(texto, VT_MAX_SUBJECT) + _texto_fijo(leyenda, VT_MAX_LEGEND)


def leer_tema(d: bytes) -> tuple[int, str]:
    n = struct.unpack_from("<H", d, 0)[0]
    return n, d[2:2 + VT_MAX_SUBJECT].split(b"\x00")[0].decode("cp1252", "replace")


PARAMS_VOTO = struct.Struct("<HHBHB")  # VT_T_VOTINGPARAMS


def params_voto(menu: int, opciones: int, abierta: bool, parciales: int = VT_INT_RES_INDIV, comprimido: bool = False) -> bytes:
    return PARAMS_VOTO.pack(menu, opciones, 1 if abierta else 0, parciales, 1 if comprimido else 0)


def leer_params_voto(d: bytes) -> dict:
    v = PARAMS_VOTO.unpack_from(d, 0)
    return {"menu": v[0], "opciones": v[1], "abierta": bool(v[2]), "parciales": v[3], "comprimido": bool(v[4])}


def mostrar_resultados(si: bool = True) -> bytes:  # VT_T_SHOW_RESULTS
    return struct.pack("<B", 1 if si else 0)


RESULTADO_CAB = struct.Struct("<HII25IHB")  # VT_T_RESULT_REC hasta bCompressed


def leer_resultado(d: bytes) -> dict:
    """VT_T_RESULT_REC: totales por opción y voto por delegado (índice = DelegateId - 1).

    Los manuales dicen 25 totales (VT_C_MAX_ANSWER_OPTIONS); el FNID.DEF del DDTK dice 24. Se usa
    el que haga cuadrar el largo del mensaje (wFillLevel = los bytes de votos que vienen)."""
    n_op = 25
    for n in (25, 24):
        cab = 2 + 4 * (2 + n) + 3
        if len(d) >= cab and len(d) == cab + struct.unpack_from("<H", d, cab - 3)[0]:
            n_op = n
            break
    cab = struct.Struct(f"<HII{n_op}IHB")
    v = cab.unpack_from(d, 0)
    numero, presentes, no_votaron = v[0], v[1], v[2]
    votos = list(v[3:3 + n_op]) + [0] * (25 - n_op)
    lleno, comprimido = v[3 + n_op], bool(v[4 + n_op])
    crudo = d[cab.size:cab.size + lleno]
    por_delegado = {}
    if comprimido:
        for i, b in enumerate(crudo):
            for delegado, nib in ((2 * i + 1, b & 0x0F), (2 * i + 2, b >> 4)):
                if nib not in (0x0E, 0x0F):
                    por_delegado[delegado] = nib
    else:
        for i, b in enumerate(crudo):
            if b not in (VT_NO_VOTO, VT_SIN_ASIGNAR):
                por_delegado[i + 1] = b
    return {"numero": numero, "presentes": presentes, "no_votaron": no_votaron, "votos": votos,
            "por_delegado": por_delegado}


def resultado(numero: int, presentes: int, no_votaron: int, votos: list[int], por_delegado: dict[int, int]) -> bytes:
    lleno = max(por_delegado) if por_delegado else 0
    crudo = bytearray([VT_SIN_ASIGNAR] * lleno)
    for dlg, voto in por_delegado.items():
        crudo[dlg - 1] = voto
    v = (list(votos) + [0] * 25)[:25]
    return RESULTADO_CAB.pack(numero, presentes, no_votaron, *v, lleno, 0) + bytes(crudo)


# --- Asistencia (AT, 0x09xx) — fase 2 -----------------------------------------------------
AT_C_START_AT_APP = 0x0901
AT_C_STOP_AT_APP = 0x0902
AT_C_ACTIVATE = 0x0904
AT_C_GET_INDIV_REGISTRATION = 0x0906
AT_C_SEND_INDIV_REGISTRATION = 0x090A
AT_C_SEND_TOTAL_REGISTRATION = 0x090B
AT_CONTROL, AT_MONITOR = 1, 2
AT_PRESENTE = {1, 3}  # AT_C_ATTEND, AT_C_ATTEND_NOCHANGE
AT_MAX = 250


def at_control(tipo: int = AT_MONITOR) -> bytes:  # AT_T_APPCONTROL
    return struct.pack("<B", tipo)


def at_activar(asistencia: bool = True, acceso: bool = False) -> bytes:  # AT_T_ACTIVATE
    return struct.pack("<BB", 1 if asistencia else 0, 1 if acceso else 0)


def leer_asistencia(d: bytes) -> list[tuple[int, int, int]]:  # AT_T_REGISTER_INDIV: [(unidad, delegado, estado)]
    n = struct.unpack_from("<H", d, 0)[0]
    return [struct.unpack_from("<HHB", d, 2 + 5 * i) for i in range(min(n, (len(d) - 2) // 5))]


def asistencia(items: list[tuple[int, int, int]]) -> bytes:
    return struct.pack("<H", len(items)) + b"".join(struct.pack("<HHB", u, dl, e) for u, dl, e in items)


def leer_asistencia_total(d: bytes) -> tuple[int, int]:  # AT_T_REGISTER_TOTAL {wAttend; wLeave}
    return struct.unpack_from("<HH", d, 0)


# --- Tiempo de uso de la palabra (MM) — fase 2 --------------------------------------------

def tiempo_palabra(minutos: int, reloj: bool = True, pausa_prioridad: bool = True, cuenta_regresiva: bool = True,
                   anillo_sigue: bool = True) -> bytes:  # MM_T_SET_SPEECHTIME_SETTINGS
    return struct.pack("<HBBBB", max(0, int(minutos)), int(reloj), int(pausa_prioridad), int(cuenta_regresiva),
                       int(anillo_sigue))


_NOMBRES.update({v: k for k, v in globals().items() if k.startswith(("VT_C_", "AT_C_")) and isinstance(v, int)})


def leer_prioridad(d: bytes) -> tuple[int, bool]:
    """MM_C_CHAIR_MICS_ON: el formato no está en el manual [A VERIFICAR con la CCU]. Se lee lo que venga:
    {wUnitId; bOn} si llegan 3 bytes, solo la unidad si llegan 2, y sin datos «prioridad prendida»."""
    u = struct.unpack_from("<H", d, 0)[0] if len(d) >= 2 else 0
    on = bool(d[2]) if len(d) >= 3 else True
    return u, on


def prioridad(u: int, on: bool) -> bytes:
    return struct.pack("<HB", u, 1 if on else 0)
