// ============================================================================
// src/app/ui/PruebaSalaViewModel.hpp — la prueba de sala (simulacro antes del evento real)
//
// El operador prueba un evento con mucha gente ANTES del evento real: N participantes
// «de prueba» entran por el camino real de los invitados (los genera hinchada, el
// probador de carga que viaja en el programa, o un generador externo con la
// credencial que se muestra), con video y audio, y se mide si la máquina y la red
// aguantan (docs/funciones/PRUEBA-SALA.md).
//
// Es el MODELO/CONTROLADOR (lo usan la tarjeta «Prueba de sala» de la página Sala y
// el MCP: prueba_sala_*), con las mismas reglas para los dos:
//
//   - Arrancar: la GUARDIA primero (sala/PruebaSalaReglas: con poca memoria, carga
//     alta o si los N no entran, no arranca). Se pide al gateway una credencial de
//     prueba que vence sola en kVidaCredencialS y se renueva mientras dura (si el
//     programa se cae, el gateway la revoca y echa a los de prueba solo). Si el cupo
//     de la sala no alcanza se sube y al terminar vuelve al de antes. Con hinchada,
//     se lanza en esta máquina; sin, se muestra la URL y la credencial.
//   - Durante: una vez por segundo (un temporizador de acá, nunca la vista) se mide
//     la máquina, el ancho de banda y la CPU de la app y del gateway; si la guardia
//     dice que se pasó, se CORTA sola. La foto de la sala dice quién entró, quién
//     tiene video y quién se cortó; el gateway avisa cuánto tardó la HD de cada uno.
//   - «Entrada directa para todos»: el interruptor del gateway (cualquiera entra sin
//     lobby), visible y con advertencia; solo existe durante la prueba y se apaga solo.
//   - Terminar (a mano, por tiempo, por la guardia, porque el probador terminó o
//     porque el gateway la cerró): se corta el probador, se revoca la credencial (el
//     gateway echa a los de prueba y apaga la entrada directa), vuelve el cupo, y el
//     informe se guarda en el evento (Pruebas/prueba-sala-<fecha>.json) y en su lista
//     de chequeo (ítem «prueba_sala»). Del evento no queda nada más.
//   - Si el programa se cerró en medio de una prueba, al volver a arrancar se termina
//     de limpiar (Runtime/prueba-sala-en-curso.json).
//
// Vive en el hilo de la interfaz; lo que mide son llamadas al sistema baratas.
// ============================================================================
#pragma once

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>

#include "sala/PruebaSalaReglas.hpp"

namespace ba::app {
class ConferenceClient;
class EventoViewModel;
class RecursosViewModel;
namespace sala {
class CupoSala;
}

class PruebaSalaViewModel : public QObject {
    Q_OBJECT
    /// "apagada" | "corriendo" | "terminando"
    Q_PROPERTY(QString estado READ estado NOTIFY estadoChanged)
    Q_PROPERTY(bool corriendo READ corriendo NOTIFY estadoChanged)
    /// «Entrada directa para todos»: cualquiera entra sin lobby (solo durante la prueba).
    Q_PROPERTY(bool entradaDirecta READ entradaDirecta NOTIFY entradaDirectaChanged)
    /// Para un generador externo: la credencial de prueba (el «PIN» que usa), la URL y
    /// la línea de hinchada lista para copiar. Vacías sin prueba.
    Q_PROPERTY(QString credencial READ credencial NOTIFY accesoChanged)
    Q_PROPERTY(QString urlIngreso READ urlIngreso NOTIFY accesoChanged)
    Q_PROPERTY(QString comandoExterno READ comandoExterno NOTIFY accesoChanged)
    /// ¿Viene hinchada en el programa?
    Q_PROPERTY(bool hinchadaDisponible READ hinchadaDisponible CONSTANT)
    /// "hinchada" | "externo" (el de la prueba en curso o la última)
    Q_PROPERTY(QString generador READ generador NOTIFY estadoChanged)
    /// El informe en vivo (PruebaSalaReglas::Informe::vivo): pedidos, entraron, conVideo,
    /// fallidos, cortes, hd, aVideoMs, ultima, picos, segundos.
    Q_PROPERTY(QVariantMap vivo READ vivo NOTIFY vivoChanged)
    /// El de la última prueba terminada (con `archivo` y `nota`); vacío si no hubo.
    Q_PROPERTY(QVariantMap ultimoInforme READ ultimoInforme NOTIFY informeChanged)
    /// La máquina: {memLibreMb, carga, hilosApp, puede ("" o por qué no)} (revisarMaquina).
    Q_PROPERTY(QVariantMap maquina READ maquina NOTIFY maquinaChanged)

   public:
    struct Partes {
        ConferenceClient& sala;
        sala::CupoSala& cupo;
        RecursosViewModel& recursos;
        EventoViewModel& evento;
        std::filesystem::path dataRoot;
    };
    /// La credencial vive esto y se renueva cada kRenovarS mientras dura la prueba.
    static constexpr int kVidaCredencialS = 90;
    static constexpr int kRenovarS = 30;

    explicit PruebaSalaViewModel(Partes p, QObject* parent = nullptr);
    ~PruebaSalaViewModel() override;

    QString estado() const { return estado_; }
    bool corriendo() const { return estado_ == QLatin1String("corriendo"); }
    bool entradaDirecta() const { return directa_; }
    QString credencial() const { return credencial_; }
    QString urlIngreso() const { return url_; }
    QString comandoExterno() const;
    bool hinchadaDisponible() const;
    QString generador() const { return generador_; }
    QVariantMap vivo() const { return vivo_; }
    QVariantMap ultimoInforme() const { return ultimo_; }
    QVariantMap maquina() const { return maquina_; }

    /// Arranca la prueba con `n` participantes durante `minutos`. `conHinchada`: lanza el
    /// probador del programa; si no, espera carga externa. `hablan`: cuántos hablan a la
    /// vez (0 = cada uno a su ritmo). Devuelve "" o por qué no arrancó (la guardia, sin
    /// sala, sin evento, ya hay una…).
    Q_INVOKABLE QString arrancar(int n, int minutos, bool conHinchada, int hablan = 2);
    /// La termina a mano (limpieza completa e informe).
    Q_INVOKABLE void parar();
    /// «Entrada directa para todos». Devuelve "" o por qué no (sin prueba).
    Q_INVOKABLE QString setEntradaDirecta(bool on);
    /// Mide la máquina ahora y dice si se podría arrancar con `n` (actualiza `maquina`).
    Q_INVOKABLE void revisarMaquina(int n = 10, bool conHinchada = true);

    /// Todo junto, para el MCP: estado, generador, entrada directa, acceso (sin la
    /// credencial si `conCredencial` es false), vivo, máquina y el último informe.
    nlohmann::json estadoJson(bool conCredencial) const;
    nlohmann::json ultimoInformeJson() const { return ultimoJson_; }

   signals:
    void estadoChanged();
    void entradaDirectaChanged();
    void accesoChanged();
    void vivoChanged();
    void informeChanged();
    void maquinaChanged();
    void notify(const QString& texto, const QString& nivel);

   private:
    void tic();
    void renovar();
    void terminar(const std::string& motivo, bool porGuardia);
    void lanzarHinchada(int n, int minutos, int hablan);
    void alPruebaSala(const QString& nombre, const QJsonObject& ev);
    void limpiarColgada();
    QString rutaHinchada() const;
    std::filesystem::path archivoEnCurso() const;
    void setEstado(const QString& e);
    static int64_t ahoraMs();

    Partes p_;
    QString estado_ = QStringLiteral("apagada");
    QString generador_;
    bool directa_ = false;
    QString credencial_, url_;
    QVariantMap vivo_, ultimo_, maquina_;
    nlohmann::json ultimoJson_ = nlohmann::json::object();
    sala::prueba::Umbrales umbrales_;
    sala::prueba::Informe informe_;
    QTimer tic_;
    QPointer<QProcess> hinchada_;
    QString salidaHinchada_;
    int pedidos_ = 0;
    int minutos_ = 10, hablan_ = 2; ///< los de la prueba (para la línea del generador externo)
    int cupoAntes_ = 0;      ///< el cupo de antes si se subió (0 = no se tocó)
    int64_t inicioMs_ = 0;
    int64_t topeMs_ = 0;     ///< cuándo se termina sola
    int64_t renovadaMs_ = 0;
    bool colgadaPendiente_ = false; ///< quedó una prueba de antes de cerrar el programa
    nlohmann::json colgada_;
};

} // namespace ba::app
