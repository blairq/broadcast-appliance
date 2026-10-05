// SPDX-License-Identifier: GPL-2.0-or-later
//
// SeguirMicrofono: cuando un micrófono de banca se abre (su nivel pasa el
// umbral durante un rato), propone poner esa banca al aire: «Banca 12 –
// Pérez pidió la palabra · [Armar]». En modo automático la arma sola en el
// preview (nunca corta: el TAKE es del operador, y Bancas no mueve una cámara
// que está al aire). Mira los niveles del bus de eventos (audio/nivel/<fuente>)
// y el micrófono de cada integrante del Cuerpo (microfono.fuente). No toca el
// audio. Config/seguir-microfono.json.
#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <string>

namespace ba::app::control {
class BusEventos;
}

namespace ba::app::legislatura {

class Cuerpo;
class Bancas;

class SeguirMicrofono : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap estado READ estadoUi NOTIFY cambio)
   public:
    using json = nlohmann::json;
    SeguirMicrofono(const std::filesystem::path& datos, control::BusEventos& bus, Cuerpo& cuerpo, Bancas& bancas,
                    QObject* parent = nullptr);

    /// {activo, modo, umbral_db, sostener_ms, soltar_ms, propuesta?, abiertos[]}.
    json estado() const;
    /// Cambia lo que venga: activo, modo (proponer|automatico), umbral_db, sostener_ms, soltar_ms. "" o el error.
    std::string configurar(const json& c);
    /// Arma la banca propuesta (en el preview). "" o el error.
    std::string aceptar();
    void descartar();

    QVariantMap estadoUi() const;
    Q_INVOKABLE QString configurarUi(const QVariantMap& c);
    Q_INVOKABLE QString aceptarUi() { return QString::fromStdString(aceptar()); }
    Q_INVOKABLE void descartarUi() { descartar(); }

   signals:
    void cambio();

   private:
    void mirar();
    void guardar() const;
    std::filesystem::path archivo_;
    control::BusEventos& bus_;
    Cuerpo& cuerpo_;
    Bancas& bancas_;
    QTimer reloj_;
    bool activo_ = false;
    std::string modo_ = "proponer";
    double umbralDb_ = -30;
    int sostenerMs_ = 800;
    int soltarMs_ = 2500;
    struct Mic {
        std::int64_t arribaDesde = 0; ///< cuándo pasó el umbral (0: abajo)
        std::int64_t abajoDesde = 0;
        bool abierto = false;
    };
    std::map<std::string, Mic> mics_; ///< fuente -> estado
    json propuesta_;                  ///< {banca, nombre, fuente, desde_ms} o null
    std::string descartada_;          ///< fuente descartada (hasta que se cierre)
};

} // namespace ba::app::legislatura
