// ============================================================================
// src/app/asamblea/Presidencia.hpp — la presidencia y la sesión remota frente a
// la sala (modelo)
//
// Lo que la presidencia hace sobre todos y lo que la banca virtual necesita de
// la sala (el gateway), en un solo lugar para la GUI, el MCP y las consolas web
// de la presidencia (las tablets de /presidencia/):
//
//   - «Silenciar a todos» y «Hablo yo»: la central (menos el presidente), el
//     orador al aire, los remotos con la palabra (en un debate, el turno pasa a
//     nadie) y el aviso al bus (presidencia/silencio, presidencia/habla).
//   - La prioridad del presidente en su unidad: la central ya calló a los suyos;
//     acá se callan los remotos y se publica presidencia/silencio.
//   - Las consolas: el estado que se les manda (como mucho 4 veces por segundo),
//     el enlace al programa en vivo y lo que piden (habilitar, dar la palabra,
//     debate, votar —por asamblea::Votaciones—, apretar para hablar…). Un error
//     vuelve a la consola que pidió.
//   - La sesión remota con el gateway: credenciales, quién manda medios (con
//     número, para que una lista vieja no pise a la nueva), las vistas del
//     celular (toma canales de escena mientras haya alguien), el origen de los
//     links, cortar, las fotos y los avisos que llegan del gateway.
//
// No conoce la interfaz: avisa con `aviso` (texto, nivel) y `presidenciaHablaCambio`.
// ============================================================================
#pragma once

#include <QObject>
#include <QStringList>
#include <QTimer>

#include <functional>
#include <string>

#include <nlohmann/json.hpp>

namespace ba::app {
class BroadcastEngine;
class ConferenceClient;
} // namespace ba::app

namespace ba::app::control {
class BusEventos;
}

namespace ba::app::asamblea {

class Bancas;
class ConferenciaDcn;
class CuerpoViewModel;
class Debate;
class OrdenDelDia;
class Quorum;
class SesionRemota;
class Votaciones;

class Presidencia : public QObject {
    Q_OBJECT
   public:
    using json = nlohmann::json;
    /// "" = hecho; si no, por qué no (también lo que contesta la central, que llega después).
    using Listo = std::function<void(const std::string& error)>;

    /// Lo que usa (todo vive más que este modelo). `debate`, `votaciones`, `quorum`
    /// y `ordenDia` pueden faltar.
    struct Partes {
        SesionRemota& sesion;
        Bancas& bancas;
        Debate* debate = nullptr;
        ConferenciaDcn& dcn;
        Votaciones* votaciones = nullptr;
        CuerpoViewModel& cuerpo;
        Quorum* quorum = nullptr;
        OrdenDelDia* ordenDia = nullptr;
        ConferenceClient& sala;
        control::BusEventos& bus;
        BroadcastEngine& motor;
        /// ¿El operador tiene micrófono para la sala? (para hablarle a un remoto)
        std::function<bool()> microfonoOperador;
    };

    explicit Presidencia(Partes p, QObject* parent = nullptr);
    ~Presidencia() override;

    /// Calla a todos: la central (menos la presidencia), el orador al aire, los remotos con
    /// la palabra (en un debate el turno pasa a nadie, el debate sigue). `listo` recibe lo
    /// que contesta la central ("" sin central).
    void silenciarTodos(const std::string& quien, Listo listo = {});
    /// «Hablo yo»: calla a todos y abre el micrófono de la presidencia en la central; con
    /// false lo cierra. El error (sin central, sin unidad de presidente, o el de la central) va a `listo`.
    void hablaPresidencia(bool activo, const std::string& quien, Listo listo = {});
    bool presidenciaHabla() const { return presidenciaHabla_; }

    /// «Apretar para hablar» desde la app: mientras `activo`, el remoto oye la voz del
    /// operador (su micrófono de la sala) en su celular. "" o el error.
    std::string hablarARemoto(const std::string& integrante, bool activo);

    /// Le vuelve a decir todo al gateway (credenciales, vistas, quién tiene la palabra).
    void sincronizar();

   signals:
    /// Algo para el operador (la interfaz lo muestra).
    void aviso(const QString& texto, const QString& nivel);
    void presidenciaHablaCambio();

   private:
    void conectar();
    /// Los remotos que tienen la palabra (y el orador remoto al aire) dejan de hablar.
    void silenciarRemotos();
    int unidadPresidente() const;
    void prioridadCambio();

    // --- Las consolas ---------------------------------------------------------------
    json estadoConsola() const;
    void programarEstado();
    void mandarEstadoConsola();
    void enlaceProgramaConsola(const std::string& consola);
    void accionConsola(const std::string& consola, const json& m);
    void mandarA(const std::string& destino, const json& msg);

    // --- La sesión remota con el gateway --------------------------------------------
    /// Las vistas del celular: el programa o las escenas (toma sus canales mientras haya
    /// alguien conectado). `forzar`: aunque no cambió nada.
    void aplicarVistas(bool forzar = false);
    void mandarMedios(const QStringList& ids);
    void avisarPunto(const json& p);

    Partes p_;
    bool presidenciaHabla_ = false;
    bool prioridadCentral_ = false; ///< el botón de prioridad del presidente, como lo vio la última vez
    QTimer consolaTimer_;
    std::string firmaVistas_;
    QStringList mediosPendientes_;
    bool mediosProgramados_ = false;
    /// Arranca en la hora de arranque: siempre mayor que el de la corrida anterior (el gateway
    /// sigue andando y descarta las listas con número menor al último).
    long long mediosSeq_;
    int oyentePunto_ = 0;
};

} // namespace ba::app::asamblea
