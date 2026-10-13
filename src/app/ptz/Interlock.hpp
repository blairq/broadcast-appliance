// src/app/ptz/Interlock.hpp — la regla del interlock de PTZ, sola
//
// La cámara que está AL AIRE no se mueve sin que alguien lo confirme: el
// movimiento se vería en la transmisión. La regla es una sola para todos los
// que mueven cámaras (la página «Cámaras PTZ», la tarjeta PTZ, el editor de
// caras, el MCP); ControlCamaras::chequearMovimiento la aplica con el estado
// de verdad (si está al aire y el ajuste de Config/ptz-interlock.json). Acá
// queda pura, para poder probarla sin la app.
#pragma once

#include <QString>

namespace ba::app::ptz {

/// El ajuste del interlock (Config/ptz-interlock.json).
struct AjusteInterlock {
    bool activo = true;               ///< de fábrica, prendido
    bool seguirSuavePermitido = true; ///< el seguimiento suave pasa igual
};

/// Qué clase de movimiento se pide: uno común, o el seguimiento suave de una
/// persona (que se mueve de a poco y puede quedar permitido aunque esté al aire).
inline constexpr const char* kMovimientoSeguirSuave = "seguir_suave";

/// "" si la cámara de `entrada` se puede mover ya; si no, el porqué (hace
/// falta que alguien confirme). `forzar`: quien pide ya confirmó (o lo va a
/// confirmar el operador, como el MCP con la confirmación de aire). `modo`:
/// "" o kMovimientoSeguirSuave.
inline QString motivoInterlock(const QString& entrada, bool alAire, const AjusteInterlock& ajuste, bool forzar,
                               const QString& modo) {
    if (entrada.isEmpty() || !alAire || !ajuste.activo || forzar) return {};
    if (modo == QLatin1String(kMovimientoSeguirSuave) && ajuste.seguirSuavePermitido) return {};
    return QStringLiteral("La cámara «%1» está AL AIRE: moverla se ve en la transmisión.").arg(entrada);
}

} // namespace ba::app::ptz
