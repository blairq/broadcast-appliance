// src/gateway/Borde.cpp — ver Borde.hpp

#include "Borde.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

#include <mbedtls/sha256.h>

#include "HttpIo.hpp"
#include "i18n/I18n.hpp"

namespace ba::gateway::borde {

using ba::i18n::traducir;

namespace {
std::string minus(std::string s) {
    for (auto& c : s) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    return s;
}
} // namespace

std::string ipTexto(std::uint32_t ip) {
    const auto* b = reinterpret_cast<const unsigned char*>(&ip);
    return std::to_string(b[0]) + "." + std::to_string(b[1]) + "." + std::to_string(b[2]) + "." + std::to_string(b[3]);
}

bool ipLocal(std::uint32_t ip) {
    const auto* b = reinterpret_cast<const unsigned char*>(&ip);
    return b[0] == 10 || b[0] == 127 || (b[0] == 172 && b[1] >= 16 && b[1] <= 31) || (b[0] == 192 && b[1] == 168) ||
           (b[0] == 169 && b[1] == 254);
}

bool mismoOrigen(const std::string& headers) {
    const std::string origin = http::getHeader(headers, "Origin");
    if (origin.empty()) return true; // no es un navegador de otra página (curl, app nativa)
    const std::string host = minus(http::getHeader(headers, "Host"));
    std::string o = minus(origin);
    const auto p = o.find("://");
    if (p == std::string::npos) return false;
    o = o.substr(p + 3);
    return !host.empty() && o == host;
}

std::string hashConSal(const std::string& s) {
    unsigned char out[32];
    mbedtls_sha256(reinterpret_cast<const unsigned char*>(s.data()), s.size(), out, 0);
    static const char* h = "0123456789abcdef";
    std::string r;
    for (unsigned char c : out) {
        r += h[c >> 4];
        r += h[c & 15];
    }
    return r;
}

const char* tipoDe(const std::string& relOriginal) {
    // La única tabla de tipos del gateway (las páginas, los bordes, el tema y los archivos del
    // evento). Sin distinguir mayúsculas: «Acta.PDF» también.
    const std::string rel = minus(relOriginal);
    const auto termina = [&](const char* e) {
        const std::string x(e);
        return rel.size() >= x.size() && rel.compare(rel.size() - x.size(), x.size(), x) == 0;
    };
    if (termina(".html")) return "text/html; charset=utf-8";
    if (termina(".js") || termina(".mjs")) return "text/javascript; charset=utf-8";
    if (termina(".css")) return "text/css; charset=utf-8";
    if (termina(".json")) return "application/json";
    if (termina(".webmanifest")) return "application/manifest+json";
    if (termina(".svg")) return "image/svg+xml";
    if (termina(".png")) return "image/png";
    if (termina(".jpg") || termina(".jpeg")) return "image/jpeg";
    if (termina(".webp")) return "image/webp";
    if (termina(".gif")) return "image/gif";
    if (termina(".ico")) return "image/x-icon";
    if (termina(".pdf")) return "application/pdf";
    // Las fuentes (las del programa y las del tema del evento, Eventos/<x>/Tema/).
    if (termina(".woff2")) return "font/woff2";
    if (termina(".woff")) return "font/woff";
    if (termina(".ttf")) return "font/ttf";
    if (termina(".otf")) return "font/otf";
    return "application/octet-stream";
}

bool servir(long long fd, const std::string& webroot, const std::string& rel, const char* cabeceras) {
    std::error_code ec;
    const auto raiz = std::filesystem::weakly_canonical(webroot, ec);
    if (ec || webroot.empty()) return http::sendResponse(fd, 404, "text/plain", traducir("gateway", "no encontrado") + "\n"), true;
    const auto destino = std::filesystem::weakly_canonical(std::filesystem::path(webroot) / http::urlDecode(rel), ec);
    const std::string r = raiz.generic_string(), d = destino.generic_string();
    if (ec || d.size() < r.size() || d.compare(0, r.size(), r) != 0 || (d.size() > r.size() && d[r.size()] != '/'))
        return http::sendResponse(fd, 400, "text/plain", traducir("gateway", "ruta invalida") + "\n"), true;
    std::ifstream f(destino, std::ios::binary);
    if (!f || std::filesystem::is_directory(destino, ec))
        return http::sendResponse(fd, 404, "text/plain", traducir("gateway", "no encontrado") + "\n"), true;
    std::ostringstream ss;
    ss << f.rdbuf();
    http::sendResponse(fd, 200, tipoDe(rel), ss.str(), cabeceras);
    return true;
}

} // namespace ba::gateway::borde
