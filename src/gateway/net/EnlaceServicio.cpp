// ============================================================================
// src/gateway/net/EnlaceServicio.cpp — ver EnlaceServicio.hpp
// ============================================================================

#include "net/EnlaceServicio.hpp"

#include <chrono>

#include "i18n/I18nTexto.hpp"
#include "platform/Hilo.hpp"

namespace ba::gateway::net::enlace {

using json = nlohmann::json;
using ba::i18n::traducir;

namespace {
constexpr int kTopeKbps = 1000000; ///< 1 Gbps: más no tiene sentido por un túnel
int64_t ahoraEpochMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}
} // namespace

Servicio::Servicio(AbrirFn abrir, std::string eco, AvisoFn alCambiar)
    : abrir_(std::move(abrir)), eco_(std::move(eco)), alCambiar_(std::move(alCambiar)) {
    estado_ = {{"estado", abrir_ ? "libre" : "sin_tunel"}, {"hayTunel", static_cast<bool>(abrir_)}, {"eco", eco_}};
}

Servicio::~Servicio() { parar(); }

void Servicio::parar() {
    cerrado_ = true;
    cancelar_ = true;
    std::lock_guard<std::mutex> l(hiloM_);
    if (hilo_.joinable()) hilo_.join();
}

bool Servicio::leerPedido(const json& j, Pedido& p, std::string& error) {
    if (!j.is_object()) return true;
    const auto entero = [&](const char* k, int& v, int min, int max) {
        if (!j.contains(k)) return true;
        if (!j[k].is_number()) {
            error = std::string(k);
            return false;
        }
        const double x = j[k].get<double>();
        if (x < min || x > max) {
            error = std::string(k);
            return false;
        }
        v = static_cast<int>(x);
        return true;
    };
    if (j.contains("subida") && j["subida"].is_boolean()) p.subida = j["subida"].get<bool>();
    if (j.contains("bajada") && j["bajada"].is_boolean()) p.bajada = j["bajada"].get<bool>();
    if (!entero("topeSubidaKbps", p.topeSubidaKbps, 500, kTopeKbps)) return false;
    if (!entero("topeBajadaKbps", p.topeBajadaKbps, 500, kTopeKbps)) return false;
    if (!entero("tasaKbps", p.tasaKbps, 0, kTopeKbps)) return false;
    if (!entero("segundos", p.segundos, 2, 30)) return false;
    if (!entero("escalonMs", p.escalonMs, 500, 5000)) return false;
    if (!p.subida && !p.bajada) {
        error = "subida/bajada";
        return false;
    }
    return true;
}

json Servicio::medir(const json& pedido) {
    if (!abrir_) return {{"ok", false}, {"error", traducir("gateway", "no hay túnel WireGuard configurado")}};
    Pedido p;
    std::string mal;
    if (!leerPedido(pedido, p, mal)) return {{"ok", false}, {"error", mal}};
    if (cerrado_) return {{"ok", false}, {"error", "cerrando"}};
    if (corriendo_.exchange(true)) return {{"ok", false}, {"error", traducir("gateway", "ya hay una medición del enlace en curso")}};
    std::lock_guard<std::mutex> lh(hiloM_);
    if (hilo_.joinable()) hilo_.join(); // la anterior ya terminó
    cancelar_ = false;
    {
        std::lock_guard<std::mutex> l(m_);
        estado_ = {{"estado", "midiendo"}, {"hayTunel", true}, {"eco", eco_}, {"fase", "hola"}, {"avance", 0}};
        if (!ultimo_.is_null()) estado_["ultimo"] = ultimo_;
    }
    publicar(estado());
    hilo_ = std::thread([this, p]() {
        ba::platform::nombrarHilo("enlace");
        correr(p);
    });
    return {{"ok", true}};
}

void Servicio::cancelar() { cancelar_ = true; }

json Servicio::estado() const {
    std::lock_guard<std::mutex> l(m_);
    return estado_;
}

void Servicio::publicar(const json& e) {
    if (alCambiar_) alCambiar_(e);
}

void Servicio::correr(Pedido p) {
    json r;
    std::unique_ptr<Transporte> t = abrir_();
    if (!t) {
        r = {{"ok", false}, {"error", traducir("gateway", "no se pudo abrir el UDP por el túnel")}};
    } else {
        Medidor med(
            *t,
            [this](const json& parcial) {
                json e = parcial;
                e["hayTunel"] = true;
                e["eco"] = eco_;
                {
                    std::lock_guard<std::mutex> l(m_);
                    if (!ultimo_.is_null()) e["ultimo"] = ultimo_;
                    estado_ = e;
                }
                publicar(e);
            },
            cancelar_);
        r = med.correr(p);
    }
    r["fecha"] = ahoraEpochMs();
    r["pedido"] = {{"subida", p.subida},
                   {"bajada", p.bajada},
                   {"topeSubidaKbps", p.topeSubidaKbps},
                   {"topeBajadaKbps", p.topeBajadaKbps},
                   {"tasaKbps", p.tasaKbps}};
    json e;
    {
        std::lock_guard<std::mutex> l(m_);
        ultimo_ = r;
        estado_ = {{"estado", "libre"}, {"hayTunel", true}, {"eco", eco_}, {"ultimo", ultimo_}};
        e = estado_;
    }
    corriendo_ = false;
    publicar(e);
}

std::string scriptEco() {
    static const unsigned char kEco[] = {
#include "EcoEnlace.inc"
    };
    return std::string(reinterpret_cast<const char*>(kEco), sizeof(kEco));
}

std::string instaladorEco(const std::string& ipServidor, const std::string& red, uint16_t puerto) {
    const std::string dir = "/usr/local/lib/broadcast-appliance";
    std::string s;
    s += "#!/bin/sh\n";
    s += "# " + traducir("gateway", "Broadcast Appliance: instala o actualiza el eco del medidor del enlace en este "
                                    "servidor (pegar como root; se puede repetir).") +
         "\n";
    s += "set -e\n";
    s += "command -v python3 >/dev/null || (apt-get update && apt-get install -y python3)\n";
    s += "install -d " + dir + "\n";
    s += "cat > " + dir + "/ba-enlace-eco.py <<'BA_ECO_FIN'\n";
    std::string py = scriptEco();
    if (!py.empty() && py.back() != '\n') py += '\n';
    s += py;
    s += "BA_ECO_FIN\n";
    s += "cat > /etc/systemd/system/ba-enlace-eco.service <<'BA_UNIDAD_FIN'\n";
    s += "[Unit]\nDescription=Broadcast Appliance: eco del medidor del enlace (UDP " + std::to_string(puerto) +
         ")\nAfter=wg-quick@wg0.service\nWants=wg-quick@wg0.service\n\n";
    s += "[Service]\nExecStart=/usr/bin/env python3 " + dir + "/ba-enlace-eco.py --direccion " + ipServidor +
         " --puerto " + std::to_string(puerto) + " --red " + red + "\n";
    s += "Restart=always\nRestartSec=3\nDynamicUser=yes\n\n[Install]\nWantedBy=multi-user.target\n";
    s += "BA_UNIDAD_FIN\n";
    // ufw prendido: deja pasar el eco solo por la interfaz del túnel.
    s += "if command -v ufw >/dev/null && ufw status | grep -q 'Status: active'; then ufw allow in on wg0 to any port " +
         std::to_string(puerto) + " proto udp; fi\n";
    s += "systemctl daemon-reload\n";
    s += "systemctl enable ba-enlace-eco\n";
    s += "systemctl restart ba-enlace-eco\n";
    s += "systemctl --no-pager --lines=3 status ba-enlace-eco || true\n";
    return s;
}

} // namespace ba::gateway::net::enlace
