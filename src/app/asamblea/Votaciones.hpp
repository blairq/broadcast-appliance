// ============================================================================
// src/app/asamblea/Votaciones.hpp — las votaciones de la asamblea (modelo)
//
// Junta las dos formas de votar y Quórum:
//
//   - la CENTRAL de conferencia (bancas con unidad, asamblea/ConferenciaDcn);
//   - los CELULARES de los integrantes remotos (asamblea/SesionRemota).
//
// Reglas, para todos los que piden (consolas de presidencia, GUI, MCP):
//
//   - con la central conectada, la votación la lleva la central y los celulares
//     la siguen (se abre, pausa y cierra igual en los dos lados);
//   - sin central, la llevan los celulares, numeradas una tras otra;
//   - cerrar en los celulares muestra el resultado a todos;
//   - el resultado oficial va a Quórum (acta, CSV nominal, gráfico) y al bus
//     (sesion/votacion); si votaron también los celulares en una de la
//     central, un voto por integrante (vale el último);
//   - el estado, sea quien sea el que la lleve (los celulares siguen a la
//     central), también al bus: votacion/estado {estado, numero, tema} y, para
//     las condiciones de la línea de tiempo, votacion/abierta (abierta o en
//     pausa) y votacion/cerrada (true/false).
// ============================================================================
#pragma once

#include <QObject>
#include <QString>

#include <functional>
#include <string>

#include <nlohmann/json.hpp>

namespace ba::app::control {
class BusEventos;
}

namespace ba::app::asamblea {

class ConferenciaDcn;
class Quorum;
class SesionRemota;

class Votaciones : public QObject {
    Q_OBJECT
   public:
    using json = nlohmann::json;
    using Listo = std::function<void(const std::string& error)>;

    Votaciones(SesionRemota& sesion, ConferenciaDcn& dcn, Quorum& quorum, control::BusEventos& bus,
               QObject* parent = nullptr);

    /// La votación de una consola de presidencia: abrir {tema, menu, segundos},
    /// pausar, reanudar, cerrar, a_quorum. A la central si está conectada; si no,
    /// a los celulares. El error inmediato va a `listo` (el de la central, cuando contesta).
    void accion(const std::string& accion, const json& args, const std::string& quien, Listo listo);

    // --- En los celulares (sin la central) -----------------------------------------
    /// La abre en los celulares con el número que sigue. "" o por qué no.
    std::string abrirEnCelulares(const std::string& tema, const std::string& menu, int segundos);
    /// La cierra y muestra el resultado a todos.
    void cerrarEnCelulares();
    void pausarEnCelulares(bool pausa);
    /// El resultado de los celulares (cerrada) como oficial en Quórum. {resultado} o {error}.
    json celularesAQuorum(const std::string& asunto = {});

    /// Un resultado oficial (del tablero, de Secretaría, de la central): a Quórum
    /// y al bus. Lo que devuelve Quórum ({…} o {error}).
    json cargarOficial(const json& resultado);

    // --- Para la interfaz ------------------------------------------------------------
    Q_INVOKABLE QString abrirEnCelularesUi(const QString& tema, const QString& menu, int segundos);
    Q_INVOKABLE void cerrarEnCelularesUi() { cerrarEnCelulares(); }
    Q_INVOKABLE void pausarEnCelularesUi(bool pausa) { pausarEnCelulares(pausa); }

   private:
    /// La de la central, también en los celulares.
    void seguirCentral();
    /// El estado al bus (votacion/estado, votacion/abierta, votacion/cerrada).
    void publicarEstado();
    /// La de la central con los votos de los celulares (un voto por integrante).
    json combinada(const json& resultadoCentral) const;

    SesionRemota& sesion_;
    ConferenciaDcn& dcn_;
    Quorum& quorum_;
    control::BusEventos& bus_;
};

} // namespace ba::app::asamblea
