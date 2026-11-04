// src/app/ui/RelojNiveles.cpp — ver el .hpp

#include "ui/RelojNiveles.hpp"

#include "emision/NivelesMotor.hpp"

namespace ba::app {

RelojNiveles::RelojNiveles(NivelesMotor& canal, int periodoMs, QObject* parent) : QObject(parent), canal_(canal) {
    reloj_.setInterval(periodoMs);
    connect(&reloj_, &QTimer::timeout, this, &RelojNiveles::vuelta);
}

RelojNiveles::~RelojNiveles() {
    canal_.despertarAlSonar(nullptr); // espera al aviso en curso: después nadie llama
}

void RelojNiveles::revisar() {
    if (usos_.alguien()) {
        if (!reloj_.isActive() && !durmiendo_) vuelta(); // la que empieza a mirar, ya
        return;
    }
    // Nadie mira: ni reloj ni despertador.
    durmiendo_ = false;
    canal_.despertarAlSonar(nullptr);
    if (reloj_.isActive()) {
        reloj_.stop();
        emit andandoCambio();
    }
}

void RelojNiveles::vuelta() {
    ++vueltas_;
    bool movio = false;
    for (const auto& m : medidores_) movio = m() || movio;
    if (movio || canal_.haySonido()) {
        if (!reloj_.isActive()) {
            reloj_.start();
            emit andandoCambio();
        }
        return;
    }
    dormir();
}

void RelojNiveles::dormir() {
    // Todo quieto: a dormir hasta que algo suene (lo despierta el hilo que lo publica).
    const bool estaba = reloj_.isActive();
    reloj_.stop();
    durmiendo_ = true;
    canal_.despertarAlSonar([this]() { QMetaObject::invokeMethod(this, &RelojNiveles::despertar, Qt::QueuedConnection); });
    if (estaba) emit andandoCambio();
}

void RelojNiveles::despertar() {
    if (!durmiendo_) return;
    durmiendo_ = false;
    if (usos_.alguien() && !reloj_.isActive()) vuelta();
}

} // namespace ba::app

#include "moc_RelojNiveles.cpp"
