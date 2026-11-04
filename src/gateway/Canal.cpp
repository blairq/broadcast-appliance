// src/gateway/Canal.cpp — ver Canal.hpp

#include "Canal.hpp"

#include <algorithm>
#include <chrono>
#include <vector>

#include "HttpIo.hpp"
#include "i18n/I18nTexto.hpp"

namespace ba::gateway::canal {

using json = nlohmann::json;

namespace {
std::int64_t ahoraMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::mutex& anotadasM() {
    static std::mutex m;
    return m;
}
std::vector<std::weak_ptr<Salida>>& anotadas() {
    static std::vector<std::weak_ptr<Salida>> v;
    return v;
}
} // namespace

bool Salida::enviar(const json& m) { return crudo(0x1, texto(m)); }

bool Salida::crudo(int opcode, const std::string& payload) {
    std::lock_guard<std::mutex> l(sendLock);
    return fd >= 0 && http::writeWsFrame(fd, opcode, payload);
}

void Salida::soltar() {
    std::lock_guard<std::mutex> l(sendLock);
    fd = -1;
}

void Salida::cortar() {
    std::lock_guard<std::mutex> l(sendLock);
    if (fd >= 0) {
        http::writeWsFrame(fd, 0x8, "");
        http::shutdownFd(fd);
    }
}

std::string texto(const json& m) { return m.dump(-1, ' ', false, json::error_handler_t::replace); }

bool aceptar(long long fd, const std::string& headers) {
    const std::string key = http::getHeader(headers, "Sec-WebSocket-Key");
    if (key.empty()) {
        http::sendResponse(fd, 400, "text/plain", ba::i18n::traducir("gateway", "falta Sec-WebSocket-Key") + "\n");
        return false;
    }
    const std::string ok = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                           "Sec-WebSocket-Accept: " +
                           http::base64(http::sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").data(), 20) +
                           "\r\n\r\n";
    if (!http::sendAll(fd, ok.data(), ok.size())) return false;
    // Promovida: sin plazo (una página puede estar callada minutos). El lazo vigila el
    // latido; el corte al cerrar lo da shutdown().
    http::setSocketTimeout(fd, 0);
    return true;
}

json primerMensaje(long long fd, int plazoMs) {
    const std::int64_t hasta = ahoraMs() + plazoMs;
    for (;;) {
        const int falta = static_cast<int>(hasta - ahoraMs());
        if (falta <= 0 || http::waitReadable(fd, falta) <= 0) return nullptr;
        std::string payload;
        int op = 0;
        if (!http::readWsFrame(fd, payload, op) || op == 0x8) return nullptr;
        if (op == 0x9) {
            http::writeWsFrame(fd, 0xA, payload);
            continue;
        }
        if (op == 0x2) {
            // Una página de antes del contrato (hablaba binario con largo) abierta desde
            // antes de actualizar: que se recargue y traiga la de ahora.
            http::writeWsFrame(fd, 0x1, texto(json{{"type", "recargar"}}));
            http::writeWsFrame(fd, 0x8, "");
            return nullptr;
        }
        if (op != 0x1) continue;
        json m = json::parse(payload, nullptr, false);
        if (!m.is_object() || m.value("type", std::string()) != "auth") {
            rechazar(fd, QT_TRANSLATE_NOOP("web/comun", "Mensaje inválido."), "mensaje");
            return nullptr;
        }
        if (m.contains("v") && !(m["v"].is_number_integer() && m["v"].get<int>() == kVersion)) {
            rechazar(fd, QT_TRANSLATE_NOOP("web/comun", "Esta página quedó vieja: recargala."), "version");
            return nullptr;
        }
        return m;
    }
}

void rechazar(long long fd, const std::string& error, const std::string& codigo) {
    json r{{"type", "auth"}, {"ok", false}, {"error", error}};
    if (!codigo.empty()) r["error_codigo"] = codigo;
    http::writeWsFrame(fd, 0x1, texto(r));
    http::writeWsFrame(fd, 0x8, "");
}

Fin leer(Salida& s, const Lazo& l, const std::function<bool(const json&)>& alMensaje) {
    const long long fd = s.fd;
    std::vector<std::int64_t> ventana;
    for (;;) {
        if (l.latidoMs > 0 && http::waitReadable(fd, l.latidoMs) <= 0) return Fin::Mudo;
        std::string payload;
        int op = 0;
        if (!http::readWsFrame(fd, payload, op) || op == 0x8) return Fin::Cerrado;
        if (op == 0x9) {
            s.crudo(0xA, payload);
            continue;
        }
        if (op != 0x1 || payload.size() > l.maxBytes) continue;
        if (l.porSegundo > 0) {
            const std::int64_t ahora = ahoraMs();
            ventana.erase(std::remove_if(ventana.begin(), ventana.end(), [&](std::int64_t t) { return ahora - t > 1000; }),
                          ventana.end());
            if (static_cast<int>(ventana.size()) >= l.porSegundo) return Fin::Exceso;
            ventana.push_back(ahora);
        }
        const json m = json::parse(payload, nullptr, false);
        if (!m.is_object()) continue;
        if (m.value("type", std::string()) == "ping") {
            s.enviar(json{{"type", "pong"}});
            continue;
        }
        if (!alMensaje(m)) return Fin::Cerrado;
    }
}

void anotar(const std::shared_ptr<Salida>& s) {
    std::lock_guard<std::mutex> l(anotadasM());
    auto& v = anotadas();
    v.erase(std::remove_if(v.begin(), v.end(), [](const std::weak_ptr<Salida>& w) { return w.expired(); }), v.end());
    v.push_back(s);
}

void aTodos(const json& m) {
    std::vector<std::shared_ptr<Salida>> vivas;
    {
        std::lock_guard<std::mutex> l(anotadasM());
        auto& v = anotadas();
        v.erase(std::remove_if(v.begin(), v.end(), [](const std::weak_ptr<Salida>& w) { return w.expired(); }), v.end());
        for (const auto& w : v)
            if (auto s = w.lock()) vivas.push_back(std::move(s));
    }
    const std::string t = texto(m);
    for (const auto& s : vivas) s->crudo(0x1, t);
}

} // namespace ba::gateway::canal
