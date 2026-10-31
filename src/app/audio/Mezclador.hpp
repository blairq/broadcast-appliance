// ============================================================================
// src/app/audio/Mezclador.hpp — el modelo del Mezclador (la ventana propia)
//
// Lo que maneja: lo que sale de cada entrada (después de su rack) y adónde va (la
// matriz de salida). Ver docs/planes/PLAN-MEZCLADOR.md (M1).
//
// - Niveles: un solo temporizador (30 Hz) que corre SOLO mientras alguna vista lo
//   usa (usar(vista, true/false): por cuenta de quién, no un bool compartido que
//   una vista escondida le apaga a otra). Lee Motor::niveles() (entradas y buses
//   por canal, sin candado) y los picos de la matriz; calcula la retención de
//   picos (2 s) y deja todo en arreglos planos: QML recibe UN aviso por cuadro.
// - Listas: las entradas con audio y las salidas (las columnas de la matriz), que
//   se rehacen solo cuando cambian (no en cada cuadro: las columnas leen los
//   dispositivos del sistema).
// - Lo mismo lo lee el MCP (estado()).
// ============================================================================
#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

#include <nlohmann/json.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ba::app {
class AppRuntime;
}

namespace ba::app::audio {

class Calibracion;
class Matriz;
class RackManager;

class Mezclador : public QObject {
    Q_OBJECT
    /// Las entradas con audio, en orden: [{nombre}].
    Q_PROPERTY(QStringList entradas READ entradas NOTIFY listasCambio)
    /// Las salidas (columnas de la matriz): [{id, nombre, tipo}] (tipo: pista, escucha, fisica, salida, entrada).
    Q_PROPERTY(QVariantList salidas READ salidas NOTIFY listasCambio)
    /// Por entrada, en el orden de `entradas`, 6 números: pico L, pico R, RMS L, RMS R,
    /// retención L, retención R (dB, -100 = nada).
    Q_PROPERTY(QVariantList nivelesEntradas READ nivelesEntradas NOTIFY nivelesCambio)
    /// Lo mismo por salida, en el orden de `salidas`.
    Q_PROPERTY(QVariantList nivelesSalidas READ nivelesSalidas NOTIFY nivelesCambio)
    /// El loudness del programa: {momentaneo, corto, integrado, lra, true_peak_db, objetivo…}.
    Q_PROPERTY(QVariantMap loudness READ loudness NOTIFY nivelesCambio)
    /// Si alguna vista lo está usando (el temporizador corre).
    Q_PROPERTY(bool activo READ activo NOTIFY activoCambio)
    /// Los analizadores de LSP que se pueden abrir sobre una entrada: [{id, nombre, para}].
    Q_PROPERTY(QVariantList analisisLsp READ analisisLsp CONSTANT)
    /// La pestaña a la vista en la ventana: consola o calibracion (en el modelo: el
    /// asistente la cambia con mezclador_pestana).
    Q_PROPERTY(QString pestana READ pestana WRITE setPestana NOTIFY pestanaCambio)
    /// La matriz desplegada abajo de la consola.
    Q_PROPERTY(bool matrizAbierta READ matrizAbierta WRITE setMatrizAbierta NOTIFY pestanaCambio)
    /// La calibración de sala (audio::Calibracion), la pestaña Calibración.
    Q_PROPERTY(QObject* calibracion READ calibracionQml CONSTANT)

   public:
    Mezclador(AppRuntime& rt, Matriz* matriz, QObject* parent = nullptr);
    ~Mezclador() override;

    QStringList entradas() const { return entradas_; }
    QVariantList salidas() const { return salidas_; }
    QVariantList nivelesEntradas() const { return nivelesE_; }
    QVariantList nivelesSalidas() const { return nivelesS_; }
    QVariantMap loudness() const { return loudness_; }
    bool activo() const { return !usuarios_.isEmpty(); }

    /// Una vista (la ventana, la tarjeta) empieza o deja de usarlo. Mientras haya
    /// alguna, mide a 30 Hz; si se destruye sin avisar, se la saca sola.
    Q_INVOKABLE void usar(QObject* vista, bool si);
    /// El estado de una tira: {nombre, volumenDb, mute, pfl, envios: [ids de columnas]}.
    Q_INVOKABLE QVariantMap tira(const QString& nombre) const;
    /// De qué salida son el volumen y el mute de las tiras (PLAN-SALIDAS: la salida activa
    /// de la interfaz, que tiene su propia mezcla; "" = la por omisión). Lo mueve la vista
    /// por el MixerModel, que sigue a la misma.
    void setSalida(const std::string& salida) { salida_ = salida; }
    /// A qué destinos va cada entrada: {nombre: [ids]} (para los chips de envío).
    Q_INVOKABLE QVariantMap envios() const;
    QVariantList analisisLsp() const;
    /// Abre sobre la entrada el analizador de LSP `cual` (espectro, fase,
    /// osciloscopio, latencia, perfil) con su interfaz. "" si salió; si no, el motivo.
    Q_INVOKABLE QString abrirAnalisisLsp(const QString& entrada, const QString& cual);
    void setRacks(RackManager* r);
    QString pestana() const { return pestana_; }
    void setPestana(const QString& p);
    bool matrizAbierta() const { return matrizAbierta_; }
    void setMatrizAbierta(bool a) {
        if (a == matrizAbierta_) return;
        matrizAbierta_ = a;
        emit pestanaCambio();
    }
    Calibracion* calibracion() const { return calibracion_.get(); }
    QObject* calibracionQml() const;

    /// Lo mismo que ve la ventana, para el MCP: entradas (niveles y estado) y salidas.
    nlohmann::json estado() const;
    /// Se mide una vez a pedido (el MCP, sin vista abierta).
    void medirYa();

   signals:
    void listasCambio();
    void nivelesCambio();
    void activoCambio();
    void pestanaCambio();

   private:
    void rehacerListas();
    void tic();

    AppRuntime& rt_;
    std::string salida_; ///< ver setSalida
    Matriz* matriz_ = nullptr;
    RackManager* racks_ = nullptr;
    std::unique_ptr<Calibracion> calibracion_;
    QString pestana_ = "consola";
    bool matrizAbierta_ = false;
    QTimer timer_;
    QSet<QObject*> usuarios_;
    QStringList entradas_;
    QVariantList salidas_;
    std::vector<std::string> idSalidas_; ///< el id de cada salida (orden de salidas_)
    QVariantList nivelesE_, nivelesS_;
    QVariantMap loudness_;
    // La retención: valor (dB) y cuándo se tomó, por entrada/salida y canal.
    struct Retencion {
        float db[2] = {-100, -100};
        qint64 ms[2] = {0, 0};
    };
    QHash<QString, Retencion> retE_, retS_;
};

} // namespace ba::app::audio
