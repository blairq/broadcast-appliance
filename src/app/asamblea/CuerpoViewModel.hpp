// SPDX-License-Identifier: GPL-2.0-or-later
//
// La interfaz del Cuerpo (asamblea/Cuerpo): la tabla de integrantes con su
// banca, cargo, bloque, cámara/preset y micrófono; importar planilla; validar.
#pragma once

#include "asamblea/Cuerpo.hpp"

#include <QImage>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>

namespace ba::app::asamblea {

class CuerpoViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString nombre READ nombre NOTIFY cambio)
    Q_PROPERTY(QVariantList integrantes READ integrantes NOTIFY cambio)
    /// {filas, columnas, bancas: [{banca, fila, columna, integrante?}], sin_lugar}.
    Q_PROPERTY(QVariantMap recinto READ recinto NOTIFY cambio)
   public:
    CuerpoViewModel(std::filesystem::path datos, QObject* parent = nullptr);
    Cuerpo& cuerpo() { return *c_; }

    QString nombre() const;
    QVariantList integrantes() const;
    Q_INVOKABLE void setNombre(const QString& n);
    /// Crea o cambia (con `id`). "" o el error.
    Q_INVOKABLE QString guardar(const QVariantMap& campos);
    Q_INVOKABLE bool borrar(const QString& id);
    Q_INVOKABLE bool validar(const QString& id);
    /// Importa una planilla CSV. Texto con lo que pasó.
    Q_INVOKABLE QString importar(const QString& archivo, bool reemplazar);
    /// Escribe la planilla de ejemplo en Exportado/ y devuelve la ruta.
    Q_INVOKABLE QString plantilla();
    Q_INVOKABLE QVariantMap buscar(const QString& clave) const;
    QVariantMap recinto() const;
    /// Ver Cuerpo::setRecinto. "" o el error.
    Q_INVOKABLE QString editarRecinto(const QVariantMap& cambios);

    // --- La foto de cada integrante (la cara de su botón en la botonera) -----------
    /// Sube una imagen (archivo o file:// URL): se recorta en cuadrado y se guarda en
    /// Fotos/cuerpo/<id>.jpg. "" o el error.
    Q_INVOKABLE QString subirFoto(const QString& id, const QString& archivo);
    /// La captura con su cámara: lleva la PTZ al preset de la banca (si no está al
    /// aire), espera a que llegue y toma lo que se ve. El resultado llega por `fotoLista`.
    Q_INVOKABLE void capturarFoto(const QString& id);
    Q_INVOKABLE bool borrarFoto(const QString& id);
    /// La ruta del archivo de la foto ("" si no tiene).
    QString rutaFoto(const QString& idOBanca) const;
    /// Lo que necesita para capturar: llevar la cámara (devuelve "" o el error) y
    /// tomar el cuadro de una entrada.
    struct Ganchos {
        std::function<QString(const QString& banca)> apuntar;
        std::function<QImage(const QString& camara)> capturar;
    };
    void setGanchos(Ganchos g) { g_ = std::move(g); }

   signals:
    void cambio();
    void fotoLista(const QString& id, bool ok, const QString& texto);

   private:
    std::filesystem::path datos_;
    std::unique_ptr<Cuerpo> c_;
    Ganchos g_;
    QString guardarFoto(const QString& id, const QImage& img);
};

} // namespace ba::app::asamblea
