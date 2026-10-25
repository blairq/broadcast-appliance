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
//
// Además de barra, panel y texto hay tres capas que no son formas:
//
//   {"tipo":"medio", "archivo":"medios/bandera.gif", "ancho":200, "alto":120,
//    "inicio":300, "bucle":true}
//      Un GIF, una imagen o una secuencia de cuadros (un video importado con
//      su alfa). Su tiempo es el del gráfico: arranca en `inicio` ms.
//   {"tipo":"torta",  "datos":"{votos}", "ancho":420, "alto":420, "grosor":0}
//   {"tipo":"barras", "datos":"{votos}", "ancho":900, "alto":360,
//    "orientacion":"vertical"}
//      Gráficos de datos: `datos` es una lista [{etiqueta, valor, color}] (o el
//      parámetro que la trae). Entran animados y, cuando se les cambian los
//      valores al aire, se mueven de los de antes a los nuevos.
// ============================================================================
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace ba::grafico {

/// El QML que sale de una descripción.
struct Traduccion {
    QString qml;
    QString error; ///< vacío = salió bien
};

/// Cómo se dibuja: de cero (la entrada) o como un CAMBIO de valores con el
/// gráfico ya en pantalla.
struct Modo {
    /// Ya está en pantalla: sin la animación de entrada ni los fundidos de
    /// `aparece` (sería «volver a entrar» cada vez que cambia un número).
    bool sinEntrada = false;
    /// Los valores de antes del cambio: las tortas y las barras arrancan ahí y
    /// se mueven a los nuevos. Vacío = no hay «antes».
    QJsonObject anteriores;
};

/// Traduce una descripción declarativa a QML.
///
/// :param d: la descripción (el objeto "declarativo" de grafico.json).
/// :param parametros: los valores con los que reemplazar los `{nombre}`.
Traduccion traducir(const QJsonObject& d, const QJsonObject& parametros, const Modo& modo = {});

/// Los tipos de capa y los efectos que se pueden usar, para la ayuda y para
/// que la interfaz arme el menú sin tener la lista escrita aparte.
QJsonObject repertorio();

/// Los datos de una torta o unas barras, como sea que vengan, en la forma
/// única [{etiqueta, valor, color?}]. Acepta esa misma lista, un objeto
/// {"Sí": 10, "No": 4}, pares [["Sí", 10], …], números sueltos, el JSON de
/// cualquiera de esas cosas en un texto, o el texto que escribe una persona en
/// un campo: «Afirmativos: 34; Negativos: 12 #c62828; Abstenciones: 3» (separa
/// el punto y coma o el renglón; el color, opcional, al final). Lo usan las
/// capas declarativas y los componentes Torta y Barras (ver Componentes.hpp):
/// UNA sola lectura, así que un campo de texto de Rótulos, el JSON que manda el
/// asistente y un QML escrito a mano dan lo mismo.
QJsonArray normalizarDatos(const QJsonValue& v);

} // namespace ba::grafico
