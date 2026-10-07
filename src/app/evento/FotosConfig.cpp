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
        // Lo interno (.restaurar/, lo que empiece con punto) no es configuración.
        if (it->path().filename().string().rfind('.', 0) == 0) {
            if (it->is_directory(ec)) it.disable_recursion_pending();
            continue;
        }
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

nlohmann::json FotosConfig::conTapadosDe(const json& foto, const json& actual) {
    if (foto.is_string() && foto.get<std::string>() == kTapado) return actual;
    if (foto.is_object()) {
        json out = json::object();
        for (auto it = foto.begin(); it != foto.end(); ++it) {
            const json a = actual.is_object() && actual.contains(it.key()) ? actual[it.key()] : json();
            if (it.value().is_string() && it.value().get<std::string>() == kTapado && a.is_null()) continue; // no hay de dónde
            out[it.key()] = conTapadosDe(it.value(), a);
        }
        return out;
    }
    if (foto.is_array()) {
        json out = json::array();
        for (std::size_t i = 0; i < foto.size(); ++i)
            out.push_back(conTapadosDe(foto[i], actual.is_array() && i < actual.size() ? actual[i] : json()));
        return out;
    }
    return foto;
}

nlohmann::json FotosConfig::prepararRestauracion(const json& foto, const std::vector<std::string>& archivos,
                                                 const std::string& desde) {
    const json a = foto.value("archivos", json::object());
    if (a.empty()) return json{{"error", "la foto está vacía"}};
    const fs::path dir = config_ / ".restaurar";
    std::error_code ec;
    fs::remove_all(dir / "archivos", ec);
    json hechos = json::array(), omitidos = json::array();
    for (auto it = a.begin(); it != a.end(); ++it) {
        const std::string rel = it.key();
        if (!archivos.empty() && std::find(archivos.begin(), archivos.end(), rel) == archivos.end()) continue;
        if (rel == "launcher.json" || rel.find("..") != std::string::npos) {
            omitidos.push_back(rel);
            continue;
        }
        json contenido = leer(it.value().value("sha", std::string()));
        if (contenido.is_null() || (contenido.is_object() && contenido.contains("_texto"))) {
            omitidos.push_back(rel); // no está el contenido, o era un JSON roto
            continue;
        }
        std::ifstream f(config_ / fs::u8path(rel), std::ios::binary);
        const json actual = f ? json::parse(f, nullptr, false) : json();
        contenido = conTapadosDe(contenido, actual.is_discarded() ? json() : actual);
        const fs::path destino = dir / "archivos" / fs::u8path(rel);
        fs::create_directories(destino.parent_path(), ec);
        std::ofstream o(destino, std::ios::binary);
        o << contenido.dump(2);
        hechos.push_back(rel);
    }
    for (const std::string& pedido : archivos)
        if (!a.contains(pedido)) omitidos.push_back(pedido);
    if (hechos.empty()) return json{{"error", "nada para restaurar"}, {"omitidos", omitidos}};
    guardar(dir / "plan.json", json{{"desde", desde}, {"t_foto", foto.value("t", 0LL)}, {"archivos", hechos}});
    return json{{"archivos", hechos}, {"omitidos", omitidos}};
}

nlohmann::json FotosConfig::restauracionPendiente(const fs::path& config) {
    return cargar(config / ".restaurar" / "plan.json");
}

bool FotosConfig::cancelarRestauracion(const fs::path& config) {
    std::error_code ec;
    fs::remove(config / ".restaurar" / "plan.json", ec);
    fs::remove_all(config / ".restaurar" / "archivos", ec);
    return !ec;
}

nlohmann::json FotosConfig::aplicarPendiente(const fs::path& config, std::int64_t ahoraMs) {
    const json plan = restauracionPendiente(config);
    if (plan.is_null()) return nullptr;
    const fs::path dir = config / ".restaurar";
    const fs::path respaldo = dir / ("antes-" + std::to_string(ahoraMs));
    std::error_code ec;
    json aplicados = json::array();
    for (const auto& r : plan.value("archivos", json::array())) {
        if (!r.is_string()) continue;
        const std::string rel = r.get<std::string>();
        const fs::path nuevo = dir / "archivos" / fs::u8path(rel), destino = config / fs::u8path(rel);
        if (!fs::exists(nuevo, ec)) continue;
        if (fs::exists(destino, ec)) {
            fs::create_directories((respaldo / fs::u8path(rel)).parent_path(), ec);
            fs::copy_file(destino, respaldo / fs::u8path(rel), fs::copy_options::overwrite_existing, ec);
        }
        fs::create_directories(destino.parent_path(), ec);
        const fs::path tmp = destino.string() + ".tmp";
        fs::copy_file(nuevo, tmp, fs::copy_options::overwrite_existing, ec);
        fs::rename(tmp, destino, ec);
        if (!ec) aplicados.push_back(rel);
    }
    json hecho = plan;
    hecho["aplicado"] = ahoraMs;
    hecho["aplicados"] = aplicados;
    hecho["respaldo"] = respaldo.filename().string();
    guardar(dir / ("aplicado-" + std::to_string(ahoraMs) + ".json"), hecho);
    cancelarRestauracion(config);
    return hecho;
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
