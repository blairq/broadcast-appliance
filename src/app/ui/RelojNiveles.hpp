// ============================================================================
// src/app/ui/RelojNiveles.hpp — el ritmo de los vúmetros en la interfaz, a pedido
//
// Los niveles se mueven todo el tiempo y van por su canal (emision/NivelesMotor), no
// por la foto del motor. La interfaz los lee a 10 Hz SOLO mientras:
//
//   - alguien los usa: una vista que muestra vúmetros (`Suscripcion { objetivo:
//     Studio.niveles; activa: visible }`) o el bus de eventos con alguien que pidió
//     los niveles (StudioViewModel lo anota como un uso más), y
//   - algo se mueve: en silencio (todo por debajo de NivelesMotor::kSilencioDb y los
//     vúmetros ya caídos), el reloj se para y arma el despertador del canal; lo
//     despierta el hilo que publique algo que suene. Con la app quieta, la interfaz
//     no se despierta por los vúmetros.
//
// Cada vuelta corre lo que mide (`medir`, en orden): cada uno dice si lo que muestra se
// movió (un vúmetro que todavía cae cuenta).
// ============================================================================
#pragma once

#include "comun/UsoPorVistas.hpp"

#include <QObject>
#include <QTimer>

#include <functional>
#include <vector>

namespace ba::app {

class NivelesMotor;

class RelojNiveles : public QObject {
    Q_OBJECT
    /// Si está dando vueltas ahora (alguien mira y algo se mueve).
    Q_PROPERTY(bool andando READ andando NOTIFY andandoCambio)
   public:
    explicit RelojNiveles(NivelesMotor& canal, int periodoMs = 100, QObject* parent = nullptr);
    ~RelojNiveles() override;

    /// Lo que corre en cada vuelta; devuelve si lo que muestra se movió.
    void medir(std::function<bool()> fn) { medidores_.push_back(std::move(fn)); }
    /// Una vista (o el bus) empieza o deja de usar los niveles (comun/UsoPorVistas).
    Q_INVOKABLE void usar(QObject* vista, bool si, const QString& que = {}) { usos_.usar(vista, si, que); }
    bool andando() const { return reloj_.isActive(); }
    /// Cuántas vueltas dio (las pruebas, el diagnóstico).
    quint64 vueltas() const { return vueltas_; }

   signals:
    void andandoCambio();

   private:
    void revisar();
    void vuelta();
    void dormir();
    void despertar();

    NivelesMotor& canal_;
    std::vector<std::function<bool()>> medidores_;
    QTimer reloj_;
    UsoPorVistas usos_{this, [this](const QString&) { revisar(); }};
    quint64 vueltas_ = 0;
    bool durmiendo_ = false;
};

} // namespace ba::app
