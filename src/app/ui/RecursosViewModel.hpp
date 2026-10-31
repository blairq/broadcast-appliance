// ============================================================================
// src/app/ui/RecursosViewModel.hpp — los recursos de la máquina para la tarjeta «Sistema»
//
// Dueño del muestreador (diag::Recursos, que mide en su hilo una vez por
// segundo) y su cara para la interfaz: una fila por recurso (CPU, GPU, RAM,
// VRAM, red) con el valor, el pico, el estado por umbral y la historia de los
// últimos minutos para la línea chiquita. Todo por propiedades con aviso
// (`cambio`): la vista se entera sola, no pregunta.
//
// Le pasa al muestreador lo que el sistema no sabe: el pid del gateway (cambia
// si se relanza) y lo del motor (cuadros por segundo, perdidos, lo que sale por
// la transmisión). Lo leen también la herramienta MCP `sistema_recursos` y el
// disparador de alarmas `recursos` (por `muestreador()`).
//
// En la misma vuelta (una por segundo, en el hilo de la interfaz):
//   - copia lo medido a las métricas del proceso (diag::Metricas, F4-01): el motor,
//     el bucle de la interfaz, el colchón de la grabación, las reconexiones de la
//     transmisión y la térmica; las de las herramientas MCP y del candado del
//     motor se cargan solas donde pasan;
//   - arma los SLI de la página de diagnóstico (`slis`), con el transporte A→B que
//     mide el gateway (llega en la foto de la sala: `alTransporte`);
//   - lleva la política térmica «priorizar programa» (diag::PoliticaTermica, F4-05)
//     y avisa cada paso que se apaga o se devuelve (`alDegradar`).
// ============================================================================
#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include <chrono>
#include <cstdint>
#include <memory>

#include "comun/FotoEntreHilos.hpp"
#include "diag/PoliticaTermica.hpp"
#include "diag/Recursos.hpp"

#include <functional>
#include <map>
#include <string>
#include <vector>

#include <QVariantMap>

namespace ba::app {

class AppRuntime;

class RecursosViewModel : public QObject {
    Q_OBJECT
    /// [{clave, titulo, pct (0–100, -1 desconocido), texto, conocido, pico, estado, historia: [0–100|-1],
    ///   historia2 (la red: la entrada), techo (la escala de la historia)}]
    Q_PROPERTY(QVariantList filas READ filas NOTIFY cambio)
    /// El peor estado: normal | aviso | critico | desconocido.
    Q_PROPERTY(QString estado READ estado NOTIFY cambio)
    /// Lo que le queda al recurso más cargado (%; -1 si no se sabe) y cuál es.
    Q_PROPERTY(double margen READ margen NOTIFY cambio)
    Q_PROPERTY(QString limita READ limita NOTIFY cambio)
    /// Nuestros procesos: [{nombre, texto}] («app 12 % · 850 MB»).
    Q_PROPERTY(QVariantList procesos READ procesos NOTIFY cambio)
    /// Los indicadores de servicio: [{clave, titulo, texto, estado (normal|aviso|critico|desconocido)}].
    Q_PROPERTY(QVariantList slis READ slis NOTIFY cambio)
    /// La térmica: {nivel, texto, apagados: [paso], estado}.
    Q_PROPERTY(QVariantMap termica READ termica NOTIFY cambio)
    /// Cuántos segundos de historia muestra la línea de cada fila.
    Q_PROPERTY(int segundosHistoria READ segundosHistoria CONSTANT)
   public:
    explicit RecursosViewModel(AppRuntime& rt, QObject* parent = nullptr);
    ~RecursosViewModel() override;

    QVariantList filas() const { return filas_; }
    QString estado() const { return estado_; }
    double margen() const { return margen_; }
    QString limita() const { return limita_; }
    QVariantList procesos() const { return procesos_; }
    int segundosHistoria() const { return kHistoriaS; }
    QVariantList slis() const { return slis_; }
    QVariantMap termica() const { return termica_; }

    /// De dónde sale lo que mide el gateway del transporte A→B (la foto de la sala:
    /// `uso.transporte`). Sin esto, esos SLI quedan desconocidos.
    void alTransporte(std::function<nlohmann::json()> f) { transporte_ = std::move(f); }
    /// Un paso de la política térmica se apaga (`apagar`) o se devuelve. Los que no
    /// tienen quien los atienda quedan en el informe como «sin_gancho».
    using Degradar = std::function<bool(const std::string& paso, bool apagar)>;
    void alDegradar(Degradar f) { degradar_ = std::move(f); }
    /// DESDE CUALQUIER HILO (la herramienta `sistema_recursos` corre en el pool, F3-03): lo
    /// que copió la última vuelta, {transporte: [...] (el de la foto de la sala),
    /// termica: {politica, sin_gancho}}. Con la interfaz trabada se queda quieto.
    nlohmann::json fotoServicio() const { return fotoServicio_.leer(); }

    diag::Recursos& muestreador() { return *recursos_; }

   signals:
    void cambio();

   private:
    static constexpr int kHistoriaS = 180;
    /// En el hilo de la interfaz, después de cada medición: el motor, los
    /// procesos y las filas.
    void publicar();
    QString titulo(const QString& clave) const;

    AppRuntime& rt_;
    std::unique_ptr<diag::Recursos> recursos_;
    QVariantList filas_;
    QString estado_ = QStringLiteral("desconocido");
    double margen_ = -1;
    QString limita_;
    QVariantList procesos_;
    std::uint32_t gatewayPid_ = 0;
    // Para los cuadros perdidos y lo que sale, entre una vuelta y la otra.
    int perdidosAntes_ = -1, totalAntes_ = -1;
    std::uint64_t bytesAntes_ = 0;
    std::chrono::steady_clock::time_point tAntes_{};

    /// Las métricas de la vuelta y los SLI (F4-01).
    void medirServicio(const diag::Recursos::Muestra& m, const nlohmann::json& informe);
    /// La política térmica (F4-05).
    void atenderTermica(const diag::Recursos::Muestra& m);
    QVariantList slis_;
    QVariantMap termica_;
    std::function<nlohmann::json()> transporte_;
    Degradar degradar_;
    diag::PoliticaTermica politica_;
    int apagadosAplicados_ = 0;
    std::vector<std::string> sinGancho_;
    std::map<std::string, bool> conectadaAntes_; ///< por salida: para contar las reconexiones
    nlohmann::json transporteVuelta_ = nlohmann::json::array(); ///< el de esta vuelta (medirServicio)
    FotoEntreHilos<nlohmann::json> fotoServicio_;
};

} // namespace ba::app
