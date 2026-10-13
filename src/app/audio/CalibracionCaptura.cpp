// src/app/audio/CalibracionCaptura.cpp — de lo grabado a la medición (ver Calibracion.hpp)
//
// Aparte del modelo: sin Qt ni racks, para que la prueba (test_calibracion) lo use
// con la sala simulada.
#include "audio/Calibracion.hpp"

#include "audio/CalibracionAnalisis.hpp"
#include "audio/LspDsp.hpp"

#include <algorithm>
#include <cmath>

namespace ba::app::audio {

using json = nlohmann::json;

namespace {
constexpr float kAntesMs = 2.0f; // la IR guardada empieza 2 ms antes del directo
const std::vector<float> kCentros = {63, 125, 250, 500, 1000, 2000, 4000, 8000};

json aRedondo(const std::vector<float>& v, double k) {
    json a = json::array();
    for (float x : v) a.push_back(std::round(static_cast<double>(x) * k) / k);
    return a;
}
} // namespace

json Calibracion::procesarCaptura(const lspdsp::Api* L, ba_medicion* m, int sr, const std::vector<float>& cap,
                                  std::vector<float>* irOut) {
    if (L == nullptr || m == nullptr) return {{"error", "falta ba-lspdsp"}};
    if (cap.empty()) return {{"error", "no se grabó nada"}};
    // La misma medición, ahora con lo grabado en la entrada (el estímulo es el mismo).
    L->medicionArrancar(m);
    std::vector<float> basura(1024), cero(1024, 0.0f);
    const size_t total = L->medicionDuracionMuestras(m) + static_cast<size_t>(sr);
    for (size_t i = 0; i < total && !L->medicionTerminada(m); i += 1024) {
        const size_t n = 1024;
        const float* in = i + n <= cap.size() ? cap.data() + i : nullptr;
        std::vector<float> parte;
        if (in == nullptr) {
            parte.assign(n, 0.0f);
            for (size_t k = 0; i + k < cap.size() && k < n; ++k) parte[k] = cap[i + k];
            in = parte.data();
        }
        L->medicionProcesar(m, basura.data(), in, n);
    }
    if (!L->medicionTerminada(m)) return {{"error", "la captura no alcanzó para terminar la medición"}};
    ba_medicion_resultado r{};
    if (L->medicionAnalizar(m, BA_RT_T20, &r) != 0) return {{"error", "falló la deconvolución"}};
    json j;
    j["directo_ms"] = std::round(r.directo_ms * 100.0) / 100.0;
    j["pico_db"] = std::round(r.pico_db * 10.0) / 10.0;
    j["snr_db"] = std::round(r.snr_db * 10.0) / 10.0;
    j["rt60_s"] = std::round(r.rt60_s * 1000.0) / 1000.0;
    j["correlacion"] = std::round(r.correlacion * 1000.0) / 1000.0;
    j["limite_integracion_s"] = std::round(r.limite_integracion_s * 1000.0) / 1000.0;
    j["ruido_suficiente"] = r.ruido_suficiente != 0;
    // La IR desde 2 ms antes del directo (1 s), normalizada al directo.
    std::vector<float> ir(static_cast<size_t>(sr));
    ir.resize(L->medicionIr(m, ir.data(), ir.size(), kAntesMs));
    const size_t pre = static_cast<size_t>(kAntesMs * 0.001f * sr);
    j["polaridad"] = ir.size() > pre && ir[pre] < 0 ? -1 : 1;
    // La respuesta en frecuencia sobre la grilla (ventana de 500 ms: con la sala).
    const auto& g = calib::grilla();
    std::vector<float> frq(g.size()), db(g.size());
    if (L->medicionRespuesta(m, static_cast<float>(g.front()), static_cast<float>(g.back()), g.size(), 500.0f,
                             frq.data(), db.data()) == 0)
        j["curva"] = aRedondo(db, 100);
    // RT por banda.
    std::vector<float> edt(kCentros.size()), t20(kCentros.size()), t30(kCentros.size());
    L->medicionRtBandas(m, kCentros.data(), kCentros.size(), edt.data(), t20.data(), t30.data());
    j["rt_bandas"] = {{"centros", kCentros}, {"edt", aRedondo(edt, 1000)}, {"t20", aRedondo(t20, 1000)}, {"t30", aRedondo(t30, 1000)}};
    // La curva energía-tiempo (dB, de a 1 ms, 300 ms) y la IR para dibujar (100 ms, 400
    // puntos: el máximo con su signo de cada tramo).
    std::vector<float> etc, vista;
    double emax = 1e-30;
    std::vector<double> e;
    const size_t paso = static_cast<size_t>(sr / 1000);
    for (size_t i = 0; i + paso <= ir.size() && e.size() < 300; i += paso) {
        double s = 0;
        for (size_t k = 0; k < paso; ++k) s += static_cast<double>(ir[i + k]) * ir[i + k];
        e.push_back(s / paso);
        emax = std::max(emax, s / paso);
    }
    for (double v : e) etc.push_back(static_cast<float>(std::max(-100.0, 10 * std::log10(v / emax + 1e-30))));
    const size_t tramo = std::max<size_t>(1, static_cast<size_t>(sr / 10) / 400);
    for (size_t i = 0; i + tramo <= ir.size() && vista.size() < 400; i += tramo) {
        float mx = 0;
        for (size_t k = 0; k < tramo; ++k)
            if (std::abs(ir[i + k]) > std::abs(mx)) mx = ir[i + k];
        vista.push_back(mx);
    }
    j["etc"] = aRedondo(etc, 10);
    j["ir_vista"] = aRedondo(vista, 1000);
    // C50 (claridad de la palabra): energía de los primeros 50 ms contra el resto.
    double e50 = 0, eResto = 0;
    for (size_t i = 0; i < ir.size(); ++i) (i < pre + static_cast<size_t>(0.05 * sr) ? e50 : eResto) += static_cast<double>(ir[i]) * ir[i];
    j["c50_db"] = eResto > 0 ? std::round(10 * std::log10(e50 / eResto) * 10) / 10 : 99.0;
    if (irOut != nullptr) *irOut = std::move(ir);
    return j;
}

} // namespace ba::app::audio
