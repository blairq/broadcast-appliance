// ============================================================================
// src/app/ui/Flanco.hpp — hacer algo cuando un estado PASA a sí (el flanco de subida)
//
// Para lo que tiene que pasar UNA vez al empezar algo (el PIN del día al arrancar el
// vivo, T47): se conecta, tipado, al aviso del modelo y se mira el estado en cada aviso.
// Nada de leer la propiedad por su nombre ni de mirarla con un reloj: el modelo avisa.
// ============================================================================
#pragma once

#include <memory>
#include <utility>

#include <QObject>

namespace ba::app {

/// Cada vez que `emisor` emite `senal`, lee `estado()`; si pasó de false a true, `accion()`.
/// El estado de partida es el de ahora (si ya es true, no cuenta como flanco).
template <class Emisor, class Senal, class Estado, class Accion>
QMetaObject::Connection alSubir(Emisor* emisor, Senal senal, QObject* contexto, Estado estado, Accion accion) {
    auto antes = std::make_shared<bool>(estado());
    return QObject::connect(emisor, senal, contexto, [antes, estado = std::move(estado), accion = std::move(accion)]() {
        const bool ahora = estado();
        const bool subio = ahora && !*antes;
        *antes = ahora;
        if (subio) accion();
    });
}

} // namespace ba::app
