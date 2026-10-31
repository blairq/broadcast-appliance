// ============================================================================
// src/app/ui/AlarmasViewModel.hpp — el área de las alarmas, ligada a su modelo (F3-03, M2)
//
// Primera área que sale de StudioViewModel (la partición por áreas del plan: cada
// ViewModel ligado a su modelo). El modelo es el vigía de los disparadores
// (agent::AlertEngine, que corre en el hilo del plano de control); esto es lo que la
// vista ve de él y lo que lo rodea en la interfaz:
//
//   - `activas` y `critica` son PROPIEDADES con su aviso, que sale del modelo
//     (alarmasCambio): la vista se entera sola, venga el cambio de un disparo, del MCP o
//     de un reconocimiento. Antes la franja y la tarjeta Sistema llamaban a un
//     Q_INVOKABLE cada vez que algo les decía que releyeran.
//   - Lo que el vigía lee de la interfaz, como foto (agent/EntradasAlarmas: la
//     interfaz llama `refrescarFoto` en su reloj).
//   - Cada disparo: los notificadores desde el hilo del vigía (AvisoFueraDeUi, sale
//     aunque la interfaz no ande) y, en la interfaz, el bus de eventos (control web),
//     el aviso en pantalla y el sonido.
// ============================================================================
#pragma once

#include <QObject>
#include <QVariantList>

#include <nlohmann/json.hpp>

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include "agent/AlertEngine.hpp"

namespace ba::app {
class AvisoFueraDeUi;
}

namespace ba::app::agent {
class AlarmaSonora;
class EntradasAlarmas;
} // namespace ba::app::agent

namespace ba::app {

class AlarmasViewModel : public QObject {
    Q_OBJECT
    /// Las alarmas que esperan que alguien las reconozca (los eventos con `reconocer`).
    Q_PROPERTY(QVariantList activas READ activas NOTIFY cambio)
    /// Hay una crítica sin reconocer ni silenciar (la franja roja de arriba).
    Q_PROPERTY(bool critica READ critica NOTIFY cambio)
    /// El sonido de las alarmas (volumen, dispositivo, probar).
    Q_PROPERTY(QObject* sonido READ sonido CONSTANT)
   public:
    /// `deLaInterfaz` y `directas`: ver agent/EntradasAlarmas. `datos`: la carpeta de
    /// la configuración (Config/alertas.json y el sonido).
    AlarmasViewModel(agent::AlertInputs deLaInterfaz, agent::AlertInputs directas, const std::filesystem::path& datos,
                     QObject* parent = nullptr);
    ~AlarmasViewModel() override;

    /// El vigía (el MCP, los asistentes, el plano de control que lo adopta).
    agent::AlertEngine& motor() { return *motor_; }
    /// En el reloj de la interfaz: copia lo suyo para el vigía.
    void refrescarFoto();
    /// Quién lleva los notificadores afuera sin la interfaz (se crea después).
    void setAvisoFueraDeUi(AvisoFueraDeUi* a) { aviso_.store(a); }
    /// Cómo se publica un disparo en el bus de eventos (tema, datos): lo ve el control web.
    void setPublicar(std::function<void(const std::string& tema, const nlohmann::json& datos)> f) { publicar_ = std::move(f); }

    QVariantList activas() const { return activas_; }
    bool critica() const { return critica_; }
    QObject* sonido() const;

    /// Reconoce una (por id de evento) o todas (0). Cuántas.
    Q_INVOKABLE int reconocer(qint64 id);
    /// Le saca (o le devuelve) el sonido a un disparador: sus alarmas siguen, sin sonar.
    Q_INVOKABLE bool silenciar(const QString& disparador, bool silenciar);

   signals:
    void cambio();
    /// Para el operador (ya traducido).
    void notify(const QString& texto, const QString& tipo);

   private:
    /// Lo relee del modelo y avisa (en el hilo de la interfaz).
    void releer();
    void enHiloDelVigia(const nlohmann::json& ev); ///< los notificadores
    void enLaInterfaz(const nlohmann::json& ev);   ///< bus, aviso, sonido

    std::unique_ptr<agent::EntradasAlarmas> entradas_;
    std::unique_ptr<agent::AlertEngine> motor_;
    std::unique_ptr<agent::AlarmaSonora> sonido_;
    std::atomic<AvisoFueraDeUi*> aviso_{nullptr}; ///< lo lee el hilo del vigía
    std::function<void(const std::string&, const nlohmann::json&)> publicar_;
    QVariantList activas_;
    bool critica_ = false;
};

} // namespace ba::app
