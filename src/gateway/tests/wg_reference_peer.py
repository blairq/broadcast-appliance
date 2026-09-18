#!/usr/bin/env python3
"""Par WireGuard de REFERENCIA para los tests de interoperabilidad (no se
distribuye). Implementacion independiente de la de C++ (src/gateway/net),
escrita directo del whitepaper de WireGuard (§5.4) sobre primitivas de
terceros: hashlib.blake2s (CPython) y `cryptography` (OpenSSL) para X25519 y
ChaCha20-Poly1305. Si las dos implementaciones se entienden, el protocolo esta
bien implementado (y no solo "consistente consigo mismo").

Uso:
  wg_reference_peer.py respond  <puerto> <priv_b64> <peer_pub_b64> <psk_b64> <segundos>
  wg_reference_peer.py initiate <puerto_destino> <priv_b64> <peer_pub_b64> <psk_b64> <segundos>

respond:  espera la iniciacion, contesta, y devuelve cada paquete IPv4/UDP que
          recibe con origen/destino (IP y puerto) intercambiados. Sale con 0.
initiate: inicia el handshake, manda un paquete IPv4/UDP con "ping-de-python"
          y espera el eco (IP/puertos intercambiados). Sale 0 si llego bien.
"""
import base64
import hashlib
import hmac
import os
import socket
import struct
import sys
import time

from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey, X25519PublicKey
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat

CONSTRUCTION = b"Noise_IKpsk2_25519_ChaChaPoly_BLAKE2s"
IDENTIFIER = b"WireGuard v1 zx2c4 Jason@zx2c4.com"
LABEL_MAC1 = b"mac1----"


def HASH(x):
    return hashlib.blake2s(x).digest()


def MAC(k, x):
    return hashlib.blake2s(x, digest_size=16, key=k).digest()


def HMAC(k, x):
    return hmac.new(k, x, hashlib.blake2s).digest()


def KDF(n, key, inp):
    t0 = HMAC(key, inp)
    out, prev = [], b""
    for i in range(1, n + 1):
        prev = HMAC(t0, prev + bytes([i]))
        out.append(prev)
    return out


def nonce(ctr):
    return b"\0" * 4 + struct.pack("<Q", ctr)


def AEAD(k, ctr, pt, ad):
    return ChaCha20Poly1305(k).encrypt(nonce(ctr), pt, ad)


def DEAD(k, ctr, ct, ad):
    return ChaCha20Poly1305(k).decrypt(nonce(ctr), ct, ad)


def DH(priv, pub):
    return X25519PrivateKey.from_private_bytes(priv).exchange(X25519PublicKey.from_public_bytes(pub))


def PUB(priv):
    return X25519PrivateKey.from_private_bytes(priv).public_key().public_bytes(Encoding.Raw, PublicFormat.Raw)


def GEN():
    k = X25519PrivateKey.generate()
    from cryptography.hazmat.primitives.serialization import PrivateFormat, NoEncryption

    priv = k.private_bytes(Encoding.Raw, PrivateFormat.Raw, NoEncryption())
    return priv, PUB(priv)


def TAI64N():
    t = time.time()
    return struct.pack(">QI", 0x400000000000000A + int(t), int((t % 1) * 1e9))


def ip_checksum(h):
    s = 0
    for i in range(0, len(h), 2):
        s += (h[i] << 8) | h[i + 1]
    while s >> 16:
        s = (s & 0xFFFF) + (s >> 16)
    return (~s) & 0xFFFF


def udp_packet(src, dst, sport, dport, payload):
    udp = struct.pack(">HHHH", sport, dport, 8 + len(payload), 0) + payload
    hdr = bytearray(struct.pack(">BBHHHBBH4s4s", 0x45, 0, 20 + len(udp), 0, 0, 64, 17, 0, src, dst))
    struct.pack_into(">H", hdr, 10, ip_checksum(bytes(hdr)))
    return bytes(hdr) + udp


def echo_of(pkt):
    """Mismo paquete IPv4/UDP con IPs y puertos intercambiados."""
    total = struct.unpack(">H", pkt[2:4])[0]
    pkt = pkt[:total]
    src, dst = pkt[12:16], pkt[16:20]
    sport, dport = struct.unpack(">HH", pkt[20:24])
    return udp_packet(dst, src, dport, sport, pkt[28:])


class Session:
    def __init__(self, send_key, recv_key, remote_index, local_index):
        self.send_key, self.recv_key = send_key, recv_key
        self.remote_index, self.local_index = remote_index, local_index
        self.counter = 0

    def encrypt(self, pkt):
        pad = (-len(pkt)) % 16
        msg = struct.pack("<BxxxIQ", 4, self.remote_index, self.counter) + AEAD(self.send_key, self.counter,
                                                                               pkt + b"\0" * pad, b"")
        self.counter += 1
        return msg

    def decrypt(self, msg):
        idx, ctr = struct.unpack("<IQ", msg[4:16])
        assert idx == self.local_index, "indice receptor equivocado"
        return DEAD(self.recv_key, ctr, msg[16:], b"")


def respond(port, priv, peer_pub, psk, secs):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", port))
    s.settimeout(secs)
    our_pub = PUB(priv)
    sess = None
    deadline = time.time() + secs
    echoed = 0
    while time.time() < deadline:
        try:
            d, addr = s.recvfrom(65536)
        except socket.timeout:
            break
        if d[0] == 1 and len(d) == 148:
            assert MAC(HASH(LABEL_MAC1 + our_pub), d[:116]) == d[116:132], "MAC1 invalido"
            C = HASH(CONSTRUCTION)
            H = HASH(C + IDENTIFIER)
            H = HASH(H + our_pub)
            sender = struct.unpack("<I", d[4:8])[0]
            Ei = d[8:40]
            (C,) = KDF(1, C, Ei)
            H = HASH(H + Ei)
            C, k = KDF(2, C, DH(priv, Ei))
            spub = DEAD(k, 0, d[40:88], H)
            assert spub == peer_pub, "estatica del iniciador inesperada"
            H = HASH(H + d[40:88])
            C, k = KDF(2, C, DH(priv, spub))
            DEAD(k, 0, d[88:116], H)  # timestamp
            H = HASH(H + d[88:116])
            er_priv, er_pub = GEN()
            local = struct.unpack("<I", os.urandom(4))[0] or 1
            (C,) = KDF(1, C, er_pub)
            H = HASH(H + er_pub)
            (C,) = KDF(1, C, DH(er_priv, Ei))
            (C,) = KDF(1, C, DH(er_priv, spub))
            C, tau, k = KDF(3, C, psk)
            H = HASH(H + tau)
            empty = AEAD(k, 0, b"", H)
            msg = struct.pack("<BxxxII", 2, local, sender) + er_pub + empty
            msg += MAC(HASH(LABEL_MAC1 + peer_pub), msg) + b"\0" * 16
            recv_key, send_key = KDF(2, C, b"")
            sess = Session(send_key, recv_key, sender, local)
            s.sendto(msg, addr)
        elif d[0] == 4 and sess is not None:
            pkt = sess.decrypt(d)
            if len(pkt) >= 28 and pkt[0] >> 4 == 4:
                s.sendto(sess.encrypt(echo_of(pkt)), addr)
                echoed += 1
    print(f"respond: {echoed} paquetes devueltos")
    return 0 if echoed > 0 else 1


def initiate(port, priv, peer_pub, psk, secs):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0))
    s.settimeout(secs)
    our_pub = PUB(priv)
    C = HASH(CONSTRUCTION)
    H = HASH(C + IDENTIFIER)
    H = HASH(H + peer_pub)
    e_priv, e_pub = GEN()
    local = struct.unpack("<I", os.urandom(4))[0] or 1
    (C,) = KDF(1, C, e_pub)
    H = HASH(H + e_pub)
    C, k = KDF(2, C, DH(e_priv, peer_pub))
    enc_static = AEAD(k, 0, our_pub, H)
    H = HASH(H + enc_static)
    C, k = KDF(2, C, DH(priv, peer_pub))
    enc_ts = AEAD(k, 0, TAI64N(), H)
    H = HASH(H + enc_ts)
    msg = struct.pack("<BxxxI", 1, local) + e_pub + enc_static + enc_ts
    msg += MAC(HASH(LABEL_MAC1 + peer_pub), msg) + b"\0" * 16
    s.sendto(msg, ("127.0.0.1", port))
    d, addr = s.recvfrom(65536)
    assert d[0] == 2 and len(d) == 92, "respuesta invalida"
    assert MAC(HASH(LABEL_MAC1 + our_pub), d[:60]) == d[60:76], "MAC1 de la respuesta invalido"
    sender, receiver = struct.unpack("<II", d[4:12])
    assert receiver == local
    Er = d[12:44]
    (C,) = KDF(1, C, Er)
    H = HASH(H + Er)
    (C,) = KDF(1, C, DH(e_priv, Er))
    (C,) = KDF(1, C, DH(priv, Er))
    C, tau, k = KDF(3, C, psk)
    H = HASH(H + tau)
    DEAD(k, 0, d[44:60], H)
    send_key, recv_key = KDF(2, C, b"")
    sess = Session(send_key, recv_key, sender, local)
    ping = udp_packet(bytes([10, 99, 0, 2]), bytes([10, 99, 0, 1]), 5000, 7, b"ping-de-python")
    s.sendto(sess.encrypt(ping), addr)
    deadline = time.time() + secs
    while time.time() < deadline:
        d, _ = s.recvfrom(65536)
        if d[0] != 4:
            continue
        pkt = sess.decrypt(d)
        if not pkt:
            continue  # keepalive
        total = struct.unpack(">H", pkt[2:4])[0]
        pkt = pkt[:total]
        ok = pkt[12:16] == bytes([10, 99, 0, 1]) and pkt[16:20] == bytes([10, 99, 0, 2]) and pkt[28:] == b"ping-de-python"
        print("initiate: eco", "correcto" if ok else "INCORRECTO")
        return 0 if ok else 1
    return 1


if __name__ == "__main__":
    mode = sys.argv[1]
    port = int(sys.argv[2])
    priv, peer_pub, psk = (base64.b64decode(x) for x in sys.argv[3:6])
    secs = float(sys.argv[6])
    sys.exit(respond(port, priv, peer_pub, psk, secs) if mode == "respond" else initiate(port, priv, peer_pub, psk, secs))
