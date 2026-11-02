// test_nucleos.cpp — platform/Nucleos: el reparto de núcleos entre ba-app y el gateway (F4)
//
//   1. Ryzen 9 5900X (2 CCD de 12 lógicos): medios al primero, gateway al segundo.
//   2. Lo mismo pidiendo evitar el 12 y el 13: el gateway queda sin ellos.
//   3. Evitar casi todo el bloque (quedaría 1): no se saca nada.
//   4. Un solo L3: ni medios ni gateway.
//   5. Intel híbrido (P y E, un L3): medios a los P, gateway nada.
//   6. CCD desparejos (8 + 16): medios al más grande, gateway al otro.
//   7. listaCpus: tramos, sueltos, basura.
//   8. /proc/interrupts de una placa sin RSS: las CPU que la atienden.
//   9. La topología del sistema y armarGrupoGateway("") no se caen.

#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "platform/Nucleos.hpp"

using namespace ba::platform::nucleos;

namespace {

int g_fallas = 0;
#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);   \
            ++g_fallas;                                                        \
        } else {                                                               \
            std::printf("ok: %s\n", msg);                                      \
        }                                                                      \
    } while (0)

std::set<int> tramo(int a, int b) {
    std::set<int> s;
    for (int i = a; i <= b; ++i) s.insert(i);
    return s;
}

std::vector<Cpu> ccds(int primero, int segundo) {
    std::vector<Cpu> v;
    for (int i = 0; i < primero; ++i) v.push_back({i, static_cast<unsigned long>(256 + i), 0, 0});
    for (int i = 0; i < segundo; ++i) v.push_back({primero + i, static_cast<unsigned long>(256 + primero + i), 0, 1});
    return v;
}

} // namespace

int main() {
    {
        const Reparto r = repartir(ccds(12, 12), {});
        CHECK(r.medios == tramo(0, 11), "5900X: medios en el primer CCD");
        CHECK(r.gateway == tramo(12, 23), "5900X: gateway en el segundo CCD");
        CHECK(!r.hibrido, "5900X: no es híbrido");
    }
    {
        const Reparto r = repartir(ccds(12, 12), {12, 13});
        CHECK(r.gateway == tramo(14, 23), "evitar 12-13: el gateway sin ellos");
        const Reparto r0 = repartir(ccds(12, 12), {0, 1});
        CHECK(r0.gateway == tramo(12, 23), "evitar 0-1 (del bloque de medios): el gateway entero");
    }
    {
        const Reparto r = repartir(ccds(12, 12), tramo(12, 22));
        CHECK(r.gateway == tramo(12, 23), "evitar casi todo: no se saca nada");
    }
    {
        const Reparto r = repartir(ccds(8, 0), {});
        CHECK(r.medios.empty() && r.gateway.empty(), "un solo L3: nada");
    }
    {
        std::vector<Cpu> v;
        for (int i = 0; i < 16; ++i) v.push_back({i, static_cast<unsigned long>(i), 1, 0}); // P (con SMT)
        for (int i = 16; i < 24; ++i) v.push_back({i, static_cast<unsigned long>(i), 0, 0}); // E
        const Reparto r = repartir(v, {});
        CHECK(r.hibrido, "híbrido: se nota");
        CHECK(r.medios == tramo(0, 15), "híbrido: medios a los de rendimiento");
        CHECK(r.gateway.empty(), "híbrido: el gateway queda libre");
    }
    {
        const Reparto r = repartir(ccds(8, 16), {});
        CHECK(r.medios == tramo(8, 23), "CCD desparejos: medios al más grande");
        CHECK(r.gateway == tramo(0, 7), "CCD desparejos: gateway al otro");
    }
    {
        CHECK(listaCpus("0-3,8,10-11") == std::set<int>({0, 1, 2, 3, 8, 10, 11}), "listaCpus: tramos y sueltos");
        CHECK(listaCpus("").empty(), "listaCpus: vacío");
        CHECK(listaCpus("x,y-z").empty(), "listaCpus: basura");
    }
    {
        const std::string texto =
            "           CPU0       CPU1       CPU2       CPU3\n"
            "   0:         40          0          0          0   IO-APIC    2-edge      timer\n"
            " 128:     900000     100000          0          0   PCI-MSI 1048576-edge      enp5s0\n"
            " 129:       1000          0          0          0   PCI-MSI 1048577-edge      enp5s0-TxRx-1\n"
            " 130:          0          0     500000          0   PCI-MSI 1048578-edge      nvme0q1\n"
            " 131:          0          0          0     300000   PCI-MSI 1048579-edge      enp5s0x\n"
            "NMI:          0          0          0          0   Non-maskable interrupts\n";
        const std::set<int> c = cpusDeInterrupciones(texto, {"enp5s0"});
        CHECK(c == std::set<int>({0}), "interrupciones: la CPU que atiende la placa (el 1 tiene menos del 20 %)");
        CHECK(cpusDeInterrupciones(texto, {"wlan0"}).empty(), "interrupciones: sin esa placa, ninguna");
        CHECK(cpusDeInterrupciones("", {"enp5s0"}).empty(), "interrupciones: texto vacío");
    }
    {
        const nlohmann::json t = topologia();
        CHECK(t.contains("clases") && t.contains("grupos_l3"), "topología del sistema");
        CHECK(!resumen(t).empty(), "resumen");
        CHECK(!armarGrupoGateway("").empty(), "armarGrupoGateway dice qué hizo");
        alGrupoGateway(); // no se cae, haya o no grupo
        CHECK(true, "alGrupoGateway no se cae");
    }
    std::printf(g_fallas == 0 ? "TODO OK\n" : "%d FALLAS\n", g_fallas);
    return g_fallas == 0 ? 0 : 1;
}
