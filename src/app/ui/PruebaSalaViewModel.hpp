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
//   - El probador: cuánto video que vuelve decodifica (sala::prueba::Decodificar: «nada»,
//     una «muestra» o «todo»; de fábrica la muestra) y si reparte los N entre esta máquina
//     y otras: la app levanta hinchada como COORDINADOR (`hinchada coordinar`, con una clave
//     de coordinación de esta prueba sola), lanza los trabajadores de esta máquina (si
//     genera) y muestra la línea para las otras (`hinchada trabajar --coordinador IP:puerto`);
//     cuántas se sumaron sale de la consola del coordinador, leída en el fondo.
//
// Vive en el hilo de la interfaz; lo que mide son llamadas al sistema baratas. El disco
// (las carpetas del probador, lo que quedó de antes) va al fondo (sala/ArchivosPrueba, en
// el pool) o a la cola de escritura, con el resultado de vuelta acá; al cerrar, el
// probador se corta sin esperarlo.
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
#include <functional>
#include <memory>
#include <vector>

#include "sala/PruebaSalaReglas.hpp"

namespace ba::app {
class ConferenceClient;
class EnlaceViewModel;
class EventoViewModel;
class RecursosViewModel;
namespace sala {
class CupoSala;
}

class PruebaSalaViewModel : public QObject {
    Q_OBJECT
    /// "apagada" | "corriendo" | "terminando"
    /// El tope de participantes (el cupo más grande de la sala, ipc/LugaresSala).
    Q_PROPERTY(int maxParticipantes READ maxParticipantes CONSTANT)
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
    /// El enlace para los N de la prueba (EnlaceViewModel::necesidadPara, con la última
    /// medición): {veredicto, motivos, necesita, entranConMargen…, medido (bool)}.
    Q_PROPERTY(QVariantMap enlace READ enlace NOTIFY enlaceChanged)
    /// Cuánto decodifica el probador en la prueba en curso o la última: {modo, pct, pctEfectivo}.
    Q_PROPERTY(QVariantMap decodificacion READ decodificacion NOTIFY probadorChanged)
    /// Varias máquinas (la prueba en curso): {activo, otras, estaGenera, porMaquina, local,
    /// procesosPorOtra, porProcesoOtra, esperados, sumados, sumadasOtras, escuchando,
    /// direccion (IP:puerto), comando, comandoWindows}. Vacío si genera solo esta máquina.
    Q_PROPERTY(QVariantMap reparto READ reparto NOTIFY probadorChanged)
    /// Los límites para la vista (sala::prueba).
    Q_PROPERTY(int muestraMinPct READ muestraMinPct CONSTANT)
    Q_PROPERTY(int muestraMaxPct READ muestraMaxPct CONSTANT)
    Q_PROPERTY(int muestraDeFabrica READ muestraDeFabrica CONSTANT)
    Q_PROPERTY(int maxOtrasMaquinas READ maxOtrasMaquinas CONSTANT)
    /// Esta máquina generando para la prueba de OTRA (generarPara): {estado: "" | arrancando |
    /// generando | parando | terminado, activo, maquina (la medida), coordinador, procesos,
    /// porProceso, andando, cpu (% de esta máquina, hinchada), segundos, error}.
    Q_PROPERTY(QVariantMap generando READ generando NOTIFY generandoChanged)
    /// Los gráficos en vivo (un punto por segundo, los últimos 10 minutos): [{t, cpuMaquina,
    /// cpuApp, cpuGateway, cpuGenerador (hinchada acá), redEntraMbps, redSaleMbps, cpuGeneradoras (la más cargada)}].
    Q_PROPERTY(QVariantList serie READ serie NOTIFY serieChanged)

   public:
    struct Partes {
        ConferenceClient& sala;
        sala::CupoSala& cupo;
        RecursosViewModel& recursos;
        EventoViewModel& evento;
        EnlaceViewModel& enlace;
        std::filesystem::path dataRoot;
    };
    /// La credencial vive esto y se renueva cada kRenovarS mientras dura la prueba.
    static constexpr int kVidaCredencialS = 90;
    static constexpr int kRenovarS = 30;

    explicit PruebaSalaViewModel(Partes p, QObject* parent = nullptr);
    ~PruebaSalaViewModel() override;

    int maxParticipantes() const { return ba::ipc::kMaxInvitados; }
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
    QVariantMap enlace() const { return enlace_; }
    QVariantMap decodificacion() const { return decodificacion_; }
    QVariantMap reparto() const { return repartoUi_; }
    int muestraMinPct() const { return sala::prueba::kMuestraMinPct; }
    int muestraMaxPct() const { return sala::prueba::kMuestraMaxPct; }
    int muestraDeFabrica() const { return sala::prueba::Decodificar{}.pct; }
    int maxOtrasMaquinas() const { return sala::prueba::kMaxOtrasMaquinas; }
    QVariantMap generando() const { return generandoUi_; }
    QVariantList serie() const { return serie_; }
    bool generandoActivo() const {
        return generandoEstado_ == QLatin1String("arrancando") || generandoEstado_ == QLatin1String("generando") ||
               generandoEstado_ == QLatin1String("parando");
    }

    /// Arranca la prueba con `n` participantes durante `minutos`. `conHinchada`: lanza el
    /// probador del programa; si no, espera carga externa. `hablan`: cuántos hablan a la
    /// vez (0 = cada uno a su ritmo). `decodificar`: «nada», «muestra» (`muestraPct` %) o
    /// «todo» (sala::prueba::Decodificar). `otrasMaquinas` > 0: reparte los N entre esta
    /// (si `estaGenera`; si no, solo coordina) y esas, que se suman con la línea de
    /// `reparto`. Devuelve "" o por qué no arrancó (la guardia, sin sala, sin evento, ya hay
    /// una…).
    Q_INVOKABLE QString arrancar(int n, int minutos, bool conHinchada, int hablan = 2,
                                 const QString& decodificar = QStringLiteral("muestra"), int muestraPct = 5,
                                 int otrasMaquinas = 0, bool estaGenera = true);
    /// La termina a mano (limpieza completa e informe).
    Q_INVOKABLE void parar();
    /// «Entrada directa para todos». Devuelve "" o por qué no (sin prueba).
    Q_INVOKABLE QString setEntradaDirecta(bool on);
    /// Mide la máquina ahora y dice si se podría arrancar con `n` (actualiza `maquina` y
    /// la cuenta del enlace para `n`, `enlace`).
    Q_INVOKABLE void revisarMaquina(int n = 10, bool conHinchada = true, int otrasMaquinas = 0, bool estaGenera = true);
    /// Esta máquina se suma como GENERADORA a la prueba de sala de otro equipo: `codigo` es el
    /// que muestra el otro («BAGEN1.»: la dirección de su coordinador de hinchada y la clave).
    /// Lanza los `hinchada trabajar` que pide el código; terminan solos cuando termina la
    /// prueba del otro. "" o por qué no.
    Q_INVOKABLE QString generarPara(const QString& codigo);
    Q_INVOKABLE void dejarDeGenerar();

    /// Otra prueba que ocupa la sala (la de capacidad): "" o qué está corriendo.
    void setOtraPrueba(std::function<QString()> f) { otraPrueba_ = std::move(f); }

    /// Todo junto, para el MCP: estado, generador, entrada directa, acceso (sin la
    /// credencial si `conCredencial` es false), vivo, máquina y el último informe.
    nlohmann::json estadoJson(bool conCredencial) const;
    nlohmann::json ultimoInformeJson() const { return ultimoJson_; }
    /// El historial de pruebas de la máquina para el MCP (sala/HistorialPruebas; leen el disco:
    /// las llama la herramienta, no la interfaz): {pruebas} o {csv}; una entera (null si no
    /// está); dos lado a lado (null si falta alguna).
    nlohmann::json historialPruebas(int limite, bool csv) const;
    nlohmann::json verPrueba(const std::string& archivo) const;
    nlohmann::json compararPruebas(const std::string& a, const std::string& b) const;

   signals:
    void estadoChanged();
    void entradaDirectaChanged();
    void accesoChanged();
    void vivoChanged();
    void informeChanged();
    void maquinaChanged();
    void enlaceChanged();
    void probadorChanged();
    void generandoChanged();
    void serieChanged();
    void notify(const QString& texto, const QString& nivel);

   private:
    void tic();
    void renovar();
    void terminar(const std::string& motivo, bool porGuardia);
    /// Crea sus carpetas en el fondo y, de vuelta acá, arrancarProbador.
    void lanzarHinchada(int n, int minutos, int hablan);
    void arrancarProbador(int n, int minutos, int hablan, const QString& grabado);
    /// Varias máquinas: el coordinador (y, cuando escucha, los trabajadores de acá).
    void arrancarCoordinador(int n, int minutos, int hablan, const QString& grabado);
    void lanzarTrabajadoresLocales(const QString& grabado);
    /// Lee lo nuevo de la consola del coordinador (en el fondo) y cuenta los que se sumaron.
    void leerCoordinador();
    void publicarReparto();
    /// La CPU de los procesos del probador de esta máquina (con sus hijos), -1 sin probador.
    double cpuProbador();
    QProcess* nuevoProceso(const QString& consola, const QString& dir, const QProcessEnvironment& env);
    void cortarProbador();
    void lanzarGeneradores(const QString& carpeta, const QString& grabado);
    void alTerminarGenerador();
    void publicarGenerando();
    void alPruebaSala(const QString& nombre, const QJsonObject& ev);
    void limpiarColgada();
    QString rutaHinchada() const;
    std::filesystem::path archivoEnCurso() const;
    void setEstado(const QString& e);
    static int64_t ahoraMs();

    Partes p_;
    std::function<QString()> otraPrueba_;
    QString estado_ = QStringLiteral("apagada");
    QString generador_;
    bool directa_ = false;
    QString credencial_, url_;
    QVariantMap vivo_, ultimo_, maquina_, enlace_;
    int nRevisado_ = 10; ///< el N de la última revisión (para rehacer la cuenta del enlace)
    void cuentaEnlace(int n);
    nlohmann::json ultimoJson_ = nlohmann::json::object();
    sala::prueba::Umbrales umbrales_;
    sala::prueba::Informe informe_;
    QTimer tic_;
    QPointer<QProcess> hinchada_;          ///< el probador (o el coordinador, con varias máquinas)
    std::vector<QPointer<QProcess>> trabajadores_; ///< los `hinchada trabajar` de esta máquina
    QString salidaHinchada_;
    sala::prueba::Decodificar decodificar_;
    sala::prueba::Reparto reparto_{};
    QVariantMap decodificacion_, repartoUi_;
    QString clave_, lanUrl_;       ///< la clave de coordinación y el gateway en la red (varias máquinas)
    int puerto_ = 0;               ///< el del coordinador
    bool escuchando_ = false;      ///< el coordinador ya espera trabajadores
    int sumados_ = 0, sumadosOtras_ = 0;
    qint64 consolaLeida_ = 0;      ///< hasta dónde se leyó la consola del coordinador
    std::string notasCoordinador_; ///< sus líneas de trabajadores (lo demás no se guarda)
    bool leyendoConsola_ = false;
    std::vector<std::uint32_t> pidsProbador_; ///< con los hijos (se rehace cada pocos segundos)
    int64_t pidsMs_ = 0;
    qint64 muestrasLeidas_ = 0;    ///< hasta dónde se leyó muestras.jsonl del coordinador
    QVariantList generadorasUi_;   ///< [{maquina, cpu}] de la última muestra del coordinador
    // Generar para otro equipo.
    std::vector<QPointer<QProcess>> generando_;
    sala::prueba::CodigoGenerador codigoGen_;
    QString generandoEstado_, generandoError_;
    QVariantMap generandoUi_;
    QTimer genTic_;
    int64_t generandoDesde_ = 0;
    int generandoCodigo_ = 0;
    int rondaGen_ = 0;
    double cpuGen_ = -1;
    QVariantList serie_;
    int pedidos_ = 0;
    int minutos_ = 10, hablan_ = 2; ///< los de la prueba (para la línea del generador externo)
    int cupoAntes_ = 0;      ///< el cupo de antes si se subió (0 = no se tocó)
    int64_t inicioMs_ = 0;
    int64_t topeMs_ = 0;     ///< cuándo se termina sola
    int64_t renovadaMs_ = 0;
    int ronda_ = 0; ///< sube con cada probador pedido: lo que vuelve del fondo para otro, no vale
    bool colgadaPendiente_ = false; ///< quedó una prueba de antes de cerrar el programa
    nlohmann::json colgada_;
};

} // namespace ba::app
