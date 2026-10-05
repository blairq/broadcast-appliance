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

from . import mensajes as m


class Simulador:
    def __init__(self, transporte, unidades: int = 12, presidente: int = 1, actividad_s: float = 0) -> None:
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

    def arrancar(self) -> None:
        self.t.latido_s = self.t.latido_s or 5.0
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
                self.rts = [p for p in self.rts if p[0] != u]
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
        if msg.tipo != m.REQ:
            return
        with self.lock:
            self._atender(msg.fn, msg.datos)

    def _atender(self, f: int, d: bytes) -> None:
        if f == m.SC_C_GET_CCU_VERSIONINFO:
            self._rsp(f, b"SIM DCN-CCU 2.9\x00")
        elif f == m.SC_C_START_APP:
            self.sc_arrancado = True
            self._rsp(f, m.palabra(1))
        elif f == m.SC_C_STOP_APP:
            self.sc_arrancado = False
            self._rsp(f, m.palabra(0))
        elif f == m.SC_C_GET_CCU_CONFIG_PROPERTY:
            indice = m.leer_palabra(d) if len(d) >= 2 else 0
            lista = [(u, x["tipo"]) for u, x in self.unidades.items() if x["conectada"]] if indice == 0 else []
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
            delegados = [x for x in self.spk if x[1] and not self._es_presidente(x[0])]
            if len(delegados) >= self.ajustes["max_abiertos"]:
                viejo = delegados[0][0]
                self.spk = [x for x in self.spk if x[0] != viejo]
                self._ntf(m.MM_C_SPK_REMOVE_ON_PC, m.unidad(viejo))
            self._rsp(f)
            self._ntf(m.MM_C_RTS_REMOVE_ON_PC, m.rts(u, u))
            self._micro(u, True)
            if self.rts:
                self._ntf(m.MM_C_RTS_FIRST_ON_PC, m.rts(*self.rts[0]))
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
        else:
            self._rsp(f, error=1)
