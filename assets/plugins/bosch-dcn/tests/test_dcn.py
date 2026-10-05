"""Pruebas del plugin bosch-dcn sin el equipo: python3 -m unittest discover -s tests (desde la carpeta del plugin)."""
import socket
import sys
import threading
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from dcn import mensajes as m  # noqa: E402
from dcn.ccu import Ccu, ErrorCcu  # noqa: E402
from dcn.simulador import Simulador  # noqa: E402
from dcn.transporte import FlujoSocket, SerieFull, Tcp, checksum  # noqa: E402


def esperar(cond, seg=3.0):
    fin = time.monotonic() + seg
    while time.monotonic() < fin:
        if cond():
            return True
        time.sleep(0.01)
    return False


class FlujoConPerdidas(FlujoSocket):
    """Tira los primeros `n` paquetes que escribe (para probar los reenvíos)."""

    def __init__(self, s, tirar=1):
        super().__init__(s)
        self.tirar = tirar

    def escribir(self, b):
        if self.tirar > 0 and b[1:2] == b"$":
            self.tirar -= 1
            return len(b)
        return super().escribir(b)


class Mensajes(unittest.TestCase):
    def test_ida_y_vuelta(self):
        for msg in (m.Mensaje(m.REQ, m.MM_C_SET_MICRO_ON_OFF, m.micro_onoff(12, True)),
                    m.Mensaje(m.RSP, m.MM_C_RTS_GET, m.lista_rts([(3, 3), (9, 9)]), 0),
                    m.Mensaje(m.RSP, m.MM_C_SHIFT, b"", 26)):
            v = m.desempaquetar(m.empaquetar(msg))
            self.assertEqual((v.tipo, v.fn, v.datos, v.error), (msg.tipo, msg.fn, msg.datos, msg.error))

    def test_codigos(self):
        self.assertEqual(m.MM_C_SET_MICRO_ON_OFF, 0x0022)
        self.assertEqual(m.MM_C_START_MM, 0x001E)
        self.assertEqual(m.MM_C_RTS_APPEND, 0x003E)
        self.assertEqual(m.empaquetar(m.Mensaje(m.REQ, 0x0022, b""))[3:5], b"\x22\x00")  # n primero, APP después

    def test_estructuras(self):
        self.assertEqual(m.leer_lista_spk(m.lista_spk([(1, True), (7, False)])), [(1, True), (7, False)])
        a = {"modo": 0, "max_abiertos": 4, "max_pedidos": 50}
        self.assertEqual(m.leer_ajustes(m.ajustes(a))["max_pedidos"], 50)
        self.assertEqual(m.leer_config(m.config([(1, 0x10), (2, 0)]))[1], [(1, 0x10), (2, 0)])


class Serie(unittest.TestCase):
    def test_checksum_nunca_vale_escape(self):
        for esc in (0xC8, 0x1B):
            for s in range(0, 2048):
                corr, chk = checksum(bytes([s & 0xFF, s >> 8]), esc)
                self.assertNotEqual(chk, esc)
                self.assertIn(corr, (0, 0x13))

    def test_relleno_de_escape(self):
        t = SerieFull(None, escape=0x1B)
        p = t.armar(ord("$"), 0x1B, bytes([3, 2, 0, 0x1B, 0x1B]))
        self.assertIn(bytes([0x1B, 0x1B]), p[2:])  # la secuencia y los datos con ESC van duplicados
        self.assertNotEqual(p[-1], 0x1B)

    def _par(self, escape=0xC8, tirar=0):
        a, b = socket.socketpair()
        ta = SerieFull(FlujoConPerdidas(a, tirar), escape=escape, espera_ack_s=0.3)
        tb = SerieFull(FlujoSocket(b), escape=escape, espera_ack_s=0.3)
        recibidos = []
        tb.al_recibir = recibidos.append
        ta.arrancar()
        tb.arrancar()
        self.addCleanup(ta.parar)
        self.addCleanup(tb.parar)
        return ta, tb, recibidos

    def test_envio_con_ack(self):
        for esc in (0xC8, 0x1B):
            ta, _tb, rx = self._par(esc)
            f = ta.enviar(m.Mensaje(m.REQ, m.MM_C_SET_MICRO_ON_OFF, m.micro_onoff(0x1B, True)))
            self.assertTrue(f.result(timeout=2))
            self.assertTrue(esperar(lambda: rx))
            self.assertEqual(rx[0].datos, m.micro_onoff(0x1B, True))

    def test_reenvio_si_no_llega(self):
        ta, _tb, rx = self._par(tirar=1)
        ta.enviar(m.Mensaje(m.REQ, m.MM_C_SHIFT)).result(timeout=3)
        self.assertEqual(ta.contadores["reenvios"], 1)
        self.assertTrue(esperar(lambda: len(rx) == 1))

    def test_no_confirma_falla_sin_bloquear(self):
        ta, _tb, _rx = self._par(tirar=99)
        t0 = time.monotonic()
        f = ta.enviar(m.Mensaje(m.REQ, m.MM_C_SHIFT))
        self.assertLess(time.monotonic() - t0, 0.05)  # enviar() no espera
        with self.assertRaises(Exception):
            f.result(timeout=3)

    def test_checksum_mal_pide_nak(self):
        a, b = socket.socketpair()
        tb = SerieFull(FlujoSocket(b))
        tb.arrancar()
        self.addCleanup(tb.parar)
        p = bytearray(SerieFull(None).armar(ord("$"), 0, m.empaquetar(m.Mensaje(m.REQ, m.MM_C_SHIFT))))
        p[-1] ^= 0xFF
        a.sendall(bytes(p))
        a.settimeout(1)
        self.assertEqual(a.recv(16)[1:2], b"#")
        self.assertEqual(tb.contadores["checksum_mal"], 1)


class ConSimulador(unittest.TestCase):
    def setUp(self):
        a, b = socket.socketpair()
        self.sim = Simulador(SerieFull(FlujoSocket(b)), unidades=8)
        self.sim.arrancar()
        t = SerieFull(FlujoSocket(a))
        self.ccu = Ccu(t, solo_escuchar=False)
        self.cambios = []
        self.ccu.al_cambio = lambda que, d: self.cambios.append((que, d))
        t.arrancar()
        self.addCleanup(t.parar)
        self.addCleanup(self.sim.parar)
        self.ccu.sincronizar().result(timeout=3)

    def test_arranque(self):
        e = self.ccu.estado
        self.assertTrue(e.sincronizado)
        self.assertEqual(len(e.unidades), 8)
        self.assertTrue(e.es_presidente(1))
        self.assertEqual(e.ajustes["max_abiertos"], 4)
        self.assertIn("SIM", e.version)

    def test_microfonos_y_maximo(self):
        for u in (2, 3, 4, 5):
            self.ccu.microfono(u, True).result(timeout=2)
        with self.assertRaises(ErrorCcu) as ctx:
            self.ccu.microfono(6, True).result(timeout=2)
        self.assertEqual(ctx.exception.codigo, 34)
        self.assertIn("máximo", str(ctx.exception))
        self.ccu.microfono(1, True).result(timeout=2)  # el presidente no cuenta
        self.assertEqual(sorted(self.ccu.estado.abiertos()), [1, 2, 3, 4, 5])
        cerrados = self.ccu.cerrar_todos().result(timeout=3)
        self.assertEqual(sorted(cerrados), [2, 3, 4, 5])
        self.assertEqual(self.ccu.estado.abiertos(), [1])

    def test_pedidos_desde_la_sala_y_siguiente(self):
        self.sim.apretar(5)
        self.sim.apretar(7)
        self.assertTrue(esperar(lambda: [p[0] for p in self.ccu.estado.pedidos] == [5, 7]))
        self.assertIn(("pedidos", {"primero": 5}), self.cambios)
        self.ccu.pedido_antes_de(7, None).result(timeout=2)
        self.assertEqual([p[0] for p in self.ccu.estado.pedidos], [7, 5])
        self.ccu.siguiente().result(timeout=2)
        self.assertEqual(self.ccu.estado.abiertos(), [7])
        self.assertEqual([p[0] for p in self.ccu.estado.pedidos], [5])
        self.assertTrue(any(q == "microfono" and d.get("unidad") == 7 and d["abierto"] for q, d in self.cambios))

    def test_modo_y_avisos_de_afuera(self):
        self.ccu.modo("voz", max_abiertos=2).result(timeout=2)
        self.assertEqual(self.ccu.estado.ajustes["modo"], 3)
        self.sim.apretar(4)  # en modo voz el micrófono se abre directo
        self.assertTrue(esperar(lambda: self.ccu.estado.abiertos() == [4]))

    def test_desconexion_de_unidad(self):
        self.sim.desconectar(3)
        self.assertTrue(esperar(lambda: not self.ccu.estado.unidades[3]["conectada"]))
        with self.assertRaises(ErrorCcu):
            self.ccu.microfono(3, True).result(timeout=2)

    def test_solo_escuchar(self):
        self.ccu.solo_escuchar = True
        with self.assertRaises(ErrorCcu) as ctx:
            self.ccu.microfono(2, True).result(timeout=1)
        self.assertIn("solo escuchar", str(ctx.exception))

    def test_pedidos_en_paralelo_no_se_mezclan(self):
        futs = [self.ccu.microfono(u, True) for u in (2, 3)] + [self.ccu.refrescar_listas()]
        for f in futs:
            f.result(timeout=3)
        self.assertEqual(sorted(self.ccu.estado.abiertos()), [2, 3])


class PorTcp(unittest.TestCase):
    def test_tcp_con_simulador(self):
        a, b = socket.socketpair()
        sim = Simulador(Tcp(FlujoSocket(b)), unidades=4)
        sim.arrancar()
        t = Tcp(FlujoSocket(a))
        ccu = Ccu(t, solo_escuchar=False)
        t.arrancar()
        self.addCleanup(t.parar)
        self.addCleanup(sim.parar)
        ccu.sincronizar().result(timeout=3)
        ccu.microfono(2, True).result(timeout=2)
        self.assertEqual(ccu.estado.abiertos(), [2])

    def test_caida(self):
        a, b = socket.socketpair()
        t = SerieFull(FlujoSocket(a))
        ccu = Ccu(t, solo_escuchar=False)
        cayo = threading.Event()
        t.al_caer = lambda motivo: (ccu.fallar_todo(motivo), cayo.set())
        t.arrancar()
        f = ccu.pedir(m.MM_C_SHIFT)
        b.close()
        self.assertTrue(cayo.wait(2))
        with self.assertRaises(Exception):
            f.result(timeout=1)


if __name__ == "__main__":
    unittest.main()
