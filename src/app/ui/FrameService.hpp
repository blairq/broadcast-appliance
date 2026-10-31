// ============================================================================
// src/app/ui/FrameService.hpp — Frames de video para la interfaz (servicio)
//
// Capa de SERVICIOS (ni modelo ni vista): consigue imagenes de las fuentes que
// la interfaz puede mostrar y las entrega como QImage con un numero de serie.
// La vista (VideoItem) solo pide "la ultima imagen de <clave>".
//
// Claves:
//   "program"   programa al aire (vista previa del motor: libobs ya la escala
//               en la GPU)
//   "guest:N"   video del invitado N tal como lo publica B (BA.Guest.N);
//               se lee directo del transporte entre procesos, sin libobs
//   "entrada:X" la entrada de video X tal como sale de sus filtros (con el
//               recorte del PTZ digital), mientras alguien la pida con
//               mirarEntrada (la tarjeta PTZ suelta)
//   "entrada-entera:X" la misma entrada ANTES del recorte del PTZ digital (la
//               toma entera, para manejar el recorte encima): mirarEntrada
//               con `entera`
//   "escena:X"  una escena cualquiera, esté o no al aire, mientras alguien la
//               pida con mirarEscena (un monitor o una ventana que la muestra)
//   "program:<salida>" / "armada:<salida>"  el programa y el preview de una
//               salida (docs/planes/PLAN-SALIDAS.md): los de la por omisión son
//               "program" y "armada" de siempre (las dos claves valen lo mismo);
//               los de las demás se le piden al motor solos mientras alguien los
//               lea (latest) y se sueltan cuando nadie los mira.
//
// Modelo de threading: todo en el thread de la UI; `poll()` lo llama un timer.
// ============================================================================

#pragma once

#include <QImage>
#include <QJsonArray>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ipc/CanalesRegistro.hpp"

namespace ba::ipc {
class SpoutReceiver;
}

namespace ba::app {

class BroadcastEngine;

class FrameService : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList keys READ keys NOTIFY keysChanged)
    /// El color de cada salida por la clave de su programa ({"program": …, "program:<id>": …}),
    /// para el marco de su cuadro en el multiview. Vacío con una sola salida (nada que distinguir).
    Q_PROPERTY(QVariantMap colores READ colores NOTIFY keysChanged)
    /// Las claves del programa de cada salida, en su orden ("program" primero): el
    /// multiview con todas de un toque.
    Q_PROPERTY(QStringList programas READ programas NOTIFY keysChanged)
   public:
    FrameService(BroadcastEngine& engine, int guests, QObject* parent = nullptr);
    ~FrameService() override;

    /// Claves de video disponibles ("program", "guest:1"...).
    QStringList keys() const;
    /// Las cámaras de los tótems de registro, "registro:1".."registro:<n>" (el tope compartido con el gateway).
    static constexpr int kRegistroSlots = ba::ipc::kCanalesRegistro;

    /// Nombre legible de una clave ("Programa", "Invitado 1").
    Q_INVOKABLE QString label(const QString& key) const;

    /// Ultima imagen de `key` si es mas nueva que `serial` (y lo actualiza).
    bool latest(const QString& key, QImage& out, std::uint64_t& serial) const;

    /// Que escena mirar en la clave "armada" ("" = ninguna). La pone
    /// StudioViewModel cada vez que cambia el bus de preview.
    void setArmada(const QString& escena);
    /// Pide (o suelta) el video en vivo de una entrada en la clave
    /// "entrada:<nombre>". Cuenta pedidos: se renderiza mientras alguien la mire.
    /// `entera`: la toma antes del recorte del PTZ digital (clave "entrada-entera:X").
    Q_INVOKABLE void mirarEntrada(const QString& entrada, bool mirar, bool entera = false);
    /// Igual para una escena cualquiera (esté o no al aire), en la clave
    /// "escena:<nombre>": la usan los monitores y las ventanas del catálogo.
    Q_INVOKABLE void mirarEscena(const QString& escena, bool mirar);
    /// Cambia la cantidad de invitados.
    void setGuests(int guests);
    /// Nombres de los invitados por slot (indice slot-1) para las etiquetas.
    void setGuestNames(const QStringList& names);
    void setLobbySlots(int n) { lobbySlots_ = n; }
    /// Las salidas, la por omisión primera: [{id, nombre, color, porOmision}] (la lista
    /// de StudioViewModel::salidas). Para las claves, las etiquetas y los colores.
    void setSalidas(const QJsonArray& salidas);
    QVariantMap colores() const { return colores_; }
    QStringList programas() const;

   public slots:
    /// Trae frames nuevos (lo llama el timer de la UI, ~30 Hz).
    void poll();

   signals:
    void keysChanged();
    void framesUpdated();

   private:
    /// Lo común de mirarEntrada y mirarEscena: pedir o soltar un canal del motor.
    void mirarClave(const QString& clave, bool mirar, int ancho);
    struct Slot {
        QImage image;
        std::uint64_t serial = 0;
        std::uint64_t serieMotor = 0; ///< el último cuadro del motor que se tomó
        std::unique_ptr<ba::ipc::SpoutReceiver> receiver; // invitados
        mutable std::int64_t pedidoUs = 0; ///< la última vez que alguien lo leyó (latest)
    };
    BroadcastEngine& engine_;
    int guests_ = 0;
    static std::wstring channelOf(const QString& key);
    std::map<QString, Slot> slots_;
    QStringList names_;
    int lobbySlots_ = 4;
    static constexpr int kBackstageSlots = 8;
    QString armada_;
    /// Las vistas de entradas pedidas ("entrada:X" o "entrada-entera:X") y cuántos las miran.
    std::map<QString, int> entradas_;
    /// "program:<id>" → "program" si `id` es la por omisión (y lo mismo con "armada").
    static QString clavePropia(const QString& key);
    /// Las vistas de salidas que alguien leyó (latest) y cuándo: poll las pide y las suelta.
    mutable std::map<QString, std::int64_t> salidasPedidas_;
    QJsonArray salidas_;
    QVariantMap colores_;
    std::uint64_t vueltas_ = 0;
};

} // namespace ba::app
