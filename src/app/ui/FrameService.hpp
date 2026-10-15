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
//
// Modelo de threading: todo en el thread de la UI; `poll()` lo llama un timer.
// ============================================================================

#pragma once

#include <QImage>
#include <QObject>
#include <QStringList>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ba::ipc {
class SpoutReceiver;
}

namespace ba::app {

class BroadcastEngine;

class FrameService : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList keys READ keys NOTIFY keysChanged)
   public:
    FrameService(BroadcastEngine& engine, int guests, QObject* parent = nullptr);
    ~FrameService() override;

    /// Claves de video disponibles ("program", "guest:1"...).
    QStringList keys() const;

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
    /// Cambia la cantidad de invitados.
    void setGuests(int guests);
    /// Nombres de los invitados por slot (indice slot-1) para las etiquetas.
    void setGuestNames(const QStringList& names);
    void setLobbySlots(int n) { lobbySlots_ = n; }

   public slots:
    /// Trae frames nuevos (lo llama el timer de la UI, ~30 Hz).
    void poll();

   signals:
    void keysChanged();
    void framesUpdated();

   private:
    struct Slot {
        QImage image;
        std::uint64_t serial = 0;
        std::uint64_t serieMotor = 0; ///< el último cuadro del motor que se tomó
        std::unique_ptr<ba::ipc::SpoutReceiver> receiver; // invitados
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
    std::uint64_t vueltas_ = 0;
};

} // namespace ba::app
