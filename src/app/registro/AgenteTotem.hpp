// ============================================================================
// src/app/registro/AgenteTotem.hpp — los turnos con el modelo de UNA conversación de un tótem
//
// El que maneja los tiempos de registro/AgenteRegistro (lo puro: qué se le pide al modelo y qué
// se hace con lo que contesta) contra el servidor (agent/ChatEnVivo): uno por conversación,
// al lado de su registro::Conversacion. Con Qt (timers, el hilo de la interfaz).
//
// POR TURNO: lo que dijo la persona va al modelo, salvo lo que el guion resuelve solo al
// instante (Conversacion::paraElGuion: cancelar, repetir, un «sí» suelto…). Lo que el modelo
// escribe llega en streaming: el texto entero a la pantalla (alTexto) y frase por frase a la voz
// (alFrase), mientras sigue escribiendo. Al terminar, el guion retoma (alListo con su
// Respuesta: confirmar lo anotado, seguir, retomar la pregunta).
//
// EL GUION ES EL RESPALDO, sin perder nada: si el modelo no empieza a contestar en
// `registro.agentePrimeraS` (5 s), si el turno pasa de `registro.agenteTurnoS` (15 s) o si
// falla, lo oído lo contesta el guion (Conversacion::oido, con sus normalizadores), como sin
// modelo. Con `registro.agenteFallas` (2) fallas seguidas, la conversación sigue con el guion
// solo (caido()). Lo ya anotado queda.
//
// Lo que se oye mientras el modelo piensa se junta y va en el turno siguiente (no se pisa).
// ============================================================================
#pragma once

#include <QObject>
#include <QTimer>

#include <functional>
#include <memory>
#include <string>

#include "agent/ChatEnVivo.hpp"
#include "agent/Mirada.hpp"
#include "registro/AgenteRegistro.hpp"
#include "registro/Conversacion.hpp"

namespace ba::registro {

class AgenteTotem : public QObject {
    Q_OBJECT
   public:
    struct Tiempos {
        int primeraMs = 5000; ///< hasta que empieza a contestar (texto o herramientas)
        int turnoMs = 15000;  ///< el turno entero (con la vuelta para repreguntar)
        int fallasMax = 2;    ///< seguidas: después, el guion solo
    };
    /// Los de los ajustes (registro.agente*).
    static Tiempos tiemposDeAjustes();

    /// `conv` tiene que vivir más que esto (los dos son de la misma conexión del tótem).
    AgenteTotem(ba::app::agent::FuenteMirada fuente, std::string prompt, Conversacion& conv, Tiempos tiempos,
                QObject* parent = nullptr);
    ~AgenteTotem() override;

    // Lo que hay que hacer con lo que pasa (en el hilo de la interfaz).
    /// El texto que va escribiendo el modelo en este turno (entero hasta ahora): la pantalla.
    std::function<void(const std::string& acumulado)> alTexto;
    /// Una frase entera, lista para decir en voz alta.
    std::function<void(const std::string& frase)> alFrase;
    /// Terminó el turno: lo que hace el guion ahora (con lo que el modelo anotó, o con lo oído
    /// si el modelo no pudo). `porGuion`: lo contestó el guion solo.
    std::function<void(const Conversacion::Respuesta& r, bool porGuion)> alListo;
    /// El modelo dejó de usarse en esta conversación (y por qué).
    std::function<void(const std::string& motivo)> alCaer;

    /// Lo que dijo la persona. false = no es para el modelo (lo resuelve el guion solo, o el
    /// modelo está caído): quien llama lo pasa a Conversacion::oido como siempre.
    bool oido(const std::string& dicho);
    /// Un turno en curso (el modelo pensando o escribiendo).
    bool ocupado() const { return ocupado_; }
    bool caido() const { return caido_; }
    const std::string& motivoCaida() const { return motivo_; }
    const ba::app::agent::FuenteMirada& fuente() const { return fuente_; }
    /// Corta el turno en curso sin contestar nada (la atiende una persona, se canceló, se fue).
    void cancelar();

    /// Mientras el tótem saluda: que el servidor lea el sistema y las herramientas antes del
    /// primer turno (AgenteRegistro::precalentar). Sin respuesta que esperar.
    void precalentar();

   private:
    void empezar(const std::string& dicho);
    void mandar(const nlohmann::json& cuerpo);
    void alNuevo(const std::string& nuevo);
    void alTerminarVuelta(const TurnoModelo& t, const std::string& error);
    /// No contestó o falló: el guion contesta lo oído (con lo que el modelo ya hubiera anotado).
    void falla(const std::string& motivo);
    /// Terminó el turno (bien o por el guion): avisa y sigue con lo que se juntó mientras.
    void cerrar(const Conversacion::Respuesta& r, bool porGuion);

    ba::app::agent::FuenteMirada fuente_;
    Conversacion& conv_;
    AgenteRegistro agente_;
    Tiempos tiempos_;
    std::shared_ptr<ba::app::agent::ChatEnVivo::Pedido> pedido_; ///< la vuelta en curso
    std::shared_ptr<ba::app::agent::ChatEnVivo::Pedido> calentando_; ///< el precalentamiento
    QTimer primera_, tope_;
    TrozadorFrases frases_;
    std::string dicho_;     ///< lo oído de este turno
    std::string texto_;     ///< lo que escribió el modelo en este turno
    std::string pendiente_; ///< lo oído mientras pensaba: va en el turno siguiente
    bool ocupado_ = false;
    bool empezo_ = false;   ///< llegó algo de esta vuelta (texto o herramientas)
    bool caido_ = false;
    std::string motivo_;
    int fallas_ = 0;
    bool dijoAlgo_ = false, rechazos_ = false; ///< del turno, por si se corta en la segunda vuelta
};

} // namespace ba::registro
