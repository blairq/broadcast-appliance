// src/core/diag/VigiaBucle.cpp — ver VigiaBucle.hpp

#include "diag/VigiaBucle.hpp"

#include "diag/Registro.hpp"
#include "i18n/I18n.hpp"

#include <algorithm>
#include <chrono>

namespace ba::diag {

namespace {

constexpr std::int64_t kAvisoCadaUs = 10'000'000; ///< un aviso por culpable cada 10 s
constexpr std::size_t kMaxCulpables = 200;        ///< los que se recuerdan (se olvida el que menos sumó)

double ms(std::int64_t us) { return static_cast<double>(us) / 1000.0; }

} // namespace

VigiaBucle::VigiaBucle(Reloj reloj, int periodoMs, int umbralMs)
    : reloj_(std::move(reloj)), periodoUs_(std::int64_t{periodoMs} * 1000), umbralUs_(std::int64_t{umbralMs} * 1000) {
    if (!reloj_)
        reloj_ = []() {
            return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count();
        };
    avisar_ = [](const std::string& texto) { registro().linea(Nivel::Aviso, "interfaz", 0, texto); };
    // Un minuto de vueltas.
    minutoMax_ = static_cast<std::size_t>(60'000'000 / std::max<std::int64_t>(1, periodoUs_));
    minuto_.reserve(minutoMax_);
}

VigiaBucle& VigiaBucle::interfaz() {
    static VigiaBucle v;
    return v;
}

void VigiaBucle::alAvisar(Avisar a) {
    std::lock_guard<std::mutex> l(mu_);
    avisar_ = std::move(a);
}

void VigiaBucle::anotar(const char* que, int detalle) {
    const std::int64_t ahora = reloj_();
    std::lock_guard<std::mutex> l(mu_);
    Anotado& a = anotados_[anotadoPos_];
    anotadoPos_ = (anotadoPos_ + 1) % anotados_.size();
    a.us = ahora;
    a.que = que;
    a.detalle = detalle;
    a.texto.clear(); // sin liberar: el próximo texto reusa la memoria
}

void VigiaBucle::anotar(const std::string& que) {
    const std::int64_t ahora = reloj_();
    std::lock_guard<std::mutex> l(mu_);
    Anotado& a = anotados_[anotadoPos_];
    anotadoPos_ = (anotadoPos_ + 1) % anotados_.size();
    a.us = ahora;
    a.que = nullptr;
    a.detalle = 0;
    a.texto = que;
}

std::string VigiaBucle::quien(std::int64_t hasta) const {
    const Anotado* mejor = nullptr;
    for (const Anotado& a : anotados_)
        if ((a.que != nullptr || !a.texto.empty()) && a.us <= hasta && (mejor == nullptr || a.us > mejor->us)) mejor = &a;
    if (mejor == nullptr) return "?";
    if (!mejor->texto.empty()) return mejor->texto;
    // El tipo de evento de Qt va como número (QEvent::Type): nombrarlo en cada evento costaría.
    return mejor->detalle != 0 ? std::string(mejor->que) + ", evento " + std::to_string(mejor->detalle)
                               : std::string(mejor->que);
}

void VigiaBucle::tic() {
    const std::int64_t ahora = reloj_();
    std::string aviso;
    Avisar avisar;
    {
        std::lock_guard<std::mutex> l(mu_);
        if (anteriorUs_ < 0) {
            anteriorUs_ = ahora;
            return;
        }
        const std::int64_t tocaba = anteriorUs_ + periodoUs_;
        const std::int64_t retraso = std::max<std::int64_t>(0, ahora - tocaba);
        anteriorUs_ = ahora;
        ++vueltas_;
        std::size_t i = 0;
        while (i < kBordesMs.size() && retraso >= std::int64_t{kBordesMs[i]} * 1000) ++i;
        ++histo_[i];
        if (minuto_.size() < minutoMax_) {
            minuto_.push_back(retraso);
        } else if (!minuto_.empty()) {
            minuto_[minutoPos_] = retraso;
            minutoPos_ = (minutoPos_ + 1) % minuto_.size();
        }
        if (retraso <= umbralUs_) return;

        ++tarde_;
        const std::string que = quien(tocaba);
        ultimoTardeUs_ = retraso;
        ultimoTardeCuandoUs_ = ahora;
        ultimoTardeQue_ = que;
        if (culpables_.size() >= kMaxCulpables && culpables_.find(que) == culpables_.end()) {
            auto menor = std::min_element(culpables_.begin(), culpables_.end(), [](const auto& a, const auto& b) {
                return a.second.totalUs < b.second.totalUs;
            });
            culpables_.erase(menor);
        }
        Culpable& c = culpables_[que];
        ++c.veces;
        c.totalUs += retraso;
        c.maxUs = std::max(c.maxUs, retraso);
        if (c.avisadoUs >= 0 && ahora - c.avisadoUs < kAvisoCadaUs) return;
        c.avisadoUs = ahora;
        aviso = ba::i18n::llenar(ba::i18n::traducir("diag", "el bucle de la interfaz se demoró %1 ms (corría: %2)"),
                                 {std::to_string((retraso + 500) / 1000), que});
        avisar = avisar_;
    }
    if (avisar) avisar(aviso); // fuera del candado: el registro puede tardar
}

nlohmann::json VigiaBucle::informe() const {
    using nlohmann::json;
    std::lock_guard<std::mutex> l(mu_);
    json histo = json::array();
    for (std::size_t i = 0; i < histo_.size(); ++i) {
        const int desde = i == 0 ? 0 : kBordesMs[i - 1];
        json h = {{"desde_ms", desde}, {"veces", histo_[i]}};
        if (i < kBordesMs.size()) h["hasta_ms"] = kBordesMs[i];
        histo.push_back(std::move(h));
    }
    json minuto = {{"muestras", minuto_.size()}};
    if (!minuto_.empty()) {
        std::vector<std::int64_t> v = minuto_;
        const auto en = [&v](double q) {
            const auto k = static_cast<std::size_t>(q * static_cast<double>(v.size() - 1) + 0.5);
            std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
            return ms(v[k]);
        };
        minuto["p50_ms"] = en(0.50);
        minuto["p99_ms"] = en(0.99);
        minuto["max_ms"] = ms(*std::max_element(v.begin(), v.end()));
    }
    std::vector<std::pair<std::string, Culpable>> orden(culpables_.begin(), culpables_.end());
    std::sort(orden.begin(), orden.end(), [](const auto& a, const auto& b) { return a.second.totalUs > b.second.totalUs; });
    if (orden.size() > 10) orden.resize(10);
    json culpables = json::array();
    for (const auto& [que, c] : orden)
        culpables.push_back({{"que", que}, {"veces", c.veces}, {"total_ms", ms(c.totalUs)}, {"max_ms", ms(c.maxUs)}});
    json out = {{"periodo_ms", periodoUs_ / 1000},
                {"umbral_ms", umbralUs_ / 1000},
                {"vueltas", vueltas_},
                {"tarde", tarde_},
                {"histograma", std::move(histo)},
                {"ultimo_minuto", std::move(minuto)},
                {"culpables", std::move(culpables)}};
    if (ultimoTardeCuandoUs_ >= 0)
        out["ultimo_tarde"] = {{"ms", ms(ultimoTardeUs_)},
                               {"que", ultimoTardeQue_},
                               {"hace_s", static_cast<double>(reloj_() - ultimoTardeCuandoUs_) / 1e6}};
    return out;
}

} // namespace ba::diag
