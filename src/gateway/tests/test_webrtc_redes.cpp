// test_webrtc_redes.cpp — las redes de cada PeerConnection (RedesSesionRtc.hpp)
//
// La ráfaga por el TURN (40 invitados a la vez → 21 conectaban): con la lista de redes
// COMPARTIDA, cada sesión que empieza (StartUpdating) le avisa «cambiaron las redes» a
// todas las demás, y las que todavía no conectaron vuelven a juntar un puerto nuevo en
// cada red. Con un rtc::BasicNetworkManager de verdad, en su hilo:
//   - antes (escuchando la lista común directo): cada sesión nueva le llega a la primera
//     como un aviso más (el problema);
//   - después (RedesDeSesion): la primera recibe un solo aviso por más sesiones que
//     empiecen, y cada sesión nueva recibe el suyo.
#include "RedesSesionRtc.hpp"

#include "rtc_base/thread.h"

#include <chrono>
#include <cstdio>
#include <thread>

namespace gw = ba::gateway;

static int fallas = 0;
#define CHECK(c, msg)                                                                                                  \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::printf("FALLA (línea %d): %s\n", __LINE__, msg);                                                      \
            ++fallas;                                                                                                  \
        }                                                                                                              \
    } while (0)

namespace {
struct Contador : public sigslot::has_slots<> {
    int avisos = 0;
    void alCambiar() { ++avisos; }
};

/// Deja correr el hilo de red (los avisos van por tareas en su cola).
void esperar(rtc::Thread* hilo) {
    for (int i = 0; i < 5; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        hilo->BlockingCall([] {});
    }
}
} // namespace

int main() {
    auto red = rtc::Thread::CreateWithSocketServer();
    red->SetName("red-prueba", nullptr);
    red->Start();
    std::unique_ptr<rtc::BasicNetworkManager> comun;
    red->BlockingCall([&] { comun = std::make_unique<rtc::BasicNetworkManager>(red->socketserver()); });

    // --- Antes: tres «sesiones» escuchando la lista común directo.
    Contador directo[3];
    for (int i = 0; i < 3; ++i) {
        red->BlockingCall([&] {
            comun->SignalNetworksChanged.connect(&directo[i], &Contador::alCambiar);
            comun->StartUpdating();
        });
        esperar(red.get());
    }
    const int avisosPrimera = red->BlockingCall([&] { return directo[0].avisos; });
    std::printf("lista común directa: a la primera le llegaron %d avisos con 3 sesiones\n", avisosPrimera);
    CHECK(avisosPrimera >= 3, "libwebrtc: cada StartUpdating le avisa a todas (el comportamiento que se esquiva)");
    red->BlockingCall([&] {
        for (int i = 0; i < 3; ++i) {
            comun->SignalNetworksChanged.disconnect(&directo[i]);
            comun->StopUpdating();
        }
    });

    // --- Después: cada sesión por su RedesDeSesion.
    constexpr int kSesiones = 10;
    std::unique_ptr<gw::RedesDeSesion> redes[kSesiones];
    Contador porSesion[kSesiones];
    for (int i = 0; i < kSesiones; ++i) {
        red->BlockingCall([&] {
            redes[i] = std::make_unique<gw::RedesDeSesion>(comun.get());
            redes[i]->SignalNetworksChanged.connect(&porSesion[i], &Contador::alCambiar);
            redes[i]->StartUpdating();
        });
        esperar(red.get());
    }
    red->BlockingCall([&] {
        int conUno = 0, tragados = 0;
        for (int i = 0; i < kSesiones; ++i) {
            conUno += porSesion[i].avisos == 1 ? 1 : 0;
            tragados += redes[i]->tragados();
        }
        std::printf("por sesión: %d de %d con un solo aviso (la primera: %d); %d avisos ajenos tragados\n", conUno,
                    kSesiones, porSesion[0].avisos, tragados);
        CHECK(conUno == kSesiones, "cada sesión recibe un aviso: el suyo, no los de las que empiezan después");
        CHECK(tragados > 0, "los avisos de las otras sesiones no pasan");
        CHECK(!redes[0]->GetNetworks().empty(), "las redes son las de la lista común");
        // Una segunda sesión de la MISMA conexión (otro StartUpdating) recibe su aviso.
        redes[0]->StartUpdating();
    });
    esperar(red.get());
    red->BlockingCall([&] {
        CHECK(porSesion[0].avisos == 2, "una sesión nueva de la misma conexión recibe su primer aviso");
        for (auto& r : redes) r.reset(); // suelta sus StartUpdating
        comun.reset();
    });
    red->Stop();
    std::printf(fallas ? "test_webrtc_redes: %d fallas\n" : "test_webrtc_redes: todo ok\n", fallas);
    return fallas ? 1 : 0;
}
