// src/app/ui/ProveedorFotos.cpp — ver ProveedorFotos.hpp

#include "ui/ProveedorFotos.hpp"

#include "ui/FotosPersonas.hpp"

#include <QImage>
#include <QUrl>

namespace ba::app {

QImage ProveedorFotos::requestImage(const QString& id, QSize* size, const QSize& pedido) {
    const QString clave = QUrl::fromPercentEncoding(id.section('?', 0, 0).toUtf8());
    QImage img = QImage::fromData(FotosPersonas::las().jpeg(clave), "JPG");
    if (!img.isNull() && pedido.isValid() && !pedido.isEmpty())
        img = img.scaled(pedido, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (size != nullptr) *size = img.size();
    return img;
}

} // namespace ba::app
