// src/core/diag/Metricas.cpp — ver Metricas.hpp

#include "diag/Metricas.hpp"

#include "i18n/I18n.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace ba::diag {

namespace {

/// Un número como lo lee Prometheus (y una persona): enteros sin decimales.
std::string numeroTexto(double v) {
    if (std::isnan(v)) return "NaN";
    if (std::isinf(v)) return v > 0 ? "+Inf" : "-Inf";
    char b[64];
    if (v == std::floor(v) && std::fabs(v) < 1e15)
        std::snprintf(b, sizeof(b), "%.0f", v);
    else
        std::snprintf(b, sizeof(b), "%.6g", v);
    return b;
}

/// La ayuda en una sola línea (el formato de texto no admite saltos).
std::string ayudaTexto(const char* ayuda) {
    std::string a = ayuda != nullptr && *ayuda != '\0' ? ba::i18n::traducir("metricas", ayuda) : std::string();
    std::string out;
    out.reserve(a.size());
    for (char c : a) {
        if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

const char* nombreTipo(Metricas::Tipo t) {
    switch (t) {
        case Metricas::Tipo::Contador: return "counter";
        case Metricas::Tipo::Medidor: return "gauge";
        case Metricas::Tipo::Histograma: return "histogram";
    }
    return "untyped";
}

nlohmann::json etiquetasJson(const Etiquetas& e) {
    nlohmann::json j = nlohmann::json::object();
    for (const auto& [k, v] : e) j[k] = v;
    return j;
}

double redondo(double v, int dec) {
    const double f = std::pow(10.0, dec);
    return std::round(v * f) / f;
}

} // namespace

// --- Medidor -------------------------------------------------------------------------

void Medidor::desconocido() { v_.store(std::numeric_limits<double>::quiet_NaN(), std::memory_order_relaxed); }

bool Medidor::conocido() const { return !std::isnan(valor()); }

// --- Histograma ----------------------------------------------------------------------

Histograma::Histograma(std::vector<double> bordes) : bordes_(std::move(bordes)) {
    std::sort(bordes_.begin(), bordes_.end());
    bordes_.erase(std::unique(bordes_.begin(), bordes_.end()), bordes_.end());
    casilleros_ = std::make_unique<std::atomic<std::uint64_t>[]>(bordes_.size() + 1);
    for (std::size_t i = 0; i <= bordes_.size(); ++i) casilleros_[i].store(0);
}

const std::vector<double>& Histograma::bordesMs() {
    static const std::vector<double> b{1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000};
    return b;
}

void Histograma::observar(double v) {
    if (std::isnan(v)) return;
    // Pocos casilleros: la búsqueda binaria no gana nada contra recorrerlos.
    std::size_t i = 0;
    while (i < bordes_.size() && v > bordes_[i]) ++i;
    casilleros_[i].fetch_add(1, std::memory_order_relaxed);
    cuenta_.fetch_add(1, std::memory_order_relaxed);
    // La suma con un CAS: fetch_add de un double no está en todas las bibliotecas.
    double s = suma_.load(std::memory_order_relaxed);
    while (!suma_.compare_exchange_weak(s, s + v, std::memory_order_relaxed)) {
    }
}

std::vector<std::uint64_t> Histograma::casilleros() const {
    std::vector<std::uint64_t> out(bordes_.size() + 1);
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = casilleros_[i].load(std::memory_order_relaxed);
    return out;
}

double Histograma::percentil(double p) const {
    const std::vector<std::uint64_t> c = casilleros();
    std::uint64_t total = 0;
    for (const std::uint64_t n : c) total += n;
    if (total == 0) return -1;
    const double objetivo = std::clamp(p, 0.0, 1.0) * static_cast<double>(total);
    std::uint64_t acum = 0;
    for (std::size_t i = 0; i < c.size(); ++i) {
        if (c[i] == 0) continue;
        if (static_cast<double>(acum + c[i]) >= objetivo) {
            // El último casillero no tiene tope: se dice su piso (lo que se sabe seguro).
            if (i == bordes_.size()) return bordes_.empty() ? 0.0 : bordes_.back();
            const double piso = i == 0 ? 0.0 : bordes_[i - 1];
            const double frac = (objetivo - static_cast<double>(acum)) / static_cast<double>(c[i]);
            return piso + (bordes_[i] - piso) * std::clamp(frac, 0.0, 1.0);
        }
        acum += c[i];
    }
    return bordes_.empty() ? 0.0 : bordes_.back();
}

// --- Metricas ------------------------------------------------------------------------

Metricas& Metricas::proceso() {
    static auto* m = new Metricas; // vive hasta el final: la usan hilos que se apagan tarde
    return *m;
}

std::string Metricas::etiquetasATexto(const Etiquetas& e) {
    std::string out;
    for (const auto& [k, v] : e) {
        if (!out.empty()) out += ',';
        out += k;
        out += "=\"";
        for (char c : v) {
            if (c == '\\' || c == '"') out += '\\';
            if (c == '\n') {
                out += "\\n";
                continue;
            }
            out += c;
        }
        out += '"';
    }
    return out;
}

Metricas::Serie& Metricas::serie(const std::string& nombre, Tipo t, const char* ayuda, const Etiquetas& e,
                                 const std::vector<double>* bordes) {
    std::lock_guard<std::mutex> l(mu_);
    auto [f, nueva] = familias_.try_emplace(nombre);
    if (nueva) {
        f->second.tipo = t;
        f->second.ayuda = ayuda != nullptr ? ayuda : "";
    }
    // El mismo nombre con otro tipo es un error de quien lo pide: va a una serie
    // suelta (no rompe el texto exportado con dos tipos para un nombre).
    if (f->second.tipo != t) {
        static std::mutex sueltasMu;
        static std::map<std::string, Serie> sueltas;
        std::lock_guard<std::mutex> ls(sueltasMu);
        Serie& s = sueltas[nombre + "#" + std::to_string(static_cast<int>(t)) + etiquetasATexto(e)];
        if (!s.c) s.c = std::make_unique<Contador>();
        if (!s.m) s.m = std::make_unique<Medidor>();
        if (!s.h) s.h = std::make_unique<Histograma>(bordes != nullptr ? *bordes : Histograma::bordesMs());
        return s;
    }
    auto [s, creada] = f->second.series.try_emplace(etiquetasATexto(e));
    if (creada) {
        s->second.etiquetas = e;
        switch (t) {
            case Tipo::Contador: s->second.c = std::make_unique<Contador>(); break;
            case Tipo::Medidor:
                s->second.m = std::make_unique<Medidor>();
                s->second.m->desconocido(); // hasta que alguien lo ponga
                break;
            case Tipo::Histograma:
                s->second.h = std::make_unique<Histograma>(bordes != nullptr ? *bordes : Histograma::bordesMs());
                break;
        }
    }
    return s->second;
}

Contador& Metricas::contador(const std::string& nombre, const char* ayuda, const Etiquetas& e) {
    return *serie(nombre, Tipo::Contador, ayuda, e, nullptr).c;
}

Medidor& Metricas::medidor(const std::string& nombre, const char* ayuda, const Etiquetas& e) {
    return *serie(nombre, Tipo::Medidor, ayuda, e, nullptr).m;
}

Histograma& Metricas::histograma(const std::string& nombre, const char* ayuda, const Etiquetas& e,
                                 const std::vector<double>& bordes) {
    return *serie(nombre, Tipo::Histograma, ayuda, e, &bordes).h;
}

int Metricas::agregarColector(std::function<void(Metricas&)> c) {
    std::lock_guard<std::mutex> l(colMu_);
    const int id = proximoColector_++;
    colectores_.emplace_back(id, std::move(c));
    return id;
}

void Metricas::quitarColector(int id) {
    std::lock_guard<std::mutex> l(colMu_);
    colectores_.erase(std::remove_if(colectores_.begin(), colectores_.end(), [id](const auto& p) { return p.first == id; }),
                      colectores_.end());
}

void Metricas::refrescar() {
    // Los colectores corren sin el candado de las series (crean las suyas) y uno a la
    // vez (dos exportaciones juntas no pisan lo que copia cada uno).
    std::lock_guard<std::mutex> l(colMu_);
    for (const auto& [id, c] : colectores_) {
        try {
            if (c) c(*this);
        } catch (...) {
            // Un colector que falla no deja sin métricas a los demás.
        }
    }
}

std::string Metricas::texto() {
    refrescar();
    std::lock_guard<std::mutex> l(mu_);
    std::string out;
    for (const auto& [nombre, f] : familias_) {
        out += "# HELP " + nombre + " " + ayudaTexto(f.ayuda) + "\n";
        out += "# TYPE " + nombre + " " + nombreTipo(f.tipo) + "\n";
        for (const auto& [clave, s] : f.series) {
            const std::string llaves = clave.empty() ? std::string() : "{" + clave + "}";
            if (s.c) out += nombre + llaves + " " + std::to_string(s.c->valor()) + "\n";
            if (s.m) out += nombre + llaves + " " + numeroTexto(s.m->valor()) + "\n";
            if (s.h) {
                const std::vector<std::uint64_t> c = s.h->casilleros();
                std::uint64_t acum = 0;
                const std::string pre = clave.empty() ? std::string() : clave + ",";
                for (std::size_t i = 0; i < c.size(); ++i) {
                    acum += c[i];
                    const std::string le = i < s.h->bordes().size() ? numeroTexto(s.h->bordes()[i]) : std::string("+Inf");
                    out += nombre + "_bucket{" + pre + "le=\"" + le + "\"} " + std::to_string(acum) + "\n";
                }
                out += nombre + "_sum" + llaves + " " + numeroTexto(s.h->suma()) + "\n";
                out += nombre + "_count" + llaves + " " + std::to_string(s.h->cuenta()) + "\n";
            }
        }
    }
    return out;
}

nlohmann::json Metricas::json() {
    refrescar();
    std::lock_guard<std::mutex> l(mu_);
    nlohmann::json out = nlohmann::json::object();
    for (const auto& [nombre, f] : familias_) {
        nlohmann::json series = nlohmann::json::array();
        for (const auto& [clave, s] : f.series) {
            nlohmann::json j{{"etiquetas", etiquetasJson(s.etiquetas)}};
            if (s.c) j["valor"] = s.c->valor();
            if (s.m) j["valor"] = s.m->conocido() ? nlohmann::json(redondo(s.m->valor(), 3)) : nlohmann::json(nullptr);
            if (s.h) {
                j["cuenta"] = s.h->cuenta();
                j["suma"] = redondo(s.h->suma(), 3);
                for (const auto& [k, p] : {std::pair{"p50", 0.50}, std::pair{"p90", 0.90}, std::pair{"p99", 0.99}}) {
                    const double v = s.h->percentil(p);
                    j[k] = v < 0 ? nlohmann::json(nullptr) : nlohmann::json(redondo(v, 2));
                }
            }
            series.push_back(std::move(j));
        }
        out[nombre] = {{"tipo", nombreTipo(f.tipo)}, {"ayuda", ayudaTexto(f.ayuda)}, {"series", std::move(series)}};
    }
    return out;
}

namespace {

std::optional<double> valorDe(const Metricas::Tipo t, const Contador* c, const Medidor* m, const Histograma* h,
                              const std::string& sufijo) {
    if (t == Metricas::Tipo::Contador && c != nullptr && sufijo.empty()) return static_cast<double>(c->valor());
    if (t == Metricas::Tipo::Medidor && m != nullptr && sufijo.empty())
        return m->conocido() ? std::optional<double>(m->valor()) : std::nullopt;
    if (t == Metricas::Tipo::Histograma && h != nullptr) {
        if (sufijo == "cuenta") return static_cast<double>(h->cuenta());
        if (sufijo == "suma") return h->suma();
        const double p = sufijo == "p50" ? 0.50 : sufijo == "p90" ? 0.90 : sufijo == "p99" ? 0.99 : -1;
        if (p < 0) return std::nullopt;
        const double v = h->percentil(p);
        return v < 0 ? std::nullopt : std::optional<double>(v);
    }
    return std::nullopt;
}

/// «nombre:sufijo» → (nombre, sufijo).
std::pair<std::string, std::string> partir(const std::string& n) {
    const auto k = n.rfind(':');
    return k == std::string::npos ? std::make_pair(n, std::string()) : std::make_pair(n.substr(0, k), n.substr(k + 1));
}

} // namespace

std::optional<double> Metricas::valor(const std::string& nombreConSufijo, const Etiquetas& e) const {
    const auto [nombre, sufijo] = partir(nombreConSufijo);
    std::lock_guard<std::mutex> l(mu_);
    const auto f = familias_.find(nombre);
    if (f == familias_.end()) return std::nullopt;
    const auto s = f->second.series.find(etiquetasATexto(e));
    if (s == f->second.series.end()) return std::nullopt;
    return valorDe(f->second.tipo, s->second.c.get(), s->second.m.get(), s->second.h.get(), sufijo);
}

std::vector<std::pair<Etiquetas, double>> Metricas::valores(const std::string& nombreConSufijo) const {
    const auto [nombre, sufijo] = partir(nombreConSufijo);
    std::vector<std::pair<Etiquetas, double>> out;
    std::lock_guard<std::mutex> l(mu_);
    const auto f = familias_.find(nombre);
    if (f == familias_.end()) return out;
    for (const auto& [clave, s] : f->second.series)
        if (const auto v = valorDe(f->second.tipo, s.c.get(), s.m.get(), s.h.get(), sufijo)) out.emplace_back(s.etiquetas, *v);
    return out;
}

} // namespace ba::diag
