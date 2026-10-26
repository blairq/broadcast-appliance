// ============================================================================
// src/app/dispositivos/Sondeo.hpp — la app del lado de afuera de la sonda
//
// Lanza el proceso hijo (SondaDrivers.hpp), lee sus líneas y le pone tiempo
// límite a cada driver. Todo asincrónico con QProcess: nada corre en el hilo de
// la interfaz más que leer unas líneas de JSON.
//
//   - Un driver que no contesta en `limiteMs` desde su «empieza»: se mata el
//     proceso, cuenta como Tiempo, y los que faltaban siguen en otro proceso.
//   - El proceso termina sin «fin» con un driver empezado: ese driver se cayó
//     (Caida, con lo último que escribió por stderr) y siguen los demás.
//   - El proceso no llega ni a empezar (GStreamer no arrancó, se cayó antes):
//     no se le echa la culpa a ningún driver; se avisa y se termina.
// ============================================================================
#pragma once

#include "hw/SaludDrivers.hpp"

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>
#include <QTimer>

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <vector>

namespace ba::app::dispositivos {

struct ResultadoSonda {
    std::string id;
    bool ok = false;
    bool ausente = false; ///< no está instalado (no es una falla)
    hw::Falla falla = hw::Falla::Error;
    std::string motivo;   ///< ya legible (hw::motivoLegible)
    std::string detalle;  ///< lo que dijo al andar
    nlohmann::json dispositivos = nlohmann::json::array();
};

class Sondeo : public QObject {
    Q_OBJECT
   public:
    struct Opciones {
        QString programa;        ///< el ejecutable (ba-app, o el test)
        QStringList argsBase;    ///< {"--sonda-drivers"}
        QProcessEnvironment entorno = QProcessEnvironment::systemEnvironment();
        int limiteMs = 8000;     ///< por driver
        int arranqueMs = 30000;  ///< hasta el primer «empieza» (gst_init en el hijo)
    };
    explicit Sondeo(Opciones o, QObject* parent = nullptr);
    ~Sondeo() override;

    using AlListar = std::function<void(bool ok, std::vector<hw::DriverListado> lista, QString error)>;
    using AlProbar = std::function<void(const ResultadoSonda& r)>;
    using AlTerminar = std::function<void(QString error)>;

    /// Los drivers instalados, sin cargarlos.
    void listar(AlListar listo);
    /// Los prueba de a uno: `uno` por cada resultado, `fin` al terminar todos
    /// (con un error si el proceso no pudo ni arrancar).
    void probar(const QStringList& ids, AlProbar uno, AlTerminar fin);
    bool ocupado() const { return proceso_ != nullptr; }
    /// El que se está probando ahora ("" si ninguno).
    QString actual() const { return actual_; }
    void setLimiteMs(int ms) { o_.limiteMs = ms; }

   signals:
    void progreso(int hechos, int total, const QString& actual);

   private:
    void lanzar(const QStringList& args);
    void leerLineas();
    void alTerminarProceso(int codigo, QProcess::ExitStatus estado);
    void vencio();
    void seguir();     ///< otro proceso con los que faltan
    void terminar(const QString& error);
    void cerrarProceso();
    Opciones o_;
    QProcess* proceso_ = nullptr;
    QTimer reloj_;
    QByteArray pendiente_, errores_;
    // listar
    bool listando_ = false;
    AlListar alListar_;
    std::vector<hw::DriverListado> lista_;
    // probar
    QStringList faltan_;
    int total_ = 0, hechos_ = 0;
    QString actual_;
    bool huboAvance_ = false; ///< este proceso resolvió al menos uno (para no relanzar en falso)
    bool fin_ = false;
    AlProbar alProbar_;
    AlTerminar alTerminar_;
};

} // namespace ba::app::dispositivos
