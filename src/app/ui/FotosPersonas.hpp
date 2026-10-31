// ============================================================================
// src/app/ui/FotosPersonas.hpp — la cara de cada persona conectada (una sola foto por persona)
//
// Las fotos que mandan los que están conectados —los invitados de la sala de
// conferencia («sala:<id>») y los integrantes que sesionan desde el navegador
// («integrante:<id>»)— viven acá, una por persona, con su versión. Las vistas las
// muestran por `urls[clave]` (image://personas/<clave>?v=<versión>: la imagen la da el
// proveedor, sin data: URLs en base64 que se copian en cada lista), y la URL cambia solo
// cuando llega una foto distinta. Quien las trae (ConferenceViewModel, SesionRemota) las
// pone con `poner`.
//
// En QML: el singleton `Fotos` (Fotos.urls["integrante:" + id]); las imágenes las da
// ui/ProveedorFotos (image://personas). Solo Qt Core: lo usan modelos que se prueban sin
// la interfaz.
// ============================================================================
#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QVariantMap>

#include <mutex>

namespace ba::app {

class FotosPersonas : public QObject {
    Q_OBJECT
    /// {clave: URL de su foto} de las que hay.
    Q_PROPERTY(QVariantMap urls READ urls NOTIFY cambio)

   public:
    /// La de la app (la crea el primero que la pide; vive hasta el final).
    static FotosPersonas& las();

    QVariantMap urls() const;
    /// La URL de la foto de `clave` ("" si no hay).
    QString url(const QString& clave) const;
    /// Una foto (JPEG) de `clave`: si es distinta de la que había, sube su versión y avisa.
    void poner(const QString& clave, const QByteArray& jpeg);
    /// Se fue: su foto también.
    void quitar(const QString& clave);
    /// Deja solo las de `vivas` entre las que empiezan con `prefijo` («sala:»…).
    void dejarSolo(const QString& prefijo, const QSet<QString>& vivas);
    /// Los bytes (JPEG) de la foto de `clave`; vacío si no hay. Seguro entre hilos.
    QByteArray jpeg(const QString& clave) const;

   signals:
    void cambio();

   private:
    FotosPersonas() = default;
    struct Foto {
        QByteArray jpeg;
        int version = 0;
    };
    mutable std::mutex mu_;
    QHash<QString, Foto> fotos_;
};

} // namespace ba::app
