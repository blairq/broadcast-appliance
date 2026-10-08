"""Las funciones del Open Interface que NO están en los manuales ni en FNID.DEF (guía, apéndice).

Salen del software de Bosch del DVD (docs/bosch/desensamblado.md): los códigos, de los enums de
``Bosch.Dcn.OpenInterface.Logic``; los nombres de los parámetros, de las interfaces
``IDcnOiCpc*`` de ``Bosch.Dcn.OpenInterface.Interfaces``; el formato en el cable, de la clase
``…Marshaler`` que usa cada una (``BinaryWriter``: little-endian, un BOOLEAN es un byte).

Cada función es una entrada del catálogo con su formato. Un codificador genérico arma y lee los
datos, así la app las puede usar todas con un solo control (``funcion``) y una sola herramienta MCP
(``dcn_funcion``). Las más útiles tienen además su control con nombre en plugin.py.

``verificado``: el formato sale entero del Marshaler (orden y tipo de cada campo). Las que dicen
False tienen campos que el desensamblado no deja claros: aceptan ``hex`` (los bytes tal cual) y
devuelven los bytes crudos además de lo que se pudo leer. NINGUNA está probada contra una CCU
real: muchas son del software de control de Bosch (clases ``…Cpc…``) y puede que la CCU solo las
acepte del controlador principal o con otra licencia. La CCU contesta un error si no la conoce.

``riesgo``:
  * "normal": se usa sin más;
  * "peligrosa": licencias y visor de las unidades; pide ``confirmar=true``;
  * las de descarga de firmware a la CCU (DL, 0x1201–0x120C) están COMENTADAS al final: no se pueden
    llamar desde la app. Un error ahí deja la central inservible, y el equipo ya no se fabrica.
"""
from __future__ import annotations

import struct

# --- Tipos de campo -------------------------------------------------------------------------------
#
#   u8 u16 i16 u32 i32   enteros (little-endian)
#   bool                 BOOLEAN de un byte (0/1)
#   bandera_w            WORD con el valor en el byte alto: TRUE = 0x0100 (así lo arma Bosch)
#   alto_w               WORD con un número de 0 a 255 en el byte alto (valor << 8)
#   cero_w               WORD en 0 que Bosch manda adelante (no se pide)
#   texto11              texto fijo de 11 bytes (VT_C_MAX_LEN_LEGEND), relleno con ceros
#   lista_u16            WORD cantidad + WORD[]
#   lista_u8             BYTE cantidad + BYTE[]
#   pares_u8             pares (BYTE, BYTE) sin cantidad, hasta el final
#   textos11x4           4 textos de 11 bytes
#   resto                los bytes que quedan (en hexadecimal)

_SIMPLES = {"u8": "<B", "u16": "<H", "i16": "<h", "u32": "<I", "i32": "<i", "bool": "<?"}


def _a_bytes_texto(v, largo: int) -> bytes:
    b = str(v or "").encode("latin-1", "replace")[: largo - 1]
    return b + b"\x00" * (largo - len(b))


def _de_bytes_texto(b: bytes) -> str:
    return b.split(b"\x00")[0].decode("latin-1", "replace")


def codificar(campos: list, args: dict) -> bytes:
    out = b""
    for c in campos:
        nombre, tipo = c[0], c[1]
        if tipo == "cero_w":
            out += b"\x00\x00"
            continue
        if nombre not in args:
            raise ValueError(f"falta el parámetro «{nombre}»")
        v = args[nombre]
        if tipo in _SIMPLES:
            out += struct.pack(_SIMPLES[tipo], bool(v) if tipo == "bool" else int(v))
        elif tipo == "bandera_w":
            out += struct.pack("<H", 0x0100 if v else 0)
        elif tipo == "alto_w":
            n = int(v)
            if not 0 <= n <= 255:
                raise ValueError(f"«{nombre}» va de 0 a 255")
            out += struct.pack("<H", n << 8)
        elif tipo == "texto11":
            out += _a_bytes_texto(v, 11)
        elif tipo == "textos11x4":
            vs = list(v or [])[:4]
            out += b"".join(_a_bytes_texto(x, 11) for x in vs + [""] * (4 - len(vs)))
        elif tipo == "lista_u16":
            vs = [int(x) for x in v]
            out += struct.pack(f"<H{len(vs)}H", len(vs), *vs)
        elif tipo == "lista_u8":
            vs = [int(x) for x in v]
            out += struct.pack(f"<B{len(vs)}B", len(vs), *vs)
        elif tipo == "pares_u8":
            for par in v:
                out += struct.pack("<BB", int(par[0]), int(par[1]))
        elif tipo == "resto":
            out += bytes.fromhex(str(v).replace(" ", ""))
        else:
            raise ValueError(f"tipo de campo desconocido: {tipo}")
    return out


def decodificar(campos: list, d: bytes) -> dict:
    out: dict = {}
    i = 0
    try:
        for c in campos:
            nombre, tipo = c[0], c[1]
            if tipo in _SIMPLES:
                fmt = _SIMPLES[tipo]
                out[nombre] = struct.unpack_from(fmt, d, i)[0]
                i += struct.calcsize(fmt)
            elif tipo == "cero_w":
                i += 2
            elif tipo == "bandera_w":
                out[nombre] = struct.unpack_from("<H", d, i)[0] != 0
                i += 2
            elif tipo == "alto_w":
                out[nombre] = struct.unpack_from("<H", d, i)[0] >> 8
                i += 2
            elif tipo == "texto11":
                out[nombre] = _de_bytes_texto(d[i:i + 11])
                i += 11
            elif tipo == "textos11x4":
                out[nombre] = [_de_bytes_texto(d[i + k * 11:i + (k + 1) * 11]) for k in range(4)]
                i += 44
            elif tipo == "lista_u16":
                n = struct.unpack_from("<H", d, i)[0]
                out[nombre] = list(struct.unpack_from(f"<{n}H", d, i + 2))
                i += 2 + 2 * n
            elif tipo == "lista_u8":
                n = d[i]
                out[nombre] = list(d[i + 1:i + 1 + n])
                i += 1 + n
            elif tipo == "pares_u8":
                out[nombre] = [[d[k], d[k + 1]] for k in range(i, len(d) - 1, 2)]
                i = len(d)
            elif tipo == "resto":
                out[nombre] = d[i:].hex(" ")
                i = len(d)
    except (struct.error, IndexError):
        out["_incompleto"] = True
    if i < len(d):
        out["_sobra"] = d[i:].hex(" ")
    return out


# --- El catálogo ----------------------------------------------------------------------------------
#
# nombre: (código, nombre de Bosch, clase, parámetros, respuesta, verificado, riesgo, para qué)
#   clase: "pedido" (la app lo manda) | "aviso" (lo manda la CCU)
#   parámetros/respuesta: [(campo, tipo, descripción)]

U16 = "u16"
_RTS = [("unidad_actual", U16, "unidad del pedido que se busca"), ("delegado_actual", U16, "su delegado (0xFFFF: cualquiera)"),
        ("unidad_nueva", U16, "unidad del pedido nuevo"), ("delegado_nuevo", U16, "su delegado (0xFFFF: ninguno)")]
_SPK = [("unidad_actual", U16, "orador que se busca"), ("unidad_nueva", U16, "orador nuevo")]
_VOTACION_N = [("numero", U16, "número de votación")]
_ACCESO = [("unidad", U16, ""), ("delegado", U16, ""), ("evento", "u8", "tipo de evento de acceso")]

FUNCIONES: dict[str, tuple] = {
    # --- Micrófonos (MM, APP 0) ---
    "pedido_reemplazar": (0x0029, "MM_C_RTS_REPLACE", "pedido", _RTS, [], True, "normal",
                          "Reemplaza un pedido de palabra por otro, en el mismo lugar de la lista."),
    "orador_insertar": (0x0032, "MM_C_SPK_INSERT", "pedido", _SPK, [], True, "normal",
                        "Inserta un orador antes de otro en la lista de oradores."),
    "orador_reemplazar": (0x0033, "MM_C_SPK_REPLACE", "pedido", _SPK, [], True, "normal",
                          "Reemplaza un orador por otro, en el mismo lugar."),
    "pedidos_max": (0x0036, "MM_C_SET_MAX_RTS_LEN", "pedido", [("largo", "i16", "máximo de pedidos")], [], True, "normal",
                    "Cambia el máximo de pedidos de palabra sin tocar los demás ajustes."),
    "ajustes_microfono": (0x0037, "MM_C_SET_MIC_SETTINGS", "pedido",
                          [("cancelar_pedido", "bool", "el delegado puede cancelar su pedido"),
                           ("apagar_micro", "bool", "el delegado puede apagar su micrófono")], [], True, "normal",
                          "Qué puede hacer el delegado con su botón (cancelRequest, microOff)."),
    "tono_atencion": (0x0038, "MM_C_SET_ATTENTION_TONE", "pedido",
                      [("_", "cero_w", ""), ("tono", "alto_w", "número de tono")], [], True, "normal",
                      "Elige el tono de atención de las unidades."),
    "microfono_ambiente": (0x0039, "MM_C_SET_AMBIENT_CTRL", "pedido",
                           [("_", "cero_w", ""), ("activo", "bandera_w", "prende el control del micrófono ambiente")],
                           [], True, "normal", "Prende o apaga el micrófono ambiente (DDI en modo 7)."),
    "cerrar_tras_siguiente": (0x003F, "MM_C_MICSOFF_AFTER_SHIFT", "pedido",
                              [("_", "cero_w", ""), ("activo", "bandera_w", "cerrar los micrófonos al pasar al siguiente")],
                              [], True, "normal", "Con «siguiente», se cierran los micrófonos de los que estaban hablando."),
    "proximo_en_hablar": (0x0047, "MM_C_NEXT_TO_SPEAK_ON_PC", "aviso", [],
                          [("unidad", U16, ""), ("delegado", U16, "")], True, "normal",
                          "La CCU avisa quién es el próximo en hablar."),
    # --- Votación (VT, APP 1) ---
    "llamado_nominal_orden": (0x0108, "VT_C_SET_ROLL_CALL", "pedido",
                              [("delegados", "lista_u16", "delegados en el orden en que votan")], [], True, "normal",
                              "Orden de la votación por llamado nominal."),
    "votacion_parametros2": (0x010B, "VT_C_SET_VOTINGPARAMS2", "pedido",
                             [("menu", "i16", "menú de votación"), ("opciones", U16, "cantidad de respuestas"),
                              ("abierta", "bool", "nominal (con voto por delegado)"), ("parciales", "i16", "tipo de resultado parcial"),
                              ("comprimido", "bool", "resultado comprimido"),
                              ("leyenda_tiempo", "texto11", ""), ("leyenda_presentes", "texto11", ""),
                              ("leyenda_no_votaron", "texto11", ""), ("leyendas_respuestas", "textos11x4", "4 textos"),
                              ("leyenda_nominal", "texto11", "")], [], False, "normal",
                             "Parámetros de la votación con las leyendas de las pantallas de las unidades. "
                             "[A VERIFICAR] la cantidad de leyendas de respuestas (el desensamblado muestra 4)."),
    "pantalla_sala": (0x010D, "VT_C_CHANGEHALLDISPLAY", "pedido", [("a_pantalla_sala", "bool", "")], [], True, "normal",
                      "Manda los resultados a la pantalla de sala (o los saca)."),
    "votacion_reloj": (0x010E, "VT_C_GET_TIMER_VALUES", "pedido", [], [("segundos", "i16", "valor del reloj")], True, "normal",
                       "El reloj de la votación."),
    "votacion_por_respuesta": (0x010F, "VT_C_GET_RESULTS_ANSWER", "pedido", [("respuesta", "u8", "número de respuesta")],
                               [("delegados", "lista_u16", "los que votaron esa respuesta")], True, "normal",
                               "Quiénes votaron una respuesta."),
    "votacion_pedido_iniciar": (0x0110, "VT_C_START_VOTING_REQ", "aviso", [], _VOTACION_N, True, "normal",
                                "El presidente abrió la votación desde su unidad."),
    "votacion_pedido_cerrar": (0x0111, "VT_C_VOTING_STOPPED", "aviso", [], _VOTACION_N, True, "normal",
                               "El presidente cerró la votación."),
    "votacion_pedido_pausar": (0x0112, "VT_C_VOTING_ONHOLD", "aviso", [], _VOTACION_N, True, "normal",
                               "El presidente pausó la votación."),
    "votacion_pedido_reanudar": (0x0113, "VT_C_VOTING_RESTARTED", "aviso", [], _VOTACION_N, True, "normal",
                                 "El presidente reanudó la votación."),
    "votacion_pedido_anterior": (0x0114, "VT_C_PREV_VOTING_REQ", "aviso", [], _VOTACION_N, True, "normal",
                                 "El presidente pide la votación anterior."),
    "votacion_pedido_siguiente": (0x0115, "VT_C_NEXT_VOTING_REQ", "aviso", [], _VOTACION_N, True, "normal",
                                  "El presidente pide la votación siguiente."),
    "llamado_nominal_turno": (0x0116, "VT_C_NEXT_ROLL_CALLER", "aviso", [],
                              [("delegado", U16, "el que vota ahora"), ("segundos_restantes", "i16", "")], True, "normal",
                              "Llamado nominal: a quién le toca votar."),
    "votacion_tono": (0x011B, "VT_C_ATTENTION_TONE_NOTIFY", "aviso", [], [("tono", "u8", "")], True, "normal",
                      "Sonó el tono de atención de la votación."),
    # --- Asistencia y acceso (AT, 0x09xx) ---
    "acceso_aplicaciones": (0x0907, "AT_C_STORE_APP_ACCESS", "pedido",
                            [("microfonos", "bool", "el acceso controla los micrófonos"),
                             ("votacion", "bool", "el acceso controla la votación"),
                             ("intercom", "bool", "el acceso controla el intercomunicador")], [], True, "normal",
                            "A qué aplicaciones se aplica el control de acceso por tarjeta."),
    "acceso_cambio_permitido": (0x0908, "AT_C_GET_SETT_ALLOWED", "pedido", [], [("permitido", "bool", "")], True, "normal",
                                "Si se pueden cambiar los ajustes de asistencia ahora."),
    "acceso_estado_unidades": (0x0909, "AT_C_GET_UNIT_ACCESS_STATUS", "pedido", [],
                               [("cantidad", U16, ""), ("items", "resto", "(unidad u16, delegado u16, evento u8) por unidad")],
                               True, "normal", "Quién está en cada unidad según el control de acceso."),
    "acceso_cambio_permitido_aviso": (0x090C, "AT_C_SETTINGS_CHANGE_ALLOWED", "aviso", [], [("permitido", "bool", "")],
                                      True, "normal", "Cambió si se pueden tocar los ajustes de asistencia."),
    "acceso_estado_aviso": (0x090D, "AT_C_ACCESS_STATUS_UPDATE", "aviso", [], _ACCESO, True, "normal",
                            "Cambió el acceso en una unidad (alguien puso o sacó la tarjeta)."),
    # --- Sistema (SC, APP 16) ---
    "pc_conectar": (0x1003, "SC_C_PC_CONNECT", "pedido", [], [], True, "normal", "Abre la sesión del PC con la CCU."),
    "pc_desconectar": (0x1004, "SC_C_PC_DISCONNECT", "pedido", [], [], True, "normal", "Cierra la sesión del PC."),
    "ccu_canales_cambio": (0x1013, "SC_C_CCU_CHAN_ASSIGN_CHANGE", "aviso", [],
                           [("datos", "resto", "byte, int16, byte (canales asignados)")], False, "normal",
                           "Cambió la asignación de canales de la CCU."),
    "visor_updata": (0x1017, "SC_C_SET_UPDATA_CONFIG", "pedido", [("hex", "resto", "formato, dirección, largo, conversión + config")],
                     [], False, "peligrosa", "Configuración de datos del visor (UPDATA)."),
    "visor_tabla_lcd": (0x1019, "SC_C_SET_LCD_CONV_TABLE", "pedido", [("hex", "resto", "tabla de conversión")], [], False,
                        "peligrosa", "Tabla de conversión de caracteres de los visores LCD."),
    "visor_bytes_guia": (0x101A, "SC_C_SET_LEAD_BYTE_DATA", "pedido", [("hex", "resto", "16 bytes de rangos de lead bytes")],
                         [], False, "peligrosa", "Rangos de bytes guía (idiomas asiáticos) de los visores."),
    "version_fpga": (0x101C, "SC_C_FPGA_VERSION_INFO", "pedido", [], [("mayor", "u8", ""), ("menor", "u8", "")], True, "normal",
                     "Versión de la FPGA de la CCU."),
    "version_mmp": (0x101D, "SC_C_GET_MMP_VERSION_INFO", "pedido", [],
                    [("firmware", "i16", ""), ("build", "i16", ""), ("serie", "resto", "4 bytes del número de serie")],
                    True, "normal", "Versión del procesador multimedia (MMP) y su número de serie."),
    "licencia_iniciar": (0x1021, "SC_C_START_ENTER_LICENSE", "pedido", [], [], True, "peligrosa",
                         "Empieza a cargar una licencia."),
    "licencia_terminar": (0x1022, "SC_C_STOP_ENTER_LICENSE", "pedido", [], [], True, "peligrosa",
                          "Termina la carga de licencias."),
    "licencia_cargar": (0x1023, "SC_C_ENTER_LICENSE", "pedido", [("hex", "resto", "clave de licencia (32 bytes)")], [], False,
                        "peligrosa", "Carga una clave de licencia."),
    "codigo_autorizacion_fijar": (0x1024, "SC_C_SET_AUTHORIZATION_CODE", "pedido", [("hex", "resto", "")], [], False,
                                  "peligrosa", "Fija el código de autorización (licencias)."),
    "ccu_reconectar": (0x1025, "SC_C_RECONNECT", "aviso", [], [], True, "normal", "La CCU pide que el PC se reconecte."),
    "codigo_autorizacion": (0x1026, "SC_C_GET_AUTHORIZATION_CODE", "pedido", [],
                            [("version", U16, "versión mayor del software"), ("banderas", U16, "multi-PC, multi-CCU, OI…"),
                             ("aplicaciones_pc", "i16", ""), ("aplicaciones_oi", "u8", ""), ("tipo_licencia", "u8", "")],
                            False, "normal", "Qué licencias tiene la CCU (solo lee)."),
    "ccu_esclavas": (0x1034, "SC_C_GET_SLAVE_CCU_IDS", "pedido", [], [("ids", "lista_u8", "")], True, "normal",
                     "Las CCU esclavas conectadas."),
    # --- Instalación (SI, APP 17) ---
    "canales_fijar": (0x1102, "SI_C_SET_CHANNEL", "pedido", [("canales", "pares_u8", "[número, tipo] por canal")], [], True,
                      "normal", "Asigna los canales de audio (tipos: SI_T_CHANNEL_TYPE)."),
    "idioma_fabrica": (0x1106, "SI_C_DEF_LANGUAGE", "pedido", [("idioma", "u8", "")], [], True, "normal",
                       "Idioma de los menús de las unidades."),
    "unidades_orden": (0x1108, "SI_C_UNIT_SORT_ORDER", "pedido", [("unidades", "lista_u16", "")], [], True, "normal",
                       "Orden de las unidades (hasta 1500)."),
    "canales_individuales": (0x1123, "SI_C_GET_INDIVIDUAL_CHANNELS", "pedido", [("hex", "resto", "")],
                             [("datos", "resto", "(unidad u16, aporta_a_sala bool) por unidad")], False, "normal",
                             "Canales individuales por unidad."),
    "canales_individuales_fijar": (0x1124, "SI_C_SET_INDIVIDUAL_CHANNELS", "pedido", [("hex", "resto", "")], [], False,
                                   "normal", "Fija los canales individuales por unidad."),
    "canales": (0x1125, "SI_C_GET_CHANNEL", "pedido", [], [("canales", "pares_u8", "[número, tipo]")], True, "normal",
                "Lee la asignación de canales de audio."),
    # --- Interpretación (IN) ---
    "pupitres_habilitar": (0x024B, "IN_C_AUTH_DESK_FEATURES", "pedido", [("hex", "resto", "cantidad u16 + (cabina, pupitre, funciones)")],
                           [], False, "normal", "Habilita funciones de los pupitres de intérprete."),
    "interprete_microfono": (0x024C, "IN_C_SET_MICRO_ON_OFF", "pedido",
                             [("unidad", U16, ""), ("abierto", "bool", "")], [], True, "normal",
                             "Abre o cierra el micrófono de un pupitre de intérprete."),
    # --- Base de delegados (DB) ---
    "pantalla_lineas": (0x0308, "DB_C_SET_SCREENLINECFG", "pedido", [("hex", "resto", "posición del texto deslizante (dos campos)")],
                        [], False, "normal", "Configuración de la línea de pantalla de los delegados."),
    "ccu_idiomas": (0x0309, "DB_C_GET_CCU_LANGUAGES", "pedido", [], [("datos", "resto", "idiomas instalados")], False,
                    "normal", "Los idiomas instalados en la CCU."),
    "contenido_id": (0x030A, "DB_C_GET_CONTENT_ID", "pedido", [], [("id", "u8", "")], True, "normal",
                     "Identificador del contenido de la base de delegados."),
    # --- Intercomunicador (IC) ---
    "intercom_llamar": (0x0704, "IC_CALL_REQUEST", "pedido", [("origen", U16, "unidad que llama"), ("destino", U16, "")],
                        [], True, "normal", "Llama de una unidad a otra por intercomunicador."),
    "intercom_unidad": (0x070A, "IC_SET_IC_UNIT", "pedido",
                        [("unidad", U16, ""), ("tipo", U16, ""), ("operador", U16, "")], [], False, "normal",
                        "Define una unidad de intercomunicador (los tres campos son WORD; el nombre de cada uno, a verificar)."),
    # --- Cámaras (CC) ---
    "camaras_caracteres": (0x150B, "CC_C_STORE_CHARACTERSET", "pedido", [("hex", "resto", "largo u16 + datos")], [], False,
                           "normal", "Juego de caracteres del control de cámaras."),
}

# --- Descarga de firmware a la CCU (DL, APP 18): COMENTADAS A PROPÓSITO -----------------------------
# Están para que conste que existen y cómo son; la app no las ofrece. Si alguna vez hace falta
# regrabar la CCU, se usa la DLT de Bosch (docs/bosch/README.md). No hay ninguna que BAJE el
# firmware de la CCU a la PC: todas van hacia la CCU.
#
#   "ccu_arrancar_software":  (0x1201, "DL_C_START_CCU_SW", {código de activación u16, tipo de reinicio u8, u8})
#   "descarga_iniciar":       (0x1202, "DL_C_START_DOWNLOAD", {})
#   "descarga_registro_abs":  (0x1203, "DL_C_ABS_FILE_RECORD", {tipo u8, segmento u16, offset u16, largo u8, código[]})
#   "descarga_terminar":      (0x1204, "DL_C_STOP_DOWNLOAD", {})
#   "mmp_preparar":           (0x1206, "DL_C_PREPARE_MMP_DOWNLOAD", {número de serie del MMP: 4 bytes})
#   "most_mensajes":          (0x1207, "DL_C_SEND_MOST_MESSAGES", {cantidad u8, mensajes MOST[]})
#   "most_preparar":          (0x1208, "DL_C_PREPARE_MOST_NODES_DOWNLOAD", {})
#   "most_nodos":             (0x120A, "DL_C_GET_NR_OF_MOST_NODES", {} → {cantidad u8})
#   "fuente_descargar":       (0x120B, "DL_C_FONT_DOWNLOAD", {estado, número de fuente, datos (3084 bytes)})
#   "fuente_mapa":            (0x120C, "DL_C_SET_FONT_MAP", {fuente de idioma, fuente de texto variable})
FIRMWARE_COMENTADAS = 10

POR_CODIGO = {v[0]: k for k, v in FUNCIONES.items()}
AVISOS = {v[0]: k for k, v in FUNCIONES.items() if v[2] == "aviso"}


def info(nombre: str) -> dict:
    cod, bosch, clase, params, resp, verif, riesgo, para = FUNCIONES[nombre]
    return {"nombre": nombre, "codigo": f"0x{cod:04X}", "bosch": bosch, "clase": clase,
            "parametros": [{"nombre": p[0], "tipo": p[1], "descripcion": p[2]} for p in params if p[1] != "cero_w"],
            "respuesta": [{"nombre": r[0], "tipo": r[1], "descripcion": r[2]} for r in resp],
            "verificado": verif, "riesgo": riesgo, "para": para}


def buscar(nombre_o_codigo) -> str:
    """El nombre del catálogo de una función, por nombre, nombre de Bosch o código (0x0047 / 71)."""
    s = str(nombre_o_codigo).strip()
    if s in FUNCIONES:
        return s
    for k, v in FUNCIONES.items():
        if v[1].lower() == s.lower():
            return k
    try:
        cod = int(s, 0)
    except ValueError:
        cod = None
    if cod is not None and cod in POR_CODIGO:
        return POR_CODIGO[cod]
    if cod is not None and 0x1201 <= cod <= 0x120C:
        raise ValueError("las funciones de descarga de firmware a la CCU no se pueden usar desde la app")
    raise ValueError(f"no hay ninguna función «{s}» (dcn_funciones las lista)")


def armar(nombre: str, args: dict) -> bytes:
    params = FUNCIONES[nombre][3]
    if "hex" in args and not any(p[0] == "hex" for p in params):
        return bytes.fromhex(str(args["hex"]).replace(" ", ""))  # los bytes tal cual, para probar
    return codificar(params, args)


def leer(nombre: str, d: bytes) -> dict:
    r = decodificar(FUNCIONES[nombre][4], d)
    if d and not FUNCIONES[nombre][5]:
        r["_crudo"] = d.hex(" ")
    return r


# --- Unidades dobles (DDI en modo doble, DIS/WD dobles) ---------------------------------------------
# Del software sinóptico de Bosch (bosch.dcn.bcpc.logic, Area.UnitConnected): en una unidad de tipo
# FLUSH_DUAL (0x02), DISC_DELEGATE_DUAL (0x04) o WIRELESS_DISC_DUAL_DELEGATE (0x07), la mitad
# DERECHA es la unidad + 256: si (wUnitId & 511) > 256 es «DualStationRight» de la unidad
# wUnitId − 256; si no, «DualStationLeft». La CCU le da UNA dirección a la DDI y la segunda mitad
# aparece en los avisos de micrófono como esa dirección + 256.
TIPOS_DOBLES = {0x02, 0x04, 0x07}
DESPLAZAMIENTO_DERECHA = 256

TIPOS = {0x00: "Concentus delegado", 0x10: "Concentus presidente", 0x01: "empotrada delegado (DDI modo 4)",
         0x02: "empotrada doble (DDI modo 0, 2 o 3)", 0x11: "empotrada presidente (DDI modo 1)",
         0x03: "discusión delegado", 0x04: "discusión doble", 0x13: "discusión presidente",
         0x06: "inalámbrica delegado", 0x07: "inalámbrica doble", 0x16: "inalámbrica presidente",
         0x09: "votación", 0x30: "entrada (DDI modo 5)", 0x40: "salida (DDI modo 6)",
         0x50: "micrófono ambiente (DDI modo 7)", 0x60: "pupitre de intérprete", 0x61: "expansor de audio analógico",
         0x62: "expansor de audio digital", 0x63: "interfaz CobraNet", 0x71: "datos (DDB)", 0x72: "datos RS-232",
         0x81: "Integrus 4 canales", 0x82: "Integrus 8 canales", 0x83: "Integrus 16 canales", 0x84: "Integrus 32 canales",
         0xE0: "CCU", 0xE1: "CCU esclava", 0xE2: "punto de acceso inalámbrico"}


def nombre_tipo(tipo: int) -> str:
    return TIPOS.get(tipo, f"tipo 0x{tipo:02X}")


def mitad(u: int, tipo_base: int | None = None) -> tuple[int, str] | None:
    """(unidad base, "izquierda"|"derecha") si `u` es una mitad de una unidad doble."""
    if (u & 511) > DESPLAZAMIENTO_DERECHA:
        return u - DESPLAZAMIENTO_DERECHA, "derecha"
    if tipo_base in TIPOS_DOBLES:
        return u, "izquierda"
    return None
