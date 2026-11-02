// ============================================================================
// src/app/ui/EnlaceViewModel.hpp — La velocidad del enlace (por el túnel) y cuánto necesita el evento
//
// Los invitados entran por el túnel WireGuard (un VPS con IP pública) y el gateway le
// manda el retorno a cada uno: lo crítico es la SUBIDA del lugar. La medición la hace el
// gateway (net/EnlaceServicio: el medidor contra nuestro eco del VPS, por la pila IP del
// túnel); este modelo la pide, sigue su avance por el evento «enlace» (sin preguntar) y
// junta lo medido con lo que necesita el evento (sala/EnlaceNecesita: N invitados con la
// configuración de la grilla) para decir «entra / justo / no entra» y hasta cuántos.
//
// Lo usan la tarjeta «Enlace» de la página Sala, la prueba de sala (antes de arrancar) y
// las herramientas MCP enlace_* (docs/funciones/ENLACE.md). Una medición carga el
// enlace: con invitados conectados o algo al aire hay que confirmarla, y entonces se mide
// con un tope bajo (kTopeEnVivoKbps). Con una prueba de sala corriendo, no.
//
// Al terminar una medición, el resultado va a la lista de chequeo del evento abierto
// (ítem «enlace»). Vive en el hilo de la interfaz; no hace I/O propio.
// ============================================================================
#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include <functional>

#include "sala/EnlaceNecesita.hpp"

namespace ba::app {
class ConferenceClient;
class EventoViewModel;

class EnlaceViewModel : public QObject {
    Q_OBJECT
    /// El tope de invitados (ipc/LugaresSala): hasta dónde va el «Invitados» del panel.
    Q_PROPERTY(int maxInvitados READ maxInvitados CONSTANT)
    /// "sin_sala" | "sin_tunel" | "libre" | "midiendo"
    Q_PROPERTY(QString estado READ estado NOTIFY medicionChanged)
    Q_PROPERTY(bool midiendo READ midiendo NOTIFY medicionChanged)
    Q_PROPERTY(bool hayTunel READ hayTunel NOTIFY medicionChanged)
    /// El eco del otro lado del túnel ("10.99.0.1:51821").
    Q_PROPERTY(QString eco READ eco NOTIFY medicionChanged)
    /// Mientras mide: "hola" | "reposo" | "subida" | "bajada"; y el avance (0..1).
    Q_PROPERTY(QString fase READ fase NOTIFY medicionChanged)
    Q_PROPERTY(double avance READ avance NOTIFY medicionChanged)
    /// Lo medido (en vivo mientras mide; si no, la última buena): {reposo, subida, bajada, eco, …}.
    Q_PROPERTY(QVariantMap medicion READ medicion NOTIFY medicionChanged)
    /// La última terminada (con `fecha`, `ok`, `error`); vacía si no hubo.
    Q_PROPERTY(QVariantMap ultima READ ultima NOTIFY medicionChanged)
    /// Para cuántos invitados se hace la cuenta (de fábrica, el cupo de la sala).
    Q_PROPERTY(int invitados READ invitados WRITE setInvitados NOTIFY necesidadChanged)
    /// La cuenta (EnlaceNecesita::aJson): necesita, veredicto, motivos, entranConMargen…
    Q_PROPERTY(QVariantMap necesidad READ necesidad NOTIFY necesidadChanged)
    /// "" o por qué medir ahora pide confirmación (invitados conectados, algo al aire).
    Q_PROPERTY(QString riesgo READ riesgo NOTIFY riesgoChanged)

   public:
    struct Partes {
        ConferenceClient& sala;
        EventoViewModel& evento;
        std::function<bool()> alAire;           ///< sale algo (transmisión, grabación…)
        std::function<int()> altaHasta;         ///< «alta_hasta» de la grilla (0 = sin límite)
        std::function<int()> altaMax;           ///< «alta_max» de la grilla (el más chico)
        std::function<bool()> pruebaCorriendo;  ///< la prueba de sala está corriendo
    };
    /// Con invitados o al aire: la rampa llega solo hasta acá (y el escalón dura menos).
    static constexpr int kTopeEnVivoKbps = 5000;

    explicit EnlaceViewModel(Partes p, QObject* parent = nullptr);

    int maxInvitados() const { return ba::ipc::kMaxInvitados; }
    QString estado() const { return estado_; }
    bool midiendo() const { return estado_ == QLatin1String("midiendo"); }
    bool hayTunel() const { return hayTunel_; }
    QString eco() const { return eco_; }
    QString fase() const { return fase_; }
    double avance() const { return avance_; }
    QVariantMap medicion() const { return medicion_; }
    QVariantMap ultima() const { return ultima_; }
    int invitados() const { return invitados_; }
    void setInvitados(int n);
    QVariantMap necesidad() const { return necesidad_; }
    QString riesgo() const { return riesgo_; }

    /// Mide. `confirmado`: el operador aceptó el `riesgo` (si no hay riesgo, da igual).
    /// `tasaKbps` > 0: una sola tasa fija en vez de la rampa; `topeKbps` > 0: hasta dónde
    /// sube la rampa (de fábrica 100 Mbps). Devuelve "" o por qué no arrancó.
    Q_INVOKABLE QString medir(bool confirmado = false, int topeKbps = 0, int tasaKbps = 0);
    Q_INVOKABLE void cancelar();
    /// Pide el instalador del eco para el VPS; llega por `instaladorListo`.
    Q_INVOKABLE void pedirInstalador();

    /// La cuenta para otro N (la prueba de sala, el MCP) con la última medición.
    nlohmann::json necesidadPara(int invitados) const;
    /// Lo último medido BIEN como lo usa la cuenta (sala::enlace::deResultado).
    sala::enlace::Medicion medido() const;
    /// La última medición buena entera (null = ninguna).
    nlohmann::json ultimaBuena() const { return buenaJson_; }
    /// Todo junto, para el MCP.
    nlohmann::json estadoJson() const;
    /// Recalcula el riesgo (los invitados de la foto, al aire) y la cuenta.
    void revisar();

   signals:
    void medicionChanged();
    void necesidadChanged();
    void riesgoChanged();
    /// El instalador del eco (script para pegar en el VPS) o el error.
    void instaladorListo(const QString& script, const QString& error);
    /// Terminó una medición (ok o no): para quien quiera enterarse (la prueba de sala).
    void termino();
    void notify(const QString& texto, const QString& nivel);

   private:
    void aplicar(const nlohmann::json& e);
    void recalcular();
    void alTerminar();
    sala::enlace::Perfil perfil(int invitados) const;

    Partes p_;
    QString estado_ = QStringLiteral("sin_sala");
    bool hayTunel_ = false;
    QString eco_, fase_;
    double avance_ = 0;
    QVariantMap medicion_, ultima_, necesidad_;
    nlohmann::json ultimaJson_; ///< la última que terminó (null = no hubo)
    nlohmann::json buenaJson_;  ///< la última que terminó bien: con esa se hace la cuenta
    int invitados_ = 0;
    bool invitadosAMano_ = false;
    QString riesgo_;
    double fechaUltima_ = 0; ///< para no anotar dos veces la misma en el chequeo
};

} // namespace ba::app
