// ============================================================================
// src/gateway/tests/test_h264_sintaxis.cpp — H264Sintaxis: lo que lee el gateway para que
// la placa decodifique H.264 por D3D11 (CodecsWin.cpp)
//
// Dos flujos chicos de x264 (64x48, 45 cuadros, h264_muestras.inc):
//   - baseline con 2 referencias (POC tipo 2, como los navegadores y la placa de AMD);
//   - high con cuadros B en pirámide, 4 referencias, predicción con pesos, transformada
//     8x8, CABAC, listas de escalado JVT en el PPS y MMCO (x264 suelta referencias a mano).
// Se lee todo sin errores, el POC de cada GOP queda sin huecos (los B entre sus P), el DPB
// no pasa de max_num_ref_frames y el cuadro actual nunca pisa una superficie de referencia.
// ============================================================================

#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <vector>

#include "H264Sintaxis.hpp"

using namespace ba::gateway::h264;

namespace {

#include "h264_muestras.inc"

int g_fallos = 0;
#define CHECK(cond, msg)                                                                                     \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FALLO: %s (%s, linea %d)\n", msg, #cond, __LINE__);                                 \
            ++g_fallos;                                                                                      \
        } else {                                                                                             \
            std::printf("ok: %s\n", msg);                                                                    \
        }                                                                                                    \
    } while (0)

struct Resultado {
    int cuadros = 0, errores = 0, gopsMal = 0, refsMax = 0, maxRefsSps = 0, conMmco = 0, pisadas = 0;
    std::vector<int> pocs;
    const Pps* pps = nullptr;
    std::array<std::unique_ptr<Pps>, 256> ppsTodos{};
};

void leer(const unsigned char* d, size_t n, Resultado& r) {
    std::array<std::unique_ptr<Sps>, 32> sps{};
    std::array<const Sps*, 32> spsP{};
    std::array<const Pps*, 256> ppsP{};
    Dpb dpb;
    std::vector<int> gop;
    int sup = 0;
    const auto cerrar = [&] {
        std::sort(gop.begin(), gop.end());
        for (size_t i = 1; i < gop.size(); ++i)
            if (gop[i] - gop[i - 1] != gop[1] - gop[0]) {
                ++r.gopsMal;
                break;
            }
        gop.clear();
    };
    for (const Nal& x : partirAnnexB(d, n)) {
        const int tipo = d[x.inicio] & 0x1f, ref = (d[x.inicio] >> 5) & 3;
        const std::vector<uint8_t> rb = aRbsp(d + x.inicio + 1, x.largo - 1);
        if (tipo == 7) {
            auto s = leerSps(rb.data(), rb.size());
            if (!s) {
                ++r.errores;
                continue;
            }
            r.maxRefsSps = s->max_num_ref_frames;
            sps[static_cast<size_t>(s->id)] = std::make_unique<Sps>(*s);
            spsP[static_cast<size_t>(s->id)] = sps[static_cast<size_t>(s->id)].get();
        } else if (tipo == 8) {
            auto p = leerPps(rb.data(), rb.size(), spsP.data());
            if (!p) {
                ++r.errores;
                continue;
            }
            r.ppsTodos[static_cast<size_t>(p->id)] = std::make_unique<Pps>(*p);
            ppsP[static_cast<size_t>(p->id)] = r.ppsTodos[static_cast<size_t>(p->id)].get();
            r.pps = ppsP[static_cast<size_t>(p->id)];
        } else if (tipo == 1 || tipo == 5) {
            auto s = leerSlice(rb.data(), rb.size(), ref, tipo, spsP.data(), ppsP.data());
            if (!s) {
                ++r.errores;
                continue;
            }
            if (s->first_mb != 0) continue;
            const Sps& sp = *spsP[static_cast<size_t>(ppsP[static_cast<size_t>(s->pps_id)]->sps_id)];
            if (s->idr) cerrar();
            dpb.empezar(sp, *s);
            r.pocs.push_back(dpb.pocArriba());
            gop.push_back(dpb.pocArriba());
            if (!s->mmco.empty()) ++r.conMmco;
            int c = sup;
            while (dpb.usa(c % 8))
                ++c;
            sup = (c + 1) % 8;
            for (const Ref& rr : dpb.refs())
                if (rr.superficie == c % 8) ++r.pisadas;
            dpb.terminar(c % 8);
            r.refsMax = std::max<int>(r.refsMax, static_cast<int>(dpb.refs().size()));
            ++r.cuadros;
        }
    }
    cerrar();
}

} // namespace

int main() {
    std::printf("== test_h264_sintaxis ==\n");
    {
        Resultado r;
        leer(kBase, sizeof(kBase), r);
        std::printf("baseline: %d cuadros, %d errores, refs máx %d (SPS %d)\n", r.cuadros, r.errores, r.refsMax, r.maxRefsSps);
        CHECK(r.cuadros == 45 && r.errores == 0, "baseline: se leen los 45 cuadros sin errores");
        CHECK(r.gopsMal == 0, "baseline: POC sin huecos");
        CHECK(r.refsMax == 2 && r.maxRefsSps == 2, "baseline: el DPB llega a 2 referencias y no más");
        bool creciente = true;
        for (size_t i = 1; i < r.pocs.size(); ++i)
            if (r.pocs[i] != 0 && r.pocs[i] <= r.pocs[i - 1]) creciente = false;
        CHECK(creciente, "baseline: sin cuadros B, el POC crece cuadro a cuadro");
        CHECK(r.pisadas == 0, "baseline: el cuadro nuevo no pisa una referencia");
    }
    {
        Resultado r;
        leer(kAlto, sizeof(kAlto), r);
        std::printf("high: %d cuadros, %d errores, refs máx %d (SPS %d), %d con MMCO\n", r.cuadros, r.errores, r.refsMax,
                    r.maxRefsSps, r.conMmco);
        CHECK(r.cuadros == 45 && r.errores == 0, "high: se leen los 45 cuadros sin errores");
        CHECK(r.gopsMal == 0, "high: POC sin huecos (los B entre sus P)");
        CHECK(r.refsMax <= r.maxRefsSps && r.maxRefsSps == 4, "high: el DPB no pasa de 4 referencias");
        CHECK(r.conMmco > 0, "high: x264 manda MMCO y se leen");
        int atras = 0;
        for (size_t i = 1; i < r.pocs.size(); ++i)
            if (r.pocs[i] != 0 && r.pocs[i] < r.pocs[i - 1]) ++atras;
        CHECK(atras > 0, "high: hay cuadros B (POC fuera de orden)");
        CHECK(r.pisadas == 0, "high: el cuadro nuevo no pisa una referencia");
        CHECK(r.pps != nullptr && r.pps->transform_8x8_mode && r.pps->entropy_coding_mode && r.pps->weighted_pred,
              "high: PPS con 8x8, CABAC y pesos");
        CHECK(r.pps != nullptr && r.pps->escalado.l4[0][0] == 6 && r.pps->escalado.l4[3][0] == 10 &&
                  r.pps->escalado.l8[0][0] == 6 && r.pps->escalado.l8[1][0] == 9,
              "high: listas de escalado JVT (las de fábrica de la norma)");
    }
    {
        // Bytes de prevención: 00 00 03 01 → 00 00 01.
        const uint8_t e[] = {0x11, 0x00, 0x00, 0x03, 0x01, 0x00, 0x00, 0x03, 0x00, 0x22};
        const auto r = aRbsp(e, sizeof(e));
        CHECK((r == std::vector<uint8_t>{0x11, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x22}), "aRbsp quita los 03");
        const uint8_t a[] = {0, 0, 0, 1, 0x67, 1, 2, 0, 0, 1, 0x68, 3, 0, 0};
        const auto n = partirAnnexB(a, sizeof(a));
        CHECK(n.size() == 2 && n[0].arranque == 0 && n[0].inicio == 4 && n[0].largo == 3 && n[1].inicio == 10 &&
                  n[1].largo == 2,
              "partirAnnexB: códigos de 3 y 4 bytes, sin los ceros del final");
    }
    std::printf(g_fallos == 0 ? "OK\n" : "FALLOS: %d\n", g_fallos);
    return g_fallos == 0 ? 0 : 1;
}
