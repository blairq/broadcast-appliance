// ============================================================================
// src/app/agent/ServidorOpencode.hpp — UN servidor de OpenCode para las pestañas
//
// OpenCode 2 es un servidor (API) y una interfaz que se le conecta. Con los dos
// asistentes (evento y automatización) en OpenCode, cada uno levantaba su
// servidor privado (--standalone). Ahora hay uno solo para la app: `opencode
// serve` en 127.0.0.1, puerto libre y una clave nueva en cada arranque
// (OPENCODE_SERVER_PASSWORD: sin ella la API contesta 401), y cada pestaña se le
// conecta con `--server <url>` desde SU carpeta de trabajo (el servidor toma la
// configuración —proveedores, MCP— de la carpeta de cada sesión: probado). Lo
// levanta la primera pestaña que lo pide y se apaga cuando la última lo suelta o
// al cerrar la app. Los roles automáticos (agentes de un tiro) no lo usan: van
// con su --standalone.
// ============================================================================
#pragma once

#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <map>

class QProcess;
class QTimer;

namespace ba::app::agent {

class ServidorOpencode : public QObject {
    Q_OBJECT
   public:
    static ServidorOpencode& el();

    struct Pedido {
        QString ejecutable;         ///< el opencode instalado
        QMap<QString, QString> env; ///< el mismo entorno aislado que la interfaz (HOME, XDG…)
        QStringList unsetEnv;
        QString workDir;
    };
    /// `quien` lo usa. `listo(url, clave, "")` cuando contesta; `listo("", "", error)`
    /// si no arrancó. Si ya está andando con el mismo ejecutable, enseguida.
    void pedir(QObject* quien, const Pedido& p, std::function<void(QString, QString, QString)> listo);
    void soltar(QObject* quien);

   private:
    ServidorOpencode() = default;
    void arrancar(const Pedido& p);
    void parar();
    void avisar(const QString& error);

    QProcess* proc_ = nullptr;
    QString exe_, url_, clave_;
    bool listo_ = false;
    std::map<QObject*, std::function<void(QString, QString, QString)>> esperan_;
    std::map<QObject*, bool> usan_;
};

} // namespace ba::app::agent
