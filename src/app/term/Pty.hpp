// ============================================================================
// src/app/term/Pty.hpp — Pseudo-terminal: corre un programa con TTY propia
//
// La pestaña Asistente corre la TUI del agente de IA (Claude Code, Gemini CLI,
// opencode) como en una terminal de verdad: el programa ve una TTY (colores,
// tamaño, teclas), su salida la interpreta libvterm (TerminalSession) y la
// dibuja TerminalView.
//
//   Linux/macOS: forkpty (el hijo es lider de sesion con la TTY esclava).
//   Windows:     ConPTY (CreatePseudoConsole, Windows 10 1809+).
//
// Threading: vive en el thread de la UI. En POSIX la lectura es con
// QSocketNotifier; en Windows, un thread lector reenvia al thread de la UI.
// ============================================================================
#pragma once

#include <QByteArray>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

namespace ba::app::term {

class Pty : public QObject {
    Q_OBJECT
   public:
    struct Options {
        QString program;
        QStringList args;
        QString workDir;
        QMap<QString, QString> env; ///< se agregan/pisan sobre el entorno actual
        QStringList unsetEnv;       ///< se quitan del entorno
        int cols = 100;
        int rows = 30;
    };

    explicit Pty(QObject* parent = nullptr);
    ~Pty() override;

    bool start(const Options& opt, QString* error = nullptr);
    void write(const QByteArray& data);
    void resize(int cols, int rows);
    /// Cierra la TTY (el programa recibe SIGHUP) y, si no sale, lo mata.
    void terminate();
    bool running() const;
    qint64 pid() const;

   signals:
    void output(const QByteArray& data);
    void finished(int exitCode);

   private:
    /// Manda lo que haya en cola sin esperar (lo que no entra, sigue cuando hay lugar).
    void escribirPendiente();
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace ba::app::term
