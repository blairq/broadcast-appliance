#!/usr/bin/env python3
"""Un servidor RTMP local mínimo, para el banco de caos (F1-14): nada del sistema.

Acepta que alguien PUBLIQUE (rtmp2sink de la app: handshake, connect, createStream,
publish), guarda lo que llega como FLV (uno por sesión, si se pide) y anota cuándo
llegó cada cuadro de video: con eso el banco mide cuánto tardó la transmisión en
volver después de cada falla (RTO) y los huecos.

No reproduce ni reenvía nada: solo recibe. Solo la biblioteca estándar de Python.

    python3 -I tools/caos/rtmp_local.py [--puerto 1935] [--flv CARPETA]

Como módulo (lo usa tools/caos/banco.py):

    s = ServidorRtmp(puerto=0, carpeta_flv=None); s.iniciar()
    ... s.puerto, s.sesiones(), s.primer_video_desde(t), s.huecos(umbral_s) ...
    s.parar()
"""
from __future__ import annotations

import argparse
import os
import socket
import struct
import threading
import time

# --- AMF0 -----------------------------------------------------------------------------


class Indefinido:
    """El `undefined` de AMF0."""


def amf_leer(b: bytes, i: int):
    t = b[i]
    i += 1
    if t == 0:  # número
        return struct.unpack(">d", b[i:i + 8])[0], i + 8
    if t == 1:  # booleano
        return b[i] != 0, i + 1
    if t == 2:  # texto
        n = struct.unpack(">H", b[i:i + 2])[0]
        return b[i + 2:i + 2 + n].decode("utf-8", "replace"), i + 2 + n
    if t in (3, 8):  # objeto / arreglo ECMA
        if t == 8:
            i += 4
        o = {}
        while True:
            n = struct.unpack(">H", b[i:i + 2])[0]
            i += 2
            if n == 0 and b[i] == 9:
                return o, i + 1
            k = b[i:i + n].decode("utf-8", "replace")
            i += n
            o[k], i = amf_leer(b, i)
    if t in (5, 6):  # null / undefined
        return None, i
    if t == 10:  # arreglo
        n = struct.unpack(">I", b[i:i + 4])[0]
        i += 4
        a = []
        for _ in range(n):
            v, i = amf_leer(b, i)
            a.append(v)
        return a, i
    if t == 11:  # fecha
        return struct.unpack(">d", b[i:i + 8])[0], i + 10
    if t == 12:  # texto largo
        n = struct.unpack(">I", b[i:i + 4])[0]
        return b[i + 4:i + 4 + n].decode("utf-8", "replace"), i + 4 + n
    raise ValueError(f"AMF0: tipo {t} no soportado")


def amf_todos(b: bytes) -> list:
    out, i = [], 0
    while i < len(b):
        v, i = amf_leer(b, i)
        out.append(v)
    return out


def amf(v) -> bytes:
    if isinstance(v, Indefinido):
        return b"\x06"
    if v is None:
        return b"\x05"
    if isinstance(v, bool):
        return b"\x01" + (b"\x01" if v else b"\x00")
    if isinstance(v, (int, float)):
        return b"\x00" + struct.pack(">d", float(v))
    if isinstance(v, str):
        e = v.encode()
        return b"\x02" + struct.pack(">H", len(e)) + e
    if isinstance(v, dict):
        o = b"\x03"
        for k, x in v.items():
            e = k.encode()
            o += struct.pack(">H", len(e)) + e + amf(x)
        return o + b"\x00\x00\x09"
    raise TypeError(type(v))


# --- Una conexión ---------------------------------------------------------------------


class Sesion:
    """Lo que se anota de cada conexión (un publish): tiempos de pared (time.time())."""

    def __init__(self, n: int, desde: str):
        self.n = n
        self.desde = desde
        self.inicio = time.time()
        self.publica = None  # cuándo dijo publish
        self.nombre = ""
        self.fin = None
        self.videos = 0
        self.audios = 0
        self.claves = 0
        self.bytes = 0
        self.llegadas = []  # tiempo de pared de cada cuadro de video

    def json(self) -> dict:
        ll = self.llegadas
        return {"n": self.n, "desde": self.desde, "inicio": self.inicio, "publica": self.publica, "nombre": self.nombre,
                "fin": self.fin, "videos": self.videos, "audios": self.audios, "claves": self.claves, "bytes": self.bytes,
                "primer_video": ll[0] if ll else None, "ultimo_video": ll[-1] if ll else None}


class _Conexion(threading.Thread):
    def __init__(self, srv: "ServidorRtmp", s: socket.socket, sesion: Sesion):
        super().__init__(daemon=True)
        self.srv, self.s, self.ses = srv, s, sesion
        self.chunk_in = 128
        self.chunk_out = 128
        self.ventana = 0  # Window Acknowledgement Size que anunció el otro
        self.leidos = 0
        self.acusados = 0
        self.previos = {}  # csid -> {ts, largo, tipo, stream, delta, ext}
        self.armando = {}  # csid -> bytearray del mensaje a medias
        self.flv = None

    # --- E/S ---
    def _leer(self, n: int) -> bytes:
        b = bytearray()
        while len(b) < n:
            if self.srv._parar.is_set():
                raise ConnectionError("parar")
            try:
                x = self.s.recv(n - len(b))
            except socket.timeout:
                continue
            if not x:
                raise ConnectionError("cerrada")
            b += x
        self.leidos += n
        if self.ventana and self.leidos - self.acusados >= self.ventana:
            self.acusados = self.leidos
            self._mandar(2, 3, 0, struct.pack(">I", self.leidos & 0xFFFFFFFF))
        return bytes(b)

    def _mandar(self, csid: int, tipo: int, stream: int, cuerpo: bytes, ts: int = 0):
        cab = bytes([csid & 0x3F]) + struct.pack(">I", ts)[1:] + struct.pack(">I", len(cuerpo))[1:] + bytes([tipo]) + struct.pack("<I", stream)
        out = bytearray(cab)
        for k in range(0, len(cuerpo), self.chunk_out):
            if k:
                out += bytes([0xC0 | (csid & 0x3F)])
            out += cuerpo[k:k + self.chunk_out]
        self.s.sendall(bytes(out))

    def _comando(self, stream: int, *args):
        self._mandar(3 if stream == 0 else 5, 20, stream, b"".join(amf(a) for a in args))

    # --- RTMP ---
    def run(self):
        try:
            self._saludo()
            while not self.srv._parar.is_set():
                self._chunk()
        except (ConnectionError, OSError, ValueError, struct.error):
            pass
        finally:
            self.ses.fin = time.time()
            if self.flv:
                self.flv.close()
            try:
                self.s.close()
            except OSError:
                pass

    def _saludo(self):
        c0c1 = self._leer(1537)
        s1 = struct.pack(">I", 0) + b"\x00\x00\x00\x00" + os.urandom(1528)
        self.s.sendall(b"\x03" + s1 + c0c1[1:])
        self._leer(1536)  # C2

    def _chunk(self):
        b0 = self._leer(1)[0]
        fmt, csid = b0 >> 6, b0 & 0x3F
        if csid == 0:
            csid = 64 + self._leer(1)[0]
        elif csid == 1:
            x = self._leer(2)
            csid = 64 + x[0] + x[1] * 256
        p = self.previos.setdefault(csid, {"ts": 0, "largo": 0, "tipo": 0, "stream": 0, "delta": 0, "ext": False})
        nuevo = csid not in self.armando
        if fmt == 0:
            h = self._leer(11)
            t = int.from_bytes(h[0:3], "big")
            p["largo"], p["tipo"] = int.from_bytes(h[3:6], "big"), h[6]
            p["stream"] = struct.unpack("<I", h[7:11])[0]
            p["ext"] = t == 0xFFFFFF
            if p["ext"]:
                t = struct.unpack(">I", self._leer(4))[0]
            p["ts"], p["delta"] = t, 0
        elif fmt in (1, 2):
            h = self._leer(7 if fmt == 1 else 3)
            d = int.from_bytes(h[0:3], "big")
            if fmt == 1:
                p["largo"], p["tipo"] = int.from_bytes(h[3:6], "big"), h[6]
            p["ext"] = d == 0xFFFFFF
            if p["ext"]:
                d = struct.unpack(">I", self._leer(4))[0]
            p["delta"] = d
            p["ts"] = (p["ts"] + d) & 0xFFFFFFFF
        else:
            if p["ext"]:
                self._leer(4)
            if nuevo:
                p["ts"] = (p["ts"] + p["delta"]) & 0xFFFFFFFF
        m = self.armando.setdefault(csid, bytearray())
        m += self._leer(min(self.chunk_in, p["largo"] - len(m)))
        if len(m) >= p["largo"]:
            del self.armando[csid]
            self._mensaje(p["tipo"], p["stream"], p["ts"], bytes(m))

    def _mensaje(self, tipo: int, stream: int, ts: int, cuerpo: bytes):
        if tipo == 1:
            self.chunk_in = struct.unpack(">I", cuerpo[:4])[0] & 0x7FFFFFFF
        elif tipo == 5:
            self.ventana = struct.unpack(">I", cuerpo[:4])[0]
        elif tipo in (20, 17):
            self._orden(stream, amf_todos(cuerpo[1:] if tipo == 17 else cuerpo))
        elif tipo in (8, 9, 18):
            if tipo == 9:
                self.ses.llegadas.append(time.time())
                self.ses.videos += 1
                if cuerpo and (cuerpo[0] >> 4) == 1:
                    self.ses.claves += 1
            elif tipo == 8:
                self.ses.audios += 1
            self.ses.bytes += len(cuerpo)
            self._flv(tipo, ts, cuerpo)

    def _orden(self, stream: int, a: list):
        if not a or not isinstance(a[0], str):
            return
        nombre, txn = a[0], a[1] if len(a) > 1 else 0
        if nombre == "connect":
            self._mandar(2, 5, 0, struct.pack(">I", 2500000))
            self._mandar(2, 6, 0, struct.pack(">IB", 2500000, 2))
            self._mandar(2, 1, 0, struct.pack(">I", 4096))
            self.chunk_out = 4096
            self._comando(0, "_result", txn, {"fmsVer": "FMS/3,0,1,123", "capabilities": 31},
                          {"level": "status", "code": "NetConnection.Connect.Success", "description": "Conectado.",
                           "objectEncoding": 0})
        elif nombre == "createStream":
            self._comando(0, "_result", txn, None, 1)
        elif nombre == "publish":
            self.ses.publica = time.time()
            self.ses.nombre = a[3] if len(a) > 3 and isinstance(a[3], str) else ""
            self._mandar(2, 4, 0, struct.pack(">HI", 0, 1))  # Stream Begin 1
            self._comando(1, "onStatus", 0, None, {"level": "status", "code": "NetStream.Publish.Start",
                                                   "description": "Publicando.", "details": self.ses.nombre})
            if self.srv.carpeta_flv:
                ruta = os.path.join(self.srv.carpeta_flv, f"sesion-{self.ses.n:03d}.flv")
                self.flv = open(ruta, "wb")
                self.flv.write(b"FLV\x01\x05\x00\x00\x00\x09" + b"\x00\x00\x00\x00")
        elif nombre in ("deleteStream", "FCUnpublish", "closeStream"):
            self.ses.fin = time.time()
        # releaseStream / FCPublish: rtmp2sink no espera respuesta.

    def _flv(self, tipo: int, ts: int, cuerpo: bytes):
        if self.flv is None:
            return
        if tipo == 18:  # @setDataFrame onMetaData …: en el FLV va sin el @setDataFrame
            try:
                v, i = amf_leer(cuerpo, 0)
                if v == "@setDataFrame":
                    cuerpo = cuerpo[i:]
            except (ValueError, IndexError, struct.error):
                pass
        cab = bytes([tipo]) + struct.pack(">I", len(cuerpo))[1:] + struct.pack(">I", ts & 0xFFFFFF)[1:] + bytes([(ts >> 24) & 0xFF]) + b"\x00\x00\x00"
        self.flv.write(cab + cuerpo + struct.pack(">I", len(cab) + len(cuerpo)))


# --- El servidor ----------------------------------------------------------------------


class ServidorRtmp:
    def __init__(self, puerto: int = 0, carpeta_flv: str | None = None, escucha: str = "127.0.0.1"):
        self.escucha = escucha
        self.puerto = puerto
        self.carpeta_flv = carpeta_flv
        self._parar = threading.Event()
        self._mu = threading.Lock()
        self._sesiones: list[Sesion] = []
        self._hilo = None
        self._sock = None

    def iniciar(self):
        if self.carpeta_flv:
            os.makedirs(self.carpeta_flv, exist_ok=True)
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._sock.bind((self.escucha, self.puerto))
        self._sock.listen(8)
        self._sock.settimeout(0.3)
        self.puerto = self._sock.getsockname()[1]
        self._hilo = threading.Thread(target=self._aceptar, daemon=True)
        self._hilo.start()
        return self

    def _aceptar(self):
        while not self._parar.is_set():
            try:
                c, d = self._sock.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            c.settimeout(0.5)
            with self._mu:
                ses = Sesion(len(self._sesiones) + 1, f"{d[0]}:{d[1]}")
                self._sesiones.append(ses)
            _Conexion(self, c, ses).start()

    def parar(self):
        self._parar.set()
        if self._sock:
            self._sock.close()
        if self._hilo:
            self._hilo.join(2)

    # --- Lo que mide el banco ---
    def sesiones(self) -> list[dict]:
        with self._mu:
            return [s.json() for s in self._sesiones]

    def _llegadas(self) -> list[float]:
        with self._mu:
            todas = [t for s in self._sesiones for t in list(s.llegadas)]
        return sorted(todas)

    def ultimo_video(self) -> float | None:
        ll = self._llegadas()
        return ll[-1] if ll else None

    def primer_video_desde(self, t: float, sesion_nueva: bool = False) -> float | None:
        """El primer cuadro de video que llegó después de `t` (con `sesion_nueva`, solo de
        una conexión abierta después de `t`: la transmisión que volvió, no la vieja que
        seguía mandando)."""
        with self._mu:
            cand = [x for s in self._sesiones if not sesion_nueva or s.inicio >= t for x in list(s.llegadas) if x >= t]
        return min(cand) if cand else None

    def huecos(self, umbral_s: float = 0.5, desde: float = 0.0) -> list[dict]:
        """Los silencios de video más largos que `umbral_s` (entre sesiones también)."""
        ll = [x for x in self._llegadas() if x >= desde]
        return [{"desde": a, "hasta": b, "s": round(b - a, 3)} for a, b in zip(ll, ll[1:]) if b - a > umbral_s]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--puerto", type=int, default=1935)
    ap.add_argument("--flv", help="carpeta donde guardar lo recibido (un FLV por sesión)")
    a = ap.parse_args()
    s = ServidorRtmp(a.puerto, a.flv).iniciar()
    print(f"escuchando en rtmp://127.0.0.1:{s.puerto}/live (Ctrl-C para cortar)", flush=True)
    try:
        while True:
            time.sleep(5)
            for x in s.sesiones():
                print(f"  sesión {x['n']}: {x['videos']} cuadros, {x['bytes']} bytes{' (cerrada)' if x['fin'] else ''}", flush=True)
    except KeyboardInterrupt:
        pass
    finally:
        s.parar()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
