// ============================================================================
// src/motor/AnalisisCeldas.hpp — cuántos cuadros NUEVOS muestra cada celda de un mosaico
//
// La grilla de personas la pinta el gateway (src/gateway/Grillas.hpp): a la app llega UN
// video (BA.Grilla.<k>) con todas las caras. Que el canal llegue a 25 o 30 cps no dice
// nada de cada cara: una celda puede repetir su cuadro mientras las otras cambian. Esto
// lo mide desde los píxeles, celda por celda: en cada cuadro del mosaico se toma una
// firma de cada celda (una red de puntos del centro de la celda, lejos del rótulo de
// abajo y del borde del resaltado); si la firma cambió, la celda mostró un cuadro nuevo.
// De ahí, por celda: los cuadros nuevos por segundo, los intervalos entre uno y otro
// (p50/p95/máx: el ritmo, lo que se ve como «no fluido») y los huecos (intervalos de más
// de 1,5 veces el período de lo que manda su capa). Del mosaico entero: los cuadros que
// llegaron, sus intervalos y los que el emisor publicó y no se leyeron (el número de
// cuadro del canal, si viene: GST_BUFFER_OFFSET en bavideoanillocomun).
//
// Puro (sin GStreamer): lo usa MedidorCeldas (una sonda en un pad) y la vista del
// programa (Motor::verAvisar). No toma candados: quien lo usa desde dos hilos lo protege.
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ba::motor {

class AnalisisCeldas {
   public:
    /// Un lugar del mosaico, en fracciones del cuadro (0..1). `referenciaCps`: lo que manda
    /// su fuente (la capa de la cámara); 0 = no se sabe (sin huecos ni «por debajo»).
    struct Celda {
        std::string id;
        std::string capa;
        double x = 0, y = 0, w = 0, h = 0;
        double referenciaCps = 0;
    };

    /// La red de puntos de cada celda (más puntos, menos chance de no ver un cambio chico).
    static constexpr int kPuntosX = 12, kPuntosY = 8;

    explicit AnalisisCeldas(std::vector<Celda> celdas = {}) : celdas_(std::move(celdas)), estado_(celdas_.size()) {}

    const std::vector<Celda>& celdas() const { return celdas_; }

    /// La firma de la zona (fracciones) de un cuadro de 4 bytes por píxel. Pública para las pruebas.
    static std::uint64_t firma(const std::uint8_t* px, int w, int h, int stride, double x, double y, double cw, double ch) {
        std::uint64_t f = 1469598103934665603ULL; // FNV-1a
        if (px == nullptr || w <= 0 || h <= 0) return f;
        for (int j = 0; j < kPuntosY; ++j) {
            // Del 10 % al 65 % del alto: el rótulo va abajo (o arriba, chico) y el resaltado es un borde.
            const double fy = y + ch * (0.12 + 0.53 * j / (kPuntosY - 1));
            const int py = std::clamp(static_cast<int>(fy * h), 0, h - 1);
            const std::uint8_t* fila = px + static_cast<std::size_t>(py) * static_cast<std::size_t>(stride);
            for (int i = 0; i < kPuntosX; ++i) {
                const double fx = x + cw * (0.12 + 0.76 * i / (kPuntosX - 1));
                const int pxi = std::clamp(static_cast<int>(fx * w), 0, w - 1);
                const std::uint8_t* p = fila + static_cast<std::size_t>(pxi) * 4;
                for (int k = 0; k < 4; ++k) {
                    f ^= p[k];
                    f *= 1099511628211ULL;
                }
            }
        }
        return f;
    }

    /// Un cuadro del mosaico (BGRA o cualquier formato de 4 bytes por píxel), llegado a
    /// `tNs` (reloj monotónico). `indice`: su número en el canal del emisor (0 = no se sabe).
    void cuadro(const std::uint8_t* px, int w, int h, int stride, std::int64_t tNs, std::uint64_t indice = 0) {
        if (px == nullptr || w <= 0 || h <= 0 || stride < w * 4) return;
        if (cuadros_ == 0) primeroNs_ = tNs;
        else llegadas_.push_back(static_cast<double>(tNs - ultimoNs_) / 1e6);
        ultimoNs_ = tNs;
        ++cuadros_;
        if (indice != 0) {
            if (ultimoIndice_ != 0 && indice > ultimoIndice_ + 1) perdidos_ += indice - ultimoIndice_ - 1;
            if (ultimoIndice_ != 0 && indice == ultimoIndice_) ++repetidosCanal_;
            ultimoIndice_ = indice;
        }
        for (std::size_t i = 0; i < celdas_.size(); ++i) {
            const Celda& c = celdas_[i];
            Estado& e = estado_[i];
            const std::uint64_t f = firma(px, w, h, stride, c.x, c.y, c.w, c.h);
            if (e.vista && f == e.firma) {
                ++e.repetidos;
                continue;
            }
            if (e.vista) {
                ++e.nuevos;
                if (e.cambioNs != 0) e.intervalos.push_back(static_cast<double>(tNs - e.cambioNs) / 1e6);
                e.cambioNs = tNs;
            }
            e.vista = true;
            e.firma = f;
        }
    }

    std::uint64_t cuadros() const { return cuadros_; }

    /// Percentil `p` (0..1) de `v` (se ordena una copia); NaN si está vacío.
    static double percentil(std::vector<double> v, double p) {
        if (v.empty()) return std::nan("");
        std::sort(v.begin(), v.end());
        const std::size_t i = std::min(v.size() - 1, static_cast<std::size_t>(std::lround(p * static_cast<double>(v.size() - 1))));
        return v[i];
    }

    /// {segundos, cuadros, cps, llegada_ms {p50,p95,max}, perdidos, repetidos,
    ///  celdas: [{id, capa, referencia_cps, cps, nuevos, repetidos, intervalo_ms {p50,p95,max},
    ///  huecos, por_debajo}], resumen: {cps_p50, cps_p5, por_debajo_pct}}.
    nlohmann::json resumen() const {
        using json = nlohmann::json;
        const double seg = cuadros_ > 1 ? static_cast<double>(ultimoNs_ - primeroNs_) / 1e9 : 0.0;
        const auto ms = [](const std::vector<double>& v) {
            if (v.empty()) return json(nullptr);
            const auto r = [](double x) { return std::round(x * 10) / 10; };
            return json{{"p50", r(percentil(v, 0.5))}, {"p95", r(percentil(v, 0.95))}, {"max", r(*std::max_element(v.begin(), v.end()))}};
        };
        const auto r2 = [](double x) { return std::round(x * 100) / 100; };
        json out{{"segundos", r2(seg)},
                 {"cuadros", cuadros_},
                 {"cps", seg > 0 ? r2(static_cast<double>(cuadros_ - 1) / seg) : 0.0},
                 {"llegada_ms", ms(llegadas_)},
                 {"perdidos", perdidos_},
                 {"repetidos", repetidosCanal_}};
        json celdas = json::array();
        std::vector<double> cps;
        int debajo = 0, conRef = 0;
        for (std::size_t i = 0; i < celdas_.size(); ++i) {
            const Celda& c = celdas_[i];
            const Estado& e = estado_[i];
            // Los cuadros nuevos sobre el tiempo entre el primero que se vio y el último.
            const double f = seg > 0 ? static_cast<double>(e.nuevos) / seg : 0.0;
            cps.push_back(f);
            int huecos = 0;
            if (c.referenciaCps > 0)
                for (double x : e.intervalos)
                    if (x > 1.5 * 1000.0 / c.referenciaCps) ++huecos;
            const bool bajo = c.referenciaCps > 0 && f < 0.9 * c.referenciaCps;
            if (c.referenciaCps > 0) {
                ++conRef;
                if (bajo) ++debajo;
            }
            json x{{"id", c.id},           {"capa", c.capa},        {"referencia_cps", c.referenciaCps},
                   {"cps", r2(f)},         {"nuevos", e.nuevos},     {"repetidos", e.repetidos},
                   {"intervalo_ms", ms(e.intervalos)}, {"huecos", huecos}, {"por_debajo", bajo}};
            celdas.push_back(std::move(x));
        }
        out["celdas"] = std::move(celdas);
        out["resumen"] = {{"celdas", celdas_.size()},
                          {"cps_p50", cps.empty() ? 0.0 : r2(percentil(cps, 0.5))},
                          {"cps_p5", cps.empty() ? 0.0 : r2(percentil(cps, 0.05))},
                          {"por_debajo_pct", conRef > 0 ? std::round(1000.0 * debajo / conRef) / 10 : 0.0}};
        return out;
    }

   private:
    struct Estado {
        bool vista = false;
        std::uint64_t firma = 0;
        std::int64_t cambioNs = 0;
        std::uint64_t nuevos = 0, repetidos = 0;
        std::vector<double> intervalos; ///< ms entre un cuadro nuevo y el anterior
    };
    std::vector<Celda> celdas_;
    std::vector<Estado> estado_;
    std::uint64_t cuadros_ = 0, perdidos_ = 0, repetidosCanal_ = 0, ultimoIndice_ = 0;
    std::int64_t primeroNs_ = 0, ultimoNs_ = 0;
    std::vector<double> llegadas_;
};

} // namespace ba::motor
