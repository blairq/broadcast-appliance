// ============================================================================
// src/gateway/proxy/GestorProxy.cpp — ver GestorProxy.hpp
// ============================================================================

#include "proxy/GestorProxy.hpp"

#include "proxy/Acme.hpp"
#include "proxy/Certificados.hpp"
#include "proxy/ProxyNombres.hpp"
#include "i18n/I18n.hpp"
#include "security/CredentialStore.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>

namespace ba::gateway::proxy {

using json = nlohmann::json;

namespace {

std::int64_t ahoraS() { return static_cast<std::int64_t>(std::time(nullptr)); }

/// El nombre en el almacén para el certificado de `host` (el almacén pide [a-z0-9._-], ≤64).
std::string clave(const std::string& host, const char* que) {
    static const char* hx = "0123456789abcdef";
    const std::string h = sha256(host);
    std::string id;
    for (int i = 0; i < 12; ++i) {
        id += hx[static_cast<unsigned char>(h[static_cast<size_t>(i)]) >> 4];
        id += hx[static_cast<unsigned char>(h[static_cast<size_t>(i)]) & 15];
    }
    return "proxy-" + id + "-" + que;
}

} // namespace

json GestorProxy::serviciosDeFabrica(uint16_t puertoSala) {
    const auto s = [puertoSala](const char* n, const char* ruta, bool soloLan) {
        return json{{"nombre", n}, {"destino", puertoSala}, {"ruta", ruta}, {"soloLan", soloLan}};
    };
    return json::array({s("sala", "/", false), s("sesion", "/sesion/", false), s("presidencia", "/presidencia/", false),
                        s("registro", "/registro/", false), s("botonera", "/botonera/", true), s("prompter", "/prompter/", true), s("tablero", "/board/", true)});
}

GestorProxy::GestorProxy(json cfg, uint16_t puertoSala, std::string credenciales, std::vector<std::string> ipsLan, LogFn log)
    : cfg_(std::move(cfg)), puertoSala_(puertoSala), credenciales_(std::move(credenciales)), ipsLan_(std::move(ipsLan)),
      log_(std::move(log)) {}

GestorProxy::~GestorProxy() { stop(); }

bool GestorProxy::start() {
    ProxyNombres::Config c;
    c.bind = cfg_.value("bind", std::string("0.0.0.0"));
    c.puertoTls = static_cast<uint16_t>(cfg_.value("puertoTls", 443));
    c.puertoHttp = static_cast<uint16_t>(cfg_.value("puertoHttp", 80));
    c.dominio = cfg_.value("dominio", std::string());
    if (c.dominio.empty()) {
        if (log_) log_(ba::i18n::traducir("gateway", "proxy: falta el dominio (proxy.dominio): no arranca"));
        return false;
    }
    const json servicios = cfg_.contains("servicios") && cfg_["servicios"].is_array() && !cfg_["servicios"].empty()
                               ? cfg_["servicios"]
                               : serviciosDeFabrica(puertoSala_);
    for (const auto& s : servicios) {
        ProxyNombres::Servicio sv;
        sv.nombre = s.value("nombre", std::string());
        sv.destino = static_cast<uint16_t>(s.value("destino", static_cast<int>(puertoSala_)));
        sv.ruta = s.value("ruta", std::string("/"));
        sv.soloLan = s.value("soloLan", false);
        // Solo el servidor de la sala entiende la cabecera PROXY (la IP real).
        sv.cabeceraProxy = s.value("cabeceraProxy", sv.destino == puertoSala_);
        if (!sv.nombre.empty()) c.servicios.push_back(sv);
    }
    c.porDefecto = cfg_.value("porDefecto", std::string("sala"));
    px_ = std::make_unique<ProxyNombres>(c, log_);

    // El de la casa: autofirmado con todos los nombres y las IP de la red local.
    // Se reusa mientras los nombres no cambien (huella estable).
    const ba::security::CredentialStore store{std::filesystem::path(credenciales_)};
    std::vector<std::string> nombres = px_->hosts();
    nombres.insert(nombres.end(), ipsLan_.begin(), ipsLan_.end());
    std::string lista;
    for (const auto& n : nombres) lista += n + "\n";
    std::string viejos, casaCert, casaClave;
    if (!(store.read("proxy-casa-nombres", viejos) && viejos == lista && store.read("proxy-casa-cert", casaCert) &&
          store.read("proxy-casa-clave", casaClave))) {
        casaClave = claveNueva();
        casaCert = autofirmado(casaClave, nombres);
        store.write("proxy-casa-cert", casaCert);
        store.write("proxy-casa-clave", casaClave);
        store.write("proxy-casa-nombres", lista);
    }
    if (!px_->start(casaCert, casaClave)) {
        px_.reset();
        return false;
    }
    // Los de cada nombre que ya estaban (de Let's Encrypt).
    for (const auto& h : px_->hosts()) {
        std::string cert, k;
        if (store.read(clave(h, "cert"), cert) && store.read(clave(h, "clave"), k)) px_->ponerCertificado(h, cert, k, "acme");
    }
    if (cfg_.contains("acme") && cfg_["acme"].value("activo", false)) {
        fin_ = false;
        hilo_ = std::thread([this]() { hiloAcme(); });
    }
    return true;
}

void GestorProxy::stop() {
    fin_ = true;
    cv_.notify_all();
    if (hilo_.joinable()) hilo_.join();
    if (px_) px_->stop();
}

void GestorProxy::renovar(const std::string& host) {
    {
        std::lock_guard<std::mutex> l(mu_);
        pedido_ = true;
        forzar_ = true;
        pedidoHost_ = host;
    }
    cv_.notify_all();
}

void GestorProxy::hiloAcme() {
    pedirSiHaceFalta(false, {});
    while (!fin_) {
        bool forzar = false;
        std::string host;
        {
            std::unique_lock<std::mutex> l(mu_);
            cv_.wait_for(l, std::chrono::hours(12), [this]() { return fin_.load() || pedido_; });
            if (fin_) break;
            forzar = forzar_;
            host = pedidoHost_;
            pedido_ = forzar_ = false;
            pedidoHost_.clear();
        }
        pedirSiHaceFalta(forzar, host);
    }
}

void GestorProxy::pedirSiHaceFalta(bool forzar, const std::string& soloHost) {
    if (!px_) return;
    andando_ = true;
    const json a = cfg_.value("acme", json::object());
    const std::string dir = a.value("directorio", std::string(a.value("pruebas", false) ? ClienteAcme::kLetsEncryptPruebas
                                                                                       : ClienteAcme::kLetsEncrypt));
    const ba::security::CredentialStore store{std::filesystem::path(credenciales_)};
    std::string cuenta;
    store.read("proxy-acme-cuenta", cuenta);
    ClienteAcme acme(dir, a.value("email", std::string()), cuenta, log_);
    if (cuenta.empty()) store.write("proxy-acme-cuenta", acme.cuentaPem());
    std::string errores;
    for (const auto& h : px_->hosts()) {
        if (!soloHost.empty() && h != soloHost) continue;
        std::string cert;
        const bool tiene = store.read(clave(h, "cert"), cert);
        const std::int64_t queda = tiene ? vence(cert) - ahoraS() : 0;
        if (!forzar && tiene && queda > 30LL * 86400) continue; // le queda más de un mes
        if (log_) log_(ba::i18n::llenar(ba::i18n::traducir("gateway", "acme: pidiendo el certificado de %1"), {h}));
        const auto r = acme.pedir({h}, [this](const std::string& t, const std::string& resp, bool poner) {
            if (poner) px_->ponerDesafio(t, resp);
            else px_->quitarDesafio(t);
        });
        if (r.ok) {
            store.write(clave(h, "cert"), r.cert);
            store.write(clave(h, "clave"), r.clave);
            px_->ponerCertificado(h, r.cert, r.clave, "acme");
        } else {
            errores += (errores.empty() ? "" : "; ") + h + ": " + r.error;
        }
        if (fin_) break;
    }
    std::lock_guard<std::mutex> l(mu_);
    ultimoError_ = errores;
    ultimaVez_ = ahoraS();
    andando_ = false;
}

json GestorProxy::estado() const {
    json out = px_ ? px_->estado() : json{{"activo", false}};
    std::lock_guard<std::mutex> l(mu_);
    const json a = cfg_.value("acme", json::object());
    out["acme"] = {{"activo", a.value("activo", false)},
                   {"pruebas", a.value("pruebas", false)},
                   {"email", a.value("email", std::string())},
                   {"ultimo_error", ultimoError_},
                   {"ultima_vez", ultimaVez_},
                   {"andando", andando_.load()}};
    return out;
}

} // namespace ba::gateway::proxy
