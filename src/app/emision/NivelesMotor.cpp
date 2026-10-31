// src/app/emision/NivelesMotor.cpp — ver el .hpp

#include "emision/NivelesMotor.hpp"

#include <algorithm>

namespace ba::app {

void NivelesMotor::ponerNiveles(std::shared_ptr<const Niveles> n) {
    const std::lock_guard<std::mutex> l(mu_);
    niveles_ = std::move(n);
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
    const std::lock_guard<std::mutex> g(mu_);
    Programa& p = programa_[clave(salida)];
    p.ultimo[0] = l;
    p.ultimo[1] = r;
    p.acum[0] = std::max(p.acum[0], l);
    p.acum[1] = std::max(p.acum[1], r);
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

} // namespace ba::app
