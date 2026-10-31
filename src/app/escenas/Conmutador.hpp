// ============================================================================
// src/app/escenas/Conmutador.hpp — el conmutador de escenas de cada salida (modelo)
//
// Qué escena está al aire, cuál está armada (el bus de preview) y cómo se pasa
// de una a otra. Es la mesa de corte: la GUI (botones, T-bar, doble clic), el
// MCP (scene_switch, take, scene_build…), la playlist, el guion del evento y el
// respaldo de entradas cambian de escena por acá, y todos con las mismas reglas:
//
//   - la transición de entrada de la escena (si tiene y no se pidió otra);
//   - el audio que sigue al video (AFV): las fuentes marcadas suben con la escena
//     que las tiene y bajan con la que no, en el tiempo de la transición;
//   - después de un TAKE, la que salió queda armada (volver es un solo golpe);
//   - la escena al aire se guarda (vuelve igual tras un corte de luz).
//
// UNO POR SALIDA (docs/planes/PLAN-SALIDAS.md): cada salida es un operador
// completo, con su aire, su armada, su T-bar y su AFV, que mueve el fader de cada
// fuente EN SU mezcla (el bus de su salida en el motor). Las transiciones de
// entrada son de la escena: las comparten todas. Los tiene el registro
// (Conmutadores), que crea y saca uno por cada salida del motor; el de la por
// omisión existe siempre y es el que usa todo lo que no dice salida.
//
// Las transiciones de entrada y el AFV se guardan en Config/transiciones.json:
// lo de siempre (`entrada`, `afv`, `afv_niveles`) es de la por omisión; el AFV de
// las demás va en `salidas: {<id>: {afv, afv_niveles}}`.
// Avisan con señales (alAire, armadaCambio, afvCambio): los view models se
// actualizan con ellas; el conmutador no sabe nada de la interfaz.
//
// HILOS (F3-03): son modelos del aire, DEL DUEÑO DEL MOTOR (el plano de control: la
// interfaz los muda allá con la ventanilla, VentanillaMotor::adoptar). La interfaz migrada
// les pide por la ventanilla y lee lo suyo de la foto del motor (la armada, el AFV y la
// T-bar de cada salida, las transiciones de entrada: lo suman con
// AppRuntime::aportarALaFoto); lo que todavía los usa directo desde la interfaz (la línea
// de tiempo, la asamblea) entra con el turno. Cada operación toma el turno del motor por
// todo lo que dura (emision/TurnoMotor.hpp: nadie ve un corte a medias, ni la armada de una
// salida que se está sacando) y las señales salen en el hilo del conmutador (el dueño): a
// quien escucha desde otro hilo le llegan encoladas. El fundido del AFV va por la agenda
// del dueño (no espera a la interfaz).
// ============================================================================
#pragma once

#include "escenas/Afv.hpp"

#include <QObject>
#include <QString>

#include <nlohmann/json.hpp>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ba::app {
class AppRuntime;
}

namespace ba::app::escenas {

class Conmutadores;

class Conmutador : public QObject {
    Q_OBJECT
   public:
    /// El de la salida `salida` (su id). Lo crea el registro.
    Conmutador(AppRuntime& rt, Conmutadores& registro, std::string salida, Afv afv, QObject* parent = nullptr);

    const std::string& salida() const { return salida_; }

    /// Pone `escena` al aire (corte directo, sin pasar por el preview), con la
    /// transición pedida (`tipo` vacío = la de entrada de la escena o la global).
    /// "" = hecho; si no, por qué no.
    std::string poner(const std::string& escena, const std::string& tipo = {}, int ms = -1);
    /// La arma en el bus de preview. Error si ya está al aire (el TAKE no tendría sentido).
    std::string armar(const std::string& escena);
    /// Pasa la armada al aire; la que sale queda armada. "" o por qué no.
    std::string take(const std::string& tipo = {}, int ms = -1);
    /// La T-bar: `t` de 0 a 1 hacia la armada; `soltar` la termina. "" o por qué no.
    std::string tbar(double t, bool soltar);
    /// Por dónde va la T-bar (0…1; 0 = quieta). Para mostrarla (el bus: aire/<salida>/tbar).
    double tbarPosicion() const;

    std::string alAire() const;
    std::string armada() const;

    /// La transición con la que entra `escena` cuando no se pide otra (de la escena:
    /// la misma en todas las salidas). `tipo`: cut, fade, swipe, slide, fade_to_color;
    /// vacío = la global.
    std::string setTransicionEntrada(const std::string& escena, const std::string& tipo, int ms);
    nlohmann::json transicionEntrada(const std::string& escena) const; ///< {tipo, ms} o {}

    /// Audio que sigue al video EN ESTA salida: la fuente sube con la escena que la tiene.
    void setAfv(const std::string& fuente, bool sigue);
    bool afv(const std::string& fuente) const;
    std::vector<std::string> afvFuentes() const;
    /// Una copia (la lee el registro al guardar).
    Afv afvEstado() const;

   signals:
    /// Cambió la escena al aire (ya guardada).
    void alAireCambio(const QString& escena);
    /// Cambió la escena armada (vacía = ninguna).
    void armadaCambio(const QString& escena);
    void afvCambio();

   private:
    friend class Conmutadores; // lo suyo en la foto del motor (con el turno)
    /// La parte común de poner/take: transición de entrada, AFV, guardar y avisar.
    std::string cortar(const std::string& escena, const std::string& tipo, int ms, const char* via);
    void aplicarAfv(const std::string& destino, int ms);
    void setArmadaYAvisar(const std::string& escena);
    /// `emitir` (una señal de este conmutador) en su hilo, si sigue vivo; y la foto del
    /// motor se rearma (lo que muestra la interfaz).
    void avisar(std::function<void(Conmutador&)> emitir, unsigned partesFoto);

    AppRuntime& rt_;
    Conmutadores& registro_;
    const std::string salida_;
    std::string armada_;
    double tbarPos_ = 0; ///< ver tbarPosicion
    Afv afv_;
};

/// Los conmutadores, uno por salida del motor (ver arriba), hijos suyos (se mudan con él al
/// hilo del dueño). Lo crea el área de las salidas (SalidasViewModel) y lo muda al dueño; se
/// rehace solo cuando cambian las salidas.
class Conmutadores : public QObject {
    Q_OBJECT
   public:
    explicit Conmutadores(AppRuntime& rt, QObject* parent = nullptr);
    ~Conmutadores() override;

    /// El de la por omisión (existe siempre).
    Conmutador& porOmision() { return *porOmision_; }
    /// El de una salida ("" = la por omisión); nullptr si no hay.
    Conmutador* de(const std::string& salida);
    /// Los de todas, la por omisión primera.
    std::vector<Conmutador*> todos() const;

    /// Uno por cada salida del motor: crea los que faltan y saca los que sobran.
    void sincronizar();

    // Lo que comparten todos (lo usa cada Conmutador).
    nlohmann::json transicionEntrada(const std::string& escena) const;
    std::string setTransicionEntrada(const std::string& escena, const std::string& tipo, int ms);
    /// Guarda Config/transiciones.json con las transiciones y el AFV de cada salida.
    void guardar() const;

   signals:
    /// Se creó uno (salida nueva), por su id: para conectarse a sus señales (de()). Por el id
    /// y no el puntero: a quien escucha desde otro hilo le llega encolado, y para entonces
    /// la salida puede haberse ido.
    void creado(const QString& salida);
    /// Cambió qué salidas hay (después de creado/sacar).
    void cambio();

   private:
    Conmutador* crear(const std::string& salida);

    AppRuntime& rt_;
    int observador_ = -1;
    int aporte_ = -1; ///< lo suyo en la foto del motor
    nlohmann::json transicionesEntrada_ = nlohmann::json::object();
    /// El AFV guardado de cada salida (también de las que todavía no existen: vuelve al crearlas).
    std::map<std::string, Afv> afvGuardado_;
    std::map<std::string, std::unique_ptr<Conmutador>> conmutadores_;
    Conmutador* porOmision_ = nullptr;
};

} // namespace ba::app::escenas
