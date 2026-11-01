// ============================================================================
// src/app/sala/OradoresSala.hpp — el modo oradores de la sala (modelo)
//
// La fila de oradores con reloj: la configuración (tiempo general, uno o
// varios al aire, lugar, carrusel, margen) y lo que se le hace a cada uno (al
// aire, a la fila, moverlo, su tiempo, presentador). La GUI (la página Sala, la
// tarjeta de oradores, el cue «orador» del guion) y el MCP (speakers_*,
// speaker_*) pasan por acá, con las mismas reglas: un tiempo tiene que ser de
// más de cero segundos; el lugar en la fila empieza en 0.
//
// Un tiempo escrito ("2:30", "3", "45s", "3m", "+2:00", "-30s", "=2:30") se lee acá
// (leerTiempo), igual para la interfaz y el MCP: un número escrito solo son MINUTOS (lo
// que escribe el operador). Un ENTERO JSON (lo que manda un programa: el MCP, el guion)
// son SEGUNDOS, como `graceSec` y como lo esperan los modelos (leerTiempoJson). Y la
// regla del reloj de quien habla (reloj: los últimos 10 s avisan, pasado cero se
// excede) y la fila en orden (fila) también: la página, la tarjeta, el reloj flotante y
// remotos_fila_ver muestran lo mismo.
// ============================================================================
#pragma once

#include <string>

#include <nlohmann/json.hpp>
#include <QJsonObject>
#include <QObject>

#include "sala/Pedido.hpp"

namespace ba::app::sala {

class OradoresSala {
   public:
    explicit OradoresSala(ConferenceClient& sala);

    /// Cómo va el reloj de quien está al aire.
    struct Reloj {
        double restanteMs = 0;    ///< lo que le queda (negativo: se pasó)
        std::string estado;       ///< "ok" | "aviso" (los últimos 10 s) | "excedido"
        std::string texto;        ///< "m:ss" (redondeado para arriba) o "+m:ss" si se pasó
    };
    static Reloj reloj(int asignadoS, double alAireDesdeMs, double ahoraMs);
    /// "m:ss" de `ms` ("+m:ss" si es negativo).
    static std::string textoTiempo(double ms);
    /// Lee un tiempo escrito: "2:30", "3" (minutos), "1,5" (minutos), "45s", "3m"; "+2:00" /
    /// "-30s" suman o restan a `actual`; "=2:30" lo deja en eso. Un número solo son MINUTOS
    /// (lo que escribe el operador). "" o el error (ya traducido); en `segundos`, el
    /// resultado (más de cero).
    static std::string leerTiempo(const std::string& texto, int actual, int& segundos);
    /// Un tiempo como viene en un JSON: un número son SEGUNDOS (120 = dos minutos); un
    /// texto, leerTiempo (sobre `actual`: «3» = tres minutos, «+2», «-30s»). "" o el error.
    static std::string leerTiempoJson(const nlohmann::json& valor, int actual, int& segundos);
    /// Sobre qué se suma o resta un tiempo de `id` («+2»): el suyo (allottedSec) o, si no
    /// tiene (o `id` vacío), el general (speakers.defaultSec) de la foto de la sala.
    static int tiempoActual(const QJsonObject& foto, const std::string& id);
    /// La fila de una foto de la sala, en orden: cada participante tal cual, con
    /// `position` (desde 1) si espera su turno.
    static nlohmann::json fila(const nlohmann::json& foto);

    /// Cambia los campos presentes: enabled, defaultSec, multi, defaultSlot,
    /// carousel (manual|chain|time), graceSec. Devuelve el error inmediato o "".
    std::string configurar(const nlohmann::json& cambios, Listo listo = {});
    /// Al aire el siguiente de la fila (con «uno a la vez», el que estaba sale).
    void siguiente(Listo listo = {});
    void alAire(const std::string& id, bool on, Listo listo = {});
    /// Lugar en la fila (0 = primero).
    void mover(const std::string& id, int lugar, Listo listo = {});
    /// Segundos para hablar (más de cero). Devuelve el error inmediato o "".
    std::string tiempo(const std::string& id, int segundos, Listo listo = {});
    /// El tiempo como lo pide alguien (leerTiempoJson, sobre tiempoActual): la página
    /// manda el texto que escribió el operador; el MCP, un texto o un número de segundos.
    /// Devuelve el error inmediato o ""; en `segundos` (si no es nulo), lo que quedó.
    std::string tiempo(const std::string& id, const nlohmann::json& valor, Listo listo = {}, int* segundos = nullptr);
    /// El tiempo general por orador, con la misma lectura (sobre el general de ahora).
    std::string tiempoGeneral(const nlohmann::json& valor, Listo listo = {}, int* segundos = nullptr);
    /// A la fila de oradores (o de vuelta a ella si ya habló).
    void aLaFila(const std::string& id, Listo listo = {});
    /// Presentador fijo: se queda al aire mientras rotan los oradores.
    void presentador(const std::string& id, bool on, Listo listo = {});

   private:
    ConferenceClient& sala_;
    nlohmann::json pedido_ = nlohmann::json::object(); ///< la configuración pedida (se republica)
    QObject vida_; ///< mientras vive, la sala le pide que republique
};

} // namespace ba::app::sala
