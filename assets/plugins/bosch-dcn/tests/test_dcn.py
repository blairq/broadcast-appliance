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
from dcn.transporte import FlujoSocket, SerieFull, SerieSimple, Tcp, checksum  # noqa: E402


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
        # El latido y los datos pueden salir en la misma escritura: se tira la que lleve un paquete de datos.
        if self.tirar > 0 and bytes([0xC8, ord("$")]) in b:
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


class Fase2(unittest.TestCase):
    def setUp(self):
        a, b = socket.socketpair()
        self.sim = Simulador(SerieFull(FlujoSocket(b)), unidades=6)
        self.sim.votar_solos = False
        self.sim.arrancar()
        t = SerieFull(FlujoSocket(a))
        self.ccu = Ccu(t, solo_escuchar=False, votacion=True, asistencia=True)
        self.cambios = []
        self.ccu.al_cambio = lambda que, d: self.cambios.append((que, d))
        t.arrancar()
        self.addCleanup(t.parar)
        self.addCleanup(self.sim.parar)
        self.ccu.sincronizar().result(timeout=3)

    def test_estructuras(self):
        r = m.leer_resultado(m.resultado(7, 5, 1, [3, 1], {2: 0, 3: 0, 4: 1, 6: 0}))
        self.assertEqual((r["numero"], r["presentes"], r["no_votaron"], r["votos"][:2]), (7, 5, 1, [3, 1]))
        self.assertEqual(r["por_delegado"], {2: 0, 3: 0, 4: 1, 6: 0})
        self.assertEqual(m.leer_tema(m.tema(3, "Expediente 12/26"))[1], "Expediente 12/26")

    def test_votacion_completa(self):
        self.assertEqual(self.ccu.estado.fase2_errores, {})
        self.ccu.votacion_preparar(12, "Ordenanza de arbolado", "si_no_abstencion").result(timeout=2)
        self.assertEqual(self.sim.vt_tema, (12, "Ordenanza de arbolado"))
        self.ccu.votacion_abrir().result(timeout=2)
        for u, o in ((2, 0), (3, 0), (4, 1), (5, 2)):
            self.sim.votar(u, o)
        self.assertTrue(esperar(lambda: (self.ccu.estado.votacion.get("ultimo") or {}).get("votos", [0])[0] == 2))
        r = self.ccu.votacion_cerrar().result(timeout=2)
        self.assertEqual(r["votos"][:3], [2, 1, 1])
        self.assertEqual(r["por_delegado"][4], 1)
        self.assertEqual(self.ccu.estado.votacion["estado"], "cerrada")
        with self.assertRaises(ErrorCcu) as ctx:
            self.ccu.votacion_pausar().result(timeout=2)
        self.assertIn("no hay una votación", str(ctx.exception))

    def test_asistencia(self):
        self.assertEqual(self.ccu.estado.asistencia_total, {"presentes": 6, "ausentes": 0})
        self.sim.presente(3, False)
        self.assertTrue(esperar(lambda: self.ccu.estado.asistencia_total.get("presentes") == 5))
        self.assertFalse(self.ccu.estado.asistencia[3]["presente"])

    def test_tiempo(self):
        self.ccu.tiempo_palabra(5).result(timeout=2)
        self.assertEqual(self.sim.tiempo, {"minutos": 5, "reloj": True})


class SegunElManualYElDvd(unittest.TestCase):
    def test_trama_simple_del_ejemplo_de_bosch(self):
        # Presentación de Bosch: MM_C_SPK_APPEND de la unidad 0x1C por Simple = 1b 24 0d 03 04 00 30 00 1c 00 60
        t = SerieSimple.armar(m.Mensaje(m.REQ, m.MM_C_SPK_APPEND, m.unidad(0x1C)))
        self.assertEqual(t.hex(" "), "1b 24 0d 03 04 00 30 00 1c 00 60")

    def test_full_igual_que_scs_dll(self):
        # Referencia escrita siguiendo el desensamblado de SCS.DLL (segmento 4, 0x0345-0x048b): la suma
        # arranca en ESC+'$'+SEC, cada 0xC8 del largo o de los datos se escribe dos veces y SUMA dos veces;
        # si la suma da 0x37 (~0x37 = 0xC8) va CORR = 0x13; CHK = ~suma.
        def scs(sec, tipo, cuerpo):
            out = bytearray([0xC8, 0x24, sec])
            suma = (sec + 0xEC) & 0xFF
            for b in [tipo, len(cuerpo) & 0xFF, len(cuerpo) >> 8] + list(cuerpo):
                if b == 0xC8 and len(out) > 3:
                    out.append(0xC8)
                    suma = (suma + 0xC8) & 0xFF
                out.append(b)
                suma = (suma + b) & 0xFF
            corr = 0x13 if suma == 0x37 else 0
            suma = (suma + corr) & 0xFF
            return bytes(out + bytearray([corr, (~suma) & 0xFF]))
        t = SerieFull(None)
        for unidad in (12, 200, 0xC8C8):  # 200 = 0xC8: el caso que importa
            msg = m.Mensaje(m.REQ, m.MM_C_SET_MICRO_ON_OFF, m.micro_onoff(unidad, True))
            cuerpo = m.empaquetar(msg)
            self.assertEqual(t.armar(ord("$"), 5, cuerpo), scs(5, cuerpo[0], cuerpo[3:]), f"unidad {unidad}")

    def test_full_recibe_lo_que_arma_scs_dll(self):
        a, b = socket.socketpair()
        rx = []
        t = SerieFull(FlujoSocket(a))
        t.al_recibir = rx.append
        t.arrancar()
        self.addCleanup(t.parar)
        pkt = SerieFull(None).armar(ord("$"), 7, m.empaquetar(m.Mensaje(m.NTF, m.MM_C_MICRO_ON_OFF,
                                                                      m.palabra(200) + m.palabra(1) + m.palabra(3))))
        self.assertIn(b"\xc8\xc8", pkt)
        b.sendall(pkt)
        self.assertTrue(esperar(lambda: rx))
        self.assertEqual(m.leer_micro_on_pc(rx[0].datos)[0], 200)
        self.assertEqual(t.contadores["checksum_mal"], 0)

    def test_nak_de_largo_cero(self):
        v = m.desempaquetar(bytes([m.NAK, 0, 0]))
        self.assertEqual((v.tipo, v.fn), (m.NAK, 0))

    def test_resultado_con_24_totales(self):
        # Por si la CCU manda lo que dice el FNID.DEF del DDTK ({ w dd d24 w b x… }) en vez de 25.
        import struct
        d = struct.pack("<HII24IHB", 4, 3, 0, 2, 1, *([0] * 22), 3, 0) + bytes([0, 1, 0])
        r = m.leer_resultado(d)
        self.assertEqual((r["numero"], r["votos"][:2], r["por_delegado"]), (4, [2, 1], {1: 0, 2: 1, 3: 0}))

    def test_full_manda_latido_antes_que_datos(self):
        a, b = socket.socketpair()
        t = SerieFull(FlujoSocket(a))
        t.arrancar()
        self.addCleanup(t.parar)
        t.enviar(m.Mensaje(m.REQ, m.MM_C_SHIFT))
        b.settimeout(1)
        primero = b.recv(5)
        self.assertEqual(primero[:2], bytes([0xC8, ord("?")]))

    def test_tcp_lleva_el_fnid_en_los_datos(self):
        # Como lo arma el CCU_DDTK.exe: tipo = 0x43<<24 | fn<<8 | byType, largo = datos + 16, datos desde el wFnId.
        a, b = socket.socketpair()
        t = Tcp(FlujoSocket(a))
        t.arrancar()
        self.addCleanup(t.parar)
        t.enviar(m.Mensaje(m.REQ, m.MM_C_SET_MICRO_ON_OFF, m.micro_onoff(12, True))).result(timeout=1)
        b.settimeout(1)
        p = b.recv(64)
        import struct
        tipo, largo = struct.unpack_from("<II", p, 0)
        self.assertEqual(tipo, 0x43000000 | (m.MM_C_SET_MICRO_ON_OFF << 8) | m.REQ)
        self.assertEqual(largo, 16 + 2 + 3)
        self.assertEqual(p[16:18], b"\x22\x00")

    def test_simple_con_simulador(self):
        a, b = socket.socketpair()
        sim = Simulador(SerieSimple(FlujoSocket(b)), unidades=4)
        sim.arrancar()
        t = SerieSimple(FlujoSocket(a))
        ccu = Ccu(t, solo_escuchar=False)
        t.arrancar()
        self.addCleanup(t.parar)
        self.addCleanup(sim.parar)
        ccu.sincronizar().result(timeout=3)
        ccu.microfono(3, True).result(timeout=2)
        self.assertEqual(ccu.estado.abiertos(), [3])

    def test_parametros_del_enlace(self):
        a, b = socket.socketpair()
        sim = Simulador(SerieFull(FlujoSocket(b)), unidades=2)
        sim.arrancar()
        t = SerieFull(FlujoSocket(a))
        ccu = Ccu(t, solo_escuchar=True)
        t.arrancar()
        self.addCleanup(t.parar)
        self.addCleanup(sim.parar)
        r = ccu.parametros_enlace(5, 2, 2).result(timeout=3)
        self.assertEqual(r, {"latido_s": 5, "reintentos": 2, "reintento_s": 2})
        ccu.sincronizar().result(timeout=3)
        self.assertEqual(sim.enlace["latido_s"], 5)


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
