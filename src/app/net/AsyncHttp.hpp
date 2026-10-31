// ============================================================================
// src/app/net/AsyncHttp.hpp — HttpsClient en un thread de trabajo, con la
// respuesta de vuelta en el thread de `ctx` (event loop de Qt).
// ============================================================================
#pragma once

#include <QMetaObject>
#include <QObject>
#include <QPointer>

#include <functional>
#include <memory>
#include <thread>

#include "net/HttpsClient.hpp"
#include "platform/Hilo.hpp"

namespace ba::app::net {

/// Ejecuta `req` sin bloquear; `done` corre en el thread de `ctx` (y no corre
/// si `ctx` ya no existe). El progreso de subida, si hay, tambien vuelve ahi.
inline void asyncRequest(QObject* ctx, HttpRequest req, std::function<void(const HttpResponse&)> done,
                         std::function<void(std::uint64_t, std::uint64_t)> progress = {}) {
    QPointer<QObject> guard(ctx);
    if (progress) {
        req.progress = [guard, progress](std::uint64_t s, std::uint64_t t) {
            QMetaObject::invokeMethod(
                guard.data(),
                [guard, progress, s, t]() {
                    if (guard) progress(s, t);
                },
                Qt::QueuedConnection);
        };
    }
    std::thread([guard, req = std::move(req), done = std::move(done)]() {
        ba::platform::nombrarHilo("http");
        auto res = std::make_shared<HttpResponse>(HttpsClient::request(req));
        if (!guard) return;
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, done, res]() {
                if (guard) done(*res);
            },
            Qt::QueuedConnection);
    }).detach();
}

} // namespace ba::app::net
