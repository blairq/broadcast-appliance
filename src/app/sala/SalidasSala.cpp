// src/app/sala/SalidasSala.cpp — las salidas del aparato, del lado de la sala (ver el .hpp)

#include "sala/SalidasSala.hpp"

#include "ui/ConferenceClient.hpp"

namespace ba::app::sala {

SalidasSala::SalidasSala(ConferenceClient& sala, QObject* parent) : QObject(parent), sala_(sala) {
    sala_.registrarRepublicador("salidas", this, [this]() {
        if (!lista_.isEmpty()) sala_.post("/api/v1/salidas", QJsonObject{{"lista", lista_}});
    });
    // Quién mira cada salida (evento de estado: una sala que vuelve lo dice de nuevo).
    connect(&sala_, &ConferenceClient::programaEvento, this, [this](const QJsonObject& o) {
        miranPrograma_ = o.value("mirando").toInt(-1);
        miranSalidas_ = o.value("salidas").toObject();
        emit miranCambio();
    });
    connect(&sala_, &ConferenceClient::onlineChanged, this, &SalidasSala::miranCambio);
}

void SalidasSala::poner(const QJsonArray& lista) {
    if (lista == lista_) return;
    lista_ = lista;
    if (sala_.online()) sala_.post("/api/v1/salidas", QJsonObject{{"lista", lista_}});
}

int SalidasSala::miranPrograma() const { return sala_.online() ? miranPrograma_ : -1; }

QJsonObject SalidasSala::miranSalidas() const { return sala_.online() ? miranSalidas_ : QJsonObject(); }

} // namespace ba::app::sala

#include "moc_SalidasSala.cpp"
