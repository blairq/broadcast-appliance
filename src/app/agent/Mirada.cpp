// ============================================================================
// src/app/agent/Mirada.cpp — ver Mirada.hpp
// ============================================================================
#include "agent/Mirada.hpp"

#include <QBuffer>
#include <QCoreApplication>
#include <QImage>
#include <QPointer>

#include <thread>

#include "agent/ModeloPropio.hpp"
#include "ajustes/Ajustes.hpp"
#include "net/Credentials.hpp"
#include "net/HttpsClient.hpp"
#include "platform/Paths.hpp"

namespace ba::app::agent {

using nlohmann::json;

bool fuentePropia(const std::filesystem::path& dataRoot, FuenteMirada& out, std::string* error) {
    const ModeloPropio mp = ModeloPropio::leer(dataRoot / "Config" / "modelo-propio.json");
    if (!mp.completo()) {
        if (error != nullptr) *error = "no hay un modelo propio configurado";
        return false;
    }
    QByteArray clave;
    const QString cred = QString::fromStdWString(ba::platform::credentialsDir().wstring());
    if (!ba::app::cred::read(cred, "modelo-propio-api-key", clave)) {
        if (error != nullptr) *error = "falta la clave del modelo propio";
        return false;
    }
    out.url = mp.url;
    out.clave = clave.trimmed().toStdString();
    out.modelo = mp.modelo;
    out.nombre = "modelo propio";
    return true;
}

bool modeloPropioListo(const std::filesystem::path& dataRoot) {
    FuenteMirada f;
    return fuentePropia(dataRoot, f);
}

std::string mirarCon(const FuenteMirada& f, const QImage& img, const PedidoMirada& p, std::string& error) {
    if (img.isNull()) {
        error = "no hay imagen";
        return {};
    }
    int x = 0, y = 0, w = 0, h = 0;
    recorteMirada(img.width(), img.height(), p, x, y, w, h);
    QImage parte = p.recorteDesde > 0 ? img.copy(x, y, w, h) : img;
    if (p.anchoMax > 0 && parte.width() > p.anchoMax) parte = parte.scaledToWidth(p.anchoMax, Qt::SmoothTransformation);
    QByteArray jpg;
    QBuffer buf(&jpg);
    buf.open(QIODevice::WriteOnly);
    if (!parte.save(&buf, "JPG", p.calidad)) {
        error = "no se pudo codificar la imagen";
        return {};
    }
    const json body = cuerpoMirada(f.modelo, p, jpg.toBase64().toStdString());

    std::string url = f.url;
    while (!url.empty() && url.back() == '/')
        url.pop_back();
    net::HttpRequest req;
    req.method = "POST";
    req.url = url + "/chat/completions";
    req.headers = {{"Content-Type", "application/json"}};
    if (!f.clave.empty()) req.headers.push_back({"Authorization", "Bearer " + f.clave});
    req.body = body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    req.timeoutMs = p.esperaMs > 0 ? p.esperaMs : ba::ajustes::sAMs("mcp.imagenTimeoutS", 45);
    const net::HttpResponse r = net::HttpsClient::request(req);
    if (r.status == 0) {
        error = "no contesta: " + r.error;
        return {};
    }
    if (r.status >= 400) {
        error = "el servidor contesto " + std::to_string(r.status);
        return {};
    }
    try {
        const json d = json::parse(r.body);
        const std::string texto = d["choices"][0]["message"].value("content", std::string());
        if (texto.empty()) error = "el modelo contesto vacio";
        return texto;
    } catch (const json::exception& e) {
        error = std::string("no se entendio la respuesta: ") + e.what();
        return {};
    }
}

std::string mirarConElModeloPropio(const std::filesystem::path& dataRoot, const QImage& img, const PedidoMirada& p,
                                   std::string& error) {
    FuenteMirada f;
    if (!fuentePropia(dataRoot, f, &error)) return {};
    return mirarCon(f, img, p, error);
}

void mirarAparte(FuenteMirada f, const QImage& img, PedidoMirada p, QObject* contexto, RespuestaMirada luego) {
    QPointer<QObject> ctx(contexto);
    // Un hilo suelto, como `imagen_preguntar`: el cliente HTTP bloquea y el tope
    // de espera (esperaMs) acota cuánto vive.
    std::thread([f = std::move(f), img, p = std::move(p), ctx, luego = std::move(luego)]() mutable {
        std::string error;
        std::string texto = mirarCon(f, img, p, error);
        QMetaObject::invokeMethod(
            qApp,
            [ctx, luego = std::move(luego), texto = std::move(texto), error = std::move(error)]() mutable {
                if (ctx.isNull() || !luego) return;
                luego(std::move(texto), std::move(error));
            },
            Qt::QueuedConnection);
    }).detach();
}

} // namespace ba::app::agent
