// src/app/net/ClienteCapacidad.cpp — lo de red de la prueba de capacidad (ver el .hpp)

#include "net/ClienteCapacidad.hpp"

#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>

#include <algorithm>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#include "i18n/I18n.hpp"
#include "net/HttpsClient.hpp"

namespace ba::app::net {

using json = nlohmann::json;

namespace {
/// Corre `trabajo` en el pool y `listo` con lo que devuelva, en el hilo de `quien`.
void enElFondo(QObject* quien, std::function<json()> trabajo, std::function<void(const json&)> listo) {
    QPointer<QObject> q(quien);
    QThreadPool::globalInstance()->start([q, trabajo = std::move(trabajo), listo = std::move(listo)]() {
        json r = trabajo();
        if (!q) return;
        QMetaObject::invokeMethod(
            q.data(),
            [q, r = std::move(r), listo]() {
                if (q && listo) listo(r);
            },
            Qt::QueuedConnection);
    });
}

std::string sinBarra(const QString& url) {
    std::string u = url.trimmed().toStdString();
    while (!u.empty() && u.back() == '/') u.pop_back();
    return u;
}

double percentil(std::vector<double> v, double p) {
    if (v.empty()) return -1;
    std::sort(v.begin(), v.end());
    const auto i = static_cast<std::size_t>(p * static_cast<double>(v.size() - 1) + 0.5);
    return v[std::min(i, v.size() - 1)];
}

/// Un GET /healthz: ms o -1 si no contestó.
double ping(const std::string& base) {
    HttpRequest rq;
    rq.url = base + "/healthz";
    rq.timeoutMs = 4000;
    // pinggy sin cuenta le muestra un aviso a los navegadores; con esto no.
    rq.headers = {{"X-Pinggy-No-Screen", "1"}};
    const auto t0 = std::chrono::steady_clock::now();
    const HttpResponse r = HttpsClient::request(rq);
    if (r.status != 200) return -1;
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}
} // namespace

void pedirMedidaRemota(QObject* quien, const QString& url, const QString& credencial, const QString& huella,
                       std::function<void(const json&)> listo) {
    const std::string base = sinBarra(url);
    const std::string cred = credencial.toStdString();
    const std::string h = huella.trimmed().toStdString();
    enElFondo(
        quien,
        [base, cred, h]() {
            HttpRequest rq;
            rq.url = base + "/api/prueba-sala/medida";
            rq.timeoutMs = 3000;
            rq.headers = {{"X-BA-Prueba", cred}, {"X-Pinggy-No-Screen", "1"}, {"Cache-Control", "no-store"}};
            rq.huellaSha256 = h;
            const HttpResponse r = HttpsClient::request(rq);
            json out = json::parse(r.body, nullptr, false);
            if (!out.is_object()) out = json::object();
            out["status"] = r.status;
            out["ok"] = r.status == 200 && out.value("ok", false);
            if (!r.error.empty()) out["error"] = r.error;
            else if (r.status == 401) out["error"] = ba::i18n::traducir("capacidad", "la credencial del otro equipo no vale (venció o está mal copiada)");
            else if (r.status == 404) out["error"] = ba::i18n::traducir("capacidad", "el otro equipo no tiene la prueba de capacidad (otra versión)");
            else if (r.status != 200) out["error"] = ba::i18n::llenar(ba::i18n::traducir("capacidad", "el otro equipo contestó %1"), {std::to_string(r.status)});
            return out;
        },
        std::move(listo));
}

void medirIdaYVuelta(QObject* quien, const QString& urlPublica, const QString& secreto, IdaYVuelta cfg,
                     std::shared_ptr<std::atomic<bool>> cancelar, std::function<void(const json&)> listo) {
    const std::string base = sinBarra(urlPublica);
    const std::string sec = secreto.toStdString();
    enElFondo(
        quien,
        [base, sec, cfg, cancelar]() {
            json out{{"ok", false}};
            // En reposo: la demora y lo que no contesta.
            std::vector<double> reposo;
            int perdidos = 0, total = 0;
            for (int i = 0; i < cfg.pings && !(cancelar && *cancelar); ++i) {
                ++total;
                const double ms = ping(base);
                if (ms < 0) ++perdidos;
                else reposo.push_back(ms);
            }
            if (reposo.empty()) {
                out["error"] = ba::i18n::traducir("capacidad", "la dirección pública no contesta (¿el túnel está listo?)");
                return out;
            }
            // Cargado: N conexiones bajando relleno y la demora mientras tanto.
            std::atomic<long long> bytes{0};
            std::atomic<bool> fin{false};
            std::mutex mu;
            std::string error;
            const auto t0 = std::chrono::steady_clock::now();
            const auto hasta = t0 + std::chrono::milliseconds(cfg.cargaMs);
            std::vector<std::thread> hilos;
            for (int k = 0; k < cfg.conexiones; ++k)
                hilos.emplace_back([&]() {
                    while (!fin && std::chrono::steady_clock::now() < hasta && !(cancelar && *cancelar)) {
                        HttpRequest rq;
                        rq.url = base + "/api/capacidad/relleno?kb=" + std::to_string(cfg.kbPorPedido);
                        rq.timeoutMs = 10000;
                        rq.headers = {{"X-BA-Medir", sec}, {"X-Pinggy-No-Screen", "1"}};
                        rq.bodyChunk = [&bytes](const char*, std::size_t n) { bytes += static_cast<long long>(n); };
                        const HttpResponse r = HttpsClient::request(rq);
                        if (r.status != 200) {
                            std::lock_guard<std::mutex> l(mu);
                            if (error.empty()) error = !r.error.empty() ? r.error : "HTTP " + std::to_string(r.status);
                            break;
                        }
                    }
                });
            std::vector<double> cargado;
            while (std::chrono::steady_clock::now() < hasta && !(cancelar && *cancelar)) {
                ++total;
                const double ms = ping(base);
                if (ms < 0) ++perdidos;
                else cargado.push_back(ms);
                std::this_thread::sleep_for(std::chrono::milliseconds(400));
            }
            fin = true;
            for (std::thread& h : hilos) h.join();
            const double seg = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (cancelar && *cancelar) {
                out["error"] = ba::i18n::traducir("capacidad", "cancelada");
                return out;
            }
            out["bytes"] = bytes.load();
            out["kbps"] = seg > 0 ? std::lround(static_cast<double>(bytes.load()) * 8.0 / 1000.0 / seg) : 0;
            out["rttMs"] = std::lround(percentil(reposo, 0.5));
            out["rttP95Ms"] = std::lround(percentil(reposo, 0.95));
            out["rttCargaMs"] = cargado.empty() ? -1 : std::lround(percentil(cargado, 0.5));
            out["perdida"] = total > 0 ? static_cast<double>(perdidos) / total : 0.0;
            out["ok"] = bytes.load() > 0;
            if (!error.empty()) out["aviso"] = error;
            if (bytes.load() == 0) out["error"] = error.empty() ? ba::i18n::traducir("capacidad", "no bajó nada por el túnel") : error;
            return out;
        },
        std::move(listo));
}

} // namespace ba::app::net
