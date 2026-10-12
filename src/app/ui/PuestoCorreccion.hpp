// ============================================================================
// src/app/ui/PuestoCorreccion.hpp — el puesto web del corrector de subtítulos (T44)
//
// Une la cola de corrección (agent::FlujosTexto: las frases que esperan unos
// segundos antes de salir) con la página /correccion/ que sirve el gateway:
//   - cada vez que cambian las pendientes, se las pasa a la sala (modelo
//     sala::CorreccionSala); la página las pide cada 500 ms;
//   - lo que el corrector aprueba, corrige o descarta llega por el SSE
//     (evento «correccion») y se aplica con FlujosTexto::corregir;
//   - el enlace con su clave (por arranque) y el QR, para QrDialog (lo da el
//     modelo sala::EnlacesSala, el mismo que usa el MCP).
// ============================================================================
#pragma once

#include <QObject>
#include <QStringList>
#include <QTimer>

#include "sala/CorreccionSala.hpp"
#include "sala/EnlacesSala.hpp"

namespace ba::app {
class ConferenceClient;
namespace agent {
class FlujosTexto;
}

class PuestoCorreccion : public QObject {
    Q_OBJECT
   public:
    PuestoCorreccion(ConferenceClient& sala, agent::FlujosTexto& flujos, QObject* parent = nullptr);
    /// Para QrDialog: el enlace (siempre con la clave: sin ella no corrige).
    Q_INVOKABLE void pedirQr(const QString& target, const QString& vista, bool conClave);

   signals:
    void qrListo(const QString& url, const QStringList& filas, const QString& mensaje, const QString& clave);

   private:
    void sincronizar();
    ConferenceClient& sala_;
    agent::FlujosTexto& flujos_;
    QTimer demora_;
    sala::EnlacesSala enlaces_;
    sala::CorreccionSala cola_;
};

} // namespace ba::app
