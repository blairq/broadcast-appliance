// src/app/agent/VocesRack.hpp — lo que dicen las tarjetas «Texto a voz» (Studio.voces)
//
// Cada tarjeta «Texto a voz» de un rack (AudioRack, tipo "habla") tiene su
// voz (un modelo piper: idioma, acento, hombre o mujer), su hablante y su
// velocidad, y puede LEER UNA SALIDA DE TEXTO: lo que transcribe una «Voz a
// texto» (o, más adelante, una traducción) lo dice con esa voz, y suena por
// el puerto de la tarjeta, que se patchea como cualquier otro audio (a la
// mezcla, a una pista, a la salida de los auriculares, a otra voz a texto…).
// También dice lo que se le pide (la interfaz, MCP).
//
// Cada voz se carga una sola vez aunque la usen varias tarjetas. Lo que
// sintetiza (22,05 kHz mono) se pasa a 48 kHz estéreo y va al anillo de la
// tarjeta con su hora: las frases quedan seguidas, sin cortes.
//
// Voces: las «tts» del catálogo de modelos (datos/modelos-catalogo.json),
// con su idioma; las que no están se bajan desde acá (ModelosLocales).

#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>

namespace ba::app::audio {
class RackManager;
class Habla;
} // namespace ba::app::audio

namespace ba::app::agent {

class ModelosLocales;
class FlujosTexto;

class VocesRack : public QObject {
    Q_OBJECT
    /// Las voces del catálogo: [{repo, nombre, idioma, idiomaNombre, acento,
    /// genero, instalada}]
    Q_PROPERTY(QVariantList voces READ voces NOTIFY cambio)
    /// Los idiomas que hay: [{codigo, nombre}]
    Q_PROPERTY(QVariantList idiomas READ idiomas NOTIFY cambio)

   public:
    explicit VocesRack(ModelosLocales& modelos, QObject* parent = nullptr);
    ~VocesRack() override;

    void setRacks(audio::RackManager* racks) { racks_ = racks; }
    void setFlujos(FlujosTexto* flujos);
    /// La voz del sistema. Una tarjeta sin voz elegida usa la del idioma del evento: esta
    /// si habla esa lengua; si no, la del perfil del idioma (agent/VozIdioma).
    void setVozSistema(std::function<QString()> f) { vozSistema_ = std::move(f); }

    QVariantList voces() const;
    QVariantList idiomas() const;

    /// Lo dice la tarjeta `nodo` del rack de `fuente`. "" o el error.
    Q_INVOKABLE QString decir(const QString& fuente, const QString& nodo, const QString& texto);
    /// Baja una voz del catálogo.
    Q_INVOKABLE void bajar(const QString& repo);
    /// Cómo está la tarjeta: «lista», «cargando…», «falta bajar la voz»…
    Q_INVOKABLE QString estado(const QString& fuente, const QString& nodo) const;

    static QString nombreIdioma(const QString& codigo);

   signals:
    void cambio();

   private:
    struct Cargada;
    /// La voz de una tarjeta sin voz elegida (se resuelve en cada frase: sigue al idioma del evento).
    QString vozDeFabrica() const;
    /// La voz cargada (la carga si hace falta). Nulo si no está instalada.
    Cargada* voz(const QString& repo);
    void decirCon(const nlohmann::json& tarjeta, const QString& texto);
    void alFrase(const QString& flujo, const QString& texto);

    ModelosLocales& modelos_;
    audio::RackManager* racks_ = nullptr;
    FlujosTexto* flujos_ = nullptr;
    std::function<QString()> vozSistema_;
    std::map<QString, std::unique_ptr<Cargada>> cargadas_;
    /// Por tarjeta: hasta cuándo alcanza lo que ya se le mandó (para seguir pegado).
    std::mutex proximoLock_;
    std::map<std::string, std::uint64_t> proximo_;
};

} // namespace ba::app::agent
