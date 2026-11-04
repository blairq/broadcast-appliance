// src/core/json/ObjetoJson.cpp — ver el .hpp

#include "json/ObjetoJson.hpp"

#include <nlohmann/json.hpp>

#include <utility>

namespace ba {

ObjetoJson::ObjetoJson() : j_(std::make_unique<nlohmann::json>(nlohmann::json::object())) {}
ObjetoJson::ObjetoJson(const nlohmann::json& j) : j_(std::make_unique<nlohmann::json>(j)) {}
ObjetoJson::ObjetoJson(nlohmann::json&& j) : j_(std::make_unique<nlohmann::json>(std::move(j))) {}
ObjetoJson::ObjetoJson(const ObjetoJson& o) : j_(std::make_unique<nlohmann::json>(*o.j_)) {}
// El de origen se queda con su puntero (y un JSON movido): nunca nulo.
ObjetoJson::ObjetoJson(ObjetoJson&& o) : j_(std::make_unique<nlohmann::json>(std::move(*o.j_))) {}
ObjetoJson::~ObjetoJson() = default;

ObjetoJson& ObjetoJson::operator=(const ObjetoJson& o) {
    if (this != &o) *j_ = *o.j_;
    return *this;
}
ObjetoJson& ObjetoJson::operator=(ObjetoJson&& o) noexcept {
    j_.swap(o.j_);
    return *this;
}
ObjetoJson& ObjetoJson::operator=(const nlohmann::json& j) {
    *j_ = j;
    return *this;
}
ObjetoJson& ObjetoJson::operator=(nlohmann::json&& j) {
    *j_ = std::move(j);
    return *this;
}

} // namespace ba
