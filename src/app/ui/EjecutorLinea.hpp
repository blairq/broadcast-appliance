// ============================================================================
// src/app/ui/EjecutorLinea.hpp — el reloj y las manos de la línea, por salida
//
// La Corrida (evento/Corrida.hpp) sabe qué pasa y cuándo, pero no tiene reloj ni
// sabe hacer nada en el aire. Esto es lo que le falta, uno por salida:
//
//   - EL RELOJ: un timer exacto al próximo evento de su salida (`proximoMs`), no un
//     sondeo. Cuando llega la hora avisa (`toca`) y el que maneja la corrida hace
//     el tick (uno para todas: el resolvedor las resuelve juntas) y le vuelve a
//     pedir que se programe.
//   - LAS MANOS: lo que la corrida devuelve para esta salida («disparar la pieza
//     z7», «armar Cam 2 en el PVW») lo hace `hacer`, que pone quien sabe hacerlo
//     (EventoViewModel: el conmutador, los gráficos, la música, la sala…).
//
// Hoy hay una salida que sale de verdad (la por omisión). Con varias (PLAN-SALIDAS)
// cada ejecutor va a tener su conmutador; la estructura ya es por salida.
// ============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include <QObject>
#include <QTimer>

#include "evento/Corrida.hpp"

namespace ba::app {

class EjecutorLinea : public QObject {
    Q_OBJECT
   public:
    using Hacer = std::function<void(const evento::linea::Accion&)>;

    /// `reloj`: la hora de pared en ms (inyectable).
    EjecutorLinea(evento::linea::Corrida& corrida, std::string salida, Hacer hacer, std::function<int64_t()> reloj,
                  QObject* parent = nullptr);

    const std::string& salida() const { return salida_; }
    /// Hace una acción de esta salida.
    void hacer(const evento::linea::Accion& a) const {
        if (hacer_) hacer_(a);
    }
    /// Programa el timer para el próximo evento de la salida (o lo apaga si no hay).
    void programar();
    void parar() { timer_.stop(); }

   signals:
    /// Llegó la hora de algo de esta salida.
    void toca();

   private:
    evento::linea::Corrida& corrida_;
    std::string salida_;
    Hacer hacer_;
    std::function<int64_t()> reloj_;
    QTimer timer_;
};

} // namespace ba::app
