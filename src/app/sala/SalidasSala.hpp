// ============================================================================
// src/app/sala/SalidasSala.hpp — las salidas del aparato, del lado de la sala (modelo)
//
// La sala (B) tiene que saber qué salidas hay (docs/planes/PLAN-SALIDAS.md) para que la
// sesión remota y el cast elijan cuál mirar: [{id, nombre, color, porOmision}]. Una B
// reiniciada arranca sin lista (todo a BA.Program): se la vuelve a decir (republicador
// «salidas»). Y la sala cuenta quién mira cada una (evento «programa»): la app baja el
// retorno de una salida solo mientras alguien la mira.
//
// Es estado de dominio, como los demás de sala/*: el cliente de transporte
// (ui/ConferenceClient) no guarda nada de esto.
// ============================================================================
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>

namespace ba::app {
class ConferenceClient;
}

namespace ba::app::sala {

class SalidasSala : public QObject {
    Q_OBJECT
   public:
    explicit SalidasSala(ConferenceClient& sala, QObject* parent = nullptr);

    /// La lista para la sala; se manda si cambió (y cada vez que vuelve una B que no la sabe).
    void poner(const QJsonArray& lista);
    /// Cuántas sesiones de la sala reciben el programa; -1 = no se sabe (sin sala, o una
    /// sala que todavía no lo dijo).
    int miranPrograma() const;
    /// Lo mismo por cada salida que no es la por omisión ({id: cuántos}).
    QJsonObject miranSalidas() const;

   signals:
    /// Cambió quién mira (lo dijo la sala, o la sala se fue o volvió).
    void miranCambio();

   private:
    ConferenceClient& sala_;
    QJsonArray lista_; ///< la última (vacía = nunca se puso)
    int miranPrograma_ = -1;
    QJsonObject miranSalidas_;
};

} // namespace ba::app::sala
