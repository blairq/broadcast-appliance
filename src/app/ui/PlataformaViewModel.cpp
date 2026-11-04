// src/app/ui/PlataformaViewModel.cpp — ver el .hpp

#include "ui/PlataformaViewModel.hpp"

#include "ui/CapturaError.hpp"

#include <QClipboard>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QUrl>

#include <algorithm>

namespace ba::app {

PlataformaViewModel::PlataformaViewModel(QObject* parent) : QObject(parent) {}

PlataformaViewModel::Falta PlataformaViewModel::falta() const {
    if (!configured()) return Falta::Credencial;
    if (!loggedIn()) return Falta::Cuenta;
    return Falta::Nada;
}

QJsonObject PlataformaViewModel::estadoCuenta() const {
    return QJsonObject{{"configured", configured()},
                       {"loggedIn", loggedIn()},
                       {"loginPending", loginPending()},
                       {"account", cuentaNombre()}};
}

QJsonArray PlataformaViewModel::ultimosMensajes(int limit) const {
    const QJsonArray& all = chatMessages();
    QJsonArray last;
    for (qsizetype i = all.size() - std::clamp<qsizetype>(limit, 0, all.size()); i < all.size(); ++i)
        last.append(all[i]);
    return last;
}

QString PlataformaViewModel::conNombre(const QString& e) const {
    // Algunos motivos ya vienen con el nombre (los de la API sin traducir: «Twitch: …»).
    return e.startsWith(nombre()) ? e : tr("%1: %2").arg(nombre(), e);
}

void PlataformaViewModel::setError(const QString& e) {
    lastError_ = e;
    emit errorChanged();
    if (!e.isEmpty()) emit notify(conNombre(e), "error");
}

void PlataformaViewModel::avisarError(const QString& e) {
    if (!e.isEmpty() && !CapturaError::tomar(e)) setError(e);
}

void PlataformaViewModel::track(bool start) {
    busy_ = std::max(0, busy_ + (start ? 1 : -1));
    emit busyChanged();
}

PlataformaViewModel::Done PlataformaViewModel::alVolver(Done done) {
    track(true);
    return [this, done = std::move(done)](bool ok, const QJsonObject& d, const QString& e) {
        track(false);
        if (ok) refresh();
        if (done) done(ok, d, e);
    };
}

void PlataformaViewModel::openUrl(const QString& url) { QDesktopServices::openUrl(QUrl(url)); }
void PlataformaViewModel::copy(const QString& text) { QGuiApplication::clipboard()->setText(text); }

} // namespace ba::app

#include "moc_PlataformaViewModel.cpp"
