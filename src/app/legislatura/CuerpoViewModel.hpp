// SPDX-License-Identifier: GPL-2.0-or-later
//
// La interfaz del Cuerpo (legislatura/Cuerpo): la tabla de integrantes con su
// banca, cargo, bloque, cámara/preset y micrófono; importar planilla; validar.
#pragma once

#include "legislatura/Cuerpo.hpp"

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

namespace ba::app::legislatura {

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

   signals:
    void cambio();

   private:
    std::filesystem::path datos_;
    std::unique_ptr<Cuerpo> c_;
};

} // namespace ba::app::legislatura
