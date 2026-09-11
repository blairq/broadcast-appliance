// test_spout_transport.cpp
//
// Pruebas unitarias de la abstraccion de transporte de video SPOUT (spec 4.2)
// sobre el backend NULL/CPU (ruta de pruebas, sin GPU). Sin framework externo:
// un macro minimo de CHECK y un main() que devuelve 0 si todo pasa y 1 si algo
// falla (lo registra CTest).
//
// Orden de operaciones (importante, ver SpoutTransport.cpp):
//   `subscribe()` captura `lastSeenIndex_` en el frameIndex ACTUAL del sender.
//   `receive()` solo entrega frames con `frameIndex > lastSeenIndex_`. Por eso
//   el orden correcto es  publish -> subscribe -> send -> receive: si se envia
//   ANTES de suscribirse, el primer frame queda "ya visto" y no se entrega.
//
// Casos:
//   1. meta block round-trip: create, publish(frameIndex=5, qpc=123), read()
//      devuelve {5,123}; reabrir (open) ve los mismos valores.
//   2. sender/receiver null round-trip: publish un slot 1280x720, subscribe,
//      send un buffer lleno + meta, receive a un buffer; los bytes y el
//      meta.frameIndex coinciden.
//   3. no new frame: un segundo receive() sin un send nuevo devuelve false
//      (hasData false tras el drain).
//   4. plate-when-absent: un sender recién publicado sin send -> hasData false
//      (el "sender existe siempre, publica placa" es responsabilidad del caller).

#include "ipc/SharedMemoryRegion.hpp"
#include "ipc/SpoutTransport.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace ba::ipc;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                                          \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            ++g_failures;                                                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                      \
        }                                                                                                    \
    } while (0)

// Tamano de un frame BGRA 8888 (4 bytes/pixel), igual a slotBytes() del .cpp.
std::size_t frameBytes(const SlotConfig& cfg) {
    return static_cast<std::size_t>(cfg.width) * static_cast<std::size_t>(cfg.height) * 4u;
}

// Caso 1: round-trip del bloque de metadatos + reabrir por name.
void testMetaRoundTrip() {
    const wchar_t* name = L"BA.Test.Meta.1";

    SpoutMetaBlock block;
    Status st = SpoutMetaBlock::create(name, block);
    CHECK(st.ok);
    CHECK(block.valid());

    // Estado inicial: {0, 0}.
    FrameMeta zero = block.read();
    CHECK(zero.frameIndex == 0);
    CHECK(zero.qpc == 0);

    block.publish(5, 123);
    FrameMeta got = block.read();
    CHECK(got.frameIndex == 5);
    CHECK(got.qpc == 123);

    // Reabrir por name (camino del receiver) ve los mismos valores.
    SpoutMetaBlock reopened;
    Status ost = SpoutMetaBlock::open(name, reopened);
    CHECK(ost.ok);
    CHECK(reopened.valid());
    FrameMeta again = reopened.read();
    CHECK(again.frameIndex == 5);
    CHECK(again.qpc == 123);
}

// Caso 2: round-trip sender/receiver sobre el backend NULL.
void testSenderReceiverRoundTrip() {
    const wchar_t* name = L"BA.Test.Slot.1";
    const SlotConfig cfg{1280, 720};
    const std::size_t bytes = frameBytes(cfg);

    SpoutSender snd;
    CHECK(snd.publish(name, cfg));
    CHECK(snd.published());

    SpoutReceiver rcv;
    CHECK(rcv.subscribe(name));

    // Patrón determinista: byte i = i % 251 (primo, para detectar desplazamientos).
    std::vector<std::uint8_t> src(bytes);
    for (std::size_t i = 0; i < bytes; ++i) {
        src[i] = static_cast<std::uint8_t>(i % 251);
    }

    const FrameMeta meta{1, 1000};
    CHECK(snd.send(src.data(), bytes, meta));

    std::vector<std::uint8_t> dst(bytes, 0xAA);
    FrameMeta out{};
    CHECK(rcv.receive(dst.data(), dst.size(), out));
    CHECK(out.frameIndex == 1);
    CHECK(out.qpc == 1000);
    bool same = true;
    for (std::size_t i = 0; i < bytes; ++i) {
        if (dst[i] != src[i]) {
            same = false;
            break;
        }
    }
    CHECK(same);
}

// Caso 3: sin frame nuevo, receive() devuelve false y hasData() es false.
void testNoNewFrame() {
    const wchar_t* name = L"BA.Test.Slot.2";
    const SlotConfig cfg{640, 360};
    const std::size_t bytes = frameBytes(cfg);

    SpoutSender snd;
    CHECK(snd.publish(name, cfg));
    SpoutReceiver rcv;
    CHECK(rcv.subscribe(name));

    std::vector<std::uint8_t> src(bytes, 0x5A);
    CHECK(snd.send(src.data(), bytes, FrameMeta{7, 700}));

    std::vector<std::uint8_t> dst(bytes);
    FrameMeta out{};
    CHECK(rcv.receive(dst.data(), dst.size(), out)); // primer frame: true
    CHECK(out.frameIndex == 7);

    // Segundo receive() sin send nuevo: no hay frame nuevo.
    CHECK(!rcv.hasData());
    CHECK(!rcv.receive(dst.data(), dst.size(), out));

    // Un frame nuevo vuelve a ser entregable.
    CHECK(snd.send(src.data(), bytes, FrameMeta{8, 800}));
    CHECK(rcv.hasData());
    CHECK(rcv.receive(dst.data(), dst.size(), out));
    CHECK(out.frameIndex == 8);
}

// Caso 4: "un sender existe siempre" — publicado pero sin send -> sin datos.
void testPlateWhenAbsent() {
    const wchar_t* name = L"BA.Test.Slot.3";
    const SlotConfig cfg{1280, 720};

    SpoutSender snd;
    CHECK(snd.publish(name, cfg)); // existe, pero aún no envió nada (placa ausente)
    CHECK(snd.published());

    SpoutReceiver rcv;
    CHECK(rcv.subscribe(name));

    // Sin send: no hay frame nuevo (frameIndex 0 == lastSeen 0).
    CHECK(!rcv.hasData());
    std::vector<std::uint8_t> dst(frameBytes(cfg));
    FrameMeta out{999, 999};
    CHECK(!rcv.receive(dst.data(), dst.size(), out));
    // receive() fallido no toca metaOut.
    CHECK(out.frameIndex == 999);
}

} // namespace

int main() {
    testMetaRoundTrip();
    testSenderReceiverRoundTrip();
    testNoNewFrame();
    testPlateWhenAbsent();

    if (g_failures == 0) {
        std::printf("ALL TESTS PASSED (test_spout_transport)\n");
        return 0;
    }
    std::printf("%d CHECK(s) FAILED (test_spout_transport)\n", g_failures);
    return 1;
}
