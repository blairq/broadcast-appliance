// ============================================================================
// src/grafico/Declarativo.cpp — ver Declarativo.hpp
// ============================================================================
#include "Declarativo.hpp"

#include <QJsonArray>
#include <QJsonValue>
#include <algorithm>

#include <QRegularExpression>

namespace ba::grafico {
namespace {

/// Un texto entre comillas de QML, sin posibilidad de escaparse.
QString entreComillas(const QString& s) {
    QString e = s;
    e.replace('\\', "\\\\").replace('"', "\\\"").replace('\n', "\\n");
    return '"' + e + '"';
}

/// Reemplaza `{nombre}` por el parámetro correspondiente. Un parámetro que no
/// existe se deja vacío en vez de dejar las llaves a la vista: en vivo, un
/// zócalo que dice "{cargo}" es peor que uno sin cargo.
QString conParametros(const QString& plantilla, const QJsonObject& parametros) {
    static const QRegularExpression re(R"(\{([A-Za-z_][A-Za-z0-9_]*)\})");
    QString out;
    qsizetype desde = 0;
    auto it = re.globalMatch(plantilla);
    while (it.hasNext()) {
        const auto m = it.next();
        out += plantilla.mid(desde, m.capturedStart() - desde);
        out += parametros.value(m.captured(1)).toVariant().toString();
        desde = m.capturedEnd();
    }
    out += plantilla.mid(desde);
    return out;
}

/// Un número del JSON, con valor por defecto.
double num(const QJsonObject& o, const char* clave, double porDefecto) {
    return o.contains(clave) ? o.value(clave).toDouble(porDefecto) : porDefecto;
}

QString pesoDeTexto(const QString& p) {
    if (p == "semi") return "Font.DemiBold";
    if (p == "negrita") return "Font.Bold";
    if (p == "liviana") return "Font.Light";
    return "Font.Normal";
}

/// La familia tipográfica viaja con el bundle; el respaldo es por si falta.
constexpr const char* kFuente = R"("Inter, Noto Sans, DejaVu Sans, Sans")";

/// Ancla una capa por los bordes que declare. Se aceptan `izquierda`,
/// `derecha`, `arriba` y `abajo`, en píxeles desde ese borde.
QString posicion(const QJsonObject& c) {
    QString s;
    if (c.contains("izquierda"))
        s += QString("        x: %1\n").arg(num(c, "izquierda", 0));
    else if (c.contains("derecha"))
        s += QString("        x: raiz.width - width - %1\n").arg(num(c, "derecha", 0));
    if (c.contains("arriba"))
        s += QString("        y: %1\n").arg(num(c, "arriba", 0));
    else if (c.contains("abajo"))
        s += QString("        y: raiz.height - height - %1\n").arg(num(c, "abajo", 0));
    return s;
}

/// El fundido de entrada de una capa (`aparece`: ms desde el principio).
QString apariciónDeCapa(const QJsonObject& c, const QString& id) {
    if (!c.contains("aparece")) return {};
    const double t = num(c, "aparece", 0);
    return QString(R"(    SequentialAnimation {
        running: true
        PauseAnimation { duration: %1 }
        NumberAnimation { target: %2; property: "opacity"; to: 1; duration: 220
                          easing.type: Easing.OutQuad }
    }
)")
        .arg(t)
        .arg(id);
}

QString capaBarra(const QJsonObject& c, const QString& id, const QJsonObject& p) {
    return QString(R"(        Rectangle {
        id: %1
        width: %2
        height: %3
        color: %4
        opacity: %5
%6    }
)")
        .arg(id)
        .arg(num(c, "ancho", 12))
        .arg(num(c, "alto", 120))
        .arg(entreComillas(conParametros(c.value("color").toString("#ff6a2b"), p)))
        .arg(c.contains("aparece") ? 0.0 : num(c, "opacidad", 1.0))
        .arg(posicion(c));
}

/// Un texto que cruza la pantalla sin parar (un ticker). Se anima el x del
/// propio texto en bucle: entra por la derecha y sale por la izquierda.
/// `desplaza` son los píxeles por segundo.
QString desplazamiento(const QJsonObject& c, const QString& id) {
    if (!c.contains("desplaza")) return {};
    const double vel = std::max(10.0, num(c, "desplaza", 120));
    return QString(R"(    NumberAnimation {
        target: %1; property: "x"
        from: raiz.width; to: -%1.implicitWidth
        duration: Math.max(1000, (raiz.width + %1.implicitWidth) / %2 * 1000)
        loops: Animation.Infinite
        running: true
    }
)")
        .arg(id)
        .arg(vel);
}

QString capaTexto(const QJsonObject& c, const QString& id, const QJsonObject& p) {
    // Un texto sin ancho crece en una sola línea. Con «ancho» corta por
    // palabras, que es lo que hace falta para una pregunta o una bajada larga.
    QString caja;
    if (c.contains("ancho"))
        caja = QString("        width: %1\n        wrapMode: Text.WordWrap\n")
                   .arg(num(c, "ancho", 400));
    return QString(R"(        Text {
        id: %1
        text: %2
        color: %3
        font { family: %4; pixelSize: %5; weight: %6 }
        renderType: Text.QtRendering
        opacity: %7
%9%8    }
)")
        .arg(id)
        .arg(entreComillas(conParametros(c.value("valor").toString(), p)))
        .arg(entreComillas(conParametros(c.value("color").toString("white"), p)))
        .arg(QString::fromUtf8(kFuente))
        .arg(num(c, "tamano", 28))
        .arg(pesoDeTexto(c.value("peso").toString()))
        .arg(c.contains("aparece") ? 0.0 : 1.0)
        .arg(posicion(c))
        .arg(caja);
}

QString capaPanel(const QJsonObject& c, const QString& id, const QJsonObject& p) {
    // Un panel puede ajustarse al ancho de los textos que se apoyan en él:
    // es lo que hace que un nombre largo no se salga de la caja.
    QString ancho = QString::number(num(c, "ancho", 400));
    const QJsonArray ajusta = c.value("ajustaA").toArray();
    if (!ajusta.isEmpty()) {
        QStringList anchos;
        for (const QJsonValue& v : ajusta)
            anchos << QString("cap_%1.implicitWidth").arg(v.toString());
        ancho = QString("Math.max(%1) + %2").arg(anchos.join(", ")).arg(num(c, "margen", 56));
    }
    return QString(R"(        Rectangle {
        id: %1
        width: %2
        height: %3
        color: %4
        opacity: %5
%6    }
)")
        .arg(id)
        .arg(ancho)
        .arg(num(c, "alto", 120))
        .arg(entreComillas(conParametros(c.value("color").toString("#12151a"), p)))
        .arg(c.contains("aparece") ? 0.0 : num(c, "opacidad", 1.0))
        .arg(posicion(c));
}

} // namespace

QJsonObject repertorio() {
    return QJsonObject{
        {"capas",
         QJsonObject{
             {"barra", "Un rectángulo de color. Para la barrita de acento de un zócalo."},
             {"panel", "La caja de fondo. Con «ajustaA» crece con el ancho de los textos que nombre."},
             {"texto",
              "Un texto. «valor» acepta {parametros} entre llaves. Con «desplaza» (píxeles por "
              "segundo) cruza la pantalla sin parar, que es un ticker. Con «ancho» corta por "
              "palabras en vez de crecer en una sola línea."}}},
        {"posicion",
         "Cada capa se ubica con «izquierda» o «derecha» y «arriba» o «abajo», en píxeles desde "
         "ese borde del gráfico."},
        {"aparece", "Milisegundos desde el principio en que esa capa se funde a la vista."},
        {"entrada",
         QJsonObject{{"desliza-izquierda", "Entra desde afuera por la izquierda."},
                     {"desliza-derecha", "Entra desde afuera por la derecha."},
                     {"aparece", "Solo se funde, sin moverse."}}},
        {"salida", QJsonObject{{"desenfoca", "Se va desenfocándose y desvaneciéndose."},
                               {"desaparece", "Solo se funde."}}}};
}

Traduccion traducir(const QJsonObject& d, const QJsonObject& parametros) {
    Traduccion t;
    const QJsonArray capas = d.value("capas").toArray();
    if (capas.isEmpty()) {
        t.error = "el gráfico no tiene ninguna capa";
        return t;
    }

    const double ancho = num(d, "ancho", 900);
    const double alto = num(d, "alto", 220);

    QString cuerpo;
    QString animaciones;
    int n = 0;
    for (const QJsonValue& v : capas) {
        const QJsonObject c = v.toObject();
        const QString tipo = c.value("tipo").toString();
        // El id sale del que le pusieron o de la posición: hace falta para
        // poder animarla y para que un panel pueda ajustarse a un texto.
        const QString id = "cap_" + (c.contains("id") ? c.value("id").toString()
                                                      : QString("n%1").arg(++n));
        if (tipo == "barra") {
            cuerpo += capaBarra(c, id, parametros);
        } else if (tipo == "texto") {
            cuerpo += capaTexto(c, id, parametros);
        } else if (tipo == "panel") {
            cuerpo += capaPanel(c, id, parametros);
        } else {
            t.error = QString("no sé qué es una capa de tipo «%1». Las que hay: barra, panel, texto.")
                          .arg(tipo);
            return t;
        }
        animaciones += apariciónDeCapa(c, id);
        animaciones += desplazamiento(c, id);
    }

    // La entrada y la salida se aplican al EFECTO, que es lo que se dibuja:
    // mover el item de origen no movería nada (ver docs/GRAFICOS.md).
    const QJsonObject entrada = d.value("entrada").toObject();
    const QString efectoEntrada = entrada.value("efecto").toString("desliza-izquierda");
    const double durEntrada = num(entrada, "duracion", 450);
    QString desdeX = "0";
    if (efectoEntrada == "desliza-izquierda") desdeX = QString("-%1").arg(ancho * 0.5);
    else if (efectoEntrada == "desliza-derecha") desdeX = QString("%1").arg(ancho * 0.5);

    const QJsonObject salida = d.value("salida").toObject();
    const bool desenfocaAlSalir = salida.value("efecto").toString("desenfoca") == "desenfoca";
    const double durSalida = num(salida, "duracion", 320);

    t.qml = QString(R"(// GENERADO a partir de la descripción del gráfico. No editar a mano:
// se vuelve a generar en cada dibujo. Para cambiarlo, cambiá la descripción.
import QtQuick
import QtQuick.Effects

Item {
    id: raiz
    width: %1
    height: %2

    /// Cuándo se va, en ms desde que apareció. 0 = se queda.
    property int salidaEn: 0

    // El origen del efecto no se mueve nunca; quien se desliza es el efecto.
    Item {
        id: contenido
        width: raiz.width
        height: raiz.height
        visible: false
%3    }

    MultiEffect {
        id: efecto
        source: contenido
        width: raiz.width
        height: raiz.height
        blurEnabled: blur > 0
        blurMax: 48
    }

    ParallelAnimation {
        running: true
        NumberAnimation {
            target: efecto; property: "x"
            from: %4; to: 0
            duration: %5
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: efecto; property: "opacity"
            from: 0; to: 1
            duration: %6
        }
    }

%7
    SequentialAnimation {
        running: raiz.salidaEn > 0
        PauseAnimation { duration: Math.max(0, raiz.salidaEn) }
        ParallelAnimation {
            NumberAnimation { target: efecto; property: "blur"
                              to: %8; duration: %9 }
            NumberAnimation { target: efecto; property: "opacity"
                              to: 0; duration: %9; easing.type: Easing.InQuad }
        }
    }
}
)")
                .arg(ancho)
                .arg(alto)
                .arg(cuerpo)
                .arg(desdeX)
                .arg(durEntrada)
                .arg(durEntrada * 0.6)
                .arg(animaciones)
                .arg(desenfocaAlSalir ? 1.0 : 0.0)
                .arg(durSalida);
    return t;
}

} // namespace ba::grafico
