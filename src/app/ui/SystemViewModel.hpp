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

#include <nlohmann/json.hpp>

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
    /// Arrancar al encender el equipo (InicioConElSistema): {activo, abre, notas, preparacion}.
    /// Se lee del sistema aparte (no en el hilo de la interfaz): al crear y con refresh().
    Q_PROPERTY(QVariantMap inicio READ inicio NOTIFY changed)
    /// Los elementos de GStreamer que faltan: [{elemento, para, imprescindible}]. Recorre el
    /// registro de GStreamer aparte, una vez (no cambia con el programa abierto).
    Q_PROPERTY(QVariantList faltantes READ faltantes NOTIFY faltantesChanged)
   public:
    explicit SystemViewModel(QObject* parent = nullptr);

    QString dataRoot() const;
    QStringList credentials() const;
    QVariantList backups() const;
    bool canRollback() const;
    /// {dataRoot, programDate, credentials, backups, canRollback} (system_info). Todo de
    /// archivos y sin estado propio: DESDE CUALQUIER HILO (el MCP lo lee en el pool, F3-03).
    nlohmann::json info() const;
    QString version() const;
    QVariantList preparacion() const;
    /// Los chequeos del equipo (F4-07: sesiones de codificador, placas a 48 kHz, PTP,
    /// deriva), que van al final de la preparación. Los arma quien tiene el motor.
    void alEquipo(std::function<QVariantList()> f) { equipo_ = std::move(f); }

    /// Vuelve a leer lo del sistema (el inicio, la preparación).
    Q_INVOKABLE void refresh();
    QVariantMap inicio() const { return inicio_; }
    /// "" o el error (y se relee `inicio`).
    Q_INVOKABLE QString inicioPoner(bool activo);
    QVariantList faltantes() const;
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
    void faltantesChanged();
    /// La app tiene que cerrarse para que el launcher actualice (o la reabra con otro evento).
    void quitForUpdate(const QString& aviso);

   private:
    /// Lee el inicio en otro hilo y, al volver, lo pone y avisa `changed`.
    void leerInicio();
    /// Ídem con los elementos de GStreamer que faltan (faltantesChanged).
    void leerFaltantes();

    std::function<QVariantList()> equipo_;
    QVariantMap inicio_;
    QVariantList faltantes_;
    mutable bool faltantesPedidos_ = false;
};

/// Ruta local desde una URL file:// o una ruta.
QString localPath(const QString& folderOrUrl);

} // namespace ba::app
