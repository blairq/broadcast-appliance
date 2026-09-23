// ============================================================================
// src/app/agent/Despachador.hpp — eventos -> rol -> agente de un solo tiro
//
// El residente es el aparato, no el agente. La app ya vigila (AlertEngine,
// cada 250 ms); cuando algo pasa y un rol activo lo reclama, el despachador
// lanza un agente DE UN SOLO TIRO —`claude -p` u `opencode run`— con el juego
// de herramientas del rol y el encargo como argumento. El proceso vive lo que
// dura el trabajo y se muere. Lo que desaparece con eso: procesos ociosos,
// sesiones que se pisan, latidos, y la inyeccion de texto en una TUI que
// capaz esta en un menu.
//
// Cada CORRIDA queda registrada con sus pasos (lo que el agente dijo, que
// herramienta llamo, que le contesto) leidos de la salida JSON del motor: es
// la vista de «que esta haciendo», pareja para todos los motores, y sin
// raspar ninguna terminal. La pagina Agentes la muestra; `agente_corridas`
// la expone por MCP (todo lo que se toca por MCP se ve en la interfaz, y al
// reves).
//
// Reglas que hacen que esto no se descontrole:
//   * Una corrida por rol a la vez: lo demas espera en cola.
//   * Agrupar: un rol puede juntar los eventos que lleguen en N ms en UNA
//     corrida (dos picos de audio seguidos son un problema, no dos).
//   * Profundidad 1: un agente puede encargarle algo a otro rol, pero ese otro
//     no puede encargar a nadie. Sin esto, dos roles se mandan pedidos en
//     circulo y se comen el limite de uso en minutos.
//   * Memoria como dato, no como contexto: cada corrida recibe los resumenes
//     de las ultimas de su rol (y el contexto que le arme el sistema), en vez
//     de arrastrar una conversacion que crece hasta que el motor la compacta.
//   * Tope de tiempo por corrida: un agente que no termina se mata y queda
//     como error, con lo que alcanzo a hacer.
// ============================================================================
#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "agent/Lanzamiento.hpp"

class QProcess;
class QTimer;

namespace ba::app::agent {

class Roles;

class Despachador : public QObject {
    Q_OBJECT
    /// Los roles con su estado (ver Roles::aJson) mas `corriendo` y `enCola`.
    Q_PROPERTY(QVariantList roles READ rolesQml NOTIFY cambio)
    /// Las ultimas corridas, la mas nueva primero (sin los pasos: corrida(id)).
    Q_PROPERTY(QVariantList corridas READ corridasQml NOTIFY cambio)
    Q_PROPERTY(int enCurso READ enCurso NOTIFY cambio)
    /// Los proveedores que se pueden elegir por rol (los mismos del asistente).
    Q_PROPERTY(QVariantList proveedores READ proveedores CONSTANT)

  public:
    using json = nlohmann::json;

    static constexpr int kProfundidadMax = 1;
    static constexpr int kTopeMinutos = 20;
    static constexpr std::size_t kCorridasGuardadas = 200;
    static constexpr int kResumenesDeContexto = 5;

    Despachador(Roles& roles, Motores motores, QObject* parent = nullptr);
    ~Despachador() override;

    Roles& roles() { return roles_; }
    const Roles& roles() const { return roles_; }

    /// Un evento del aparato (un disparo del AlertEngine). true si un rol
    /// activo lo reclamo (y entonces el asistente interactivo no lo recibe).
    bool evento(const json& ev);

    /// Un encargo explicito a un rol. `origen`: de donde viene ("operador",
    /// "lobby:<id>", "rol:<otro>"). Devuelve el id de la corrida (o vacio y
    /// el motivo en `error`).
    std::string encargar(const std::string& rol, const std::string& texto, const std::string& origen,
                         int profundidad = 0, std::string* error = nullptr,
                         const std::vector<std::string>& adjuntos = {});
    /// Profundidad de la corrida en curso de ese rol, o -1 si no hay.
    int profundidadDe(const std::string& rol) const;

    json corridas(int limit = 50, const std::string& rol = {}) const;
    /// Una corrida con sus pasos, o null.
    json corrida(const std::string& id) const;
    int enCurso() const;

    /// Contexto que el sistema le arma a un rol en cada corrida (quien esta
    /// en la sala, por ejemplo). Va antes del encargo.
    void setContexto(const std::string& rol, std::function<std::string()> fn);

    // --- Para la interfaz --------------------------------------------------------
    QVariantList rolesQml() const;
    QVariantList corridasQml() const;
    QVariantList proveedores() const;
    Q_INVOKABLE void setActivo(const QString& rol, bool activo);
    Q_INVOKABLE void setProveedor(const QString& rol, const QString& proveedor);
    Q_INVOKABLE void setModelo(const QString& rol, const QString& modelo);
    Q_INVOKABLE QStringList modelosDe(const QString& proveedor) const;
    /// Devuelve el error (vacio si se encolo).
    Q_INVOKABLE QString encargarDesdeUi(const QString& rol, const QString& texto);
    Q_INVOKABLE void detener(const QString& id);
    Q_INVOKABLE QVariantMap corridaQml(const QString& id) const;

  signals:
    void cambio();
    /// Un evento fue asignado a un rol (antes de lanzar): para que el sistema
    /// prepare lo que haga falta (la exencion de aprobacion de esa persona).
    void eventoTomado(const QString& rol, const QVariantMap& evento);
    void corridaTerminada(const QString& id, bool ok);
    void notify(const QString& message, const QString& level);

  private:
    struct Corrida;
    struct Pendiente;
    void lanzar(const std::string& rol);
    void lanzarAhora(Corrida& c);
    void terminar(Corrida& c, const std::string& estado, const std::string& error);
    void leerSalida(Corrida& c);
    void procesarLinea(Corrida& c, const std::string& linea);
    void paso(Corrida& c, json p);
    std::string contextoPara(const std::string& rol) const;
    Corrida* porId(const std::string& id);
    const Corrida* porId(const std::string& id) const;
    Corrida* enCursoDe(const std::string& rol);
    const Corrida* enCursoDe(const std::string& rol) const;
    static json resumenCorrida(const Corrida& c);

    Roles& roles_;
    Motores motores_;
    std::deque<std::unique_ptr<Corrida>> corridas_;
    std::map<std::string, std::unique_ptr<Pendiente>> agrupando_;
    std::map<std::string, std::deque<std::string>> cola_; ///< rol -> ids esperando
    std::map<std::string, std::function<std::string()>> contexto_;
    int seq_ = 0;
};

} // namespace ba::app::agent
