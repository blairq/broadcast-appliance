// ============================================================================
// src/app/ui/LugaresCambiados.hpp — qué lugares de invitado cambiaron de nombre
//
// Los nombres por lugar (ResumenSala::slotNames, índice = lugar - 1) llegan enteros con la
// parte Participantes de la sala; los rótulos de los cuadros (FrameService::rotulos) se
// tocan SOLO en los lugares que cambiaron. Pura: se prueba sola
// (tests/test_trabajo_por_foto.cpp).
// ============================================================================
#pragma once

#include <QString>
#include <QStringList>

#include <algorithm>
#include <vector>

namespace ba::app {

/// Los lugares (desde 1) cuyo nombre es distinto en `ahora` que en `antes` (uno que falta
/// cuenta como vacío).
inline std::vector<int> lugaresCambiados(const QStringList& antes, const QStringList& ahora) {
    std::vector<int> out;
    const qsizetype n = std::max(antes.size(), ahora.size());
    for (qsizetype i = 0; i < n; ++i) {
        const QString a = i < antes.size() ? antes.at(i) : QString();
        const QString b = i < ahora.size() ? ahora.at(i) : QString();
        if (a != b) out.push_back(static_cast<int>(i + 1));
    }
    return out;
}

} // namespace ba::app
