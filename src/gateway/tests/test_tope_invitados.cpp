// test_tope_invitados.cpp — un solo tope de invitados y un rango UDP que alcanza
//
// Lo que mostró la prueba de carga con 40 invitados (hinchada):
//   - el gateway recortaba el cupo a 16 al arrancar (kMaxGuests) aunque en caliente subía
//     hasta 64: ahora el tope es uno solo (ipc::kMaxInvitados), al arrancar y en caliente;
//   - la reserva sigue al cupo dentro de su rango (no lo pasa);
//   - el rango UDP no se recorta a 151 puertos: hasta kTopeRangoRtc;
//   - cada sesión abre un puerto por RED: las interfaces virtuales (el puente de las
//     máquinas virtuales de la Mac, Docker, VPN) y las de enlace local no cuentan y se
//     le dicen a libwebrtc que las ignore;
//   - con 100 puertos y cuatro redes entraban 25 sesiones: el uso de la sala dice si
//     alcanza para la sala llena (PuertosRtc).
#include "Config.hpp"
#include "PuertosRtc.hpp"
#include "ipc/LugaresSala.hpp"
#include "ipc/RangoRtc.hpp"
#include "net/NetUtil.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace ba::gateway;

namespace {
int fallas = 0;
#define CHECK(c, m)                                             \
    do {                                                        \
        if (!(c)) {                                             \
            std::printf("FALLA: %s (linea %d)\n", m, __LINE__); \
            ++fallas;                                           \
        } else {                                                \
            std::printf("ok: %s\n", m);                         \
        }                                                       \
    } while (0)

Config conArgs(std::vector<std::string> a) {
    a.insert(a.begin(), "ba-gateway");
    std::vector<char*> v;
    for (auto& s : a) v.push_back(s.data());
    Config c;
    c.parseArgs(static_cast<int>(v.size()), v.data());
    return c;
}

net::DireccionLocal dir(const std::string& interfaz, const std::string& ip, bool v6, bool virtual_, bool enlace) {
    net::DireccionLocal d;
    d.interfaz = interfaz;
    d.ip = ip;
    d.v6 = v6;
    d.virtual_ = virtual_;
    d.enlaceLocal = enlace;
    return d;
}
} // namespace

int main() {
    // --- El tope de invitados: uno solo ---
    {
        const Config c = conArgs({"--guests", "40"});
        CHECK(c.guests == 40, "el cupo de 40 queda en 40 al arrancar (antes: 16)");
        CHECK(c.guestsReserve >= c.guests && c.guestsReserve <= ba::ipc::kMaxInvitados, "la reserva cubre el cupo, dentro del tope");
    }
    {
        const Config c = conArgs({"--guests", "900", "--guests-reserve", "900"});
        CHECK(c.guests == ba::ipc::kMaxInvitados, "más que el tope: el tope");
        CHECK(c.guestsReserve == ba::ipc::kMaxInvitados, "la reserva tampoco pasa el tope");
    }
    {
        const Config c = conArgs({"--guests", "3", "--guests-reserve", "8"});
        CHECK(c.guests == 3 && c.guestsReserve == 8, "una reserva mayor que el cupo se respeta");
    }
    {
        // Lo que guarda la app (gateway.json): el cupo y la reserva tal cual.
        const auto f = std::filesystem::temp_directory_path() / "ba-test-tope-invitados.json";
        std::ofstream(f) << R"({"guests": 40, "guestsReserva": 4, "rtcPortMin": 40000, "rtcPortMax": 42000})";
        Config c;
        c.loadFile(f.string());
        c.parseArgs(1, nullptr);
        std::filesystem::remove(f);
        CHECK(c.guests == 40, "gateway.json con 40: 40");
        CHECK(c.guestsReserve == 40, "la reserva efectiva es el mayor (los canales del cupo)");
        CHECK(c.rtcPortMin == 40000 && c.rtcPortMax == 40000 + kTopeRangoRtc - 1, "el rango se recorta a kTopeRangoRtc, no a 151");
    }

    // --- La reserva: lo que pidió el operador, en su rango ---
    CHECK(ba::ipc::reservaValida(40) == 40 && ba::ipc::reservaValida(0) == 1 && ba::ipc::reservaValida(900) == ba::ipc::kMaxInvitados,
          "la reserva se guarda en su rango, sin arrastrarla con el cupo");
    CHECK(ba::ipc::lugaresIniciales(40, 4) == 40 && ba::ipc::lugaresIniciales(2, 8) == 8,
          "los lugares del arranque: el cupo o la reserva, el mayor");

    // --- Las redes en que WebRTC abre puertos ---
    {
        CHECK(net::esVirtual("bridge100") && net::esVirtual("vmenet0") && net::esVirtual("utun3") && net::esVirtual("docker0") &&
                  net::esVirtual("veth12ab") && net::esVirtual("hyper-v virtual ethernet adapter vethernet (wsl)"),
              "las interfaces virtuales se reconocen");
        CHECK(!net::esVirtual("en0") && !net::esVirtual("eth0") && !net::esVirtual("bridge0") && !net::esVirtual("wlan0") &&
                  !net::esVirtual("intel(r) ethernet connection ethernet"),
              "las de verdad no (bridge0 es el de Thunderbolt)");
        // La Mac de la prueba de carga: en0 (IPv4 e IPv6), el puente de las máquinas
        // virtuales (IPv4 y una ULA), utun con enlace local y awdl.
        const std::vector<net::DireccionLocal> mac = {
            dir("en0", "192.168.1.105", false, false, false),       dir("en0", "2803:9810::f04", true, false, false),
            dir("en0", "fe80::aa:80ac", true, false, true),         dir("bridge100", "192.168.64.1", false, true, false),
            dir("bridge100", "fd2c:1be3::d9b4", true, true, false), dir("utun0", "fe80::debc", true, true, true),
            dir("awdl0", "fe80::b49b", true, true, true)};
        const net::RedesRtc r = net::redesRtc(mac, false);
        CHECK(r.redes == 2, "dos redes: en0 por IPv4 y por IPv6 (antes cuatro, con el puente)");
        CHECK(std::find(r.ignorar.begin(), r.ignorar.end(), "bridge100") != r.ignorar.end() &&
                  std::find(r.ignorar.begin(), r.ignorar.end(), "utun0") != r.ignorar.end() &&
                  std::find(r.ignorar.begin(), r.ignorar.end(), "en0") == r.ignorar.end(),
              "a libwebrtc se le dice que ignore las virtuales, no en0");
        const net::RedesRtc todas = net::redesRtc(mac, true);
        CHECK(todas.redes == 4 && todas.ignorar.empty(), "con red.rtcVirtuales, también las virtuales (sin las de enlace local)");
        // Las de esta máquina: las virtuales no cuentan como redes.
        const auto aca = net::direccionesLocales();
        const net::RedesRtc ra = net::redesRtc(aca, false);
        std::printf("esta máquina: %zu direcciones, %d redes para WebRTC, %zu interfaces ignoradas\n", aca.size(), ra.redes,
                    ra.ignorar.size());
        for (const auto& d : aca)
            std::printf("  %-10s %-40s %s%s%s\n", d.interfaz.c_str(), d.ip.c_str(), d.v6 ? "v6" : "v4", d.virtual_ ? " virtual" : "",
                        d.enlaceLocal ? " enlace-local" : "");
        CHECK(!aca.empty() && !net::localIPv4s().empty(), "la máquina tiene direcciones (y una IPv4 para el QR)");
    }

    // --- ¿Alcanza el rango? ---
    {
        const PuertosRtc antes = puertosRtc(40000, 40099, 4, 40, 4);
        CHECK(antes.entran == 25 && !antes.alcanza, "100 puertos y 4 redes: 25 sesiones, no alcanza para 40 + el lobby");
        const PuertosRtc ahora = puertosRtc(40000, 40099, 2, 40, 4);
        CHECK(ahora.entran == 50 && ahora.alcanza, "con 2 redes: 50, alcanza para 40 + 4");
        const PuertosRtc tope = puertosRtc(40000, 40099, 2, ba::ipc::kMaxInvitados, 4);
        CHECK(!tope.alcanza, "el tope de invitados con 100 puertos no entra: se avisa");
        CHECK(puertosRtc(ba::ipc::kRtcPuertoMin, ba::ipc::kRtcPuertoMax, 3, ba::ipc::kMaxInvitados, 8).alcanza,
              "con el rango de fábrica (1600) sí, aun con tres por sesión");
        const nlohmann::json j = aJson(antes, 40000, 40099, 3);
        CHECK(j.value("alcanza", true) == false && j.value("sinPuerto", 0) == 3 && j.value("necesarias", 0) == 44,
              "el uso de la sala lo dice");
    }

    std::printf(fallas ? "test_tope_invitados: %d fallas\n" : "test_tope_invitados: todo ok\n", fallas);
    return fallas ? 1 : 0;
}
