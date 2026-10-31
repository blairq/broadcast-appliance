// Politica.cpp — ver Politica.hpp

#include "Politica.hpp"

#include "ajustes/Ajustes.hpp"
#include "ipc/HealthBlock.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <system_error>

namespace ba::launcher {

int esperaRelanzarS(int consecutivas) {
    static const int seq[] = {0, 1, 2, 5};
    if (consecutivas >= 0 && consecutivas < 4) return seq[consecutivas];
    return std::max(5, ba::ajustes::entero("launcher.relanzarMaxS", 10));
}

std::uint64_t Relanzador::caida(std::uint64_t ahoraMs) {
    const std::uint64_t espera = static_cast<std::uint64_t>(esperaRelanzarS(consecutivas_)) * 1000;
    ++consecutivas_;
    agendar(ahoraMs, espera);
    return espera;
}

void Relanzador::agendar(std::uint64_t ahoraMs, std::uint64_t esperaMs) {
    relanzarEn_ = ahoraMs + esperaMs;
    lanzadoEn_.reset();
}

void Relanzador::lanzado(std::uint64_t ahoraMs) {
    relanzarEn_.reset();
    lanzadoEn_ = ahoraMs;
}

bool Relanzador::vigilar(std::uint64_t ahoraMs, bool lateFresco, std::uint64_t estableMs) {
    if (consecutivas_ == 0 || !lanzadoEn_ || !lateFresco || ahoraMs - *lanzadoEn_ < estableMs) return false;
    consecutivas_ = 0;
    return true;
}

void Relanzador::olvidar() {
    consecutivas_ = 0;
    relanzarEn_.reset();
    lanzadoEn_.reset();
}

Readopcion decidirReadopcion(bool vivoYLate, std::uint32_t versionPublicada, std::uint32_t versionEsperada,
                             bool hayAire) {
    if (!vivoYLate) return Readopcion::Lanzar;
    if (versionPublicada == versionEsperada) return Readopcion::Adoptar;
    return hayAire ? Readopcion::Posponer : Readopcion::Reemplazar;
}

bool salidasActivas(const std::filesystem::path& alAire) {
    std::error_code ec;
    if (!std::filesystem::exists(alAire, ec)) return false;
    std::ifstream in(alAire, std::ios::binary);
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
    if (!j.is_object()) return true; // ilegible con la app viva: ante la duda, hay aire
    // Un campo de otro tipo no tira: cuenta como apagado.
    const auto si = [](const nlohmann::json& x, const char* k) {
        const auto c = x.find(k);
        return c != x.end() && c->is_boolean() && c->get<bool>();
    };
    if (si(j, "cerrado")) return false;
    const auto prendida = [&si](const nlohmann::json& x) {
        return x.is_object() && (si(x, "streaming") || si(x, "recording"));
    };
    if (prendida(j)) return true;
    const auto it = j.find("salidas");
    if (it == j.end() || !it->is_object()) return false;
    return std::any_of(it->begin(), it->end(), prendida);
}

bool hayAire(const ba::ipc::HealthBlock& salud, const std::filesystem::path& runtime) {
    if (!salud.valid()) return false;
    const ba::ipc::Snapshot a = salud.read(ba::ipc::kSlotSaludApp);
    if (a.pid == 0 || salud.isStale(ba::ipc::kSlotSaludApp)) return false;
    return salidasActivas(runtime / "al-aire.json");
}

} // namespace ba::launcher
