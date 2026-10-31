// ============================================================================
// src/app/evento/CierreEvento.cpp — ver CierreEvento.hpp
// ============================================================================
#include "evento/CierreEvento.hpp"
#include "arranque/ArchivoConfig.hpp"

#include "i18n/I18n.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>

#include "crypto/Sha256.hpp"
#include "evento/FotosConfig.hpp"

namespace ba::evento {

namespace fs = std::filesystem;

namespace {

bool digitos(const std::string& s, std::size_t i, std::size_t n) {
    if (i + n > s.size()) return false;
    for (std::size_t k = i; k < i + n; ++k)
        if (s[k] < '0' || s[k] > '9') return false;
    return true;
}

std::int64_t aMsUtc(std::tm tm, bool utc) {
#if defined(_WIN32)
    const std::time_t t = utc ? _mkgmtime(&tm) : std::mktime(&tm);
#else
    const std::time_t t = utc ? timegm(&tm) : std::mktime(&tm);
#endif
    return t < 0 ? 0 : static_cast<std::int64_t>(t) * 1000;
}

/// app.log, app.log.1, app.log.2 → [app.log.2, app.log.1, app.log] (lo más viejo primero).
std::map<std::string, std::vector<fs::path>> logsPorNombre(const fs::path& dir) {
    std::map<std::string, std::vector<std::pair<int, fs::path>>> m;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        std::string n = e.path().filename().string();
        int rot = 0;
        const auto punto = n.rfind('.');
        if (punto != std::string::npos && digitos(n, punto + 1, n.size() - punto - 1) && punto + 1 < n.size()) {
            rot = std::stoi(n.substr(punto + 1));
            n = n.substr(0, punto);
        }
        const std::string ext = fs::path(n).extension().string();
        if (ext != ".log" && ext != ".jsonl") continue;
        m[n].push_back({rot, e.path()});
    }
    std::map<std::string, std::vector<fs::path>> out;
    for (auto& [n, v] : m) {
        std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (auto& x : v) out[n].push_back(x.second);
    }
    return out;
}

std::string shaDeArchivo(const fs::path& p, const std::atomic<bool>* cancelar, bool& cortado) {
    std::ifstream f(p, std::ios::binary);
    ba::crypto::Sha256 h;
    std::vector<char> buf(1 << 20);
    while (f) {
        if (cancelar != nullptr && cancelar->load()) {
            cortado = true;
            return {};
        }
        f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        if (f.gcount() > 0) h.update(buf.data(), static_cast<std::size_t>(f.gcount()));
    }
    static const char* kHex = "0123456789abcdef";
    std::string hex;
    for (const std::uint8_t b : h.digest()) {
        hex += kHex[b >> 4];
        hex += kHex[b & 15];
    }
    return hex;
}

} // namespace

const std::vector<std::string>& CierreEvento::archivosAsamblea() {
    static const std::vector<std::string> a{"orden-del-dia.json", "quorum.json",       "votaciones-oficiales.json",
                                            "cuerpo.json",        "sesion-remota.json", "conferencia.json",
                                            "seguir-microfono.json"};
    return a;
}

int CierreEvento::copiarAsamblea(const fs::path& config, const fs::path& evento) {
    int n = 0;
    std::error_code ec;
    for (const std::string& a : archivosAsamblea()) {
        // Lo último (aunque esté encolado y no haya llegado al disco), en su esquema.
        const json j = ba::app::config::leerObjeto(config / a);
        if (j.empty()) continue;
        fs::create_directories(evento / "asamblea", ec);
        std::ofstream o(evento / "asamblea" / a, std::ios::binary);
        o << FotosConfig::sinSecretos(j).dump(1, '\t', false, nlohmann::json::error_handler_t::replace);
        ++n;
    }
    return n;
}

std::int64_t CierreEvento::horaDeLinea(const std::string& l) {
    if (!l.empty() && l[0] == '{') {
        const json j = json::parse(l, nullptr, false);
        if (j.is_object()) {
            for (const char* k : {"ms", "t"})
                if (j.contains(k) && j[k].is_number_integer()) return j[k].get<std::int64_t>();
        }
        return 0;
    }
    std::size_t i = 0;
    if (l.rfind("==== ", 0) == 0) i = 5;
    else if (!l.empty() && l[0] == '[') i = 1;
    // AAAA-MM-DDTHH:MM:SS
    if (!(digitos(l, i, 4) && l.size() > i + 19 - 1 && l[i + 4] == '-' && digitos(l, i + 5, 2) && l[i + 7] == '-' &&
          digitos(l, i + 8, 2) && (l[i + 10] == 'T' || l[i + 10] == ' ') && digitos(l, i + 11, 2) && l[i + 13] == ':' &&
          digitos(l, i + 14, 2) && l[i + 16] == ':' && digitos(l, i + 17, 2)))
        return 0;
    std::tm tm{};
    tm.tm_year = std::stoi(l.substr(i, 4)) - 1900;
    tm.tm_mon = std::stoi(l.substr(i + 5, 2)) - 1;
    tm.tm_mday = std::stoi(l.substr(i + 8, 2));
    tm.tm_hour = std::stoi(l.substr(i + 11, 2));
    tm.tm_min = std::stoi(l.substr(i + 14, 2));
    tm.tm_sec = std::stoi(l.substr(i + 17, 2));
    tm.tm_isdst = -1;
    std::size_t k = i + 19;
    int ms = 0;
    if (k < l.size() && l[k] == '.' && digitos(l, k + 1, 3)) {
        ms = std::stoi(l.substr(k + 1, 3));
        k += 4;
    }
    const bool utc = k < l.size() && l[k] == 'Z';
    const std::int64_t base = aMsUtc(tm, utc);
    return base == 0 ? 0 : base + ms;
}

nlohmann::json CierreEvento::recortarRegistros(const fs::path& logs, std::int64_t desdeMs, std::int64_t hastaMs,
                                               const fs::path& destino) {
    json out = json::object();
    std::error_code ec;
    for (const auto& [nombre, archivos] : logsPorNombre(logs)) {
        std::vector<std::string> lineas;
        for (const fs::path& a : archivos) {
            std::ifstream f(a, std::ios::binary);
            std::string l;
            bool dentro = false;
            while (std::getline(f, l)) {
                const std::int64_t t = horaDeLinea(l);
                if (t != 0) dentro = t >= desdeMs && (hastaMs == 0 || t <= hastaMs);
                if (dentro) lineas.push_back(l);
            }
        }
        if (lineas.empty()) continue;
        fs::create_directories(destino, ec);
        std::ofstream o(destino / nombre, std::ios::binary);
        for (const std::string& l : lineas) o << l << '\n';
        out[nombre] = lineas.size();
    }
    return out;
}

nlohmann::json CierreEvento::sellar(const fs::path& evento, std::int64_t ahoraMs, const std::atomic<bool>* cancelar) {
    json archivos = json::object();
    std::error_code ec;
    std::vector<fs::path> todos;
    for (fs::recursive_directory_iterator it(evento, ec), fin; !ec && it != fin; it.increment(ec)) {
        const std::string rel = fs::relative(it->path(), evento, ec).generic_string();
        if (it->is_directory(ec)) {
            if (rel == "Respaldos") it.disable_recursion_pending();
            continue;
        }
        if (rel == "sellos.json" || rel.size() > 4 && rel.compare(rel.size() - 4, 4, ".tmp") == 0) continue;
        // Lo que se sigue escribiendo mientras el evento existe no se sella:
        // cambiaría al otro segundo. (La bitácora sí: al sellar, el evento ya terminó.)
        if (rel == "config/ultima.json") continue;
        todos.push_back(it->path());
    }
    std::sort(todos.begin(), todos.end());
    for (const fs::path& p : todos) {
        bool cortado = false;
        const std::string sha = shaDeArchivo(p, cancelar, cortado);
        if (cortado) return nullptr;
        archivos[fs::relative(p, evento, ec).generic_string()] = json{{"sha256", sha}, {"bytes", fs::file_size(p, ec)}};
    }
    json s{{"t", ahoraMs}, {"archivos", archivos}};
    const fs::path tmp = evento / "sellos.json.tmp";
    {
        std::ofstream o(tmp, std::ios::binary);
        o << s.dump(1, '\t', false, nlohmann::json::error_handler_t::replace);
    }
    fs::rename(tmp, evento / "sellos.json", ec);
    return s;
}

nlohmann::json CierreEvento::verificar(const fs::path& evento, const std::atomic<bool>* cancelar) {
    std::ifstream f(evento / "sellos.json", std::ios::binary);
    const json s = f ? json::parse(f, nullptr, false) : json();
    if (!s.is_object())
        return json{{"ok", false},
                    {"error", ba::i18n::traducir("evento", "el evento no tiene sellos.json (no se cerró, o se está sellando)")}};
    const json a = s.value("archivos", json::object());
    json cambiados = json::array(), faltan = json::array(), nuevos = json::array();
    std::error_code ec;
    for (auto it = a.begin(); it != a.end(); ++it) {
        const fs::path p = evento / fs::u8path(it.key());
        if (!fs::exists(p, ec)) {
            faltan.push_back(it.key());
            continue;
        }
        bool cortado = false;
        const std::string sha = shaDeArchivo(p, cancelar, cortado);
        if (cortado) return json{{"ok", false}, {"error", "cancelado"}};
        if (sha != it.value().value("sha256", std::string())) cambiados.push_back(it.key());
    }
    for (fs::recursive_directory_iterator it(evento, ec), fin; !ec && it != fin; it.increment(ec)) {
        if (it->is_directory(ec)) continue;
        const std::string rel = fs::relative(it->path(), evento, ec).generic_string();
        if (rel == "sellos.json" || rel == "config/ultima.json" || rel.rfind("Respaldos/", 0) == 0) continue;
        if (!a.contains(rel)) nuevos.push_back(rel);
    }
    return json{{"ok", cambiados.empty() && faltan.empty()},
                {"sellado", s.value("t", 0LL)},
                {"archivos", a.size()},
                {"cambiados", cambiados},
                {"faltan", faltan},
                {"nuevos", nuevos}};
}

} // namespace ba::evento
