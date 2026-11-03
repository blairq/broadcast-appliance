// ============================================================================
// src/app/ui/ProveedorCuadros.hpp — De dónde saca VideoItem sus cuadros
//
// Lo único que la vista de video necesita del servicio de cuadros: «la última
// imagen de <clave> si es más nueva que la que tengo» y un aviso cuando llegan
// cuadros nuevos. FrameService es el de verdad; el banco de la sala
// (tests/bench_sala_conferencia.cpp) usa uno falso, sin motor ni canales.
// ============================================================================

#pragma once

#include <QImage>
#include <QObject>
#include <QString>

#include <cstdint>

namespace ba::app {

class ProveedorCuadros : public QObject {
    Q_OBJECT
   public:
    using QObject::QObject;

    /// La última imagen de `clave` si es más nueva que `serial` (y lo actualiza).
    /// `intervaloUs`: cada cuánto, como mucho, quiere cuadros quien lee (0: todos): el
    /// servicio puede leer el canal más espaciado si nadie quiere más (celdas chicas).
    virtual bool ultimo(const QString& clave, QImage& out, std::uint64_t& serial, std::int64_t intervaloUs) const = 0;

   signals:
    /// Llegó al menos un cuadro nuevo (de cualquier clave): una vez por vuelta, no por cuadro.
    void framesUpdated();
};

} // namespace ba::app
