// ============================================================================
// src/gateway/proxy/Acme.cpp — ver Acme.hpp
// ============================================================================

#include "proxy/Acme.hpp"

#include "proxy/Certificados.hpp"

#include "../../app/net/HttpsClient.hpp"

#include <chrono>
#include <thread>

#include <nlohmann/json.hpp>

namespace ba::gateway::proxy {

using json = nlohmann::json;
namespace hn = ba::app::net;

namespace {
std::string cabecera(const hn::HttpResponse& r, const char* n) { return r.header(n); }

/// El PEM de un DER (para el CSR no hace falta; para depurar sí).
std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ')) s.pop_back();
    return s;
}
} // namespace

ClienteAcme::ClienteAcme(std::string directorio, std::string email, std::string cuentaPem, LogFn log)
    : dirUrl_(std::move(directorio)), email_(std::move(email)), cuenta_(std::move(cuentaPem)), log_(std::move(log)) {
    if (cuenta_.empty()) cuenta_ = claveNueva();
}

std::string ClienteAcme::jwk() const {
    std::string x, y;
    if (!coordenadas(cuenta_, x, y)) return {};
    // Canónica (RFC 7638): solo los miembros obligatorios, en orden alfabético y sin espacios.
    return R"({"crv":"P-256","kty":"EC","x":")" + base64url(x) + R"(","y":")" + base64url(y) + R"("})";
}

std::string ClienteAcme::autorizacion(const std::string& token) const { return token + "." + base64url(sha256(jwk())); }

bool ClienteAcme::directorio(std::string& err) {
    hn::HttpRequest q;
    q.url = dirUrl_;
    const hn::HttpResponse r = hn::HttpsClient::request(q);
    const json d = json::parse(r.body, nullptr, false);
    if (r.status != 200 || !d.is_object()) {
        err = "no se pudo leer el directorio ACME (" + (r.error.empty() ? std::to_string(r.status) : r.error) + ")";
        return false;
    }
    nuevoNonce_ = d.value("newNonce", std::string());
    nuevaCuenta_ = d.value("newAccount", std::string());
    nuevaOrden_ = d.value("newOrder", std::string());
    hn::HttpRequest h;
    h.method = "HEAD";
    h.url = nuevoNonce_;
    nonce_ = cabecera(hn::HttpsClient::request(h), "Replay-Nonce");
    if (nonce_.empty()) {
        err = "la CA no dio un nonce";
        return false;
    }
    return true;
}

ClienteAcme::Http ClienteAcme::firmado(const std::string& url, const std::string& payload, bool conJwk) {
    Http out;
    for (int intento = 0; intento < 3; ++intento) {
        json prot{{"alg", "ES256"}, {"nonce", nonce_}, {"url", url}};
        if (conJwk) prot["jwk"] = json::parse(jwk());
        else prot["kid"] = kid_;
        const std::string p64 = base64url(prot.dump());
        const std::string c64 = payload.empty() && !conJwk ? std::string() : base64url(payload); // POST-as-GET: ""
        std::string firma;
        if (!firmarEs256(cuenta_, p64 + "." + c64, firma)) {
            out.error = "no se pudo firmar";
            return out;
        }
        const json jws{{"protected", p64}, {"payload", c64}, {"signature", base64url(firma)}};
        hn::HttpRequest q;
        q.method = "POST";
        q.url = url;
        q.headers = {{"Content-Type", "application/jose+json"}};
        q.body = jws.dump();
        const hn::HttpResponse r = hn::HttpsClient::request(q);
        out.status = r.status;
        out.cuerpo = r.body;
        out.error = r.error;
        out.location = cabecera(r, "Location");
        const std::string n = cabecera(r, "Replay-Nonce");
        if (!n.empty()) nonce_ = n;
        // badNonce: se reintenta con el nonce nuevo que vino en la respuesta.
        const json e = json::parse(r.body, nullptr, false);
        if (r.status == 400 && e.is_object() && e.value("type", std::string()).find("badNonce") != std::string::npos) continue;
        break;
    }
    return out;
}

ClienteAcme::Resultado ClienteAcme::pedir(const std::vector<std::string>& nombres, const Desafio& desafio) {
    Resultado res;
    const auto fallar = [&](const std::string& e) {
        res.error = e;
        if (log_) log_("acme: " + e);
        return res;
    };
    if (nombres.empty()) return fallar("sin nombres");
    if (jwk().empty()) return fallar("la clave de la cuenta no sirve");
    std::string err;
    if (!directorio(err)) return fallar(err);

    // 1) La cuenta (si ya existe, la CA devuelve la misma: onlyReturnExisting no hace falta).
    json cuenta{{"termsOfServiceAgreed", true}};
    if (!email_.empty()) cuenta["contact"] = json::array({"mailto:" + email_});
    Http r = firmado(nuevaCuenta_, cuenta.dump(), true);
    if ((r.status != 200 && r.status != 201) || r.location.empty())
        return fallar("la CA no aceptó la cuenta (" + std::to_string(r.status) + ": " + trim(r.cuerpo.substr(0, 300)) + ")");
    kid_ = r.location;

    // 2) La orden.
    json ids = json::array();
    for (const auto& n : nombres) ids.push_back({{"type", "dns"}, {"value", n}});
    r = firmado(nuevaOrden_, json{{"identifiers", ids}}.dump(), false);
    const json orden = json::parse(r.cuerpo, nullptr, false);
    if (r.status != 201 || !orden.is_object() || r.location.empty())
        return fallar("la CA no aceptó la orden (" + std::to_string(r.status) + ": " + trim(r.cuerpo.substr(0, 300)) + ")");
    const std::string urlOrden = r.location;
    const std::string finalizar = orden.value("finalize", std::string());

    // 3) Cada autorización: el desafío HTTP-01.
    std::vector<std::string> tokens;
    const auto limpiar = [&]() {
        for (const auto& t : tokens) desafio(t, {}, false);
    };
    for (const auto& urlAuth : orden.value("authorizations", json::array())) {
        Http a = firmado(urlAuth.get<std::string>(), "", false);
        const json auth = json::parse(a.cuerpo, nullptr, false);
        if (!auth.is_object()) return limpiar(), fallar("autorización ilegible");
        if (auth.value("status", std::string()) == "valid") continue;
        std::string token, urlDesafio;
        for (const auto& c : auth.value("challenges", json::array()))
            if (c.value("type", std::string()) == "http-01") {
                token = c.value("token", std::string());
                urlDesafio = c.value("url", std::string());
            }
        if (token.empty()) return limpiar(), fallar("la CA no ofrece HTTP-01 para " + auth["identifier"].value("value", std::string()));
        desafio(token, autorizacion(token), true);
        tokens.push_back(token);
        firmado(urlDesafio, "{}", false); // «listo, vení a mirar»
        bool valido = false;
        for (int i = 0; i < 30 && !valido; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            const json s = json::parse(firmado(urlAuth.get<std::string>(), "", false).cuerpo, nullptr, false);
            const std::string st = s.is_object() ? s.value("status", std::string()) : std::string();
            if (st == "valid") valido = true;
            else if (st == "invalid") {
                std::string det;
                for (const auto& c : s.value("challenges", json::array()))
                    if (c.contains("error")) det = c["error"].value("detail", std::string());
                return limpiar(), fallar("la CA no pudo validar " + s["identifier"].value("value", std::string()) + ": " + det +
                                         " (¿el nombre apunta a esta máquina y el puerto 80 llega desde internet?)");
            }
        }
        if (!valido) return limpiar(), fallar("la validación no terminó en 60 s");
    }
    limpiar();

    // 4) Finalizar con el CSR (clave nueva para el certificado) y bajarlo.
    res.clave = claveNueva();
    const std::vector<unsigned char> csr = csrDer(res.clave, nombres);
    if (csr.empty()) return fallar("no se pudo armar el CSR");
    r = firmado(finalizar, json{{"csr", base64url(csr.data(), csr.size())}}.dump(), false);
    if (r.status != 200) return fallar("la CA no aceptó el CSR (" + std::to_string(r.status) + ": " + trim(r.cuerpo.substr(0, 300)) + ")");
    std::string urlCert;
    for (int i = 0; i < 30 && urlCert.empty(); ++i) {
        const json o = json::parse(firmado(urlOrden, "", false).cuerpo, nullptr, false);
        if (o.is_object() && o.value("status", std::string()) == "valid") urlCert = o.value("certificate", std::string());
        else if (o.is_object() && o.value("status", std::string()) == "invalid") return fallar("la orden quedó inválida");
        else std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    if (urlCert.empty()) return fallar("el certificado no salió en 60 s");
    r = firmado(urlCert, "", false);
    if (r.status != 200 || r.cuerpo.find("BEGIN CERTIFICATE") == std::string::npos) return fallar("no se pudo bajar el certificado");
    res.cert = r.cuerpo;
    res.ok = true;
    if (log_) log_("acme: certificado nuevo para " + nombres.front() + (nombres.size() > 1 ? " y otros" : ""));
    return res;
}

} // namespace ba::gateway::proxy
