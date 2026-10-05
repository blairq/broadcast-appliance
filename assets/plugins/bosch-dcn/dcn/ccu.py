"""La CCU vista desde la app: pedidos, arranque, estado y avisos (guía §4, §6 y §7.2). NO BLOQUEANTE.

Todo lo que se le pide a la CCU vuelve ENSEGUIDA con un ``Future``:

    ccu = Ccu(transporte, solo_escuchar=True)
    ccu.al_cambio = lambda que, datos: ...     # "microfono", "pedidos", "oradores", "modo", "unidades", "todo"
    f = ccu.microfono(12, True)                # no espera
    f.add_done_callback(...)                   # o, desde un hilo que PUEDE esperar: f.result(timeout=3)

Los pedidos van a una cola y salen de a uno (el manual pide esperar la respuesta antes del
siguiente): la cola la atiende el hilo de E/S del transporte en cada vuelta (``al_tic``), con su
reloj de 10 s por respuesta. Ese hilo nunca espera nada. Los ``al_cambio`` salen de ese hilo: el que
los recibe tiene que ser rápido (publicar un evento, no más).

El estado habla de UNIDADES (wUnitId). Las bancas las pone el plugin con el mapa del Cuerpo.
"""
from __future__ import annotations

import threading
import time
from collections import deque
from concurrent.futures import Future
from typing import Callable

from . import mensajes as m


class ErrorCcu(RuntimeError):
    def __init__(self, codigo: int, texto: str | None = None) -> None:
        super().__init__(texto or m.texto_error(codigo))
        self.codigo = codigo


def _fallado(e: Exception) -> Future:
    f: Future = Future()
    f.set_exception(e)
    return f


def _copiar(de: Future, a: Future) -> None:
    if a.done():
        return
    e = de.exception()
    if e is not None:
        a.set_exception(e)
    else:
        a.set_result(de.result())


def luego(fut: Future, fn: Callable) -> Future:
    """Otro Future: cuando `fut` sale bien, ``fn(resultado)`` (que puede devolver otro Future)."""
    out: Future = Future()

    def cb(f: Future) -> None:
        e = f.exception()
        if e is not None:
            out.set_exception(e)
            return
        try:
            r = fn(f.result())
        except Exception as ex:  # noqa: BLE001
            out.set_exception(ex)
            return
        if isinstance(r, Future):
            r.add_done_callback(lambda g: _copiar(g, out))
        else:
            out.set_result(r)

    fut.add_done_callback(cb)
    return out


def todos(futs: list[Future]) -> Future:
    """Se resuelve cuando terminan todos; si alguno falló, con el primer error."""
    out: Future = Future()
    if not futs:
        out.set_result([])
        return out
    faltan = [len(futs)]
    lock = threading.Lock()

    def cb(_f: Future) -> None:
        with lock:
            faltan[0] -= 1
            if faltan[0]:
                return
        err = next((f.exception() for f in futs if f.exception() is not None), None)
        if err is not None:
            out.set_exception(err)
        else:
            out.set_result([f.result() for f in futs])

    for f in futs:
        f.add_done_callback(cb)
    return out


class Estado:
    def __init__(self) -> None:
        self.lock = threading.RLock()
        self.unidades: dict[int, dict] = {}       # unidad -> {tipo, conectada}
        self.oradores: list[list] = []            # [[unidad, microfono_prendido]] en orden (SPK)
        self.pedidos: list[tuple[int, int]] = []  # [(unidad, delegado)] en orden (RTS)
        self.ajustes: dict = {}
        self.version = ""
        self.sincronizado = False

    def abiertos(self) -> list[int]:
        with self.lock:
            return [u for u, on in self.oradores if on]

    def es_presidente(self, u: int) -> bool:
        return (self.unidades.get(u, {}).get("tipo", 0) & 0xF0) == m.TIPO_PRESIDENTE

    def poner_micro(self, u: int, on: bool) -> None:
        with self.lock:
            for o in self.oradores:
                if o[0] == u:
                    o[1] = on
                    return
            if on:
                self.oradores.append([u, True])

    def sacar_orador(self, u: int) -> None:
        with self.lock:
            self.oradores = [o for o in self.oradores if o[0] != u]

    def sacar_pedido(self, u: int, d: int | None = None) -> None:
        with self.lock:
            self.pedidos = [p for p in self.pedidos if not (p[0] == u and (d is None or p[1] == d))]

    def insertar_pedido(self, buscar: tuple[int, int], nuevo: tuple[int, int]) -> None:
        with self.lock:
            self.sacar_pedido(*nuevo)
            if buscar[0] != m.EMPTY_UNIT:
                for i, p in enumerate(self.pedidos):
                    if p[0] == buscar[0]:
                        self.pedidos.insert(i, nuevo)
                        return
            self.pedidos.append(nuevo)


class Ccu:
    def __init__(self, transporte, solo_escuchar: bool = True, espera_rsp_s: float = 10.0,
                 registro: Callable[[str], None] | None = None) -> None:
        self.t = transporte
        self.solo_escuchar = solo_escuchar
        self.espera_rsp_s = espera_rsp_s
        self.estado = Estado()
        self.al_cambio: Callable[[str, dict], None] = lambda _que, _datos: None
        self.registro = registro or (lambda _texto: None)
        self._cola: deque[tuple[int, bytes, Future]] = deque()
        self._lock = threading.Lock()  # solo la cola (pedir() viene de cualquier hilo)
        self._actual: list | None = None  # [fn, datos, Future, vence, t0]: lo toca solo el hilo de E/S
        self.t.al_recibir = self._recibir
        self.t.al_tic = self._tic

    # --- pedir (no bloquea) ---
    def pedir(self, f: int, datos: bytes = b"", cambia: bool = True) -> Future:
        if cambia and self.solo_escuchar:
            return _fallado(ErrorCcu(0, "la CCU está en «solo escuchar»: apagalo en Conferencia → Conexión para mandarle órdenes"))
        fut: Future = Future()
        with self._lock:
            self._cola.append((f, datos, fut))
        return fut

    def fallar_todo(self, motivo: str) -> None:
        """El enlace se cayó: todo lo pendiente falla (lo llama el plugin desde ``al_caer``)."""
        with self._lock:
            pendientes, self._cola = list(self._cola), deque()
        actual, self._actual = self._actual, None
        if actual is not None:
            pendientes.append((actual[0], actual[1], actual[2]))
        for _f, _d, fut in pendientes:
            if not fut.done():
                fut.set_exception(ErrorCcu(0, motivo))

    def _tic(self, ahora: float) -> None:
        a = self._actual
        if a is not None and ahora >= a[3]:
            self._actual = None
            if not a[2].done():
                a[2].set_exception(ErrorCcu(0, f"la CCU no contestó {m.nombre_fn(a[0])} en {self.espera_rsp_s:.0f} s"))
        if self._actual is not None:
            return
        with self._lock:
            sig = self._cola.popleft() if self._cola else None
        if sig is None:
            return
        f, datos, fut = sig
        if not fut.set_running_or_notify_cancel():
            return
        self._actual = [f, datos, fut, ahora + self.espera_rsp_s, ahora]
        envio = self.t.enviar(m.Mensaje(m.REQ, f, datos))

        def no_salio(e: Future) -> None:
            if e.exception() is not None and self._actual is not None and self._actual[2] is fut:
                self._actual = None
                if not fut.done():
                    fut.set_exception(ErrorCcu(0, str(e.exception())))

        envio.add_done_callback(no_salio)

    def _recibir(self, msg: m.Mensaje) -> None:
        if msg.tipo in (m.RSP, m.NAK):
            a = self._actual
            if a is None or (msg.tipo == m.RSP and msg.fn != a[0]):
                return
            self._actual = None
            ms = (time.monotonic() - a[4]) * 1000
            if msg.tipo == m.NAK:
                a[2].set_exception(ErrorCcu(msg.error, f"la CCU rechazó el mensaje (error de protocolo {msg.error})"))
                return
            self.registro(f"{m.nombre_fn(a[0])} [{a[1].hex(' ')}] → "
                          f"{'OK' if msg.error == 0 else 'error ' + str(msg.error)} {ms:.0f} ms")
            if msg.error:
                a[2].set_exception(ErrorCcu(msg.error))
            else:
                a[2].set_result(msg.datos)
            return
        if msg.tipo == m.NTF:
            try:
                self._aviso(msg)
            except Exception as e:  # un aviso mal formado no tira abajo el enlace
                self.registro(f"aviso {m.nombre_fn(msg.fn)} no se pudo leer: {e}")

    # --- arranque (§6): todo encolado de una, sin esperar ---
    def sincronizar(self) -> Future:
        e = self.estado
        e.sincronizado = False

        def version(v: bytes) -> None:
            e.version = v.split(b"\x00")[0].decode("latin-1", "replace").strip() or v.hex()

        def config(indice: int):
            def leer(d: bytes):
                _conectadas, lista = m.leer_config(d)
                with e.lock:
                    for u, tipo in lista:
                        e.unidades[u] = {"tipo": tipo, "conectada": True}
                if len(lista) >= 1500:  # hay otro grupo de unidades
                    return luego(self.pedir(m.SC_C_GET_CCU_CONFIG_PROPERTY, m.palabra(indice + 1), cambia=False),
                                 config(indice + 1))
                return None
            return leer

        def ajustes(d: bytes) -> None:
            e.ajustes = m.leer_ajustes(d)

        def oradores(d: bytes) -> None:
            with e.lock:
                e.oradores = [[u, on] for u, on in m.leer_lista_spk(d)]

        def pedidos(d: bytes) -> None:
            with e.lock:
                e.pedidos = m.leer_lista_rts(d)

        with e.lock:
            e.unidades = {}
        fv = self.pedir(m.SC_C_GET_CCU_VERSIONINFO, cambia=False)
        fv.add_done_callback(lambda f: version(f.result()) if f.exception() is None
                             else setattr(e, "version", f"(sin versión: {f.exception()})"))
        pasos = [
            self.pedir(m.SC_C_START_APP, cambia=False),
            luego(self.pedir(m.SC_C_GET_CCU_CONFIG_PROPERTY, m.palabra(0), cambia=False), config(0)),
            self.pedir(m.MM_C_START_MON_MM if self.solo_escuchar else m.MM_C_START_MM, cambia=False),
            luego(self.pedir(m.MM_C_GET_SETTINGS, cambia=False), ajustes),
            luego(self.pedir(m.MM_C_SPK_GET, cambia=False), oradores),
            luego(self.pedir(m.MM_C_RTS_GET, cambia=False), pedidos),
        ]

        def listo(_r) -> None:
            e.sincronizado = True
            self.al_cambio("todo", {})

        return luego(todos(pasos), listo)

    # --- órdenes (§5.2): todas devuelven un Future ---
    def microfono(self, u: int, on: bool) -> Future:
        def hecho(_d) -> None:
            self.estado.poner_micro(u, on)
            self.al_cambio("microfono", {"unidad": u, "abierto": on, "origen": "app"})
        return luego(self.pedir(m.MM_C_SET_MICRO_ON_OFF, m.micro_onoff(u, on)), hecho)

    def refrescar_listas(self) -> Future:
        e = self.estado

        def spk(d: bytes) -> None:
            antes = set(e.abiertos())
            with e.lock:
                e.oradores = [[u, on] for u, on in m.leer_lista_spk(d)]
            ahora = set(e.abiertos())
            for u in ahora - antes:
                self.al_cambio("microfono", {"unidad": u, "abierto": True, "origen": "app"})
            for u in antes - ahora:
                self.al_cambio("microfono", {"unidad": u, "abierto": False, "origen": "app"})
            self.al_cambio("oradores", {})

        def rts(d: bytes) -> None:
            with e.lock:
                e.pedidos = m.leer_lista_rts(d)
            self.al_cambio("pedidos", {})

        return todos([luego(self.pedir(m.MM_C_SPK_GET, cambia=False), spk),
                      luego(self.pedir(m.MM_C_RTS_GET, cambia=False), rts)])

    def siguiente(self) -> Future:
        f = self.pedir(m.MM_C_SHIFT)
        return luego(todos([f, self.refrescar_listas()]), lambda _r: None)

    def orador_quitar(self, u: int) -> Future:
        def hecho(_d) -> None:
            self.estado.sacar_orador(u)
            self.al_cambio("oradores", {})
        return luego(self.pedir(m.MM_C_SPK_REMOVE, m.unidad(u)), hecho)

    def cerrar_todos(self, incluir_presidente: bool = False) -> Future:
        us = [u for u in self.estado.abiertos() if incluir_presidente or not self.estado.es_presidente(u)]
        return luego(todos([self.microfono(u, False) for u in us]), lambda _r: us)

    def pedido_agregar(self, u: int) -> Future:
        f = self.pedir(m.MM_C_RTS_APPEND, m.rts(u))
        return luego(todos([f, self.refrescar_listas()]), lambda _r: None)

    def pedido_quitar(self, u: int) -> Future:
        d = next((p[1] for p in self.estado.pedidos if p[0] == u), m.EMPTY_DELEGATE)

        def hecho(_d) -> None:
            self.estado.sacar_pedido(u)
            self.al_cambio("pedidos", {})
        return luego(self.pedir(m.MM_C_RTS_REMOVE, m.rts(u, d)), hecho)

    def pedido_antes_de(self, u: int, referencia: int | None) -> Future:
        """Mueve (o agrega) el pedido de `u` antes del de `referencia` (None = primero)."""
        with self.estado.lock:
            lista = [p for p in self.estado.pedidos if p[0] != u]
            nuevo = next((p for p in self.estado.pedidos if p[0] == u), (u, m.EMPTY_DELEGATE))
        i = 0 if referencia is None else next((k for k, p in enumerate(lista) if p[0] == referencia), len(lista))
        lista.insert(i, nuevo)

        def hecho(_d) -> None:
            with self.estado.lock:
                self.estado.pedidos = lista
            self.al_cambio("pedidos", {})
        return luego(self.pedir(m.MM_C_RTS_SET, m.lista_rts(lista)), hecho)

    def pedidos_limpiar(self) -> Future:
        def hecho(_d) -> None:
            with self.estado.lock:
                self.estado.pedidos = []
            self.al_cambio("pedidos", {})
        return luego(self.pedir(m.MM_C_RTS_CLEAR), hecho)

    def modo(self, nombre: str | None = None, max_abiertos: int | None = None, max_pedidos: int | None = None) -> Future:
        if nombre is not None and nombre not in m.MODOS:
            return _fallado(ErrorCcu(0, "modo: " + ", ".join(m.MODOS)))
        if self.solo_escuchar:
            return self.pedir(m.MM_C_SET_SETTINGS)  # falla con el mensaje de «solo escuchar»

        def poner(d: bytes) -> Future:
            a = m.leer_ajustes(d)
            if nombre is not None:
                a["modo"] = m.MODOS[nombre]
            if max_abiertos is not None:
                a["max_abiertos"] = int(max_abiertos)
            if max_pedidos is not None:
                a["max_pedidos"] = int(max_pedidos)

            def hecho(_d) -> None:
                self.estado.ajustes = a
                self.al_cambio("modo", {})
            return luego(self.pedir(m.MM_C_SET_SETTINGS, m.ajustes(a)), hecho)

        # Se leen los ajustes de la CCU primero: no pisar lo que cambió otro controlador.
        return luego(self.pedir(m.MM_C_GET_SETTINGS, cambia=False), poner)

    def aviso_tiempo(self, u: int, terminado: bool) -> Future:
        return self.pedir(m.MM_C_TIME_FINISHED_WARNING if terminado else m.MM_C_LAST_MINUTE_WARNING, m.unidad(u))

    # --- avisos de la CCU (NTF), desde el hilo de E/S ---
    def _aviso(self, msg: m.Mensaje) -> None:
        e, f, d = self.estado, msg.fn, msg.datos
        cambio = self.al_cambio
        if f == m.MM_C_MICRO_ON_OFF:
            u, micro, _prio = m.leer_micro_on_pc(d)
            if micro in (m.MM_C_PC_MIC_ON, m.MM_C_PC_MIC_OFF):
                on = micro == m.MM_C_PC_MIC_ON
                e.poner_micro(u, on)
                cambio("microfono", {"unidad": u, "abierto": on, "origen": "ccu"})
        elif f == m.MM_C_SPK_APPEND_ON_PC:
            u = m.leer_palabra(d)
            e.poner_micro(u, True)
            cambio("microfono", {"unidad": u, "abierto": True, "origen": "ccu"})
        elif f == m.MM_C_SPK_REMOVE_ON_PC:
            u = m.leer_palabra(d)
            estaba = u in e.abiertos()
            e.sacar_orador(u)
            if estaba:
                cambio("microfono", {"unidad": u, "abierto": False, "origen": "ccu"})
            cambio("oradores", {})
        elif f == m.MM_C_SPK_CLEAR_ON_PC:
            antes = e.abiertos()
            with e.lock:
                e.oradores = []
            for u in antes:
                cambio("microfono", {"unidad": u, "abierto": False, "origen": "ccu"})
        elif f == m.MM_C_SPK_SET_ON_PC:
            antes = set(e.abiertos())
            with e.lock:
                e.oradores = [[u, on] for u, on in m.leer_lista_spk(d)]
            ahora = set(e.abiertos())
            for u in ahora - antes:
                cambio("microfono", {"unidad": u, "abierto": True, "origen": "ccu"})
            for u in antes - ahora:
                cambio("microfono", {"unidad": u, "abierto": False, "origen": "ccu"})
        elif f in (m.MM_C_SPK_INSERT_ON_PC, m.MM_C_SPK_REPLACE_ON_PC):
            buscar, nuevo = m.leer_palabra(d), m.leer_palabra(d, 2)
            if f == m.MM_C_SPK_REPLACE_ON_PC:
                e.sacar_orador(buscar)
            e.poner_micro(nuevo, True)
            cambio("microfono", {"unidad": nuevo, "abierto": True, "origen": "ccu"})
        elif f == m.MM_C_RTS_SET_ON_PC:
            with e.lock:
                e.pedidos = m.leer_lista_rts(d)
            cambio("pedidos", {})
        elif f == m.MM_C_RTS_CLEAR_ON_PC:
            with e.lock:
                e.pedidos = []
            cambio("pedidos", {})
        elif f == m.MM_C_RTS_REMOVE_ON_PC:
            e.sacar_pedido(*m.leer_rts(d))
            cambio("pedidos", {})
        elif f == m.MM_C_RTS_INSERT_ON_PC:
            nuevo = m.leer_rts(d, 4)
            e.insertar_pedido(m.leer_rts(d), nuevo)
            cambio("pedidos", {"alta": nuevo[0]})
        elif f == m.MM_C_RTS_REPLACE_ON_PC:
            actual, nuevo = m.leer_rts(d), m.leer_rts(d, 4)
            with e.lock:
                e.pedidos = [nuevo if p[0] == actual[0] else p for p in e.pedidos]
            cambio("pedidos", {})
        elif f == m.MM_C_RTS_FIRST_ON_PC:
            cambio("pedidos", {"primero": m.leer_rts(d)[0]})
        elif f == m.MM_C_SET_MIC_OPER_MODE_ON_PC:
            e.ajustes["modo"] = m.leer_palabra(d)
            cambio("modo", {})
        elif f == m.MM_C_SET_ACTIVE_MICS_ON_PC:
            e.ajustes["max_abiertos"] = m.leer_palabra(d)
            cambio("modo", {})
        elif f == m.MM_C_SET_SETTINGS_ON_PC:
            e.ajustes = m.leer_ajustes(d)
            cambio("modo", {})
        elif f in (m.SC_C_CONNECT_UNIT, m.SC_C_DISCONNECT_UNIT):
            u, tipo = m.leer_unidad_sc(d)
            with e.lock:
                e.unidades[u] = {"tipo": tipo, "conectada": f == m.SC_C_CONNECT_UNIT}
            cambio("unidades", {"unidad": u, "conectada": f == m.SC_C_CONNECT_UNIT})
        elif f == m.SC_C_CCU_REBOOT:
            e.sincronizado = False
            cambio("reinicio", {})
