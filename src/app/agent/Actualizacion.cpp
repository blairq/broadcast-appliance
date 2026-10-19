// ============================================================================
// src/app/agent/Actualizacion.cpp — ver Actualizacion.hpp
// ============================================================================
#include "agent/Actualizacion.hpp"

#include <QByteArray>

#include <algorithm>
#include <cctype>
#include <map>

#include "net/HttpsClient.hpp"

namespace ba::app::agent {

using json = nlohmann::json;

namespace {

net::HttpResponse getJson(const std::string& url) {
    net::HttpRequest req;
    req.url = url;
    req.timeoutMs = 20000;
    // GitHub rechaza pedidos sin User-Agent.
    req.headers = {{"User-Agent", "BroadcastAppliance"}, {"Accept", "application/json"}};
    net::HttpResponse res = net::HttpsClient::request(req);
    // Redirecciones (pocas, y solo a https): un repo que cambio de dueño en
    // GitHub contesta 301 a su nuevo lugar (paso con opencode).
    for (int i = 0;
         i < 3 && (res.status == 301 || res.status == 302 || res.status == 307 || res.status == 308); ++i) {
        const std::string destino = res.header("location");
        if (destino.rfind("https://", 0) != 0) break;
        req.url = destino;
        res = net::HttpsClient::request(req);
    }
    return res;
}

bool pareceVersion(const std::string& v) {
    return !v.empty() && v.size() < 32 && std::all_of(v.begin(), v.end(), [](char c) {
        return std::isdigit(static_cast<unsigned char>(c)) || c == '.';
    });
}

} // namespace

json ultimaPublicada(const std::string& runtime, const json& base, std::string* err) {
    const auto fallo = [err](const std::string& e) {
        if (err != nullptr) *err = e;
        return json();
    };
    json out = base;
    const json basePlats = base.value("plataformas", json::object());
    if (runtime == "claude") {
        // El bucket oficial: <raiz>/latest dice la version y <raiz>/<v>/manifest.json
        // trae el checksum (SHA-256) de cada plataforma.
        const std::string fuente = base.value("fuente", std::string());
        const auto corte = fuente.find("/claude-code-releases/");
        if (corte == std::string::npos) return fallo("no se reconoce la fuente de Claude Code");
        const std::string raiz = fuente.substr(0, corte) + "/claude-code-releases";
        const auto rv = getJson(raiz + "/latest");
        std::string v = rv.body;
        v.erase(std::remove_if(v.begin(), v.end(), [](unsigned char c) { return std::isspace(c); }), v.end());
        if (rv.status != 200 || !pareceVersion(v))
            return fallo("no se pudo leer la ultima version de Claude Code");
        const auto rm = getJson(raiz + "/" + v + "/manifest.json");
        const json m = json::parse(rm.body, nullptr, false);
        if (rm.status != 200 || !m.is_object())
            return fallo("no se pudo leer el manifest de Claude Code " + v);
        const json plats = m.value("platforms", json::object());
        const std::map<std::string, std::string> nombres{{"linux-x64", "linux-x64"},
                                                         {"linux-arm64", "linux-arm64"},
                                                         {"win-x64", "win32-x64"},
                                                         {"mac-arm64", "darwin-arm64"},
                                                         {"mac-x64", "darwin-x64"}};
        json nuevas = json::object();
        for (const auto& [clave, pin] : basePlats.items()) {
            const auto n = nombres.find(clave);
            if (n == nombres.end() || !plats.contains(n->second)) continue;
            const json p = plats[n->second];
            const std::string sha = p.value("checksum", std::string()),
                              bin = p.value("binary", std::string());
            if (sha.size() != 64 || bin.empty()) continue;
            json e = pin;
            e["url"] = raiz + "/" + v + "/" + n->second + "/" + bin;
            e["sha256"] = sha;
            nuevas[clave] = e;
        }
        out["version"] = v;
        out["fuente"] = raiz + "/" + v + "/manifest.json";
        out["plataformas"] = nuevas;
    } else if (runtime == "opencode") {
        // OpenCode 2: el paquete @opencode/cli de npm y uno por plataforma
        // (@opencode/cli-<plataforma>), cada uno con su integridad SHA-512.
        const auto r = getJson("https://registry.npmjs.org/@opencode/cli/latest");
        const json j = json::parse(r.body, nullptr, false);
        if (r.status != 200 || !j.is_object()) return fallo("no se pudo leer la ultima version de opencode");
        const std::string v = j.value("version", std::string());
        if (!pareceVersion(v)) return fallo("version de opencode no reconocida");
        json nuevas = json::object();
        for (const auto& [clave, pin] : basePlats.items()) {
            const std::string npm = pin.value("npm", std::string());
            if (npm.empty()) continue;
            const auto rp = getJson("https://registry.npmjs.org/" + npm + "/" + v);
            const json m = json::parse(rp.body, nullptr, false);
            if (rp.status != 200 || !m.is_object()) continue;
            const json dist = m.value("dist", json::object());
            const std::string integ = dist.value("integrity", std::string());
            if (integ.rfind("sha512-", 0) != 0) continue; // sin hash: esa plataforma no
            json e = pin;
            e["url"] = dist.value("tarball", std::string());
            e.erase("sha256");
            e["sha512"] = integ.substr(7);
            nuevas[clave] = e;
        }
        out["version"] = v;
        out["fuente"] = "https://registry.npmjs.org/@opencode/cli/" + v;
        out["plataformas"] = nuevas;
    } else if (runtime == "antigravity") {
        // El manifiesto oficial de cada plataforma: version, url y SHA-512 (en hex; aca en
        // base64, como lo espera el instalador).
        const std::string raiz = base.value("fuente", std::string());
        if (raiz.rfind("https://", 0) != 0) return fallo("no se reconoce la fuente de Antigravity");
        std::string v;
        json nuevas = json::object();
        for (const auto& [clave, pin] : basePlats.items()) {
            const std::string man = pin.value("manifiesto", std::string());
            if (man.empty()) continue;
            const auto rm = getJson(raiz + man + ".json");
            const json m = json::parse(rm.body, nullptr, false);
            if (rm.status != 200 || !m.is_object()) continue;
            const std::string hex = m.value("sha512", std::string()), url = m.value("url", std::string()),
                              mv = m.value("version", std::string());
            if (hex.size() != 128 || url.rfind("https://", 0) != 0 || !pareceVersion(mv)) continue;
            if (v.empty()) v = mv;
            if (mv != v) continue; // todas de la misma version
            const QByteArray b64 = QByteArray::fromHex(QByteArray::fromStdString(hex)).toBase64();
            json e = pin;
            e["url"] = url;
            e.erase("sha256");
            e["sha512"] = b64.toStdString();
            nuevas[clave] = e;
        }
        if (v.empty()) return fallo("no se pudo leer el manifiesto de Antigravity");
        out["version"] = v;
        out["fuente"] = raiz;
        out["plataformas"] = nuevas;
    } else {
        return fallo("no se sabe buscar actualizaciones de " + runtime);
    }
    return out;
}

} // namespace ba::app::agent
