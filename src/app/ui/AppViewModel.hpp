// ============================================================================
// src/app/ui/AppViewModel.hpp — el área de la app misma: el vivo, la caída, cerrar y reiniciar (F3-03)
//
// Sale de StudioViewModel: lo que es de la aplicación y no de un área del estudio, sobre
// sus modelos:
//
//   config::ModoVivoConfig  el modo en vivo (T48): qué páginas se protegen mientras sale
//                           algo de verdad y lo que se desbloqueó con un motivo
//   SystemViewModel         la versión, las actualizaciones y el reinicio por el launcher
//   Configuraciones         los eventos (cada uno con lo suyo): abrir otro = reiniciar
//
// También la caída de la que se acaba de volver (T24: el cartel, cuánto tardó el aire en
// volver), lo que se corta si se cierra la app (para confirmar) y la cuenta regresiva
// antes de cerrarse para que el launcher la reabra (otro evento, una actualización).
//
// En QML: Studio.app (modoVivo, paginaBloqueada, caidaReciente, impactoDeCerrar,
// avisoDeCerrar, reinicio, reiniciarPrograma); Studio.system y Studio.configuraciones
// siguen en la raíz.
// ============================================================================
#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include "comun/FotoEntreHilos.hpp"

#include <functional>
#include <memory>
#include <string>

namespace ba::app {
class AppRuntime;
class Configuraciones;
class SystemViewModel;
} // namespace ba::app
namespace ba::app::agent {
class AgentViewModel;
}
namespace ba::app::config {
class ModoVivoConfig;
}
namespace ba::app::control {
class BusEventos;
}

namespace ba::app {

class AppViewModel : public QObject {
    Q_OBJECT
    /// Modo en vivo (T48): transmitiendo o con el evento en vivo; protege páginas y ajustes que cortan.
    Q_PROPERTY(bool modoVivo READ modoVivo NOTIFY estadoCambio)
    /// Lo que se corta si se cierra (o se actualiza) la app: {transmitiendo, grabando, personas, hay}.
    Q_PROPERTY(QVariantMap impactoDeCerrar READ impactoDeCerrar NOTIFY estadoCambio)
    /// La app está por cerrarse para que el launcher la reabra (otro evento, actualización):
    /// {aviso, segundos} con la cuenta regresiva; vacío si no.
    Q_PROPERTY(QVariantMap reinicio READ reinicio NOTIFY reinicioChanged)
    /// La caída de la que se acaba de volver (T24): {causa, componente, hace_s, aire_vuelta_s?} o vacío.
    Q_PROPERTY(QVariantMap caidaReciente READ caidaReciente NOTIFY caidaChanged)
   public:
    struct Partes {
        AppRuntime& rt;
        /// Lo que sale ahora (las pastillas de la raíz) y quiénes están conectados (la
        /// sala y la sesión remota): lo que se corta al cerrar.
        std::function<bool()> transmitiendo;
        std::function<bool()> grabando;
        std::function<int()> personas;
        /// El asistente del evento (se arma después): su conversación sigue tras el reinicio.
        std::function<agent::AgentViewModel*()> asistente;
    };
    explicit AppViewModel(Partes p, QObject* parent = nullptr);
    ~AppViewModel() override;

    SystemViewModel& sistema() { return *system_; }
    Configuraciones& configuraciones() { return *configuraciones_; }
    QObject* sistemaQml() const;
    QObject* configuracionesQml() const;

    bool modoVivo() const;
    /// ¿Hay que desbloquear la página `id` (del riel) para entrar? (en vivo y en la lista)
    Q_INVOKABLE bool paginaBloqueada(const QString& id) const;
    /// La desbloquea hasta que termine el vivo; el motivo queda en el registro.
    Q_INVOKABLE void desbloquearPagina(const QString& id, const QString& motivo);
    /// DESDE CUALQUIER HILO (F3-03): de la foto que publica al cambiar (lo que se protege y
    /// lo desbloqueado) y el motor (si sale algo: haySalida). La lee el MCP en el pool.
    nlohmann::json modoVivoEstado() const;
    std::string setModoVivoAjustes(const nlohmann::json& a);

    QVariantMap caidaReciente() const { return caida_; }
    /// DESDE CUALQUIER HILO: la caída de recién, de la foto ({} si no hay).
    nlohmann::json caidaRecienteFoto() const;
    Q_INVOKABLE void cerrarCartelCaida();
    /// Las últimas `n` caídas que anotó el supervisor (Runtime/caidas.jsonl). Un archivo:
    /// desde cualquier hilo.
    static nlohmann::json caidas(int n);

    /// {transmitiendo, grabando, personas, hay}. `personas`: los de la sala (lobby,
    /// programa, fila de oradores) y los integrantes de la sesión remota.
    QVariantMap impactoDeCerrar() const;
    /// El aviso para confirmar: `accion` vacía = cerrar la ventana; si no, lo que
    /// cierra la app (p. ej. "actualizar"). "" si no se corta nada.
    Q_INVOKABLE QString avisoDeCerrar(const QString& accion = {}) const;
    QVariantMap reinicio() const { return reinicio_; }
    /// Cierra la app y la vuelve a abrir por el launcher (con los mismos --test/--debug y
    /// la conversación del asistente): cambiar de evento, de idioma. "" o el error.
    Q_INVOKABLE QString reiniciarPrograma(const QString& aviso);

    /// En cada vuelta del reloj: el aire volvió tras la caída (el cartel se cierra solo),
    /// el modo en vivo empezó o terminó (al bus; lo desbloqueado se vuelve a proteger).
    void vigilar(control::BusEventos& bus);

   signals:
    void estadoCambio();
    void reinicioChanged();
    void caidaChanged();
    /// Pidió cerrarse (la cuenta regresiva llegó a cero).
    void quitRequested();
    /// Para el operador (ya traducido).
    void notify(const QString& texto, const QString& nivel);

   private:
    void leerCaida();
    /// Copia a la foto lo que cambió (lo conectan estadoCambio y caidaChanged).
    void publicarFoto();
    void contarHastaCerrar(const QString& aviso);

    Partes p_;
    std::unique_ptr<config::ModoVivoConfig> modoVivoCfg_; ///< Config/modo-vivo.json (modelo)
    std::unique_ptr<SystemViewModel> system_;
    std::unique_ptr<Configuraciones> configuraciones_;
    QSet<QString> desbloqueadas_;
    bool estabaEnVivo_ = false;
    QVariantMap caida_;
    qint64 caidaMs_ = 0;
    QVariantMap reinicio_;
    bool eventoNuevo_ = false; ///< el reinicio que viene abre un evento recién creado
    /// Lo de arriba para los otros hilos (F3-03): {activo, incluir_grabando,
    /// paginas_protegidas, desbloqueadas, caida}. La pone la interfaz cada vez que cambia.
    FotoEntreHilos<nlohmann::json> foto_;
};

} // namespace ba::app
