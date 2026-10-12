// test_cupo_caliente.cpp — subir el cupo de invitados sin reiniciar la sala
//
// MediaPump arranca con los canales de 2 lugares; ampliar(3) crea los del 3 andando
// (BA.Guest.3 aparece para la app) y lo que manda una sesión ruteada a ese lugar
// llega. Bajar no borra nada, y no pasa del tope (kMaxInvitados).

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "Log.hpp"
#include "MediaPump.hpp"
#include "Webrtc.hpp"
#include "ipc/SpoutTransport.hpp"

using namespace ba::gateway;

namespace {
int g_fallas = 0;
#define CHECK(c, m)                                             \
    do {                                                        \
        if (!(c)) {                                             \
            std::printf("FALLO: %s (linea %d)\n", m, __LINE__); \
            ++g_fallas;                                         \
        } else {                                                \
            std::printf("ok: %s\n", m);                         \
        }                                                       \
    } while (0)

std::wstring w(const std::string& s) { return std::wstring(s.begin(), s.end()); }

/// ¿Se puede leer el canal (lo publicó el gateway)?
bool hayCanal(const std::string& nombre) {
    ba::ipc::SpoutReceiver r;
    return r.subscribe(w(nombre).c_str());
}
} // namespace

int main() {
    const std::string pre = "BA.TestCupo." + std::to_string(static_cast<long long>(
                                                  std::chrono::steady_clock::now().time_since_epoch().count() % 1000000)) + ".";
    WebrtcHub hub(kMaxInvitados, 320, 180, 48000, 2);
    MediaPump::Cfg cfg;
    for (auto* v : {&cfg.guestAudioNames, &cfg.guestVideoNames, &cfg.shareVideoNames}) v->emplace_back();
    for (int s = 1; s <= kMaxInvitados; ++s) {
        cfg.guestVideoNames.push_back(w(pre + "Guest." + std::to_string(s)));
        cfg.shareVideoNames.push_back(w(pre + "GuestShare." + std::to_string(s)));
        cfg.guestAudioNames.push_back(w(pre + "Audio.Guest." + std::to_string(s)));
    }
    cfg.lugaresIniciales = 2;
    cfg.programW = 320;
    cfg.programH = 180;
    Log log;
    MediaPump pump(hub, cfg, log);
    pump.start();

    CHECK(pump.lugares() == 2, "arranca con los lugares del cupo");
    CHECK(hayCanal(pre + "Guest.2"), "el canal del lugar 2 está");
    CHECK(!hayCanal(pre + "Guest.3"), "el del 3 todavía no");

    pump.ampliar(3);
    CHECK(pump.lugares() == 3, "ampliar(3): tres lugares");
    CHECK(hayCanal(pre + "Guest.3") && hayCanal(pre + "GuestShare.3"), "aparecen la cámara y la pantalla del 3, sin reiniciar");

    // Lo que manda una sesión ruteada al lugar nuevo llega a su canal.
    ba::ipc::SpoutReceiver rx;
    CHECK(rx.subscribe(w(pre + "Guest.3").c_str()), "la app se suscribe al lugar nuevo");
    pump.setRoute(1, MediaRoute::program(3));
    std::vector<uint8_t> cuadro(320u * 180u * 4u, 0x7F);
    bool llego = false;
    std::vector<uint8_t> dst(320u * 180u * 4u);
    for (int i = 0; i < 50 && !llego; ++i) {
        pump.forwardVideo(1, cuadro.data(), cuadro.size(), 320, 180);
        ba::ipc::FrameMeta m{};
        llego = rx.receive(dst.data(), dst.size(), m) && dst[0] == 0x7F;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(llego, "el video del invitado del lugar 3 llega a la app");

    pump.ampliar(2);
    CHECK(pump.lugares() == 3 && hayCanal(pre + "Guest.3"), "bajar no borra canales (no saca a nadie)");
    pump.ampliar(kMaxInvitados + 5);
    CHECK(pump.lugares() == kMaxInvitados, "no pasa del tope");

    pump.stop();
    std::printf(g_fallas == 0 ? "== test_cupo_caliente: OK ==\n" : "== test_cupo_caliente: %d fallas ==\n", g_fallas);
    return g_fallas == 0 ? 0 : 1;
}
