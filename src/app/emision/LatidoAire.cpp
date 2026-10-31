// SPDX-License-Identifier: GPL-2.0-or-later
#include "emision/LatidoAire.hpp"
#include "ajustes/Ajustes.hpp"
#include "i18n/I18n.hpp"
#include "platform/Paths.hpp"

#include "diag/Registro.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>

namespace ba::app {

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
/// `notificar` de supervisor.json o de ponerPolitica: ausente = todos; una lista = esos.
void leerNotificar(const json& j, LatidoAire::Politica& p) {
    if (!j.contains("notificar")) return;
    p.notificar.clear();
    p.notificarTodos = j["notificar"].is_string() && j["notificar"] == "todos";
    if (j["notificar"].is_array())
        for (const auto& x : j["notificar"])
            if (x.is_string()) p.notificar.push_back(x.get<std::string>());
}

json notificarJson(const LatidoAire::Politica& p) { return p.notificarTodos ? json("todos") : json(p.notificar); }

std::uint64_t ahoraNs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}
} // namespace

void LatidoAire::iniciar(const fs::path& datos, std::function<void()> latir, std::function<std::uint64_t()> cuadros) {
    detener();
    datos_ = datos;
    latir_ = std::move(latir);
    cuadros_ = std::move(cuadros);
    {
        std::ifstream f(datos_ / "Config" / "supervisor.json");
        const json j = json::parse(f, nullptr, false);
        if (j.is_object()) {
            std::lock_guard<std::mutex> l(mu_);
            pol_.esperaUiS = std::clamp(j.value("espera_ui_s", pol_.esperaUiS), 10, 3600);
            pol_.matarSiUiTrabadaSinAire = j.value("matar_si_ui_trabada_sin_aire", pol_.matarSiUiTrabadaSinAire);
            leerNotificar(j, pol_);
        }
    }
    ultimoUiNs_ = 0; // hasta el primer tick de la interfaz (carga del QML): sin "traba"
    corriendo_ = true;
    hilo_ = std::thread([this] { bucle(); });
}

void LatidoAire::detener() {
    corriendo_ = false;
    if (hilo_.joinable()) hilo_.join();
}

void LatidoAire::ponerAviso(Aviso a) {
    std::lock_guard<std::mutex> l(avisoMu_);
    aviso_ = std::move(a);
}

void LatidoAire::avisar(bool trabada, std::uint64_t ms, bool vivo) {
    std::lock_guard<std::mutex> l(avisoMu_);
    if (aviso_) aviso_(trabada, ms, vivo);
}

void LatidoAire::uiTick(bool hayAire) {
    const std::uint64_t ahora = ahoraNs(), antes = ultimoUiNs_.exchange(ahora);
    hayAire_ = hayAire;
    // Volvió de una traba (más de 2 s sin tick): se avisa una vez. El primer
    // tick (antes == 0) no cuenta: es la carga de la interfaz.
    if (antes != 0 && ahora - antes > 2'000'000'000ULL) {
        const std::uint64_t ms = (ahora - antes) / 1'000'000ULL;
        trabaTerminadaMs_ = ms;
        ultimaTrabaMs_ = ms;
        ba::diag::registro().linea(ba::diag::Nivel::Aviso, "supervisor", 0,
                                  ba::i18n::llenar(motorVivo_ ? ba::i18n::traducir("supervisor", "la interfaz estuvo trabada %1 ms (el motor siguió: el aire no se cortó)")
                                                              : ba::i18n::traducir("supervisor", "la interfaz estuvo trabada %1 ms"),
                                                   {std::to_string(ms)}));
        escribirEstadoUi(false, ms);
    }
}

void LatidoAire::bucle() {
    std::uint64_t ultimos = cuadros_ ? cuadros_() : 0;
    std::uint64_t sinAvanceNs = 0, ultimaVueltaNs = ahoraNs();
    bool avisoTraba = false;
    bool alarmaAire = false; // la alarma «interfaz trabada con aire» está dada
    std::uint64_t trabaMs = 0; // lo que lleva (o llevó) la traba, para el aviso de la vuelta
    while (corriendo_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        const std::uint64_t ahora = ahoraNs();
        const std::uint64_t c = cuadros_ ? cuadros_() : 0;
        if (c != ultimos) {
            ultimos = c;
            sinAvanceNs = 0;
        } else {
            sinAvanceNs += ahora - ultimaVueltaNs;
        }
        ultimaVueltaNs = ahora;
        // Vivo: avanzó en el último segundo (un cuadro cada 1/fps; a 1 s sobra).
        const bool vivo = sinAvanceNs < static_cast<std::uint64_t>(ba::ajustes::msANs("supervisor.sinAvanceMs", 1000));
        motorVivo_ = vivo;
        const std::uint64_t ui0 = ultimoUiNs_.load();
        const std::uint64_t uiNs = ui0 == 0 ? 0 : ahora - ui0; // arrancando: la interfaz todavía carga
        const bool uiTrabada = uiNs > static_cast<std::uint64_t>(ba::ajustes::msANs("supervisor.uiTrabadaMs", 2000));
        if (uiTrabada && !avisoTraba) {
            avisoTraba = true;
            ba::diag::registro().linea(ba::diag::Nivel::Aviso, "supervisor", 0,
                                      vivo ? ba::i18n::traducir("supervisor", "la interfaz no responde; el motor sigue y el aire no se corta")
                                           : ba::i18n::traducir("supervisor", "la interfaz no responde; el motor tampoco"));
        }
        if (!uiTrabada) avisoTraba = false;
        if (uiTrabada) escribirEstadoUi(true, uiNs / 1'000'000ULL);
        // Con aire, la traba es una alarma que sale de acá (la interfaz no puede
        // darla): una vez al empezar y otra al volver. hayAire_ lo pone el último
        // tick de la interfaz, el de antes de trabarse.
        if (uiTrabada) trabaMs = uiNs / 1'000'000ULL;
        if (uiTrabada && hayAire_ && !alarmaAire) {
            alarmaAire = true;
            avisar(true, trabaMs, vivo);
        } else if (!uiTrabada && alarmaAire) {
            alarmaAire = false;
            avisar(false, trabaMs, vivo);
        }
        if (!vivo || !latir_) continue; // motor trabado: que el supervisor actúe
        Politica p = politica();
        const bool pasada = uiNs > static_cast<std::uint64_t>(p.esperaUiS) * 1'000'000'000ULL;
        // La UI trabada hace rato y nada saliendo: se deja de latir (el
        // supervisor relanza). Con aire, nunca: se corta solo si el motor cae.
        if (pasada && p.matarSiUiTrabadaSinAire && !hayAire_) continue;
        latir_();
    }
}

void LatidoAire::escribirEstadoUi(bool trabada, std::uint64_t ms) const {
    std::string s = json{{"ui_trabada", trabada}, {"ms", ms}, {"motor_vivo", motorVivo_.load()}, {"aire", hayAire_.load()}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    const std::shared_ptr<EscritorEstado> e = escritor_;
    {
        std::lock_guard<std::mutex> l(e->m);
        e->pendiente = std::move(s);
        if (e->activo) return; // el anterior sigue escribiendo (disco trabado): escribe este al terminar
        e->activo = true;
    }
    std::thread([e] {
        const fs::path dir = ba::platform::dataRoot() / "Runtime";
        for (;;) {
            std::string t;
            {
                std::lock_guard<std::mutex> l(e->m);
                if (e->pendiente.empty()) {
                    e->activo = false;
                    return;
                }
                t = std::move(e->pendiente);
                e->pendiente.clear();
            }
            std::error_code ec;
            fs::create_directories(dir, ec);
            std::ofstream o(dir / "estado-ui.json");
            o << t;
        }
    }).detach();
}

LatidoAire::Politica LatidoAire::politica() const {
    std::lock_guard<std::mutex> l(mu_);
    return pol_;
}

std::string LatidoAire::ponerPolitica(const json& c) {
    {
        std::lock_guard<std::mutex> l(mu_);
        if (c.contains("espera_ui_s")) {
            if (!c["espera_ui_s"].is_number_integer()) return ba::i18n::traducir("supervisor", "`espera_ui_s`: segundos (10 a 3600)");
            pol_.esperaUiS = std::clamp(c["espera_ui_s"].get<int>(), 10, 3600);
        }
        if (c.contains("matar_si_ui_trabada_sin_aire"))
            pol_.matarSiUiTrabadaSinAire = c.value("matar_si_ui_trabada_sin_aire", true);
        if (c.contains("notificar")) {
            if (!c["notificar"].is_array() && c["notificar"] != "todos")
                return ba::i18n::traducir("supervisor", "`notificar`: «todos» o una lista de ids de notificadores");
            leerNotificar(c, pol_);
        }
    }
    guardar();
    return {};
}

void LatidoAire::guardar() const {
    const Politica p = politica();
    std::error_code ec;
    fs::create_directories(datos_ / "Config", ec);
    std::ofstream o(datos_ / "Config" / "supervisor.json");
    o << json{{"espera_ui_s", p.esperaUiS}, {"matar_si_ui_trabada_sin_aire", p.matarSiUiTrabadaSinAire}, {"notificar", notificarJson(p)}}.dump(1, ' ', false, nlohmann::json::error_handler_t::replace);
}

json LatidoAire::estado() const {
    const Politica p = politica();
    const std::uint64_t ui0 = ultimoUiNs_.load();
    const std::uint64_t uiNs = ui0 == 0 ? 0 : ahoraNs() - ui0;
    return {{"motor_vivo", motorVivo_.load()},
            {"ui_viva", uiNs < 2'000'000'000ULL},
            {"ui_trabada_ms", uiNs < 2'000'000'000ULL ? 0 : uiNs / 1'000'000ULL},
            {"ultima_traba_ms", ultimaTrabaMs_.load()},
            {"politica",
             {{"espera_ui_s", p.esperaUiS}, {"matar_si_ui_trabada_sin_aire", p.matarSiUiTrabadaSinAire}, {"notificar", notificarJson(p)}}}};
}

std::uint64_t LatidoAire::tomarTrabaTerminada() { return trabaTerminadaMs_.exchange(0); }

} // namespace ba::app
