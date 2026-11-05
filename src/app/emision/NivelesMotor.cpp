// src/app/emision/NivelesMotor.cpp — ver el .hpp

#include "emision/NivelesMotor.hpp"
#include "NivelAudio.hpp"

#include <algorithm>

namespace ba::app {

void NivelesMotor::ponerNiveles(std::shared_ptr<const Niveles> n) {
    {
        const std::lock_guard<std::mutex> l(mu_);
        niveles_ = std::move(n);
    }
    despertarSiSuena();
}

bool NivelesMotor::haySonidoSinCandado() const {
    if (niveles_)
        for (const auto& n : *niveles_)
            if (std::max(n.pico[0], n.pico[1]) > kSilencioDb) return true;
    for (const auto& [id, p] : programa_)
        if (std::max(p.ultimo[0], p.ultimo[1]) > kSilencioDb) return true;
    return false;
}

bool NivelesMotor::haySonido() const {
    const std::lock_guard<std::mutex> l(mu_);
    return haySonidoSinCandado();
}

void NivelesMotor::despertarAlSonar(std::function<void()> fn) {
    {
        const std::lock_guard<std::mutex> a(avisoMu_); // espera al aviso en curso
        const std::lock_guard<std::mutex> l(mu_);
        despertar_ = std::move(fn);
        armado_ = static_cast<bool>(despertar_);
    }
    despertarSiSuena(); // si ya suena, ya
}

void NivelesMotor::despertarSiSuena() {
    if (!armado_.load(std::memory_order_acquire)) return; // lo de siempre: nadie duerme
    const std::lock_guard<std::mutex> a(avisoMu_);
    std::function<void()> fn;
    {
        const std::lock_guard<std::mutex> l(mu_);
        if (!despertar_ || !haySonidoSinCandado()) return;
        fn = std::move(despertar_); // una vez
        despertar_ = nullptr;
        armado_ = false;
    }
    fn();
}

std::shared_ptr<const NivelesMotor::Niveles> NivelesMotor::niveles() const {
    const std::lock_guard<std::mutex> l(mu_);
    return niveles_;
}

namespace {
float picoDe(const NivelesMotor::Niveles* v, const std::string& id, bool bus) {
    if (v != nullptr)
        for (const auto& n : *v)
            if (n.bus == bus && n.id == id) return std::max(-100.0f, std::max(n.pico[0], n.pico[1]));
    return -100.0f;
}
} // namespace

float NivelesMotor::picoEntrada(const std::string& nombre) const { return picoDe(niveles().get(), nombre, false); }

float NivelesMotor::picoBus(const std::string& id) const { return picoDe(niveles().get(), id, true); }

std::string NivelesMotor::clave(const std::string& salida) const {
    return salida == porOmision_ ? std::string() : salida;
}

void NivelesMotor::setPorOmision(const std::string& id) {
    const std::lock_guard<std::mutex> l(mu_);
    porOmision_ = id;
}

void NivelesMotor::sumarPrograma(const std::string& salida, float l, float r) {
    {
        const std::lock_guard<std::mutex> g(mu_);
        Programa& p = programa_[clave(salida)];
        p.ultimo[0] = l;
        p.ultimo[1] = r;
        p.acum[0] = std::max(p.acum[0], l);
        p.acum[1] = std::max(p.acum[1], r);
    }
    despertarSiSuena();
}

std::pair<float, float> NivelesMotor::tomarPrograma(const std::string& salida) {
    const std::lock_guard<std::mutex> g(mu_);
    const auto it = programa_.find(clave(salida));
    if (it == programa_.end()) return {-100.0f, -100.0f};
    const std::pair<float, float> out{it->second.acum[0], it->second.acum[1]};
    it->second.acum[0] = it->second.acum[1] = -100.0f;
    return out;
}

std::pair<float, float> NivelesMotor::programa(const std::string& salida) const {
    const std::lock_guard<std::mutex> g(mu_);
    const auto it = programa_.find(clave(salida));
    if (it == programa_.end()) return {-100.0f, -100.0f};
    return {it->second.ultimo[0], it->second.ultimo[1]};
}

void NivelesMotor::olvidarSalida(const std::string& salida) {
    const std::lock_guard<std::mutex> g(mu_);
    if (!salida.empty() && salida != porOmision_) programa_.erase(salida);
}

void NivelesMotor::ponerMedidores(std::vector<Medidor> m) {
    const std::lock_guard<std::mutex> g(medidoresMu_);
    medidores_ = std::move(m);
}

int NivelesMotor::medir(bool conLoudness) {
    std::vector<Medidor> ms;
    {
        const std::lock_guard<std::mutex> g(medidoresMu_);
        ms = medidores_; // unos pocos (uno por salida): se copian para medir sin el candado
    }
    nlohmann::json loud = nlohmann::json::object();
    for (const Medidor& m : ms) {
        if (m.pico) {
            const auto [l, r] = m.pico();
            sumarPrograma(m.salida, l, r);
        }
        if (conLoudness && m.loudness) loud[m.salida] = m.loudness();
    }
    if (conLoudness) {
        auto nuevo = std::make_shared<const nlohmann::json>(std::move(loud));
        {
            const std::lock_guard<std::mutex> g(mu_);
            loudness_ = std::move(nuevo);
        }
        ++medidas_;
    }
    return static_cast<int>(ms.size());
}

std::shared_ptr<const nlohmann::json> NivelesMotor::loudness() const {
    const std::lock_guard<std::mutex> g(mu_);
    return loudness_;
}

nlohmann::json NivelesMotor::loudnessDe(const std::string& salida) const {
    std::shared_ptr<const nlohmann::json> l;
    std::string id;
    {
        const std::lock_guard<std::mutex> g(mu_);
        l = loudness_;
        id = salida.empty() ? porOmision_ : salida;
    }
    if (!l) return nlohmann::json::object();
    const auto it = l->find(id);
    return it != l->end() ? *it : nlohmann::json::object();
}

} // namespace ba::app
