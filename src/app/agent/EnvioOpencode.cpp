// ============================================================================
// src/app/agent/EnvioOpencode.cpp — ver EnvioOpencode.hpp
// ============================================================================
#include "agent/EnvioOpencode.hpp"

#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QUrl>

#include <algorithm>

#include <nlohmann/json.hpp>

namespace ba::app::agent {

using json = nlohmann::json;

namespace {
constexpr int kFallosAntesDeDescartar = 5;

std::string s(const QString& q) { return q.toStdString(); }

json leer(const QByteArray& b) {
    json j = json::parse(b.toStdString(), nullptr, false);
    return j.is_discarded() ? json::object() : j;
}

/// Lo que dice el servidor cuando algo no anda ({_tag, message}), o el estado.
QString motivo(int estado, const QByteArray& r) {
    const json j = leer(r);
    if (j.is_object() && j.contains("message") && j["message"].is_string())
        return QString::fromStdString(j["message"].get<std::string>());
    return estado == 0 ? QObject::tr("el servidor de OpenCode no contesta") : QObject::tr("HTTP %1").arg(estado);
}
} // namespace

// --- La cola ------------------------------------------------------------------------

void ColaAutomatica::agregar(const QString& texto, const QStringList& adjuntos) {
    if (texto.trimmed().isEmpty() && adjuntos.isEmpty()) return;
    items_.push_back({texto.trimmed(), adjuntos});
}

ColaAutomatica::Lote ColaAutomatica::tomar(int max) {
    Lote l;
    QStringList textos;
    while (!items_.empty() && static_cast<int>(l.items.size()) < std::max(1, max)) {
        Item it = std::move(items_.front());
        items_.pop_front();
        if (!it.texto.isEmpty()) textos << it.texto;
        for (const QString& f : it.adjuntos)
            if (!l.adjuntos.contains(f)) l.adjuntos << f;
        l.items.push_back(std::move(it));
    }
    l.texto = textos.join("\n\n");
    return l;
}

void ColaAutomatica::devolver(Lote lote) {
    for (auto it = lote.items.rbegin(); it != lote.items.rend(); ++it)
        items_.push_front(std::move(*it));
}

// --- El envío -----------------------------------------------------------------------

EnvioOpencode::EnvioOpencode(QObject* parent) : QObject(parent) {
    reloj_.setSingleShot(true);
    connect(&reloj_, &QTimer::timeout, this, &EnvioOpencode::intentar);
}

EnvioOpencode::~EnvioOpencode() = default;

void EnvioOpencode::conectar(const QString& url, const QString& clave) {
    desconectar();
    if (url.isEmpty()) return;
    if (red_ == nullptr) red_ = new QNetworkAccessManager(this);
    // La API pide la clave del servidor (OPENCODE_SERVER_PASSWORD) por autenticación básica.
    const QByteArray auth = "Basic " + QByteArray("opencode:" + clave.toUtf8()).toBase64();
    QPointer<QNetworkAccessManager> red(red_);
    http_ = [red, url, auth](const QByteArray& metodo, const QString& ruta, const QByteArray& cuerpo,
                             std::function<void(int, const QByteArray&)> listo) {
        if (!red) return listo(0, {});
        QNetworkRequest pedido(QUrl(url + ruta));
        pedido.setRawHeader("Authorization", auth);
        pedido.setTransferTimeout(15000);
        if (!cuerpo.isEmpty()) pedido.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        QNetworkReply* r = red->sendCustomRequest(pedido, metodo, cuerpo);
        QObject::connect(r, &QNetworkReply::finished, r, [r, listo]() {
            r->deleteLater();
            listo(r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), r->readAll());
        });
    };
}

void EnvioOpencode::desconectar() {
    ++generacion_;
    http_ = {};
    sesion_.clear();
    cola_.vaciar();
    reloj_.stop();
    enVuelo_ = false;
    fallos_ = 0;
}

void EnvioOpencode::asegurarSesion(const QString& guardada, const QString& titulo, const QString& carpeta,
                                   std::function<void(const QString&, const QString&)> listo) {
    if (!http_) return listo({}, tr("No hay servidor de OpenCode."));
    QPointer<EnvioOpencode> yo(this);
    const quint64 gen = generacion_;
    auto crear = [yo, gen, titulo, carpeta, listo]() {
        if (!yo || gen != yo->generacion_) return;
        const json cuerpo{{"title", s(titulo)},
                          {"location", {{"directory", s(QDir::toNativeSeparators(QDir::cleanPath(carpeta)))}}}};
        yo->http_("POST", "/api/session", QByteArray::fromStdString(cuerpo.dump()),
                  [yo, gen, listo](int st, const QByteArray& r) {
                      if (!yo || gen != yo->generacion_) return;
                      const json j = leer(r);
                      const json d = j.value("data", json::object());
                      const std::string id = d.is_object() ? d.value("id", std::string()) : std::string();
                      if (st != 200 || id.empty())
                          return listo({}, tr("OpenCode no creó la sesión del evento: %1").arg(motivo(st, r)));
                      yo->setSesion(QString::fromStdString(id));
                      listo(yo->sesion_, {});
                  });
    };
    if (guardada.isEmpty()) return crear();
    http_("GET", "/api/session/" + guardada, {}, [yo, gen, guardada, crear, listo](int st, const QByteArray& r) {
        if (!yo || gen != yo->generacion_) return;
        if (st == 200) {
            yo->setSesion(guardada);
            return listo(guardada, {});
        }
        // La borraron (limpieza de chats) o es de un perfil que ya no está: otra.
        if (st == 404 || st == 400) return crear();
        listo({}, tr("OpenCode no encontró la sesión del evento: %1").arg(motivo(st, r)));
    });
}

void EnvioOpencode::setSesion(const QString& id) {
    sesion_ = id;
    if (!cola_.vacia()) QTimer::singleShot(0, this, &EnvioOpencode::intentar);
}

void EnvioOpencode::encolar(const QString& texto, const QStringList& adjuntos) {
    cola_.agregar(texto, adjuntos);
    intentar();
}

QByteArray EnvioOpencode::cuerpoPrompt(const ColaAutomatica::Lote& lote) {
    // `queue`: a la bandeja de la sesión, se entrega cuando termina lo que está haciendo
    // (`steer` lo metería en medio del turno en curso).
    json j{{"text", s(lote.texto)}, {"delivery", "queue"}};
    if (!lote.adjuntos.isEmpty()) {
        json files = json::array();
        for (const QString& f : lote.adjuntos)
            files.push_back({{"uri", s(QUrl::fromLocalFile(QFileInfo(f).absoluteFilePath()).toString())},
                             {"name", s(QFileInfo(f).fileName())}});
        j["files"] = files;
    }
    return QByteArray::fromStdString(j.dump(-1, ' ', false, json::error_handler_t::replace));
}

void EnvioOpencode::reintentarMasTarde() {
    if (!reloj_.isActive()) reloj_.start(esperaMs_);
}

void EnvioOpencode::intentar() {
    if (!http_ || sesion_.isEmpty() || cola_.vacia() || enVuelo_) return;
    enVuelo_ = true;
    QPointer<EnvioOpencode> yo(this);
    const quint64 gen = generacion_;
    // ¿Está respondiendo (o con algo en curso)? Entonces se espera y se junta lo que llegue.
    http_("GET", "/api/session/active", {}, [yo, gen](int st, const QByteArray& r) {
        if (!yo || gen != yo->generacion_) return;
        yo->enVuelo_ = false;
        if (st != 200) {
            if (++yo->fallos_ >= kFallosAntesDeDescartar) {
                const int n = yo->cola_.size();
                yo->cola_.vaciar();
                yo->fallos_ = 0;
                emit yo->fallo(tr("No se pudieron mandar %n aviso(s) al asistente: %1", "", n).arg(motivo(st, r)));
                return;
            }
            return yo->reintentarMasTarde();
        }
        const json d = leer(r).value("data", json::object());
        if (d.is_object() && d.contains(s(yo->sesion_))) return yo->reintentarMasTarde();
        yo->mandar(yo->cola_.tomar());
    });
}

void EnvioOpencode::mandar(ColaAutomatica::Lote lote) {
    if (lote.vacio()) return;
    enVuelo_ = true;
    QPointer<EnvioOpencode> yo(this);
    const quint64 gen = generacion_;
    const QByteArray cuerpo = cuerpoPrompt(lote);
    http_("POST", "/api/session/" + sesion_ + "/prompt", cuerpo,
          [yo, gen, lote = std::move(lote)](int st, const QByteArray& r) mutable {
              if (!yo || gen != yo->generacion_) return;
              yo->enVuelo_ = false;
              if (st == 200) {
                  yo->fallos_ = 0;
                  emit yo->enviado(lote.texto, static_cast<int>(lote.items.size()));
                  if (!yo->cola_.vacia()) QTimer::singleShot(0, yo.data(), &EnvioOpencode::intentar);
                  return;
              }
              const int n = static_cast<int>(lote.items.size()) + yo->cola_.size();
              yo->cola_.devolver(std::move(lote));
              if (++yo->fallos_ >= kFallosAntesDeDescartar) {
                  yo->cola_.vaciar();
                  yo->fallos_ = 0;
                  emit yo->fallo(tr("No se pudieron mandar %n aviso(s) al asistente: %1", "", n).arg(motivo(st, r)));
                  return;
              }
              yo->reintentarMasTarde();
          });
}

} // namespace ba::app::agent
