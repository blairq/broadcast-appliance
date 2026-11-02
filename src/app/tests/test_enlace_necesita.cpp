// test_enlace_necesita.cpp — cuánto enlace necesita el evento (sala/EnlaceNecesita)
//
//   la cuenta contra lo medido con la prueba de sala (10/20/30/40 invitados a la vista,
//   20 con «alta hasta 10»), alta_hasta 0 = todos en alta, los encabezados de WireGuard,
//   el veredicto por sentido (entra / justo / no entra / sin medir), la calidad con carga
//   (pérdida, jitter, la demora que sube) que baja un «entra» a «justo», la rampa que no
//   llegó (medí más alto) y hasta cuántos invitados entran; y lo que llega del gateway.

#include <cmath>
#include <cstdio>

#include "sala/EnlaceNecesita.hpp"

namespace en = ba::app::sala::enlace;
using json = nlohmann::json;

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

/// Dentro del `tol` relativo de lo medido.
bool como(double cuenta, double medido, double tol) { return std::fabs(cuenta - medido) <= medido * tol; }

en::Perfil perfil(int n, int alta) {
    en::Perfil p;
    p.invitados = n;
    p.altaHasta = alta;
    return p;
}
en::Medicion medicion(double sube, double baja) {
    en::Medicion m;
    m.subidaKbps = sube;
    m.bajadaKbps = baja;
    m.rttReposoMs = 40;
    m.rttCargaMs = 80;
    return m;
}
} // namespace

int main() {
    // --- La cuenta contra las mediciones de la prueba de sala ----------------------------
    // 2026-11-01 (a la vista): sale 15,98 / 31,92 / 49,53 / 64,65 Mbps con 10/20/30/40.
    CHECK(como(en::necesita(perfil(10, 0)).subidaKbps, 15980, 0.05), "subida con 10 ≈ 16 Mbps medidos");
    CHECK(como(en::necesita(perfil(20, 10)).subidaKbps, 31920, 0.05), "subida con 20 ≈ 32 Mbps medidos");
    CHECK(como(en::necesita(perfil(30, 10)).subidaKbps, 49530, 0.05), "subida con 30 ≈ 49,5 Mbps medidos");
    CHECK(como(en::necesita(perfil(40, 10)).subidaKbps, 64650, 0.05), "subida con 40 ≈ 65 Mbps medidos");
    // Entrada: 10 en alta 14,06 Mbps; 20 con alta_hasta 10: 5,31 Mbps.
    CHECK(como(en::necesita(perfil(10, 10)).bajadaKbps, 14060, 0.05), "bajada con 10 en alta ≈ 14 Mbps medidos");
    CHECK(como(en::necesita(perfil(20, 10)).bajadaKbps, 5310, 0.10), "bajada con 20 y alta hasta 10 ≈ 5,3 Mbps medidos");
    const en::Necesidad n20 = en::necesita(perfil(20, 10));
    CHECK(n20.enAlta == 2, "con más de alta_hasta, en alta solo el orador y el precargado");
    CHECK(en::necesita(perfil(20, 0)).enAlta == 20, "alta_hasta 0: todos en alta");
    CHECK(en::necesita(perfil(10, 10)).enAlta == 10, "justo alta_hasta: todos en alta");
    CHECK(std::fabs(n20.subidaEnlaceKbps - n20.subidaKbps * 1.05) < 1, "WireGuard suma 5 % en el enlace");
    CHECK(en::necesita(perfil(0, 10)).subidaKbps == 0, "sin invitados no hace falta nada");

    // --- Veredicto ----------------------------------------------------------------------
    // 20 invitados: ~32,4 Mbps de subida, ~5,5 de bajada.
    en::Juicio j = en::juzgar(perfil(20, 10), medicion(50000, 100000));
    CHECK(j.veredicto == en::Veredicto::Entra && j.motivos.empty(), "50 Mbps de subida: 20 entran con margen");
    j = en::juzgar(perfil(20, 10), medicion(36000, 100000));
    CHECK(j.veredicto == en::Veredicto::Justo && j.subida == en::Veredicto::Justo && !j.motivos.empty(),
          "36 Mbps: entra sin margen (justo)");
    j = en::juzgar(perfil(20, 10), medicion(20000, 100000));
    CHECK(j.veredicto == en::Veredicto::NoEntra && j.subida == en::Veredicto::NoEntra, "Starlink de 20 Mbps: 20 no entran");
    CHECK(j.entranConMargen == 9 && j.entranJusto == 12, "con 20 Mbps entran 9 con margen y 12 justo");
    j = en::juzgar(perfil(20, 10), medicion(100000, 4000));
    CHECK(j.veredicto == en::Veredicto::NoEntra && j.bajada == en::Veredicto::NoEntra && j.subida == en::Veredicto::Entra,
          "la bajada también cuenta");
    j = en::juzgar(perfil(20, 10), en::Medicion{});
    CHECK(j.veredicto == en::Veredicto::SinMedir && j.entranConMargen == -1, "sin medir: sin veredicto");

    // Calidad con carga: un «entra» pasa a «justo».
    en::Medicion m = medicion(80000, 80000);
    m.rttCargaMs = 400;
    j = en::juzgar(perfil(20, 10), m);
    CHECK(j.veredicto == en::Veredicto::Justo && j.motivos.size() == 1, "la demora que sube con carga: justo");
    m = medicion(80000, 80000);
    m.perdidaSubida = 0.04;
    CHECK(en::juzgar(perfil(20, 10), m).veredicto == en::Veredicto::Justo, "4 % de pérdida: justo");
    m = medicion(80000, 80000);
    m.jitterMs = 45;
    CHECK(en::juzgar(perfil(20, 10), m).veredicto == en::Veredicto::Justo, "jitter de 45 ms: justo");
    m = medicion(20000, 100000);
    m.perdidaSubida = 0.04;
    CHECK(en::juzgar(perfil(20, 10), m).veredicto == en::Veredicto::NoEntra, "lo que no entra no mejora");

    // La rampa no llegó (en vivo se mide suave): no es «no entra», es «medí más alto».
    m = medicion(5000, 5000);
    m.subidaAlMenos = true;
    m.bajadaAlMenos = true;
    j = en::juzgar(perfil(20, 10), m);
    CHECK(j.subida == en::Veredicto::Justo, "midió hasta 5 Mbps sin llenar el enlace: justo, no «no entra»");

    // Sin túnel (contra el proveedor): WireGuard se suma a lo que hace falta.
    m = medicion(33000, 100000);
    CHECK(en::juzgar(perfil(20, 10), m).subida == en::Veredicto::Justo, "33 Mbps por el túnel: justo");
    m.porTunel = false;
    CHECK(en::juzgar(perfil(20, 10), m).subida == en::Veredicto::NoEntra, "33 Mbps sin túnel: con WireGuard no entra");

    // --- Lo que llega del gateway ----------------------------------------------------------
    const json ultimo = {{"ok", true},
                         {"reposo", {{"rttMs", 45.0}}},
                         {"subida", {{"capacidadKbps", 30000.0}, {"kbpsSinPerdida", 28000.0}, {"saturo", true}, {"perdida", 0.01}, {"jitterMs", 3.0}, {"rttCargaMs", 120.0}}},
                         {"bajada", {{"capacidadKbps", 100000.0}, {"kbpsSinPerdida", 100000.0}, {"saturo", false}, {"perdida", 0.0}, {"jitterMs", 5.0}, {"rttCargaMs", 90.0}}}};
    const en::Medicion d = en::deResultado(ultimo);
    CHECK(d.subidaKbps == 28000 && !d.subidaAlMenos, "subida: la tasa sin pérdida, saturó");
    CHECK(d.bajadaKbps == 100000 && d.bajadaAlMenos, "bajada: llegó al tope sin saturar (hay más)");
    CHECK(d.rttReposoMs == 45 && d.rttCargaMs == 120 && d.jitterMs == 5, "demora y jitter: los peores");
    CHECK(en::deResultado(json{{"ok", false}}).subidaKbps < 0, "una medición fallida no cuenta");

    const json a = en::aJson(perfil(20, 10), en::necesita(perfil(20, 10)), en::juzgar(perfil(20, 10), d));
    CHECK(a.value("veredicto", "") == "no_entra" && a["necesita"].value("subidaKbps", 0) > 30000 &&
              a.value("entranConMargen", -1) >= 0,
          "el JSON para la vista y el MCP");

    if (g_fallas == 0) {
        std::printf("test_enlace_necesita: OK\n");
        return 0;
    }
    std::printf("test_enlace_necesita: %d fallo(s)\n", g_fallas);
    return 1;
}
