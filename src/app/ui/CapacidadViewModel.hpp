// ============================================================================
// src/app/ui/CapacidadViewModel.hpp — ¿cuántos invitados aguanta? (Configuración → Sistema → Capacidad)
//
// docs/funciones/CAPACIDAD.md. Es el MODELO/CONTROLADOR de la pestaña y de las
// herramientas MCP capacidad_*, con las mismas reglas para las dos. Tres pruebas y un modo:
//
//   - ESTE EQUIPO («local»): hinchada en esta máquina contra el propio gateway, por
//     escalones (sala/CapacidadReglas: 10, 20, 40, 60, 80…; cada uno ≤ 2 min) hasta que se
//     degrade o llegue al tope. Usa el mecanismo de la prueba de sala (credencial de prueba
//     que vence sola, entrada directa solo para ellos, marcados «prueba»: fuera del padrón,
//     la votación y el quórum), pero NO escribe nada en el evento abierto: el resultado va
//     al historial de la máquina (Capacidad/ en la carpeta de datos). Si el cupo no alcanza
//     se sube y al terminar vuelve. La CPU de hinchada se mide aparte y se descuenta.
//   - UN SISTEMA REMOTO («remoto»): hinchada de esta máquina contra otro BroadcastAppliance
//     que está «recibiendo»: se pega el CÓDIGO que muestra el otro (URL, credencial de
//     prueba y huella del certificado, sala::capacidad::Codigo) y la medida del otro llega
//     por su gateway con la credencial en una cabecera (net/ClienteCapacidad).
//   - RECIBIR («recibiendo»): este equipo se deja probar desde otro: abre la credencial,
//     sube el cupo y cada segundo le deja al gateway su medida (CPU, memoria, grilla,
//     audio, la interfaz) para el que prueba. Con la guardia: si la máquina se ahoga, corta.
//   - CONEXIÓN A INTERNET («internet»): el túnel WireGuard si está (EnlaceViewModel) y el
//     camino de los invitados por pinggy, medido ida y vuelta; «con esta conexión entran N
//     invitados» con sala::enlace (como mucho 4 en alta, el resto en baja). Si pinggy está
//     apagado, solo se prende si el operador lo pidió (y se apaga al terminar).
//
// Seguridad: nada arranca solo; con algo al aire, invitados reales conectados u otra
// prueba corriendo no arranca (`riesgo`); cancelar corta todo y limpia (hinchada, la
// credencial, el cupo, el túnel si lo prendió); la guardia corta cada segundo; si el
// programa se cierra en el medio, al volver se termina de limpiar
// (Runtime/capacidad-en-curso.json).
//
// MVC: todo por propiedades con aviso; el latido de 1 s es del modelo (mide la máquina),
// nunca de la vista. Lo de afuera llega por `Partes` (funciones), así el test lo arma con
// una sala y un generador de mentira (test_capacidad_vm).
// ============================================================================
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

#include <nlohmann/json.hpp>

#include "sala/CapacidadReglas.hpp"
#include "sala/EnlaceNecesita.hpp"
#include "sala/PruebaSalaReglas.hpp"

namespace ba::app {
namespace sala {
class GeneradorCarga;
}

class CapacidadViewModel : public QObject {
    Q_OBJECT
    /// "libre" | "local" | "remoto" | "recibiendo" | "internet" | "terminando"
    Q_PROPERTY(QString estado READ estado NOTIFY estadoChanged)
    Q_PROPERTY(bool ocupado READ ocupado NOTIFY estadoChanged)
    /// "" o por qué no se puede arrancar ahora (algo al aire, gente conectada, otra prueba…).
    Q_PROPERTY(QString riesgo READ riesgo NOTIFY riesgoChanged)
    /// Mientras corre: {fase, texto, escalon, escalones: [n…], indice, avance (0..1),
    /// segundosEscalon, maquina (la medida)}.
    Q_PROPERTY(QVariantMap progreso READ progreso NOTIFY progresoChanged)
    /// La última muestra (aJson(Muestra)).
    Q_PROPERTY(QVariantMap vivo READ vivo NOTIFY vivoChanged)
    /// Los escalones juzgados de la prueba en curso o la última: [aJson(Paso)].
    Q_PROPERTY(QVariantList pasos READ pasos NOTIFY pasosChanged)
    /// El último resultado de una prueba de capacidad (local o remota), entero.
    Q_PROPERTY(QVariantMap resultado READ resultado NOTIFY resultadoChanged)
    /// La última medición de la conexión a internet.
    Q_PROPERTY(QVariantMap internet READ internet NOTIFY internetChanged)
    /// [{archivo, fecha, tipo, maquina, frase, conMargen, maximo, limita, invitados}] (lo más nuevo primero).
    Q_PROPERTY(QVariantList historial READ historial NOTIFY historialChanged)
    /// Recibiendo: {activo, codigo, codigoInternet, url, urlInternet, cupo, venceMs, participantes, conVideo}.
    Q_PROPERTY(QVariantMap recibir READ recibir NOTIFY recibirChanged)
    /// pinggy ahora: {activo, via, estado, url, texto}.
    Q_PROPERTY(QVariantMap pinggy READ pinggy NOTIFY pinggyChanged)
    Q_PROPERTY(bool hayTunel READ hayTunel NOTIFY pinggyChanged)
    Q_PROPERTY(bool hinchadaDisponible READ hinchadaDisponible CONSTANT)
    Q_PROPERTY(QString maquina READ maquina CONSTANT)
    Q_PROPERTY(int topeMax READ topeMax CONSTANT)

   public:
    using Respuesta = std::function<void(bool ok, const QJsonObject& r)>;
    struct Partes {
        // --- La sala (ConferenceClient) ---------------------------------------------------
        std::function<bool()> online;
        std::function<QJsonObject()> foto;
        std::function<void(const QString& ruta, const QJsonObject& cuerpo, Respuesta)> post;
        std::function<void(const QString& ruta, Respuesta)> get;
        std::function<void(const QString& ruta)> del;
        std::function<void(int)> cambiarCupo;
        // --- La máquina -----------------------------------------------------------------
        /// CPU, memoria y procesos (sin grilla, audio ni interfaz); `pidsGenerador`: los de
        /// hinchada (su CPU va en cpuGenerador).
        std::function<sala::capacidad::Muestra(const std::vector<std::uint32_t>& pidsGenerador)> medirMaquina;
        /// La memoria disponible y la carga (la guardia de la prueba de sala, para arrancar).
        std::function<sala::prueba::Medida()> medidaGuardia;
        /// Mide la grilla `segundos` y llama con los cps (-1 sin grilla).
        std::function<void(int segundos, std::function<void(double)>)> medirGrilla;
        /// La edad del audio de los invitados (ms, -1 sin invitados).
        std::function<void(std::function<void(double)>)> medirAudio;
        // --- Lo que impide arrancar ---------------------------------------------------------
        std::function<bool()> alAire;
        std::function<QString()> otraPrueba; ///< "" o qué está corriendo (la prueba de sala, el enlace)
        // --- El enlace por WireGuard (EnlaceViewModel) ------------------------------------
        std::function<bool()> hayTunel;
        std::function<QString()> medirEnlace;
        std::function<void()> cancelarEnlace;
        std::function<bool()> enlaceMidiendo;
        std::function<nlohmann::json()> ultimoEnlace; ///< la última buena (null: ninguna)
        /// La cuenta del evento para `invitados` con una medición (EnlaceViewModel::juzgarCon).
        std::function<nlohmann::json(const sala::enlace::Medicion&, int invitados)> juzgarEnlace;
        // --- pinggy (AccesoRemoto) ---------------------------------------------------------
        std::function<QVariantMap()> pinggy;
        std::function<QString()> prenderPinggy;
        std::function<void()> apagarPinggy;
        // --- La red (net/ClienteCapacidad) ---------------------------------------------------
        std::function<void(QObject*, const QString& url, const QString& cred, const QString& huella,
                           std::function<void(const nlohmann::json&)>)>
            pedirRemota;
        std::function<void(QObject*, const QString& url, const QString& secreto, std::shared_ptr<std::atomic<bool>>,
                           std::function<void(const nlohmann::json&)>)>
            medirIdaYVuelta;
        // --- Lo demás -----------------------------------------------------------------------
        std::unique_ptr<sala::GeneradorCarga> generador;
        std::filesystem::path dataRoot;
        QString maquina;                     ///< el nombre de esta máquina
        std::function<int64_t()> reloj;      ///< ms (de fábrica, la hora); los tests ponen el suyo
        sala::capacidad::Tiempos tiempos{};
        sala::capacidad::Criterios criterios{};
    };
    static constexpr int kVidaCredencialS = 90; ///< la credencial vive esto y se renueva cada kRenovarS
    static constexpr int kRenovarS = 30;
    static constexpr int kRecibirMaxMin = 60;
    static constexpr int kEsperaPinggyS = 90;
    static constexpr int kSinRemotoS = 15; ///< el otro equipo sin contestar: se corta
    static constexpr int kPrepararMaxS = 15 * 60; ///< hinchada grabando su video de prueba (la primera vez)

    explicit CapacidadViewModel(Partes p, QObject* parent = nullptr);
    ~CapacidadViewModel() override;

    QString estado() const { return estado_; }
    bool ocupado() const { return estado_ != QLatin1String("libre"); }
    QString riesgo() const { return riesgo_; }
    QVariantMap progreso() const { return progreso_; }
    QVariantMap vivo() const { return vivo_; }
    QVariantList pasos() const { return pasosUi_; }
    QVariantMap resultado() const { return resultado_; }
    QVariantMap internet() const { return internet_; }
    QVariantList historial() const { return historial_; }
    QVariantMap recibir() const { return recibir_; }
    QVariantMap pinggy() const { return pinggy_; }
    bool hayTunel() const { return p_.hayTunel && p_.hayTunel(); }
    bool hinchadaDisponible() const;
    QString maquina() const { return p_.maquina; }
    int topeMax() const { return ba::ipc::kMaxInvitados; }

    /// ¿Cuántos aguanta ESTE equipo? Hasta `tope` invitados (escalones). "" o por qué no.
    Q_INVOKABLE QString probarEsteEquipo(int tope);
    /// ¿Cuántos aguanta el OTRO equipo del `codigo` (el que muestra el otro al recibir)?
    Q_INVOKABLE QString probarRemoto(const QString& codigo, int tope);
    /// Dejarse probar desde otro equipo: hasta `cupo` invitados, `minutos` como mucho.
    Q_INVOKABLE QString empezarARecibir(int cupo, int minutos);
    Q_INVOKABLE void dejarDeRecibir();
    /// La conexión a internet. `prenderPinggy`: si está apagado, prenderlo para medir (y
    /// apagarlo al terminar; reinicia la conferencia, solo sin nadie conectado).
    Q_INVOKABLE QString medirInternet(bool prenderPinggy);
    /// Corta lo que esté corriendo y limpia.
    Q_INVOKABLE void cancelar();
    /// Exporta: .csv = el historial; otra cosa (.json) = el último resultado y la conexión.
    /// `destino`: ruta o URL file://. "" o el error.
    Q_INVOKABLE QString exportar(const QString& destino);
    /// Vuelve a leer el historial del disco.
    Q_INVOKABLE void recargarHistorial();

    /// Para el MCP: todo junto (sin la credencial salvo `conCodigo`).
    nlohmann::json estadoJson(bool conCodigo) const;
    /// El último resultado de capacidad de ESTA máquina (local), entero; null si no hay.
    nlohmann::json ultimoDeEstaMaquina() const;
    /// La última medición de internet, entera; null si no hay.
    nlohmann::json ultimoInternet() const;
    /// Para la revisión previa del evento: [{grave, texto}] con el último resultado.
    QVariantList chequeoPrevio(int cupoEvento) const;

    /// El latido (1 s). Público para los tests (lo llaman a mano con su reloj).
    void tic();

   public slots:
    /// La foto de la sala o lo que está al aire cambió: recalcula el riesgo.
    void revisar();
    /// Evento del gateway (ConferenceClient::estadoEvento): si cerró la prueba, se termina.
    void alEventoSala(const QString& nombre, const QJsonObject& ev);
    /// Terminó la medición del enlace (EnlaceViewModel::termino).
    void alTerminarEnlace();
    /// pinggy cambió (AccesoRemoto::estadoChanged).
    void alCambiarPinggy();
    /// La sala volvió (ConferenceClient::onlineChanged): termina de limpiar lo colgado.
    void alCambiarOnline(bool on);

   signals:
    void estadoChanged();
    void riesgoChanged();
    void progresoChanged();
    void vivoChanged();
    void pasosChanged();
    void resultadoChanged();
    void internetChanged();
    void historialChanged();
    void recibirChanged();
    void pinggyChanged();
    void notify(const QString& texto, const QString& nivel);

   private:
    int64_t ahora() const;
    void setEstado(const QString& e);
    QString puedeArrancar() const;
    // La rampa (local y remoto).
    void abrirCredencial(std::function<void(bool, const QString& error)> listo);
    void empezarRampa();
    void prepararGenerador();
    void seguirLocal();
    void seguirRemoto();
    void empezarEscalon();
    void cerrarEscalon(const std::string& claveGuardia = {}, const std::string& motivoGuardia = {});
    void ticRampa(double uiMs);
    sala::capacidad::Muestra muestraLocal(double uiMs);
    sala::capacidad::Muestra muestraRemota();
    void terminarRampa(const std::string& motivo, bool error, bool cancelada);
    void publicarProgreso();
    // Recibir.
    void ticRecibir(double uiMs);
    // Internet.
    void ticInternet();
    void internetPinggy();
    void medirPorPinggy(const QString& url);
    void terminarInternet(const std::string& nota);
    // Comunes.
    void renovarCredencial();
    void cerrarCredencial(const std::string& motivo);
    void devolverCupo();
    void escribirEnCurso();
    void limpiarColgada();
    void guardar(const nlohmann::json& informe, const std::string& tipo);
    void medirGrillaYAudio(bool grilla);
    std::filesystem::path dirHistorial() const;
    std::filesystem::path archivoEnCurso() const;

    Partes p_;
    QTimer tic_;
    QString estado_ = QStringLiteral("libre");
    QString riesgo_;
    QVariantMap progreso_, vivo_, resultado_, internet_, recibir_, pinggy_;
    QVariantList pasosUi_, historial_;
    nlohmann::json resultadoJson_, internetJson_;

    // La rampa.
    std::vector<int> plan_;
    std::size_t indice_ = 0;
    int nAnterior_ = 0;
    QString fase_; ///< preparando | abriendo | entrando | asentando | midiendo
    int64_t inicioMs_ = 0, escalonDesde_ = 0, faseDesde_ = 0;
    std::vector<sala::capacidad::Muestra> delEscalon_, regimen_;
    std::vector<sala::capacidad::Paso> pasos_;
    double cps_ = -1, cpsEscalon_ = -1, audioMs_ = -1;
    int64_t ultimoAudioMs_ = 0, ultimaGrillaMs_ = 0;
    bool midiendoGrilla_ = false, midiendoAudio_ = false;
    int64_t ultimoTic_ = 0;
    QString credencial_, url_, huella_, maquinaMedida_;
    QString salida_; ///< carpeta de los informes de hinchada de esta prueba
    int cupoAntes_ = 0;
    int64_t renovadaMs_ = 0;
    // Remoto.
    nlohmann::json remota_;
    int64_t remotaOkMs_ = 0;
    bool pidiendoRemota_ = false;
    // Recibir.
    int64_t recibirHastaMs_ = 0;
    int cupoRecibir_ = 0;
    std::vector<sala::capacidad::Muestra> recibidas_;
    // Internet.
    QString faseInternet_; ///< wireguard | esperando_pinggy | pinggy
    int64_t faseInternetDesde_ = 0;
    bool pinggyLoPrendi_ = false;
    nlohmann::json internetParcial_;
    std::shared_ptr<std::atomic<bool>> cancelarRed_;
    // Lo colgado de antes.
    bool colgadaPendiente_ = false;
    nlohmann::json colgada_;
};

} // namespace ba::app
