"""Transportes del Open Interface: serie «Full» (2.x) y TCP 9451 (4.x). NO BLOQUEANTES.

docs/bosch-dcn-ng-open-interface.md §3.

UN SOLO HILO DE E/S por enlace y nada que espere adentro:

  * el puerto (o el socket) se abre en modo no bloqueante; se lee lo que haya y se escribe lo que
    entre (lo que no entra queda en un buffer y sale en la vuelta siguiente);
  * el hilo duerme en ``select`` sobre el descriptor (máximo 10 ms) para no gastar CPU;
  * el ACK, los reenvíos, el latido y la caída son RELOJES que se miran en cada vuelta, no esperas;
  * ``enviar(mensaje)`` encola y devuelve enseguida un ``Future`` (que se resuelve cuando la CCU
    confirmó el paquete, o falla si no lo confirmó después de los reenvíos).

Para arriba: ``al_recibir(mensaje)`` (RSP y NTF, desde el hilo de E/S: tiene que ser rápido),
``al_caer(motivo)`` y ``al_tic(ahora)`` (el cliente mira sus propios relojes en cada vuelta).

SERIE FULL (Open Interface 2.0 §4.2, verificado desensamblando el SCS.DLL del software de PC de Bosch
—DCN-PC 3.1x, el que habla con la CCU por serie— y el CCU_DDTK.exe del DVD):
``0xC8 TIPO SEC [mensaje] CORR CHK``; un 0xC8 dentro del paquete va DUPLICADO y el checksum es la suma
de los bytes TAL COMO VIAJAN (con los dos 0xC8: el manual dice lo contrario, pero el emisor y el
receptor de Bosch los cuentan a los dos); un paquete de datos en vuelo; ACK en 2 s o se reenvía (2 veces); NAK = reenviar ya;
latido ``'?'`` apenas se abre el puerto (la CCU no acepta datos antes de un latido) y cada 5 s sin
tráfico; sin nada de la CCU en 10 s, el enlace se da por caído; mensaje de 5000 bytes como máximo.

SERIE SIMPLE (§4.3): ``1B 24 0D [mensaje] CHK``, sin secuencia, sin ACK, sin latido y sin escape; si
la CCU recibe un checksum mal contesta un MDSC_NAK de largo 0. El enlace se vigila con SC_C_CHECK_LINK.

RED (CCU2, 4.x): ver Tcp.
"""
from __future__ import annotations

import os
import select
import socket
import struct
import threading
import time
from collections import deque
from concurrent.futures import Future
from typing import Callable

from . import mensajes as m

DATOS, ACK, NAKP, LATIDO = ord("$"), ord("@"), ord("#"), ord("?")
_TIPOS = {DATOS, ACK, NAKP, LATIDO}
VUELTA_S = 0.01


class EnlaceCaido(RuntimeError):
    pass


# --- Flujos no bloqueantes ----------------------------------------------------------------------
#   leer() -> bytes      lo que haya (b"" si nada), sin esperar
#   escribir(b) -> int   cuánto aceptó, sin esperar
#   esperar(seg, salida) duerme hasta que haya algo para leer (o para escribir) o pase `seg`

def _esperar_fd(fd: int, seg: float, salida: bool) -> None:
    try:
        select.select([fd], [fd] if salida else [], [], seg)
    except (OSError, ValueError):
        time.sleep(seg)


class FlujoSocket:
    def __init__(self, s: socket.socket) -> None:
        self.s = s
        self.s.setblocking(False)

    def leer(self) -> bytes:
        try:
            d = self.s.recv(4096)
        except (BlockingIOError, InterruptedError):
            return b""
        except OSError as e:
            raise EnlaceCaido(str(e)) from e
        if d == b"":
            raise EnlaceCaido("la otra punta cerró la conexión")
        return d

    def escribir(self, b: bytes) -> int:
        try:
            return self.s.send(b)
        except (BlockingIOError, InterruptedError):
            return 0
        except OSError as e:
            raise EnlaceCaido(str(e)) from e

    def esperar(self, seg: float, salida: bool) -> None:
        _esperar_fd(self.s.fileno(), seg, salida)

    def cerrar(self) -> None:
        try:
            self.s.close()
        except OSError:
            pass


class FlujoSerie:
    """Un puerto serie con pyserial (vendoreado), abierto sin esperas de lectura ni de escritura."""

    def __init__(self, puerto: str, velocidad: int, rtscts: bool = True) -> None:
        # La CCU usa control de flujo por hardware (RTS/CTS), 8N1 (Open Interface 2.0 §2.2, A.1).
        import serial  # noqa: PLC0415
        self._serial = serial
        self.p = serial.Serial(puerto, velocidad, bytesize=8, parity="N", stopbits=1, timeout=0, write_timeout=0,
                               rtscts=rtscts)
        try:
            self.fd = self.p.fileno()  # POSIX: pyserial lo abre O_NONBLOCK; en Windows no hay
        except Exception:
            self.fd = None

    def leer(self) -> bytes:
        try:
            n = self.p.in_waiting
            return self.p.read(n) if n else b""
        except Exception as e:  # SerialException: el adaptador se desenchufó
            raise EnlaceCaido(str(e)) from e

    def escribir(self, b: bytes) -> int:
        try:
            if self.fd is not None:
                return os.write(self.fd, b)
            return self.p.write(b) or 0
        except (BlockingIOError, InterruptedError):
            return 0
        except self._serial.SerialTimeoutException:
            return 0
        except Exception as e:
            raise EnlaceCaido(str(e)) from e

    def esperar(self, seg: float, salida: bool) -> None:
        if self.fd is not None:
            _esperar_fd(self.fd, seg, salida)
        else:
            time.sleep(seg)  # Windows: sin select para puertos; una vuelta corta

    def cerrar(self) -> None:
        try:
            self.p.close()
        except Exception:
            pass


# --- Base ---------------------------------------------------------------------------------------

class Transporte:
    def __init__(self, flujo, latido_s: float = 5.0, traza: Callable[[str, bytes], None] | None = None) -> None:
        self.flujo = flujo
        self.latido_s = latido_s
        self.al_recibir: Callable[[m.Mensaje], None] = lambda _msg: None
        self.al_caer: Callable[[str], None] = lambda _motivo: None
        self.al_tic: Callable[[float], None] = lambda _ahora: None
        self.traza = traza or (lambda _sentido, _b: None)
        self.contadores = {"enviados": 0, "recibidos": 0, "reenvios": 0, "nak": 0, "checksum_mal": 0}
        self.andando = False
        self.motivo_caida = ""
        self.corte_s = 2 * latido_s  # sin nada de la CCU en este tiempo, el enlace está caído
        self.ultimo_rx = self.ultimo_tx = time.monotonic()
        self._salida = bytearray()
        self._cola: deque[tuple[m.Mensaje, Future]] = deque()
        self._lock = threading.Lock()  # solo para la cola (enviar() puede venir de cualquier hilo)
        self._hilo: threading.Thread | None = None

    def arrancar(self) -> None:
        self.andando = True
        self.ultimo_rx = self.ultimo_tx = time.monotonic()
        self._hilo = threading.Thread(target=self._bucle, daemon=True, name="dcn-es")
        self._hilo.start()

    def parar(self) -> None:
        self._caer("enlace cerrado", avisar=False)

    def enviar(self, msg: m.Mensaje) -> Future:
        """Encola y vuelve enseguida. El Future se resuelve cuando el paquete salió (y, en serie, la CCU
        lo confirmó con ACK)."""
        f: Future = Future()
        if not self.andando:
            f.set_exception(EnlaceCaido(self.motivo_caida or "sin enlace con la CCU"))
            return f
        with self._lock:
            self._cola.append((msg, f))
        return f

    def _sacar(self) -> tuple[m.Mensaje, Future] | None:
        with self._lock:
            return self._cola.popleft() if self._cola else None

    def _caer(self, motivo: str, avisar: bool = True) -> None:
        if not self.andando:
            return
        self.andando = False
        self.motivo_caida = motivo
        with self._lock:
            pendientes, self._cola = list(self._cola), deque()
        for _msg, f in pendientes:
            if not f.done():
                f.set_exception(EnlaceCaido(motivo))
        self._caida_propia(motivo)
        try:
            self.flujo.cerrar()
        except Exception:
            pass
        if avisar:
            self.al_caer(motivo)

    def _caida_propia(self, motivo: str) -> None:
        pass

    def _poner(self, b: bytes) -> None:
        """A la salida (la escribe el bucle). Solo desde el hilo de E/S."""
        self.traza("tx", b)
        self._salida += b
        self.ultimo_tx = time.monotonic()

    def _bucle(self) -> None:
        while self.andando:
            try:
                self.flujo.esperar(VUELTA_S, bool(self._salida))
                if self._salida:
                    n = self.flujo.escribir(bytes(self._salida))
                    del self._salida[:n]
                d = self.flujo.leer()
            except EnlaceCaido as e:
                self._caer(str(e))
                return
            ahora = time.monotonic()
            if d:
                self.traza("rx", d)
                self.ultimo_rx = ahora
                self._bytes(d, ahora)
            self._relojes(ahora)
            if self.latido_s > 0:
                if ahora - self.ultimo_tx >= self.latido_s:
                    self._latido()
                corte = self.corte_s or 2 * self.latido_s
                if ahora - self.ultimo_rx > corte:
                    self._caer(f"la CCU no manda nada hace {corte:.0f} s")
                    return
            try:
                self.al_tic(ahora)
            except Exception:
                pass

    def _bytes(self, d: bytes, ahora: float) -> None:
        raise NotImplementedError

    def _relojes(self, ahora: float) -> None:
        raise NotImplementedError

    def _latido(self) -> None:
        raise NotImplementedError


# --- Serie Full ---------------------------------------------------------------------------------

def checksum(cuerpo: bytes, escape: int, extra: int = 0) -> tuple[int, int]:
    """(CORR, CHK) para encabezado + mensaje: suma módulo 256 invertida; si da ESC, CORR = 0x13.
    `extra`: lo que suman los bytes de escape que no están en `cuerpo` (en el receptor)."""
    s = sum(cuerpo) + extra
    chk = (~s) & 0xFF
    corr = 0
    if chk == escape:
        corr = 0x13
        chk = (~(s + corr)) & 0xFF
    return corr, chk


class SerieFull(Transporte):
    es_serie_full = True
    LARGO_MAX = 5000

    def __init__(self, flujo, escape: int = 0xC8, latido_s: float = 5.0, reintentos: int = 2,
                 espera_ack_s: float = 2.0, duplicar_escape: bool = True, traza=None) -> None:
        super().__init__(flujo, latido_s, traza)
        self._latido_dado = False
        self.escape = escape
        self.reintentos = reintentos
        self.espera_ack_s = espera_ack_s
        self.duplicar_escape = duplicar_escape
        self._sec_tx = 0
        self._sec_rx_ultimo: int | None = None
        # El paquete en vuelo: (bytes, sec, Future, vence, reenvíos hechos)
        self._vuelo: list | None = None
        self._buf = bytearray()
        self._en_paquete = False
        self._esc_pend = False
        self._extra = 0
        self._ultimo_byte = 0.0

    def armar(self, tipo: int, sec: int, msg: bytes = b"") -> bytes:
        # Como SCS.DLL: se escapa la secuencia, el largo y los datos, y la suma va sobre lo escapado.
        cola = bytes([sec]) + msg
        if self.duplicar_escape:
            cola = cola.replace(bytes([self.escape]), bytes([self.escape, self.escape]))
        enc = bytes([self.escape, tipo])
        corr, chk = checksum(enc + cola + b"\x00", self.escape)
        return enc + cola + bytes([corr, chk])

    def _latido(self) -> None:
        self._poner(self.armar(LATIDO, self._sec_tx))
        self._latido_dado = True

    def _caida_propia(self, motivo: str) -> None:
        if self._vuelo and not self._vuelo[2].done():
            self._vuelo[2].set_exception(EnlaceCaido(motivo))
        self._vuelo = None

    def _relojes(self, ahora: float) -> None:
        if self._en_paquete and ahora - self._ultimo_byte > 0.05:
            self._descartar()  # más de 50 ms entre bytes: el paquete está roto
        if not self._latido_dado:
            self._latido()  # primero un latido: la CCU no acepta datos antes
        v = self._vuelo
        if v is not None and ahora >= v[3]:
            self._reenviar(ahora)
        if self._vuelo is None:
            sig = self._sacar()
            if sig is not None:
                msg, f = sig
                if f.set_running_or_notify_cancel():
                    p = self.armar(DATOS, self._sec_tx, m.empaquetar(msg))
                    self._vuelo = [p, self._sec_tx, f, ahora + self.espera_ack_s, 0]
                    self._poner(p)

    def _reenviar(self, ahora: float) -> None:
        v = self._vuelo
        if v[4] >= self.reintentos:
            self._vuelo = None
            v[2].set_exception(EnlaceCaido(f"la CCU no confirmó el paquete después de {self.reintentos} reenvíos"))
            return
        v[4] += 1
        v[3] = ahora + self.espera_ack_s
        self.contadores["reenvios"] += 1
        self._poner(v[0])

    def _descartar(self) -> None:
        self._buf.clear()
        self._en_paquete = False
        self._esc_pend = False
        self._extra = 0  # los 0xC8 de escape, que también entran en la suma

    def _bytes(self, d: bytes, ahora: float) -> None:
        for b in d:
            if self._en_paquete and ahora - self._ultimo_byte > 0.05:
                self._descartar()
            self._ultimo_byte = ahora
            if self._esc_pend:
                self._esc_pend = False
                if b == self.escape and self._en_paquete:
                    self._extra += self.escape
                    self._agregar(b)  # ESC ESC = un ESC de datos
                elif b in _TIPOS:
                    self._buf = bytearray([self.escape, b])
                    self._en_paquete = True
                    self._extra = 0
                else:
                    self._descartar()
                continue
            if b == self.escape:
                if self._en_paquete and not self.duplicar_escape and len(self._buf) >= 3:
                    self._agregar(b)  # sin duplicar: el largo del mensaje dice dónde termina
                else:
                    self._esc_pend = True
                continue
            if self._en_paquete:
                self._agregar(b)

    def _agregar(self, b: int) -> None:
        self._buf.append(b)
        p = self._buf
        if p[1] != DATOS:
            if len(p) == 5:  # ESC TIPO SEC CORR CHK
                self._paquete(bytes(p))
            return
        if len(p) >= 6:
            largo = struct.unpack_from("<H", p, 4)[0]
            total = 3 + 3 + largo + 2
            if largo > self.LARGO_MAX or len(p) > total:
                self._descartar()
            elif len(p) == total:
                self._paquete(bytes(p))

    def _paquete(self, p: bytes) -> None:
        extra = self._extra
        self._descartar()
        tipo, sec = p[1], p[2]
        corr, chk = checksum(p[:-2] + b"\x00", self.escape, extra)
        if p[-1] != chk or p[-2] != corr:
            self.contadores["checksum_mal"] += 1
            if tipo == DATOS:
                self._poner(self.armar(NAKP, sec))
            return
        if tipo in (ACK, NAKP):
            v = self._vuelo
            if v is None or v[1] != sec:
                return
            if tipo == ACK:
                self._vuelo = None
                self._sec_tx = (sec + 1) & 0x7F
                self.contadores["enviados"] += 1
                v[2].set_result(True)
            else:
                self.contadores["nak"] += 1
                self._reenviar(time.monotonic())
        elif tipo == DATOS:
            self._poner(self.armar(ACK, sec))
            if sec == self._sec_rx_ultimo:
                return  # repetido: se confirma y no se procesa de nuevo
            self._sec_rx_ultimo = sec
            self.contadores["recibidos"] += 1
            try:
                msg = m.desempaquetar(p[3:-2])
            except ValueError:
                return
            self.al_recibir(msg)


# --- Serie Simple -------------------------------------------------------------------------------

class SerieSimple(Transporte):
    """``1B 24 0D`` + mensaje + checksum (suma de encabezado y mensaje, módulo 256, invertida)."""

    CABECERA = b"\x1b$\r"
    LARGO_MAX = 5000

    def __init__(self, flujo, traza=None) -> None:
        super().__init__(flujo, latido_s=0, traza=traza)  # sin latido: lo vigila el cliente (CHECK_LINK)
        self._buf = bytearray()
        self._ultimo_byte = 0.0

    @classmethod
    def armar(cls, msg: m.Mensaje) -> bytes:
        cuerpo = cls.CABECERA + m.empaquetar(msg)
        return cuerpo + bytes([(~sum(cuerpo)) & 0xFF])

    def _latido(self) -> None:
        pass

    def _relojes(self, ahora: float) -> None:
        if self._buf and ahora - self._ultimo_byte > 0.05:
            self._buf.clear()  # más de 50 ms entre bytes: el paquete está roto
        while True:
            sig = self._sacar()
            if sig is None:
                break
            msg, f = sig
            if f.set_running_or_notify_cancel():
                self._poner(self.armar(msg))
                self.contadores["enviados"] += 1
                f.set_result(True)

    def _bytes(self, d: bytes, ahora: float) -> None:
        self._ultimo_byte = ahora
        self._buf += d
        while True:
            i = self._buf.find(self.CABECERA)
            if i < 0:
                del self._buf[:-2]  # se guarda la punta por si la cabecera vino partida
                return
            del self._buf[:i]
            if len(self._buf) < 6:
                return
            largo = struct.unpack_from("<H", self._buf, 4)[0]
            if largo > self.LARGO_MAX:
                del self._buf[:3]
                continue
            total = 3 + 3 + largo + 1
            if len(self._buf) < total:
                return
            p = bytes(self._buf[:total])
            if (~sum(p[:-1])) & 0xFF != p[-1]:
                self.contadores["checksum_mal"] += 1
                del self._buf[:3]  # cabecera falsa adentro de otro paquete o ruido: se busca la próxima
                continue
            del self._buf[:total]
            self.contadores["recibidos"] += 1
            try:
                self.al_recibir(m.desempaquetar(p[3:-1]))
            except ValueError:
                pass


# --- TCP (CCU2, Open Interface 4.x) -------------------------------------------------------------

KEEPALIVE = 0x00447027
PROTOCOL_ERROR = 0x00447020


class Tcp(Transporte):
    """Encabezado de 16 bytes: tipo (4) + largo total (4) + reservado (4) + reservado (4).

    Un mensaje DCN lleva en el «tipo» los bytes ``byDcnMsgType, wFnId, 0x43`` y en `byData` el
    mensaje DCN ENTERO, con el wFnId adelante (y el wError en las respuestas): así lo arma el
    CCU_DDTK.exe del DVD de Bosch (largo = datos + 16, copia de los datos desde el wFnId). La CCU corta
    si no recibe nada en 15 s; se manda un latido cada 5 s. TCP no tiene ACK: el Future se resuelve
    cuando el paquete salió entero.
    """

    def __init__(self, flujo, latido_s: float = 5.0, fn_en_datos: bool = True, traza=None) -> None:
        super().__init__(flujo, latido_s, traza)
        self.corte_s = 3 * latido_s
        self.fn_en_datos = fn_en_datos
        self._buf = bytearray()

    def _latido(self) -> None:
        self._poner(struct.pack("<IIII", KEEPALIVE, 16, 0, 0))

    def _relojes(self, ahora: float) -> None:
        while True:
            sig = self._sacar()
            if sig is None:
                break
            msg, f = sig
            if not f.set_running_or_notify_cancel():
                continue
            datos = (struct.pack("<H", msg.fn) if self.fn_en_datos else b"")
            if msg.tipo == m.RSP:
                datos += struct.pack("<H", msg.error)
            datos += msg.datos
            p = bytes([msg.tipo]) + struct.pack("<H", msg.fn) + b"\x43" + struct.pack("<III", 16 + len(datos), 0, 0) + datos
            self._poner(p)
            self.contadores["enviados"] += 1
            f.set_result(True)

    def _bytes(self, d: bytes, ahora: float) -> None:
        self._buf += d
        while len(self._buf) >= 8:
            largo = struct.unpack_from("<I", self._buf, 4)[0]
            if largo < 8 or largo > 8000:
                self._buf.clear()
                return
            if len(self._buf) < largo:
                return
            p = bytes(self._buf[:largo])
            del self._buf[:largo]
            self._paquete(p)

    def _paquete(self, p: bytes) -> None:
        if p[3] != 0x43:
            t = struct.unpack_from("<I", p, 0)[0]
            if t == PROTOCOL_ERROR and len(p) >= 24:
                self.al_recibir(m.Mensaje(m.NAK, 0, b"", struct.unpack_from("<I", p, 16)[0]))
            return
        tipo, f = p[0], struct.unpack_from("<H", p, 1)[0]
        datos = p[16:]
        if self.fn_en_datos and len(datos) >= 2:
            datos = datos[2:]
        self.contadores["recibidos"] += 1
        if tipo == m.RSP:
            err = struct.unpack_from("<H", datos, 0)[0] if len(datos) >= 2 else 0
            self.al_recibir(m.Mensaje(tipo, f, datos[2:], err))
        else:
            self.al_recibir(m.Mensaje(tipo, f, datos))
