// ============================================================================
// src/grafico/Declarativo.hpp — gráficos descritos, no programados
//
// El nivel 1 de los gráficos: en vez de escribir QML, se DESCRIBE el gráfico
// con capas y animaciones de un repertorio fijo, y acá se traduce a QML.
//
// POR QUÉ. Un zócalo, una placa de título y un ticker son la misma media
// docena de primitivas combinadas distinto. Describirlas tiene tres ventajas
// sobre programarlas:
//
//   * No corre código de nadie: no hay nada que sanear.
//   * Es determinista: la misma descripción da siempre el mismo resultado.
//   * Un asistente es mucho más confiable llenando un esquema que escribiendo
//     animaciones — y sobre todo, no puede caer en los errores clásicos (mover
//     el origen de un efecto en vez del efecto, anclar algo que va a
//     deslizarse), porque el QML lo generamos nosotros.
//
// Cuando el repertorio no alcanza, se escribe QML a mano (nivel 2). La regla
// para que esto no crezca sin control: si un gráfico de nivel 2 se repite tres
// veces, su patrón se promueve acá.
//
// LA DESCRIPCIÓN:
//
//   {
//     "ancho": 900, "alto": 220,
//     "capas": [
//       {"tipo":"barra",  "ancho":12, "alto":120, "color":"{acento}",
//        "abajo":30, "izquierda":0},
//       {"tipo":"panel",  "izquierda":12, "abajo":30, "alto":120,
//        "color":"#12151a", "opacidad":0.93, "ajustaA":["nombre","cargo"]},
//       {"tipo":"texto",  "id":"nombre", "valor":"{nombre}", "tamano":44,
//        "peso":"semi", "color":"white", "izquierda":40, "abajo":86,
//        "aparece":200},
//       {"tipo":"texto",  "id":"cargo", "valor":"{cargo}", "tamano":24,
//        "color":"#b9c0c6", "izquierda":40, "abajo":46, "aparece":320}
//     ],
//     "entrada": {"efecto":"desliza-izquierda", "duracion":450},
//     "salida":  {"efecto":"desenfoca", "duracion":320}
//   }
//
// `{loQueSea}` en un texto o un color se reemplaza por el parámetro de ese
// nombre.
// ============================================================================
#pragma once

#include <QJsonObject>
#include <QString>

namespace ba::grafico {

/// El QML que sale de una descripción.
struct Traduccion {
    QString qml;
    QString error; ///< vacío = salió bien
};

/// Traduce una descripción declarativa a QML.
///
/// :param d: la descripción (el objeto "declarativo" de grafico.json).
/// :param parametros: los valores con los que reemplazar los `{nombre}`.
Traduccion traducir(const QJsonObject& d, const QJsonObject& parametros);

/// Los tipos de capa y los efectos que se pueden usar, para la ayuda y para
/// que la interfaz arme el menú sin tener la lista escrita aparte.
QJsonObject repertorio();

} // namespace ba::grafico
