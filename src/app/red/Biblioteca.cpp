// src/app/red/Biblioteca.cpp — ver Biblioteca.hpp

#include "red/Biblioteca.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <sstream>

namespace ba::app::red {

using json = nlohmann::json;

namespace {
std::string minus(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool coincide(const json& patrones, const std::string& texto) {
    if (texto.empty() || !patrones.is_array()) return false;
    const std::string t = minus(texto);
    for (const auto& p : patrones) {
        try {
            if (std::regex_search(t, std::regex(p.get<std::string>(), std::regex::icase))) return true;
        } catch (...) {
        }
    }
    return false;
}
} // namespace

// --- Oui ---------------------------------------------------------------------------

std::string Oui::normalizar(const std::string& mac) {
    // `arp` en macOS escribe "0:40:8c:f5:89:c4" (sin el cero de adelante).
    std::string out;
    std::string parte;
    const auto cerrar = [&]() {
        if (parte.empty()) return;
        if (parte.size() == 1) parte = "0" + parte;
        out += parte;
        parte.clear();
    };
    for (char c : mac) {
        if (c == ':' || c == '-' || c == '.') {
            cerrar();
            continue;
        }
        if (!std::isxdigit(static_cast<unsigned char>(c))) return {};
        parte += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (mac.find(':') == std::string::npos && mac.find('-') == std::string::npos && parte.size() == 2) cerrar();
    }
    cerrar();
    return out.size() == 12 ? out : std::string();
}

bool Oui::leerTexto(const std::string& texto) {
    std::istringstream in(texto);
    std::string linea;
    while (std::getline(in, linea)) {
        if (linea.empty() || linea[0] == '#') continue;
        const auto t1 = linea.find('\t'), t2 = linea.find('\t', t1 + 1);
        if (t1 == std::string::npos || t2 == std::string::npos) continue;
        const std::string hexa = linea.substr(0, t1);
        const int bits = std::atoi(linea.substr(t1 + 1, t2 - t1 - 1).c_str());
        std::uint64_t v = 0;
        try {
            v = std::stoull(hexa, nullptr, 16);
        } catch (...) {
            continue;
        }
        bloques_[{bits, v}] = linea.substr(t2 + 1);
    }
    return !bloques_.empty();
}

bool Oui::leer(const std::string& ruta) {
    std::ifstream f(ruta);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    return leerTexto(ss.str());
}

std::string Oui::fabricante(const std::string& mac) const {
    const std::string n = normalizar(mac);
    if (n.empty()) return {};
    const std::uint64_t v = std::stoull(n, nullptr, 16); // 48 bits
    for (int bits : {36, 28, 24}) {
        const std::uint64_t prefijo = v >> (48 - bits);
        // El prefijo se guardó con los dígitos hex que entran en `bits`
        // (redondeado hacia arriba): se compara igual.
        const int digitos = (bits + 3) / 4;
        const std::uint64_t comoTexto = v >> (48 - digitos * 4);
        auto it = bloques_.find({bits, comoTexto});
        if (it == bloques_.end()) it = bloques_.find({bits, prefijo});
        if (it != bloques_.end()) return it->second;
    }
    return {};
}

// --- Biblioteca ----------------------------------------------------------------------

bool Biblioteca::leerJson(const json& j) {
    if (!j.is_object() || !j.contains("marcas")) return false;
    marcas_ = j["marcas"];
    return true;
}

bool Biblioteca::leer(const std::string& ruta) {
    std::ifstream f(ruta);
    if (!f) return false;
    return leerJson(json::parse(f, nullptr, false));
}

Marca Biblioteca::reconocer(const Pistas& p) const {
    Marca mejor{"generica", "Genérica", "no se reconoció la marca", 0};
    const auto probar = [&](int confianza, const char* clave, const std::string& texto, const std::string& porque) {
        if (confianza <= mejor.confianza || texto.empty()) return;
        for (const auto& m : marcas_) {
            if (coincide(m["detectar"].value(clave, json::array()), texto)) {
                mejor = {m.value("id", ""), m.value("nombre", ""), porque, confianza};
                return;
            }
        }
    };
    probar(3, "onvif", p.onvifNombre, "ONVIF «" + p.onvifNombre + "»");
    probar(3, "onvif", p.onvifModelo, "ONVIF modelo «" + p.onvifModelo + "»");
    probar(2, "server", p.server, "su servidor web dice «" + p.server + "»");
    probar(2, "titulo", p.titulo, "su página web se llama «" + p.titulo + "»");
    probar(1, "mac", p.fabricanteMac, "MAC de " + p.fabricanteMac);
    if (mejor.confianza == 0)
        for (const auto& m : marcas_)
            for (const auto& pu : m["detectar"].value("puertos", json::array()))
                if (p.puertos.count(pu.get<int>())) {
                    mejor = {m.value("id", ""), m.value("nombre", ""), "tiene abierto el puerto " + std::to_string(pu.get<int>()), 0};
                    return mejor;
                }
    return mejor;
}

std::vector<Candidata> Biblioteca::candidatas(const std::string& marca, const std::string& host, int puertoRtsp,
                                              int canales) const {
    std::vector<Candidata> out;
    const auto agregar = [&](const json& m) {
        for (const auto& r : m.value("rtsp", json::array())) {
            const int puerto = r.value("puerto", puertoRtsp);
            const std::string base = "rtsp://" + host + (puerto == 554 ? "" : ":" + std::to_string(puerto));
            std::string ruta = r.value("ruta", std::string("/")), perfil = r.value("perfil", std::string());
            if (r.value("canales", false)) {
                for (int c = 2; c <= std::max(canales, 1); ++c) {
                    std::string rr = ruta, pp = perfil;
                    for (std::string* s : {&rr, &pp})
                        for (size_t i = s->find("{canal}"); i != std::string::npos; i = s->find("{canal}"))
                            s->replace(i, 7, std::to_string(c));
                    out.push_back({base + rr, pp, m.value("id", "")});
                }
                continue;
            }
            out.push_back({base + ruta, perfil, m.value("id", "")});
        }
    };
    for (const auto& m : marcas_)
        if (m.value("id", "") == marca) agregar(m);
    if (out.empty())
        for (const auto& m : marcas_)
            if (m.value("id", "") == "generica") agregar(m);
    return out;
}

json Biblioteca::ficha(const std::string& marca) const {
    for (const auto& m : marcas_)
        if (m.value("id", "") == marca) {
            json f = m;
            f.erase("detectar");
            return f;
        }
    return json::object();
}

} // namespace ba::app::red
