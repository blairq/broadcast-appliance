"""Una CCU falsa, para probar sin el equipo (guía §12): unidades, modos, listas de oradores y pedidos.

Habla el MISMO protocolo desde la otra punta (con el mismo transporte, no bloqueante):

    app, ccu = socket.socketpair()
    sim = Simulador(SerieFull(FlujoSocket(ccu), escape=0xC8), unidades=12)
    sim.arrancar()
    # del lado de la app: SerieFull(FlujoSocket(app), escape=0xC8)

``apretar(unidad)`` simula el botón de micrófono de una unidad en la sala (en modo operador, un
pedido de palabra; en los modos de delegado, el micrófono se abre si hay lugar). Con
``actividad_s`` alguien aprieta un botón cada tanto.
"""
from __future__ import annotations

import random
import threading
import time

from . import extra as x
from . import mensajes as m


class Simulador:
    def __init__(self, transporte, unidades: int = 12, presidente: int = 1, actividad_s: float = 0,
                 dobles: tuple | list = ()) -> None:
        self.t = transporte
        self.t.al_recibir = self._recibir
        self.unidades = {u: {"tipo": m.TIPO_PRESIDENTE if u == presidente else m.TIPO_DELEGADO, "conectada": True}
                         for u in range(1, unidades + 1)}
        self.ajustes = {"modo": 0, "max_abiertos": 4, "max_pedidos": 50, "cancelar_pedidos": True,
                        "apagar_micro": True, "tono": 0, "ambiente": False, "prio_cancela_todo": False}
        self.spk: list[list] = []          # [[unidad, prendido]]
        self.rts: list[tuple[int, int]] = []
        self.mm_arrancado = False
        self.sc_arrancado = False
        self.actividad_s = actividad_s
        self.lock = threading.RLock()
        # Fase 2: votación, asistencia y tiempo de palabra.
        self.vt_app = False
        self.vt_params = {"menu": 2, "opciones": 3, "abierta": True}
        self.vt_tema = (0, "")
        self.vt_estado = "cerrada"
        self.votos: dict[int, int] = {}   # delegado (= unidad acá) -> opción
        self.votar_solos = True           # al abrir, los delegados votan solos en unos segundos
        self.at_app = False
        self.presentes = {u: True for u in self.unidades}
        self.tiempo = {"minutos": 0, "reloj": False}
        # DDI en modo doble: la CCU le da UNA dirección; la mitad derecha es esa dirección + 256.
        for u in dobles:
            if u in self.unidades:
                self.unidades[u]["tipo"] = 0x02
                self.unidades[u + x.DESPLAZAMIENTO_DERECHA] = {"tipo": 0x02, "conectada": True, "mitad_derecha": True}
                self.presentes[u + x.DESPLAZAMIENTO_DERECHA] = True
        self.cerrar_tras_siguiente = False
        self.extra: dict[str, dict] = {}  # lo último que se pidió de cada función sin documentar

    def arrancar(self) -> None:
        self.t.arrancar()
        if self.actividad_s > 0:
            threading.Thread(target=self._actividad, daemon=True, name="dcn-sim").start()

    def parar(self) -> None:
        self.t.parar()

    # --- la sala ---
    def apretar(self, u: int) -> None:
        with self.lock:
            if not self.unidades.get(u, {}).get("conectada"):
                return
            if self.ajustes["modo"] in m.MODOS_OPERADOR and not self._es_presidente(u):
                if any(p[0] == u for p in self.rts) or any(o[0] == u for o in self.spk if o[1]):
                    return
                if len(self.rts) >= self.ajustes["max_pedidos"]:
                    return
                primero = not self.rts
                self.rts.append((u, u))
                self._ntf(m.MM_C_RTS_INSERT_ON_PC, m.rts_insertar((m.EMPTY_UNIT, m.EMPTY_DELEGATE), (u, u)))
                if primero:
                    self._ntf(m.MM_C_RTS_FIRST_ON_PC, m.rts(u, u))
            else:
                prendido = any(o[0] == u and o[1] for o in self.spk)
                self._micro(u, not prendido, desde_sala=True)

    # --- votación y asistencia en la sala ---
    def votar(self, u: int, opcion: int) -> None:
        with self.lock:
            if self.vt_estado != "abierta" or not self.presentes.get(u) or not self.unidades.get(u, {}).get("conectada"):
                return
            if 0 <= opcion < self.vt_params["opciones"]:
                self.votos[u] = opcion
                self._ntf(m.VT_C_RESULTSNOTIFY, self._resultado())

    def presente(self, u: int, si: bool) -> None:
        with self.lock:
            self.presentes[u] = si
            self._ntf(m.AT_C_SEND_INDIV_REGISTRATION, m.asistencia([(u, u, 1 if si else 2)]))
            pres = sum(1 for x in self.presentes.values() if x)
            self._ntf(m.AT_C_SEND_TOTAL_REGISTRATION, m.palabra(pres) + m.palabra(len(self.presentes) - pres))

    def _resultado(self) -> bytes:
        presentes = [u for u, p in self.presentes.items() if p and self.unidades[u]["conectada"]]
        votos = [0] * 25
        for _u, o in self.votos.items():
            votos[o] += 1
        return m.resultado(self.vt_tema[0], len(presentes), len(presentes) - len(self.votos), votos, dict(self.votos))

    def _votan_solos(self) -> None:
        delegados = [u for u, x in self.unidades.items() if x["conectada"] and self.presentes.get(u)]
        random.shuffle(delegados)
        for u in delegados:
            time.sleep(random.uniform(0.05, 0.25))
            if self.vt_estado != "abierta":
                return
            self.votar(u, random.choices(range(self.vt_params["opciones"]), weights=[6, 3, 1, 1][: self.vt_params["opciones"]])[0])

    def desconectar(self, u: int, conectada: bool = False) -> None:
        with self.lock:
            self.unidades[u]["conectada"] = conectada
            self._ntf(m.SC_C_CONNECT_UNIT if conectada else m.SC_C_DISCONNECT_UNIT,
                      m.unidad_sc(u, self.unidades[u]["tipo"]))

    def _actividad(self) -> None:
        while self.t.andando:
            time.sleep(max(1.0, random.uniform(0.5, 1.5) * self.actividad_s))
            delegados = [u for u, x in self.unidades.items() if x["conectada"] and not self._es_presidente(u)]
            if delegados:
                self.apretar(random.choice(delegados))

    # --- protocolo ---
    def _es_presidente(self, u: int) -> bool:
        return (self.unidades.get(u, {}).get("tipo", 0) & 0xF0) == m.TIPO_PRESIDENTE

    def _ntf(self, f: int, datos: bytes) -> None:
        if (f >> 8) == m.APP_MM and not self.mm_arrancado:
            return
        if (f >> 8) == m.APP_SC and not self.sc_arrancado:
            return
        if (f >> 8) == m.APP_VT and not self.vt_app:
            return
        if (f >> 8) == 0x09 and not self.at_app:
            return
        self.t.enviar(m.Mensaje(m.NTF, f, datos))

    def _rsp(self, f: int, datos: bytes = b"", error: int = 0) -> None:
        self.t.enviar(m.Mensaje(m.RSP, f, datos, error))

    def _micro(self, u: int, on: bool, desde_sala: bool = False) -> int:
        if u not in self.unidades:
            return 2
        if not self.unidades[u]["conectada"]:
            return 48
        o = next((x for x in self.spk if x[0] == u), None)
        if on:
            if o is None:
                delegados = [x for x in self.spk if x[1] and not self._es_presidente(x[0])]
                if not self._es_presidente(u) and len(delegados) >= self.ajustes["max_abiertos"]:
                    return 34
                self.spk.append([u, True])
                pedido = next((p for p in self.rts if p[0] == u), None)
                if pedido is not None:  # tenía pedida la palabra: sale de la lista
                    self.rts = [p for p in self.rts if p[0] != u]
                    self._ntf(m.MM_C_RTS_REMOVE_ON_PC, m.rts(*pedido))
                self._ntf(m.MM_C_SPK_APPEND_ON_PC, m.unidad(u))
            else:
                o[1] = True
        elif o is not None:
            o[1] = False
            if self._es_presidente(u):
                self.spk = [x for x in self.spk if x[0] != u]
        self._ntf(m.MM_C_MICRO_ON_OFF, m.palabra(u) + m.palabra(m.MM_C_PC_MIC_ON if on else m.MM_C_PC_MIC_OFF)
                  + m.palabra(3))
        return 0

    def _recibir(self, msg: m.Mensaje) -> None:
        if msg.tipo == m.COMM_PARAMS and msg.fn == m.RSMC_SET_COMMUNICATION_PARAMS:
            self.enlace = m.leer_parametros_enlace(msg.datos)
            self.t.enviar(m.Mensaje(m.COMM_PARAMS, m.RSMC_RSP_COMMUNICATION_PARAMS, msg.datos))
            return
        if msg.tipo != m.REQ:
            return
        with self.lock:
            self._atender(msg.fn, msg.datos)

    def _atender(self, f: int, d: bytes) -> None:
        if f == m.SC_C_CHECK_LINK:
            self._rsp(f)
        elif f == m.SC_C_GET_CCU_VERSIONINFO:
            self._rsp(f, b"SIM DCN-CCU 2.9\x00")
        elif f == m.SC_C_START_APP:
            self.sc_arrancado = True
            self._rsp(f, m.palabra(1))
        elif f == m.SC_C_STOP_APP:
            self.sc_arrancado = False
            self._rsp(f, m.palabra(0))
        elif f == m.SC_C_GET_CCU_CONFIG_PROPERTY:
            indice = m.leer_palabra(d) if len(d) >= 2 else 0
            lista = ([(u, d["tipo"]) for u, d in self.unidades.items() if d["conectada"] and not d.get("mitad_derecha")]
                     if indice == 0 else [])
            self._rsp(f, m.config(lista))
        elif f in (m.MM_C_START_MM, m.MM_C_START_MON_MM):
            self.mm_arrancado = True
            self._rsp(f, m.palabra(1))
        elif f in (m.MM_C_STOP_MM, m.MM_C_STOP_MON_MM):
            self.mm_arrancado = False
            self._rsp(f, m.palabra(0))
        elif f == m.MM_C_GET_SETTINGS:
            self._rsp(f, m.ajustes(self.ajustes))
        elif f == m.MM_C_SET_SETTINGS:
            a = m.leer_ajustes(d)
            if a["max_abiertos"] < 1 or a["max_abiertos"] > 25:
                return self._rsp(f, error=17)
            self.ajustes = a
            self._rsp(f)
            self._ntf(m.MM_C_SET_SETTINGS_ON_PC, m.ajustes(a))
        elif f == m.MM_C_SET_MIC_OPER_MODE:
            self.ajustes["modo"] = m.leer_palabra(d)
            self._rsp(f)
        elif f == m.MM_C_SET_ACTIVE_MICS:
            self.ajustes["max_abiertos"] = m.leer_palabra(d)
            self._rsp(f)
        elif f == m.MM_C_SET_MICRO_ON_OFF:
            u, on = m.leer_palabra(d), bool(d[2])
            err = self._micro(u, on)
            self._rsp(f, error=err)
        elif f == m.MM_C_SHIFT:
            if self.ajustes["modo"] not in m.MODOS_OPERADOR:
                return self._rsp(f, error=18)
            if not self.rts:
                return self._rsp(f, error=26)
            u, _d = self.rts.pop(0)
            if self.cerrar_tras_siguiente:  # MM_C_MICSOFF_AFTER_SHIFT: se cierran los que hablaban
                for o in [o for o in self.spk if o[1] and not self._es_presidente(o[0])]:
                    self.spk = [y for y in self.spk if y[0] != o[0]]
                    self._ntf(m.MM_C_SPK_REMOVE_ON_PC, m.unidad(o[0]))
            delegados = [o for o in self.spk if o[1] and not self._es_presidente(o[0])]
            if len(delegados) >= self.ajustes["max_abiertos"]:
                viejo = delegados[0][0]
                self.spk = [x for x in self.spk if x[0] != viejo]
                self._ntf(m.MM_C_SPK_REMOVE_ON_PC, m.unidad(viejo))
            self._rsp(f)
            self._ntf(m.MM_C_RTS_REMOVE_ON_PC, m.rts(u, u))
            self._micro(u, True)
            if self.rts:
                self._ntf(m.MM_C_RTS_FIRST_ON_PC, m.rts(*self.rts[0]))
                self.aviso("proximo_en_hablar", {"unidad": self.rts[0][0], "delegado": self.rts[0][1]})
        elif f == m.MM_C_SPK_GET:
            self._rsp(f, m.lista_spk([(u, on) for u, on in self.spk]))
        elif f == m.MM_C_SPK_REMOVE:
            u = m.leer_palabra(d)
            if not any(x[0] == u for x in self.spk):
                return self._rsp(f, error=9)
            self.spk = [x for x in self.spk if x[0] != u]
            self._rsp(f)
            self._ntf(m.MM_C_SPK_REMOVE_ON_PC, m.unidad(u))
        elif f == m.MM_C_SPK_CLEAR:
            self.spk = []
            self._rsp(f)
            self._ntf(m.MM_C_SPK_CLEAR_ON_PC, b"")
        elif f == m.MM_C_SPK_APPEND:
            self._rsp(f, error=self._micro(m.leer_palabra(d), True))
        elif f == m.MM_C_RTS_GET:
            self._rsp(f, m.lista_rts(self.rts))
        elif f == m.MM_C_RTS_SET:
            self.rts = m.leer_lista_rts(d)
            self._rsp(f)
            self._ntf(m.MM_C_RTS_SET_ON_PC, m.lista_rts(self.rts))
        elif f == m.MM_C_RTS_CLEAR:
            self.rts = []
            self._rsp(f)
            self._ntf(m.MM_C_RTS_CLEAR_ON_PC, b"")
        elif f == m.MM_C_RTS_REMOVE:
            u, dd = m.leer_rts(d)
            if not any(p[0] == u for p in self.rts):
                return self._rsp(f, error=8)
            self.rts = [p for p in self.rts if p[0] != u]
            self._rsp(f)
            self._ntf(m.MM_C_RTS_REMOVE_ON_PC, m.rts(u, dd))
        elif f == m.MM_C_RTS_APPEND:
            u, _dd = m.leer_rts(d)
            if u not in self.unidades:
                return self._rsp(f, error=2)
            if any(p[0] == u for p in self.rts):
                return self._rsp(f, error=6)
            if len(self.rts) >= self.ajustes["max_pedidos"]:
                return self._rsp(f, error=24)
            self.rts.append((u, u))
            self._rsp(f)
            self._ntf(m.MM_C_RTS_INSERT_ON_PC, m.rts_insertar((m.EMPTY_UNIT, m.EMPTY_DELEGATE), (u, u)))
        elif f in (m.MM_C_LAST_MINUTE_WARNING, m.MM_C_TIME_FINISHED_WARNING):
            self._rsp(f)
        elif f == m.MM_C_SET_SPEECHTIME_SETTINGS:
            self.tiempo = {"minutos": m.leer_palabra(d), "reloj": bool(d[2])}
            self._rsp(f)
        # --- votación ---
        elif f == m.VT_C_START_APP:
            self.vt_app = True
            self._rsp(f)
        elif f == m.VT_C_STOP_APP:
            self.vt_app = False
            self._rsp(f)
        elif not self.vt_app and (f >> 8) == m.APP_VT:
            self._rsp(f, error=286)
        elif f == m.VT_C_DOWNLOAD_SUBJECT:
            if self.vt_estado != "cerrada":
                return self._rsp(f, error=276)
            self.vt_tema = m.leer_tema(d)
            self._rsp(f)
        elif f == m.VT_C_SET_VOTINGPARAMS:
            if self.vt_estado != "cerrada":
                return self._rsp(f, error=276)
            self.vt_params = m.leer_params_voto(d)
            self._rsp(f)
        elif f == m.VT_C_SET_GLOBAL_SETTINGS:
            self._rsp(f)
        elif f == m.VT_C_START_VOTING:
            if self.vt_estado != "cerrada":
                return self._rsp(f, error=276)
            self.vt_estado = "abierta"
            self.votos = {}
            self._rsp(f)
            self._ntf(m.VT_C_RESULTSNOTIFY, self._resultado())
            if self.votar_solos:
                threading.Thread(target=self._votan_solos, daemon=True).start()
        elif f == m.VT_C_HOLD_VOTING:
            if self.vt_estado != "abierta":
                return self._rsp(f, error=277)
            self.vt_estado = "pausada"
            self._rsp(f)
        elif f == m.VT_C_RESTART_VOTING:
            if self.vt_estado != "pausada":
                return self._rsp(f, error=278)
            self.vt_estado = "abierta"
            self._rsp(f)
        elif f == m.VT_C_STOP_VOTING:
            if self.vt_estado == "cerrada":
                return self._rsp(f, error=277)
            self.vt_estado = "cerrada"
            self._rsp(f)
            self._ntf(m.VT_C_RESULTSNOTIFY, self._resultado())
        elif f == m.VT_C_GET_RESULTS:
            self._rsp(f, self._resultado())
        # --- asistencia ---
        elif f == m.AT_C_START_AT_APP:
            self.at_app = True
            self._rsp(f)
        elif f == m.AT_C_STOP_AT_APP:
            self.at_app = False
            self._rsp(f)
        elif f == m.AT_C_ACTIVATE:
            self._rsp(f)
        elif f == m.AT_C_GET_INDIV_REGISTRATION:
            if not self.at_app:
                return self._rsp(f, error=2305)
            items = [(u, u, 1 if p else 2) for u, p in self.presentes.items()] if m.leer_palabra(d) == 0 else []
            self._rsp(f, m.asistencia(items))
        elif f in x.POR_CODIGO:
            self._extra(x.POR_CODIGO[f], d)
        else:
            self._rsp(f, error=1)

    # --- funciones sin documentar (dcn/extra.py) ---
    _RESPUESTAS = {"version_fpga": {"mayor": 3, "menor": 1}, "version_mmp": {"firmware": 2, "build": 41, "serie": "01 02 03 04"},
                   "ccu_esclavas": {"ids": []}, "votacion_reloj": {"segundos": 0}, "contenido_id": {"id": 1},
                   "acceso_cambio_permitido": {"permitido": True}, "canales": {"canales": [[0, 2], [1, 1]]},
                   "codigo_autorizacion": {"version": 4, "banderas": 0x0001, "aplicaciones_pc": 0, "aplicaciones_oi": 1,
                                           "tipo_licencia": 0},
                   "votacion_por_respuesta": {"delegados": []}, "acceso_estado_unidades": {"cantidad": 0, "items": ""},
                   "ccu_idiomas": {"datos": "00"}, "canales_individuales": {"datos": ""}}

    def _extra(self, nombre: str, d: bytes) -> None:
        cod, _b, clase, params, resp = x.FUNCIONES[nombre][:5]
        if clase != "pedido":
            return self._rsp(cod, error=1)
        self.extra[nombre] = x.decodificar(params, d)
        if nombre == "cerrar_tras_siguiente":
            self.cerrar_tras_siguiente = bool(self.extra[nombre].get("activo"))
        elif nombre == "pedidos_max":
            self.ajustes["max_pedidos"] = int(self.extra[nombre].get("largo", 50))
        elif nombre == "votacion_por_respuesta":
            r = self.extra[nombre].get("respuesta", 0)
            return self._rsp(cod, x.codificar(resp, {"delegados": [u for u, o in self.votos.items() if o == r]}))
        self._rsp(cod, x.codificar(resp, self._RESPUESTAS.get(nombre, {})) if resp else b"")

    def aviso(self, nombre: str, datos: dict) -> None:
        """La CCU manda un aviso sin documentar (lo que haría la unidad del presidente, por ejemplo)."""
        cod, _b, _c, _p, resp = x.FUNCIONES[nombre][:5]
        with self.lock:
            if nombre in ("votacion_pedido_iniciar", "votacion_pedido_reanudar"):
                self.vt_estado = "abierta"
            elif nombre == "votacion_pedido_cerrar":
                self.vt_estado = "cerrada"
            elif nombre == "votacion_pedido_pausar":
                self.vt_estado = "pausada"
            completos = {r[0]: datos.get(r[0], 0) for r in resp}
            self.t.enviar(m.Mensaje(m.NTF, cod, x.codificar(resp, completos)))
