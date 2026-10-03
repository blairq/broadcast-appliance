// ============================================================================
// src/app/plugins/Hijo.hpp — un proceso hijo lanzado con posix_spawn
//
// POR QUÉ NO QProcess. QProcess (Qt 6.6, macOS) hace fork() y después, en el
// hijo, todavía corre código antes del exec. En un programa con decenas de
// hilos (libobs, GStreamer, audio, red) el hijo hereda candados tomados por
// otros hilos y se puede quedar trabado para siempre ANTES de ser el programa
// que se pidió: pasó de verdad, varias veces por sesión —un plugin que «no
// arrancaba en 20 s», pip o la lista de dispositivos colgados, y una docena de
// copias de ba-app dormidas como hijos—. posix_spawn crea el hijo sin correr
// código nuestro en el medio, así que eso no puede pasar.
//
// Además:
//   - el hijo no hereda NINGÚN descriptor de la app salvo stdin/stdout/stderr
//     (POSIX_SPAWN_CLOEXEC_DEFAULT en macOS);
//   - va en su propio grupo de procesos: terminar() y matar() alcanzan también
//     a lo que él lance (el rastreador de memoria de Python, por ejemplo);
//   - la salida se lee con un aviso del sistema Y con una revisión periódica,
//     y que terminó se sabe con waitpid: nada depende de un solo aviso.
// ============================================================================
#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <functional>

class QSocketNotifier;
class QTimer;

namespace ba::app::plugins {

class Hijo : public QObject {
    Q_OBJECT
   public:
    explicit Hijo(QObject* parent = nullptr);
    ~Hijo() override; ///< si sigue vivo, lo mata (al grupo)

    /// Lo lanza. `unirErrores`: stderr va con stdout. false + `error` si no se pudo.
    bool iniciar(const QString& programa, const QStringList& args, const QProcessEnvironment& env, const QString& carpeta,
                 bool unirErrores, QString* error);
    qint64 pid() const { return pid_; }
    bool corriendo() const { return pid_ > 0 && !termino_; }

    void escribir(const QByteArray& datos);
    void cerrarEntrada();
    void terminar(); ///< SIGTERM al grupo
    void matar();    ///< SIGKILL al grupo

    /// Lo que mandó y todavía no se leyó.
    QByteArray leerSalida();
    QByteArray leerErrores();

    /// Para comandos cortos: lo corre, junta toda la salida y avisa al final
    /// (`codigo` -1 si no arrancó o se lo mató por `topeMs`).
    static void correr(QObject* dueno, const QString& programa, const QStringList& args, const QProcessEnvironment& env,
                       int topeMs, std::function<void(int codigo, QByteArray salida, QByteArray errores)> listo);

   signals:
    void salida();  ///< hay algo nuevo en stdout
    void errores(); ///< hay algo nuevo en stderr
    void termino(int codigo, bool caido);

   private:
    void leer(int fd, QByteArray& buf, bool esSalida);
    void revisar();
    void cerrarTodo();

    qint64 pid_ = -1;
    int in_ = -1, out_ = -1, err_ = -1;
    QSocketNotifier* nOut_ = nullptr;
    QSocketNotifier* nErr_ = nullptr;
    QTimer* revision_ = nullptr;
    QByteArray bufOut_, bufErr_;
    bool termino_ = false;
};

} // namespace ba::app::plugins
