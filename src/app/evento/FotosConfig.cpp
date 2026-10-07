// ============================================================================
// src/app/evento/FotosConfig.cpp — ver FotosConfig.hpp
// ============================================================================
#include "evento/FotosConfig.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#include "crypto/Sha256.hpp"

namespace ba::evento {

namespace fs = std::filesystem;

namespace {

// Las mismas palabras que tapa la traza del MCP (mcp/McpTraza.cpp).
const char* const kSecretas[] = {"token", "pin", "clave", "password", "contrasena", "secret",
                                 "key",   "cookie", "authorization", "bearer"};

bool nombreSecreto(std::string k) {
    std::transform(k.begin(), k.end(), k.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const char* s : kSecretas)
        if (k.find(s) != std::string::npos) return true;
    return false;
}

/// Una tira larga y sin espacios: una clave de API o un token, no «gate» ni «umbral».
bool valorSecreto(const nlohmann::json& v) {
    if (!v.is_string()) return false;
    const std::string s = v.get<std::string>();
    return s.size() >= 16 && s.find(' ') == std::string::npos;
}

bool entra(const fs::path& p) {
    return p.extension() == ".json";
}

std::int64_t mtimeDe(const fs::path& p) {
    std::error_code ec;
    const auto t = fs::last_write_time(p, ec);
    return ec ? 0 : static_cast<std::int64_t>(t.time_since_epoch().count());
}

} // namespace

FotosConfig::FotosConfig(fs::path config, fs::path objetos) : config_(std::move(config)), objetos_(std::move(objetos)) {}

nlohmann::json FotosConfig::sinSecretos(const json& j) {
    if (j.is_object()) {
        json out = json::object();
        for (auto it = j.begin(); it != j.end(); ++it)
            out[it.key()] = nombreSecreto(it.key()) && valorSecreto(it.value()) ? json(kTapado) : sinSecretos(it.value());
        return out;
    }
    if (j.is_array()) {
        json out = json::array();
        for (const auto& x : j) out.push_back(sinSecretos(x));
        return out;
    }
    return j;
}

nlohmann::json FotosConfig::foto(std::int64_t ahoraMs) {
    json archivos = json::object();
    std::error_code ec;
    std::map<std::string, Visto> vistos;
    for (fs::recursive_directory_iterator it(config_, fs::directory_options::skip_permission_denied, ec), fin;
         !ec && it != fin; it.increment(ec)) {
        if (!it->is_regular_file(ec) || !entra(it->path())) continue;
        const std::uintmax_t bytes = it->file_size(ec);
        if (ec || bytes > 4u * 1024u * 1024u) continue;
        const std::string rel = fs::relative(it->path(), config_, ec).generic_string();
        if (ec || rel.empty()) continue;
        const std::int64_t mt = mtimeDe(it->path());
        auto previo = vistos_.find(rel);
        Visto v;
        if (previo != vistos_.end() && previo->second.mtime == mt && previo->second.bytes == bytes) {
            v = previo->second;
        } else {
            std::ifstream f(it->path(), std::ios::binary);
            std::stringstream ss;
            ss << f.rdbuf();
            json j = json::parse(ss.str(), nullptr, false);
            // Un JSON roto (se estaba escribiendo) se guarda tal cual, como texto.
            const std::string contenido =
                j.is_discarded() ? json{{"_texto", ss.str()}}.dump() : sinSecretos(j).dump(1, '\t');
            v.mtime = mt;
            v.bytes = bytes;
            v.sha = ba::crypto::sha256Hex(contenido.data(), contenido.size());
            const fs::path obj = objetos_ / (v.sha + ".json");
            if (!fs::exists(obj, ec)) {
                fs::create_directories(objetos_, ec);
                const fs::path tmp = obj.string() + ".tmp";
                {
                    std::ofstream o(tmp, std::ios::binary);
                    o << contenido;
                }
                fs::rename(tmp, obj, ec);
            }
        }
        vistos[rel] = v;
        archivos[rel] = json{{"sha", v.sha}, {"bytes", bytes}};
    }
    vistos_ = std::move(vistos);
    return json{{"t", ahoraMs}, {"archivos", archivos}};
}

nlohmann::json FotosConfig::leer(const std::string& sha) const {
    if (sha.empty() || sha.find_first_not_of("0123456789abcdef") != std::string::npos) return nullptr;
    std::ifstream f(objetos_ / (sha + ".json"), std::ios::binary);
    if (!f) return nullptr;
    json j = json::parse(f, nullptr, false);
    return j.is_discarded() ? json(nullptr) : j;
}

nlohmann::json FotosConfig::comparar(const json& antes, const json& despues, std::size_t tope) const {
    json out = json::array();
    const json a = antes.value("archivos", json::object()), d = despues.value("archivos", json::object());
    const auto uno = [&](const std::string& archivo, const std::string& shaA, const std::string& shaD) {
        json c{{"archivo", archivo}};
        if (shaA.empty()) c["que"] = "agregado";
        else if (shaD.empty()) c["que"] = "quitado";
        else c["que"] = "cambiado";
        if (!shaA.empty()) c["antes"] = shaA;
        if (!shaD.empty()) c["despues"] = shaD;
        const json ja = shaA.empty() ? json::object() : leer(shaA);
        const json jd = shaD.empty() ? json::object() : leer(shaD);
        if (!ja.is_null() && !jd.is_null()) {
            const json p = json::diff(ja, jd);
            c["total"] = p.size();
            json r = json::array();
            for (std::size_t i = 0; i < std::min(tope, p.size()); ++i) {
                json op = p[i];
                // Un valor enorme (una escena entera) no sirve para leer el cambio.
                if (op.contains("value") && op["value"].dump().size() > 400) op["value"] = "…(grande)";
                r.push_back(std::move(op));
            }
            c["cambios"] = r;
        }
        out.push_back(std::move(c));
    };
    for (auto it = a.begin(); it != a.end(); ++it) {
        const std::string shaA = it.value().value("sha", std::string());
        const std::string shaD = d.contains(it.key()) ? d[it.key()].value("sha", std::string()) : std::string();
        if (shaA != shaD) uno(it.key(), shaA, shaD);
    }
    for (auto it = d.begin(); it != d.end(); ++it)
        if (!a.contains(it.key())) uno(it.key(), {}, it.value().value("sha", std::string()));
    return out;
}

bool FotosConfig::guardar(const fs::path& archivo, const json& foto) {
    std::error_code ec;
    fs::create_directories(archivo.parent_path(), ec);
    const fs::path tmp = archivo.string() + ".tmp";
    {
        std::ofstream o(tmp, std::ios::binary);
        if (!o) return false;
        o << foto.dump(1, '\t');
    }
    fs::rename(tmp, archivo, ec);
    return !ec;
}

nlohmann::json FotosConfig::cargar(const fs::path& archivo) {
    std::ifstream f(archivo, std::ios::binary);
    if (!f) return nullptr;
    json j = json::parse(f, nullptr, false);
    return j.is_discarded() ? json(nullptr) : j;
}

} // namespace ba::evento
