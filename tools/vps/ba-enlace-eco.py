#!/usr/bin/env python3
# ============================================================================
# tools/vps/ba-enlace-eco.py — El eco del medidor del enlace (corre en el VPS del túnel)
#
# Del otro lado del túnel WireGuard (el servidor con IP pública), en la IP del túnel
# del servidor (10.99.0.1) y el puerto UDP 51821: el gateway del lugar le mide por el
# camino real de los invitados la subida, la bajada, el RTT en reposo y con carga, el
# jitter y la pérdida (src/gateway/net/Enlace.hpp: el MISMO formato, «BAE1»).
#
# Solo la biblioteca estándar de Python 3 (viene en cualquier Debian/Ubuntu). Contesta
# solo a las direcciones del túnel (--red) y con topes (--max-kbps, --max-ms): no es un
# servicio público. Instalación y actualización: docs/funciones/ENLACE.md (la app la
# arma lista para pegar: página Sala → Enlace → «Instalar el eco en el servidor…»).
#
#   python3 ba-enlace-eco.py --direccion 10.99.0.1 --puerto 51821 --red 10.99.0.0/24
# ============================================================================
import argparse
import ipaddress
import select
import socket
import struct
import sys
import time

VERSION = 1
MAGIA = b"BAE1"
HOLA, HOLA_OK, PING, PONG, DATO, INFORME_PEDIDO, INFORME, BAJADA_PEDIDO, BAJADA_OK, PARAR = range(1, 11)
CAB_IP_UDP = 28
MAX_BAJADAS = 2
VIDA_PRUEBA_S = 120


def ahora_us():
    return time.monotonic_ns() // 1000


class Recepcion:
    """Lo que llegó de una prueba de subida (RFC 3550 para el jitter)."""

    __slots__ = ("recibidos", "bytes", "seq_max", "jitter", "desordenados", "primero", "ultimo", "transito", "visto")

    def __init__(self):
        self.recibidos = 0
        self.bytes = 0
        self.seq_max = 0
        self.jitter = 0.0
        self.desordenados = 0
        self.primero = 0
        self.ultimo = 0
        self.transito = None
        self.visto = time.monotonic()

    def llegada(self, seq, envio_us, llegada_us, n):
        if self.recibidos == 0:
            self.primero = llegada_us
            self.seq_max = seq
        elif seq < self.seq_max:
            self.desordenados += 1
        else:
            self.seq_max = seq
        self.recibidos += 1
        self.bytes += n
        self.ultimo = max(self.ultimo, llegada_us)
        transito = llegada_us - envio_us
        if self.transito is not None:
            self.jitter += (abs(transito - self.transito) - self.jitter) / 16.0
        self.transito = transito
        self.visto = time.monotonic()


class Bajada:
    __slots__ = ("addr", "prueba", "pps", "tamano", "t0", "fin", "enviados")

    def __init__(self, addr, prueba, kbps, ms, tamano):
        self.addr = addr
        self.prueba = prueba
        self.tamano = tamano
        self.pps = kbps * 1000.0 / ((tamano + CAB_IP_UDP) * 8.0)
        self.t0 = ahora_us()
        self.fin = self.t0 + ms * 1000
        self.enviados = 0


class Eco:
    def __init__(self, sock, red, max_kbps, max_ms):
        self.sock = sock
        self.red = red
        self.max_kbps = max_kbps
        self.max_ms = max_ms
        self.recepciones = {}  # (addr, prueba) -> Recepcion
        self.bajadas = {}  # (addr, prueba) -> Bajada
        self.enviados = {}  # (addr, prueba) -> (enviados, cuándo terminó)
        self.ultima_limpieza = time.monotonic()

    def permitido(self, addr):
        try:
            return ipaddress.ip_address(addr[0]) in self.red
        except ValueError:
            return False

    def mandar(self, datos, addr):
        try:
            self.sock.sendto(datos, addr)
            return True
        except (BlockingIOError, OSError):
            return False

    def atender(self, d, addr, llegada):
        if len(d) < 5 or d[:4] != MAGIA or not self.permitido(addr):
            return
        tipo = d[4]
        if tipo == HOLA:
            self.mandar(MAGIA + struct.pack("!BHII", HOLA_OK, VERSION, self.max_kbps, self.max_ms), addr)
        elif tipo == PING and len(d) >= 21:
            self.mandar(MAGIA + bytes([PONG]) + d[5:21], addr)
        elif tipo == DATO and len(d) >= 21:
            prueba, seq, t = struct.unpack_from("!IIQ", d, 5)
            clave = (addr, prueba)
            r = self.recepciones.get(clave)
            if r is None:
                if len(self.recepciones) > 1000:
                    return
                r = self.recepciones[clave] = Recepcion()
            r.llegada(seq, t, llegada, len(d))
        elif tipo == INFORME_PEDIDO and len(d) >= 9:
            (prueba,) = struct.unpack_from("!I", d, 5)
            clave = (addr, prueba)
            r = self.recepciones.get(clave) or Recepcion()
            enviados = self.enviados.get(clave, (0, 0))[0]
            if clave in self.bajadas:
                enviados = self.bajadas[clave].enviados
            cuerpo = struct.pack(
                "!IIIIIIQQQ",
                prueba,
                r.recibidos,
                enviados,
                r.seq_max,
                min(int(r.jitter), 0xFFFFFFFF),
                r.desordenados,
                r.bytes,
                r.primero,
                r.ultimo,
            )
            self.mandar(MAGIA + bytes([INFORME]) + cuerpo, addr)
        elif tipo == BAJADA_PEDIDO and len(d) >= 19:
            prueba, kbps, ms, tamano = struct.unpack_from("!IIIH", d, 5)
            kbps = max(1, min(kbps, self.max_kbps))
            ms = max(100, min(ms, self.max_ms))
            tamano = max(21, min(tamano, 1392))
            clave = (addr, prueba)
            if clave not in self.bajadas and clave not in self.enviados:
                if len(self.bajadas) >= MAX_BAJADAS:
                    return  # ocupado: el medidor reintenta y después avisa
                self.bajadas[clave] = Bajada(addr, prueba, kbps, ms, tamano)
            self.mandar(MAGIA + struct.pack("!BIII", BAJADA_OK, prueba, kbps, ms), addr)
        elif tipo == PARAR and len(d) >= 9:
            (prueba,) = struct.unpack_from("!I", d, 5)
            self.terminar((addr, prueba))

    def terminar(self, clave):
        b = self.bajadas.pop(clave, None)
        if b is not None:
            self.enviados[clave] = (b.enviados, time.monotonic())

    def empujar(self):
        """Manda lo que toca de cada bajada (a ritmo)."""
        ahora = ahora_us()
        for clave, b in list(self.bajadas.items()):
            debidos = int((min(ahora, b.fin) - b.t0) * b.pps / 1e6) + 1
            tope = b.enviados + 2000
            relleno = b"\0" * (b.tamano - 21)
            while b.enviados < debidos and b.enviados < tope:
                cab = MAGIA + struct.pack("!BIIQ", DATO, b.prueba, b.enviados, ahora_us())
                if not self.mandar(cab + relleno, b.addr):
                    break  # el buffer del socket está lleno: lo que falta, después
                b.enviados += 1
            if ahora >= b.fin:
                self.terminar(clave)

    def limpiar(self):
        ahora = time.monotonic()
        if ahora - self.ultima_limpieza < 10:
            return
        self.ultima_limpieza = ahora
        self.recepciones = {k: r for k, r in self.recepciones.items() if ahora - r.visto < VIDA_PRUEBA_S}
        self.enviados = {k: v for k, v in self.enviados.items() if ahora - v[1] < VIDA_PRUEBA_S}

    def correr(self, hasta=None):
        while hasta is None or time.monotonic() < hasta:
            espera = 0.001 if self.bajadas else 1.0
            listos, _, _ = select.select([self.sock], [], [], espera)
            if listos:
                for _ in range(4096):
                    try:
                        d, addr = self.sock.recvfrom(2048)
                    except (BlockingIOError, InterruptedError):
                        break
                    self.atender(d, addr, ahora_us())
            if self.bajadas:
                self.empujar()
            self.limpiar()


def abrir(direccion, puerto, esperar_s):
    """El socket en la IP del túnel; si wg0 todavía no está, reintenta."""
    limite = time.monotonic() + esperar_s
    while True:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
            s.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 4 * 1024 * 1024)
            s.bind((direccion, puerto))
            s.setblocking(False)
            return s
        except OSError as e:
            s.close()
            if time.monotonic() > limite:
                raise
            print(f"ba-enlace-eco: todavía no se puede escuchar en {direccion}:{puerto} ({e}); reintento", file=sys.stderr)
            time.sleep(2)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Eco del medidor del enlace de Broadcast Appliance (UDP).")
    ap.add_argument("--direccion", default="10.99.0.1", help="IP del túnel de este servidor")
    ap.add_argument("--puerto", type=int, default=51821)
    ap.add_argument("--red", default="10.99.0.0/24", help="a quién se le contesta (la red del túnel)")
    ap.add_argument("--max-kbps", type=int, default=200000)
    ap.add_argument("--max-ms", type=int, default=30000)
    ap.add_argument("--esperar-s", type=float, default=600, help="cuánto reintentar si la IP todavía no existe")
    ap.add_argument("--durante-s", type=float, default=0, help="(pruebas) terminar a los N s")
    a = ap.parse_args(argv)
    sock = abrir(a.direccion, a.puerto, a.esperar_s)
    print(f"ba-enlace-eco {VERSION}: escuchando en {a.direccion}:{sock.getsockname()[1]} (red {a.red})", file=sys.stderr, flush=True)
    eco = Eco(sock, ipaddress.ip_network(a.red, strict=False), a.max_kbps, a.max_ms)
    try:
        eco.correr(time.monotonic() + a.durante_s if a.durante_s > 0 else None)
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
