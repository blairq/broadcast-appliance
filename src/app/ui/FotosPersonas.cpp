// src/app/ui/FotosPersonas.cpp — ver FotosPersonas.hpp

#include "ui/FotosPersonas.hpp"

#include <QSet>
#include <QUrl>

namespace ba::app {

FotosPersonas& FotosPersonas::las() {
    static FotosPersonas* f = new FotosPersonas(); // nunca se destruye: el motor de QML la usa hasta el final
    return *f;
}

QString FotosPersonas::url(const QString& clave) const {
    std::lock_guard<std::mutex> l(mu_);
    const auto it = fotos_.constFind(clave);
    if (it == fotos_.constEnd()) return {};
    return "image://personas/" + QString::fromUtf8(QUrl::toPercentEncoding(clave)) + "?v=" + QString::number(it->version);
}

QVariantMap FotosPersonas::urls() const {
    QStringList claves;
    {
        std::lock_guard<std::mutex> l(mu_);
        claves = fotos_.keys();
    }
    QVariantMap out;
    for (const QString& c : claves) out[c] = url(c);
    return out;
}

void FotosPersonas::poner(const QString& clave, const QByteArray& jpeg) {
    // Solo un JPEG de verdad (lo que manda un navegador puede venir vacío o roto).
    if (clave.isEmpty() || jpeg.size() < 4 || static_cast<unsigned char>(jpeg[0]) != 0xFF) return;
    {
        std::lock_guard<std::mutex> l(mu_);
        Foto& f = fotos_[clave];
        if (f.jpeg == jpeg) return;
        f.jpeg = jpeg;
        ++f.version;
    }
    emit cambio();
}

void FotosPersonas::quitar(const QString& clave) {
    {
        std::lock_guard<std::mutex> l(mu_);
        if (fotos_.remove(clave) == 0) return;
    }
    emit cambio();
}

void FotosPersonas::dejarSolo(const QString& prefijo, const QSet<QString>& vivas) {
    bool cambiaron = false;
    {
        std::lock_guard<std::mutex> l(mu_);
        for (auto it = fotos_.begin(); it != fotos_.end();) {
            if (it.key().startsWith(prefijo) && !vivas.contains(it.key())) {
                it = fotos_.erase(it);
                cambiaron = true;
            } else {
                ++it;
            }
        }
    }
    if (cambiaron) emit cambio();
}

QByteArray FotosPersonas::jpeg(const QString& clave) const {
    std::lock_guard<std::mutex> l(mu_);
    const auto it = fotos_.constFind(clave);
    return it == fotos_.constEnd() ? QByteArray() : it->jpeg;
}

} // namespace ba::app

#include "moc_FotosPersonas.cpp"
