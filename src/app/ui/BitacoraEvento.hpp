// ============================================================================
// src/app/ui/BitacoraEvento.hpp — la bitácora y el histórico del evento en curso (F3-03, M2)
//
// Segunda área que sale de StudioViewModel (la partición por áreas). Es lo de
// docs/planes/PLAN-EVENTOS-HISTORICO.md que corre solo, sin vista propia:
//
//   - H2: la bitácora del evento en curso (bitacora.jsonl), colgada de lo que ya pasa
//     por dos lugares —el registro general (MCP, botonera, guion, avisos) y el bus de
//     eventos (aire, salidas, oradores, asamblea)— sin tocar cada módulo;
//   - H3: las fotos de Config/ por evento (lo que cambió, con la app abierta o cerrada);
//   - H4: grabaciones.json y transmisiones.json del evento;
//   - H5: lo que queda al cerrar un evento (asamblea/, registro/ y, en otro hilo, los
//     sellos), también el cierre pendiente de un día que cambió con la app cerrada.
//
// La página Histórico es su HistoricoViewModel (`historico`), que cambia en vivo qué
// se registra.
// ============================================================================
#pragma once

#include <QObject>
#include <QString>

#include <nlohmann/json.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace ba::evento {
class Bitacora;
class FotosConfig;
} // namespace ba::evento

namespace ba::app {
class AppRuntime;
class EventoViewModel;
class HistoricoViewModel;
class VentanillaMotor;
namespace control {
class BusEventos;
}

class BitacoraEvento : public QObject {
    Q_OBJECT
   public:
    /// Vive menos que `evento` y `bus` (StudioViewModel la borra antes). Lo que sabe el motor
    /// (el archivo que se graba, la carpeta de grabación) lo lee de la foto de `ventanilla`
    /// (F3-03): los temas de salida los publica la interfaz, en su hilo.
    BitacoraEvento(AppRuntime& rt, VentanillaMotor& ventanilla, EventoViewModel& evento, control::BusEventos& bus,
                   QObject* parent = nullptr);
    /// Anota «la app se cierra», saca la última foto y corta los sellos a medias.
    ~BitacoraEvento() override;

    HistoricoViewModel* historico() const { return historico_.get(); }

   signals:
    /// Para el operador (ya traducido): el disco de los eventos casi lleno.
    void notify(const QString& texto, const QString& tipo);

   private:
    void armar();
    /// Saca la foto de ahora y anota en la bitácora lo que cambió desde la
    /// última. `momento`: "inicio" | "cierre" | "" (solo revisar), del evento
    /// `carpeta` (vacío = el en curso).
    void revisarConfig(const std::string& carpeta, const std::string& momento);
    /// grabaciones.json y transmisiones.json del evento en curso (H4): cada
    /// grabación con su archivo y desde/hasta; cada vivo con su URL y caídas.
    void indexarSalida(const std::string& tema, const nlohmann::json& valor);
    /// Lo que queda al cerrar un evento (H5): asamblea/, registro/ y, en otro
    /// hilo, sellos.json. Los hilos se cancelan y esperan al cerrar la app.
    void cerrarEnHistorico(const std::string& carpeta);

    /// El archivo que graba la por omisión y su copia (de la foto del motor).
    std::string archivoGrabacion() const;
    std::string copiaGrabacion() const;

    AppRuntime& rt_; ///< la carpeta de datos
    VentanillaMotor& ventanilla_;
    EventoViewModel* evento_;
    control::BusEventos* bus_;
    /// bitacora.jsonl del evento en curso.
    std::unique_ptr<evento::Bitacora> bitacora_;
    std::unique_ptr<HistoricoViewModel> historico_;
    /// Lo que no va del bus a la bitácora (cambia en vivo desde Ajustes del histórico).
    std::shared_ptr<std::vector<std::string>> busExcluir_;
    int bitacoraOyente_ = 0, bitacoraBus_ = 0;
    /// Las fotos de Config/ por evento (H3): la última vista y la revisión cada 10 s.
    std::unique_ptr<evento::FotosConfig> fotos_;
    nlohmann::json fotoUltima_;
    struct Sellador {
        std::thread hilo;
        std::shared_ptr<std::atomic<bool>> listo;
    };
    std::vector<Sellador> selladores_;
    std::atomic<bool> cancelarSellos_{false};
};

} // namespace ba::app
