// ============================================================================
// src/app/agent/Actualizacion.cpp — ver Actualizacion.hpp
// ============================================================================
#include "agent/Actualizacion.hpp"

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
        // El release de GitHub: cada archivo trae su digest SHA-256.
        const std::string fuente = base.value("fuente", std::string());
        const std::string pre = "https://github.com/";
        const auto rel = fuente.find("/releases/");
        if (fuente.rfind(pre, 0) != 0 || rel == std::string::npos)
            return fallo("no se reconoce la fuente de opencode");
        const std::string repo = fuente.substr(pre.size(), rel - pre.size());
        const auto r = getJson("https://api.github.com/repos/" + repo + "/releases/latest");
        const json j = json::parse(r.body, nullptr, false);
        if (r.status != 200 || !j.is_object()) return fallo("no se pudo leer el ultimo release de opencode");
        std::string v = j.value("tag_name", std::string());
        if (!v.empty() && v[0] == 'v') v.erase(0, 1);
        if (!pareceVersion(v)) return fallo("version de opencode no reconocida");
        json nuevas = json::object();
        for (const auto& [clave, pin] : basePlats.items()) {
            // Los archivos se llaman igual en cada release (opencode-darwin-arm64.zip...).
            const std::string url = pin.value("url", std::string());
            const std::string archivo = url.substr(url.find_last_of('/') + 1);
            for (const auto& a : j.value("assets", json::array())) {
                if (a.value("name", std::string()) != archivo) continue;
                const std::string d = a.value("digest", std::string());
                if (d.rfind("sha256:", 0) != 0 || d.size() != 7 + 64) break; // sin hash: esa plataforma no
                json e = pin;
                e["url"] = a.value("browser_download_url", std::string());
                e["sha256"] = d.substr(7);
                nuevas[clave] = e;
                break;
            }
        }
        out["version"] = v;
        out["fuente"] = "https://github.com/" + repo + "/releases/tag/" + j.value("tag_name", std::string());
        out["plataformas"] = nuevas;
    } else if (runtime == "gemini") {
        // El paquete de npm (con su integridad SHA-512). Node queda el fijado.
        const auto r = getJson("https://registry.npmjs.org/@google/gemini-cli/latest");
        const json j = json::parse(r.body, nullptr, false);
        if (r.status != 200 || !j.is_object())
            return fallo("no se pudo leer la ultima version de Gemini CLI");
        const std::string v = j.value("version", std::string());
        const json dist = j.value("dist", json::object());
        const std::string integ = dist.value("integrity", std::string());
        if (!pareceVersion(v) || integ.rfind("sha512-", 0) != 0)
            return fallo("npm no publico la integridad de Gemini CLI");
        json pkg = base.value("paquete", json::object());
        pkg["url"] = dist.value("tarball", std::string());
        pkg.erase("sha256");
        pkg["sha512"] = integ.substr(7);
        out["version"] = v;
        out["fuente"] = "https://registry.npmjs.org/@google/gemini-cli/" + v;
        out["paquete"] = pkg;
    } else {
        return fallo("no se sabe buscar actualizaciones de " + runtime);
    }
    return out;
}

} // namespace ba::app::agent
