"""El eco del medidor del enlace (tools/vps/ba-enlace-eco.py), del lado de Python.

Corre en el VPS del túnel con la biblioteca estándar; el medidor del gateway
(src/gateway/net/Enlace.hpp) habla el MISMO formato («BAE1»). Acá, con el eco en
127.0.0.1 y un cliente mínimo: hola, ping, que solo conteste a la red pedida, el
informe de una subida (con el jitter de RFC 3550) y una bajada a ritmo con cuántos
mandó. La interoperabilidad con el medidor de C++ la prueban test_enlace y test_ipstack.
"""

import pathlib
import socket
import struct
import subprocess
import sys
import time

import pytest

ECO = pathlib.Path(__file__).resolve().parent.parent / "tools" / "vps" / "ba-enlace-eco.py"
MAGIA = b"BAE1"


def _puerto_libre():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def _arrancar(red="127.0.0.0/8"):
    puerto = _puerto_libre()
    proc = subprocess.Popen(
        [sys.executable, "-I", str(ECO), "--direccion", "127.0.0.1", "--puerto", str(puerto), "--red", red,
         "--durante-s", "30", "--max-kbps", "50000"],
        stderr=subprocess.PIPE,
    )
    # Espera a que escuche (lo dice por stderr).
    proc.stderr.readline()
    c = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    c.settimeout(1.0)
    c.connect(("127.0.0.1", puerto))
    return proc, c


@pytest.fixture
def eco():
    proc, c = _arrancar()
    yield c
    c.close()
    proc.terminate()
    proc.wait(5)


def _pedir(c, datos, tipo_respuesta):
    c.send(datos)
    hasta = time.monotonic() + 2
    while time.monotonic() < hasta:
        r = c.recv(2048)
        if r[:4] == MAGIA and r[4] == tipo_respuesta:
            return r
    raise AssertionError("no contestó")


def test_hola_y_ping(eco):
    r = _pedir(eco, MAGIA + bytes([1]), 2)
    version, kbps, ms = struct.unpack_from("!HII", r, 5)
    assert version == 1 and kbps == 50000 and ms == 30000
    ping = MAGIA + struct.pack("!BIIQ", 3, 0, 7, 123456789)
    r = _pedir(eco, ping, 4)
    assert r[5:] == ping[5:]


def test_solo_contesta_a_su_red():
    proc, c = _arrancar(red="10.99.0.0/24")
    try:
        c.send(MAGIA + bytes([1]))
        with pytest.raises(socket.timeout):
            c.recv(2048)
    finally:
        c.close()
        proc.terminate()
        proc.wait(5)


def test_subida_cuenta_y_jitter(eco):
    prueba = 4242
    t0 = time.monotonic_ns() // 1000
    for i in range(200):
        # Tránsito parejo: la hora de envío avanza igual que la llegada (jitter ≈ 0).
        eco.send(MAGIA + struct.pack("!BIIQ", 5, prueba, i, t0 + i * 1000) + b"\0" * 1179)
        time.sleep(0.001)
    time.sleep(0.2)
    r = _pedir(eco, MAGIA + struct.pack("!BI", 6, prueba), 7)
    (p, recibidos, enviados, seq_max, jitter, desordenados, bytes_, primero, ultimo) = struct.unpack_from("!IIIIIIQQQ", r, 5)
    assert p == prueba and recibidos == 200 and seq_max == 199 and desordenados == 0
    assert bytes_ == 200 * 1200 and ultimo > primero and enviados == 0
    assert jitter < 5000  # µs: con sleep de 1 ms el tránsito varía poco


def test_bajada_a_ritmo(eco):
    prueba = 99
    eco.send(MAGIA + struct.pack("!BIIIH", 8, prueba, 2000, 1000, 1200))
    llegaron = 0
    ok = None
    primero = ultimo = None
    hasta = time.monotonic() + 2.5
    while time.monotonic() < hasta:
        try:
            r = eco.recv(2048)
        except socket.timeout:
            break
        if r[4] == 9:
            ok = struct.unpack_from("!III", r, 5)
        elif r[4] == 5:
            llegaron += 1
            ahora = time.monotonic()
            primero = primero or ahora
            ultimo = ahora
    assert ok == (prueba, 2000, 1000)
    # 2 Mbps con paquetes de 1200 (+28): ~203 por segundo durante 1 s.
    assert 150 <= llegaron <= 260
    assert ultimo - primero > 0.7  # a ritmo, no de golpe
    r = _pedir(eco, MAGIA + struct.pack("!BI", 6, prueba), 7)
    enviados = struct.unpack_from("!IIIIIIQQQ", r, 5)[2]
    assert enviados == llegaron  # por loopback no se pierde nada
