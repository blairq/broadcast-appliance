// test_quinientos_lugares.cpp — la sala acepta el tope de invitados (512) de punta a punta
//
// En la PC de pruebas, con 80 invitados entraban 68: el tope era una constante (64 + 4 de
// reserva). Ahora el tope es ipc::kMaxInvitados (512) y lo que limita es la máquina. Sin
// medios reales (canales chicos, sin navegadores), esto arma lo que arma el gateway:
//   - el pool de sesiones WebRTC con lugar para el tope y lo demás (main.cpp);
//   - MediaPump con los canales de 2 lugares y ampliar(512): los 512 de cada clase
//     (cámara, pantalla, vista chica, audio), sin pasarse, y lo que manda la sesión del
//     lugar 512 llega a su canal;
//   - la sala con el cupo en 512: 512 personas admitidas, cada una en su lugar, y la 513
//     no entra;
//   - el rango UDP por omisión alcanza para el tope (con tres puertos por sesión), y un
//     gateway.json con el rango de antes (40000-40099) pasa al nuevo.

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "Config.hpp"
#include "Log.hpp"
#include "MediaPump.hpp"
#include "PuertosRtc.hpp"
#include "Room.hpp"
#include "Webrtc.hpp"
#include "ipc/LugaresSala.hpp"
#include "ipc/SharedAudioRing.hpp"
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

bool hayVideo(const std::string& nombre) {
    ba::ipc::SpoutReceiver r;
    return r.subscribe(w(nombre).c_str());
}
} // namespace

int main() {
    CHECK(ba::ipc::kMaxInvitados == 512, "el tope de invitados es 512");

    // --- Puertos: el rango por omisión alcanza para el tope ---
    {
        const Config c;
        CHECK(c.rtcPortMin == 40000 && c.rtcPortMax == 41599, "el rango por omisión es 40000-41599");
        const PuertosRtc dos = puertosRtc(c.rtcPortMin, c.rtcPortMax, 2, ba::ipc::kMaxInvitados, 8);
        CHECK(dos.alcanza && dos.entran == 800, "con dos redes entran 800 sesiones: alcanza para 512 + el lobby");
        const PuertosRtc tres = puertosRtc(c.rtcPortMin, c.rtcPortMax, 3, ba::ipc::kMaxInvitados, 8);
        CHECK(tres.alcanza, "con tres por sesión (dos redes y el túnel de WireGuard) también");
        CHECK(c.rtcPortMax - c.rtcPortMin + 1 <= kTopeRangoRtc, "el rango por omisión entra en el tope del rango (lwIP)");

        const auto f = std::filesystem::temp_directory_path() / "ba-test-quinientos.json";
        std::ofstream(f) << R"({"guests": 512, "rtcPortMin": 40000, "rtcPortMax": 40099})";
        Config viejo;
        viejo.loadFile(f.string());
        viejo.parseArgs(1, nullptr);
        CHECK(viejo.guests == 512, "gateway.json con 512 invitados: 512 (antes 64)");
        CHECK(viejo.rtcPortMin == 40000 && viejo.rtcPortMax == 41599, "el rango por omisión de antes pasa al nuevo");
        std::ofstream(f) << R"({"rtcPortMin": 40000, "rtcPortMax": 40500})";
        Config elegido;
        elegido.loadFile(f.string());
        elegido.parseArgs(1, nullptr);
        CHECK(elegido.rtcPortMax == 40500, "un rango elegido a mano se respeta");
        std::ofstream(f) << R"({"rtcPortMin": 65000, "rtcPortMax": 65535})";
        Config alto;
        alto.loadFile(f.string());
        alto.parseArgs(1, nullptr);
        CHECK(alto.rtcPortMin == 65000 && alto.rtcPortMax == 65535, "arriba de todo no da la vuelta (uint16)");
        std::filesystem::remove(f);
    }

    // --- El pool de sesiones y los canales de los 512 lugares ---
    {
        const std::string pre = "BA.Test512." + std::to_string(static_cast<long long>(
                                                    std::chrono::steady_clock::now().time_since_epoch().count() % 1000000)) + ".";
        // Como main.cpp: el tope más el lobby, la fila y lo demás.
        WebrtcHub hub(ba::ipc::kMaxInvitados + 8, 64, 36, 48000, 2);
        CHECK(hub.sessions() == ba::ipc::kMaxInvitados + 8, "el pool tiene una sesión por lugar posible");
        CHECK(hub.session(ba::ipc::kMaxInvitados) != nullptr && hub.session(ba::ipc::kMaxInvitados + 9) == nullptr,
              "la sesión del 512 está; más allá del pool, no");

        MediaPump::Cfg cfg;
        for (auto* v : {&cfg.guestAudioNames, &cfg.guestVideoNames, &cfg.shareVideoNames, &cfg.guestUiVideoNames}) v->emplace_back();
        for (int s = 1; s <= ba::ipc::kMaxInvitados; ++s) {
            cfg.guestVideoNames.push_back(w(pre + "Guest." + std::to_string(s)));
            cfg.shareVideoNames.push_back(w(pre + "GuestShare." + std::to_string(s)));
            cfg.guestUiVideoNames.push_back(w(pre + "GuestUi." + std::to_string(s)));
            cfg.guestAudioNames.push_back(w(pre + "Audio.Guest." + std::to_string(s)));
        }
        cfg.lugaresIniciales = 2;
        cfg.programW = 64; // sin medios reales: canales chicos
        cfg.programH = 36;
        cfg.guestUiW = 32;
        cfg.guestUiH = 18;
        Log log;
        MediaPump pump(hub, cfg, log);
        pump.start();
        CHECK(pump.lugares() == 2, "arranca con los canales del cupo (no los 512)");
        CHECK(!hayVideo(pre + "Guest.3"), "el lugar 3 todavía no tiene canales");

        pump.ampliar(ba::ipc::kMaxInvitados);
        CHECK(pump.lugares() == ba::ipc::kMaxInvitados, "ampliar(512): 512 lugares con canales");
        int camaras = 0, pantallas = 0, chicas = 0, audios = 0;
        for (int s = 1; s <= ba::ipc::kMaxInvitados; ++s) {
            camaras += hayVideo(pre + "Guest." + std::to_string(s)) ? 1 : 0;
            pantallas += hayVideo(pre + "GuestShare." + std::to_string(s)) ? 1 : 0;
            chicas += hayVideo(pre + "GuestUi." + std::to_string(s)) ? 1 : 0;
            ba::ipc::SharedAudioRing ring;
            audios += ba::ipc::SharedAudioRing::openReader(w(pre + "Audio.Guest." + std::to_string(s)).c_str(), ring).ok ? 1 : 0;
        }
        std::printf("canales: %d cámaras, %d pantallas, %d vistas chicas, %d audios\n", camaras, pantallas, chicas, audios);
        CHECK(camaras == 512 && pantallas == 512 && chicas == 512, "los 512 lugares tienen cámara, pantalla y vista chica");
        CHECK(audios == 512, "y su anillo de audio");
        pump.ampliar(ba::ipc::kMaxInvitados + 100);
        CHECK(pump.lugares() == ba::ipc::kMaxInvitados, "no pasa del tope");

        // Lo que manda la sesión ruteada al lugar 512 llega a su canal.
        ba::ipc::SpoutReceiver rx;
        CHECK(rx.subscribe(w(pre + "Guest.512").c_str()), "la app se suscribe al lugar 512");
        pump.setRoute(ba::ipc::kMaxInvitados, MediaRoute::program(ba::ipc::kMaxInvitados));
        std::vector<uint8_t> cuadro(64u * 36u * 4u, 0x5A);
        std::vector<uint8_t> dst(64u * 36u * 4u);
        bool llego = false;
        for (int i = 0; i < 50 && !llego; ++i) {
            pump.forwardVideo(ba::ipc::kMaxInvitados, cuadro.data(), cuadro.size(), 64, 36);
            ba::ipc::FrameMeta m{};
            llego = rx.receive(dst.data(), dst.size(), m) && dst[0] == 0x5A;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        CHECK(llego, "el video del invitado del lugar 512 llega a la app");
        pump.stop();
    }

    // --- La sala con 512 adentro ---
    {
        Room::Cfg rc;
        rc.maxGuests = 2;
        rc.reserva = ba::ipc::kMaxInvitados; // como main.cpp: el cupo sube en caliente hasta el tope
        Room room(rc);
        Room::Sinks sinks;
        sinks.toGuest = [](const std::string&, const nlohmann::json&) {};
        sinks.toHosts = [](const std::string&, const nlohmann::json&) {};
        room.setSinks(sinks);
        CHECK(room.setCupo(ba::ipc::kMaxInvitados) == Room::kOk, "el cupo sube a 512 sin reiniciar");
        CHECK(room.setCupo(ba::ipc::kMaxInvitados + 1) != Room::kOk, "y no pasa del tope");
        std::set<int> lugares;
        int admitidos = 0;
        std::int64_t t = 1000;
        for (int i = 0; i < ba::ipc::kMaxInvitados; ++i) {
            const auto j = room.connect("Invitado " + std::to_string(i + 1), "10.0." + std::to_string(i / 250) + "." + std::to_string(i % 250 + 1), t++);
            if (j.err != Room::kOk) continue;
            if (room.admit(j.id, t++) == Room::kOk) {
                ++admitidos;
                lugares.insert(room.slotOf(j.id));
            }
        }
        CHECK(admitidos == 512, "entran los 512");
        CHECK(lugares.size() == 512 && *lugares.begin() == 1 && *lugares.rbegin() == 512, "cada uno en su lugar, del 1 al 512");
        const auto extra = room.connect("La 513", "10.9.9.9", t++);
        CHECK(extra.err == Room::kOk && room.admit(extra.id, t++) == Room::kFull, "la 513 espera: sala llena");
        CHECK(room.snapshot().value("slotsFree", -1) == 0, "sin lugares libres");
    }

    std::printf(g_fallas == 0 ? "== test_quinientos_lugares: OK ==\n" : "== test_quinientos_lugares: %d fallas ==\n", g_fallas);
    return g_fallas == 0 ? 0 : 1;
}
