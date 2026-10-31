// ============================================================================
// src/app/ui/SystemViewModel.hpp — "Sistema y datos": carpetas, credenciales,
// respaldos y actualizacion
//
// La actualizacion la hace el launcher (`--update <carpeta> --start`, ver
// src/launcher/Updater.hpp): la app valida la carpeta, lanza el launcher
// desprendido y se cierra; el launcher espera a que cierre, reemplaza SOLO el
// programa (nunca Config/ ni Credenciales/, que respalda antes) y vuelve a
// arrancar.
// ============================================================================
#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>

#include <functional>

namespace ba::app {

class SystemViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString dataRoot READ dataRoot CONSTANT)
    Q_PROPERTY(QStringList credentials READ credentials NOTIFY changed)
    Q_PROPERTY(QVariantList backups READ backups NOTIFY changed)
    Q_PROPERTY(bool canRollback READ canRollback NOTIFY changed)
    Q_PROPERTY(QString version READ version CONSTANT)
    /// La tarjeta de preparación (F1-11): qué falta para volver solo tras un corte,
    /// leído del sistema: [{id, estado (ok|falta|revisar|no_aplica), titulo, detalle, como}].
    Q_PROPERTY(QVariantList preparacion READ preparacion NOTIFY changed)
   public:
    explicit SystemViewModel(QObject* parent = nullptr);

    QString dataRoot() const;
    QStringList credentials() const;
    QVariantList backups() const;
    bool canRollback() const;
    QString version() const;
    QVariantList preparacion() const;
    /// Los chequeos del equipo (F4-07: sesiones de codificador, placas a 48 kHz, PTP,
    /// deriva), que van al final de la preparación. Los arma quien tiene el motor.
    void alEquipo(std::function<QVariantList()> f) { equipo_ = std::move(f); }

    Q_INVOKABLE void refresh();
    /// Arrancar al encender el equipo (InicioConElSistema): {activo, abre, notas}.
    Q_INVOKABLE QVariantMap inicioEstado() const;
    /// "" o el error.
    Q_INVOKABLE QString inicioPoner(bool activo);
    /// Los elementos de GStreamer que faltan: [{elemento, para, imprescindible}].
    Q_INVOKABLE QVariantList elementosQueFaltan() const;
    /// "" si `folder` (ruta o file://) es una version instalable; si no, por que no.
    Q_INVOKABLE QString checkUpdate(const QString& folder) const;
    /// Lanza la actualizacion y pide cerrar la app. Devuelve "" o el error.
    Q_INVOKABLE QString startUpdate(const QString& folder);
    Q_INVOKABLE QString startRollback();
    /// Abre una carpeta de datos en el explorador ("" = la raiz).
    Q_INVOKABLE void openFolder(const QString& sub) const;

    /// Lanza el launcher con `args` y cierra la app (actualizar, volver atrás, cambiar de configuración).
    /// :param aviso: lo que ve el operador mientras se cierra (por qué se cierra).
    QString launch(const QStringList& args,
                   const QString& aviso = tr("Cerrando para actualizar: el sistema vuelve a abrir solo en unos segundos."));

   signals:
    void changed();
    /// La app tiene que cerrarse para que el launcher actualice (o la reabra con otro evento).
    void quitForUpdate(const QString& aviso);

   private:
    std::function<QVariantList()> equipo_;
};

/// Ruta local desde una URL file:// o una ruta.
QString localPath(const QString& folderOrUrl);

} // namespace ba::app
