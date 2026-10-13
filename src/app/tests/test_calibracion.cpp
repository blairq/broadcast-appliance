// test_calibracion — la calibración de sala de punta a punta, en una sala simulada
// (no suena nada): barrido de ba-lspdsp → sala de mentira → medición → análisis →
// propuesta → la sala con el EQ puesto → verificación.
//
// La sala: 7 × 5 × 3 m, RT60 0,6 s, dos parlantes adelante, cinco puntos de escucha.
#include "audio/Calibracion.hpp"
#include "audio/CalibracionAnalisis.hpp"
#include "audio/LazoMedicion.hpp"
#include "audio/LspDsp.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using json = nlohmann::json;
namespace calib = ba::app::audio::calib;

static int fallas = 0;
static void check(bool ok, const char* que, double v) {
    std::printf("%s: %s (%.3f)\n", ok ? "ok" : "FALLA", que, v);
    if (!ok) ++fallas;
}

// Los filtros de la propuesta, en el tiempo (RBJ, como respuestaFiltro).
static void filtrar(std::vector<float>& x, const json& f, double sr) {
    const double f0 = f["f"].get<double>(), g = f["ganancia_db"].get<double>(), q = f["q"].get<double>();
    const double A = std::pow(10.0, g / 40.0), w0 = 2 * M_PI * f0 / sr, cw = std::cos(w0), al = std::sin(w0) / (2 * q);
    const double a0 = 1 + al / A;
    const double b0 = (1 + al * A) / a0, b1 = -2 * cw / a0, b2 = (1 - al * A) / a0, a1 = -2 * cw / a0, a2 = (1 - al / A) / a0;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (float& v : x) {
        const double y = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = v;
        y2 = y1;
        y1 = y;
        v = static_cast<float>(y);
    }
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const auto* L = ba::app::audio::lspdsp::api();
    if (L == nullptr) {
        std::printf("SKIP: %s\n", ba::app::audio::lspdsp::error().c_str());
        return 77;
    }
    const int sr = 48000;
    json s = {{"id", "prueba"},
              {"sala", {{"largo", 7.0}, {"ancho", 5.0}, {"alto", 3.0}}},
              {"objetivo", {{"pendiente_db_oct", -0.8}}},
              {"parlantes",
               {{{"id", "p1"}, {"nombre", "Izquierdo"}, {"x", 0.5}, {"y", 1.2}, {"z", 1.8}},
                {{"id", "p2"}, {"nombre", "Derecho"}, {"x", 0.5}, {"y", 3.8}, {"z", 1.8}}}},
              {"microfonos", {{{"id", "mic1"}, {"nombre", "Medición"}, {"calibracion", {{"tipo", "plano"}}}}}},
              {"puntos",
               {{{"id", "pt1"}, {"tipo", "escucha"}, {"x", 3.1}, {"y", 2.3}, {"z", 1.2}},
                {{"id", "pt2"}, {"tipo", "escucha"}, {"x", 3.9}, {"y", 1.6}, {"z", 1.2}},
                {{"id", "pt3"}, {"tipo", "escucha"}, {"x", 4.6}, {"y", 3.1}, {"z", 1.1}},
                {{"id", "pt4"}, {"tipo", "escucha"}, {"x", 5.3}, {"y", 2.0}, {"z", 1.3}},
                {{"id", "pt5"}, {"tipo", "escucha"}, {"x", 3.5}, {"y", 3.6}, {"z", 1.2}}}},
              {"mediciones", json::array()}};
    json sala = s["sala"];
    sala["rt60"] = 0.6;

    int n = 0;
    // Mide un parlante en un punto, con (o sin) filtros antes de la sala.
    const auto medir = [&](const json& pa, const json& pt, const std::string& etapa, const json& filtros) {
        ba_medicion* m = L->medicionCrear(sr, 3.0f, 20.0f, 20000.0f, 0.1f, 0.0f);
        std::vector<float> est(L->medicionDuracionMuestras(m)), cero(1024, 0.0f);
        L->medicionArrancar(m);
        for (size_t i = 0; i < est.size(); i += 1024)
            L->medicionProcesar(m, est.data() + i, cero.data(), std::min<size_t>(1024, est.size() - i));
        for (const auto& f : filtros) filtrar(est, f, sr);
        std::vector<float> cap = ba::app::audio::simularSala(sala, pa, pt, est, sr);
        cap.resize(cap.size() + sr, 0.0f);
        json r = ba::app::audio::Calibracion::procesarCaptura(L, m, sr, cap, nullptr);
        L->medicionLiberar(m);
        r["id"] = "m" + std::to_string(++n);
        r["parlante"] = pa["id"];
        r["punto"] = pt["id"];
        r["microfono"] = "mic1";
        r["etapa"] = etapa;
        return r;
    };

    for (const auto& pa : s["parlantes"])
        for (const auto& pt : s["puntos"]) {
            const json r = medir(pa, pt, "antes", json::array());
            if (r.contains("error")) {
                check(false, r["error"].get<std::string>().c_str(), 0);
                return 1;
            }
            const double d = std::sqrt(std::pow(pa["x"].get<double>() - pt["x"].get<double>(), 2) +
                                       std::pow(pa["y"].get<double>() - pt["y"].get<double>(), 2) +
                                       std::pow(pa["z"].get<double>() - pt["z"].get<double>(), 2));
            const double esperado = d / 343.0 * 1000.0 + 4.0; // + la latencia de la "placa"
            if (true)
                std::printf("    %s→%s: directo %.2f ms (esperado %.2f), snr %.0f dB, C50 %.1f dB, polaridad %d\n",
                            pa["id"].get<std::string>().c_str(), pt["id"].get<std::string>().c_str(),
                            r["directo_ms"].get<double>(), esperado, r["snr_db"].get<double>(), r["c50_db"].get<double>(),
                            r["polaridad"].get<int>());
            if (std::abs(r["directo_ms"].get<double>() - esperado) > 0.3) check(false, "llegada del directo", r["directo_ms"].get<double>() - esperado);
            if (r["snr_db"].get<double>() < 40) check(false, "relación señal/ruido", r["snr_db"].get<double>());
            s["mediciones"].push_back(r);
        }
    check(fallas == 0, "las 10 llegadas a ±0,3 ms de la geometría y SNR > 40 dB", 0);

    const json a = calib::analizar(s);
    std::printf("    fs %.0f Hz, RT60 medio %.3f s\n", a["schroeder_hz"].get<double>(), a["rt60_medio_s"].get<double>());
    for (const auto& b : a["rt_bandas"])
        std::printf("    %5.0f Hz: T20 %.3f  EDT %.3f\n", b["hz"].get<double>(), b["t20_s"].get<double>(), b["edt_s"].get<double>());
    check(std::abs(a["rt60_medio_s"].get<double>() - 0.6) < 0.15, "RT60 medio (500 Hz-1 kHz) cerca de 0,6 s", a["rt60_medio_s"].get<double>());
    // fs = 2000·√(0,6/105) ≈ 151 Hz
    check(a["schroeder_hz"].get<double>() > 110 && a["schroeder_hz"].get<double>() < 200, "frecuencia de Schroeder", a["schroeder_hz"].get<double>());

    const json& a1 = a["parlantes"]["p1"];
    int confirmados = 0, conTeorico = 0;
    for (const auto& m : a1["modos"]) {
        std::printf("    modo %.1f Hz +%.1f dB Q %.1f en %d/%d %s%s\n", m["f"].get<double>(), m["exceso_db"].get<double>(),
                    m["q"].get<double>(), m["en_puntos"].get<int>(), m["de_puntos"].get<int>(),
                    m["confirmado"].get<bool>() ? "confirmado" : "", m["teoricos"].empty() ? "" : (" ← " + m["teoricos"][0]["de"].get<std::string>()).c_str());
        if (m["confirmado"].get<bool>()) {
            ++confirmados;
            if (!m["teoricos"].empty()) ++conTeorico;
        }
    }
    check(confirmados >= 1, "modos confirmados en la mayoría de los puntos", confirmados);
    check(conTeorico == confirmados, "cada modo confirmado coincide con un modo teórico de la sala", conTeorico);

    const json p = calib::proponer(s, a);
    bool limites = true;
    for (const auto& pid : {"p1", "p2"})
        for (const auto& f : p["parlantes"][pid]["filtros"]) {
            std::printf("    %s: %.0f Hz %+.1f dB Q %.1f — %s\n", pid, f["f"].get<double>(), f["ganancia_db"].get<double>(),
                        f["q"].get<double>(), f["motivo"].get<std::string>().c_str());
            const double fr = f["f"].get<double>(), g = f["ganancia_db"].get<double>();
            if (fr < a["schroeder_hz"].get<double>() && g > 0) limites = false; // bajo fs solo cortes
            if (g > 3.0 || g < -12.0) limites = false;
        }
    check(limites, "bajo fs solo cortes; nada más de +3 dB", 0);
    check(!p["parlantes"]["p1"]["filtros"].empty(), "hay filtros para el izquierdo", p["parlantes"]["p1"]["filtros"].size());
    // Los dos parlantes están a la misma distancia de la zona en promedio (simétricos en
    // y respecto de 2,5 m... casi): el retardo propuesto es chico.
    std::printf("    retardos: p1 %.2f ms, p2 %.2f ms; niveles: p1 %+.1f, p2 %+.1f dB\n",
                p["parlantes"]["p1"]["retardo_ms"].get<double>(), p["parlantes"]["p2"]["retardo_ms"].get<double>(),
                p["parlantes"]["p1"]["ganancia_db"].get<double>(), p["parlantes"]["p2"]["ganancia_db"].get<double>());
    check(std::min(p["parlantes"]["p1"]["retardo_ms"].get<double>(), p["parlantes"]["p2"]["retardo_ms"].get<double>()) == 0,
          "el que llega último no se retarda", 0);

    // La verificación: la sala con el EQ del izquierdo puesto.
    for (const auto& pt : s["puntos"]) s["mediciones"].push_back(medir(s["parlantes"][0], pt, "verificacion", p["parlantes"]["p1"]["filtros"]));
    const json v = calib::verificar(s, calib::analizar(s));
    std::printf("    p1: desvío antes %.2f dB, después %.2f dB → %s\n", v["p1"]["desvio_antes_db"].get<double>(),
                v["p1"]["desvio_despues_db"].get<double>(), v["p1"]["veredicto"].get<std::string>().c_str());
    check(v["p1"]["veredicto"] == "mejoró", "con el EQ la zona queda más cerca del objetivo", v["p1"]["desvio_despues_db"].get<double>());

    // El micrófono: curva del modelo y archivo de calibración.
    const auto t = calib::leerArchivoCalibracion("\"Sens Factor =-1.2dB, SERNO: 700\"\n20 -2.1\n100\t-0.3\n1000 0\n10000 1.5\n20000 -3\n");
    check(t.size() == 5 && std::abs(calib::interpolar(t, 10000) - 1.5) < 1e-9, "lee un archivo de calibración de miniDSP", t.size());
    const auto c58 = calib::curvaMicrofono({{"calibracion", {{"tipo", "modelo"}, {"modelo", "sm58"}}}});
    check(c58[calib::indiceDe(5000)] > 4 && c58[calib::indiceDe(60)] < -6, "la curva del SM58: presencia y caída en graves", c58[calib::indiceDe(5000)]);

    std::printf(fallas ? "\n%d fallas\n" : "\ntodo ok\n", fallas);
    return fallas ? 1 : 0;
}
