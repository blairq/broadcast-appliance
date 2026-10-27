// ============================================================================
// src/grafico/Declarativo.cpp — ver Declarativo.hpp
// ============================================================================
#include "Declarativo.hpp"

#include "i18n/I18n.hpp"

#include <algorithm>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

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

/// La familia tipográfica viaja con el bundle (data/fonts, la carga main.cpp). UN nombre:
/// font.family no acepta una lista con comas como CSS; con «Inter, Noto Sans, ...» (Inter
/// ni siquiera viene) caía a la letra del sistema, distinta en cada máquina.
constexpr const char* kFuente = R"("Noto Sans")";

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
        caja = QString("        width: %1\n        wrapMode: Text.WordWrap\n").arg(num(c, "ancho", 400));
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

/// Un JSON como literal de QML (JSON es JavaScript válido).
QString literal(const QJsonValue& v) {
    if (v.isArray()) return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
    if (v.isObject()) return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
    if (v.isString()) return entreComillas(v.toString());
    if (v.isBool()) return v.toBool() ? "true" : "false";
    if (v.isDouble()) return QString::number(v.toDouble());
    return "null";
}

/// Lo que dice `datos` de una capa: «{votos}» es el parámetro (crudo: puede ser
/// una lista o el texto de un campo); cualquier otra cosa, el valor tal cual.
QJsonValue crudoDe(const QJsonObject& c, const QJsonObject& p) {
    const QJsonValue v = c.value("datos");
    static const QRegularExpression soloParametro(R"(^\s*\{([A-Za-z_][A-Za-z0-9_]*)\}\s*$)");
    if (v.isString()) {
        const auto m = soloParametro.match(v.toString());
        if (m.hasMatch()) return p.value(m.captured(1));
    }
    return v;
}

/// Un medio (GIF, imagen, secuencia de cuadros) con el reloj del gráfico.
QString capaMedio(const QJsonObject& c, const QString& id, const QJsonObject& p) {
    const QString ajuste = c.value("ajuste").toString("ajustar");
    const QString enumAjuste =
        ajuste == "llenar" ? "Medio.Llenar" : ajuste == "estirar" ? "Medio.Estirar" : "Medio.Ajustar";
    return QString(R"(        Medio {
        id: %1
        width: %2
        height: %3
        source: %4
        inicio: %5
        bucle: %6
        velocidad: %7
        ajuste: %8
        opacity: %9
%10    }
)")
        .arg(id)
        .arg(num(c, "ancho", 200))
        .arg(num(c, "alto", 200))
        .arg(entreComillas(conParametros(c.value("archivo").toString(), p)))
        .arg(num(c, "inicio", 0))
        .arg(c.value("bucle").toBool(true) ? "true" : "false")
        .arg(num(c, "velocidad", 1.0))
        .arg(enumAjuste)
        .arg(c.contains("aparece") ? 0.0 : num(c, "opacidad", 1.0))
        .arg(posicion(c));
}

/// Una torta o unas barras: el componente del mismo nombre (qml/Torta.qml,
/// qml/Barras.qml) con sus datos, y los de ANTES si se está dibujando un cambio.
QString capaDatos(const QString& tipo, const QJsonObject& c, const QString& id, const QJsonObject& p,
                  const Modo& modo) {
    const bool torta = tipo == "torta";
    QString props;
    const auto prop = [&](const char* clave, const char* qml) {
        if (c.contains(clave)) props += QString("        %1: %2\n").arg(QString::fromUtf8(qml), literal(c.value(clave)));
    };
    prop("duracion", "duracion");
    prop("cambio", "duracionCambio");
    prop("tamanoTexto", "tamanoTexto");
    prop("decimales", "decimales");
    prop("sufijo", "sufijo");
    prop("mostrar", "mostrar");
    if (c.contains("colorTexto"))
        props += QString("        colorTexto: %1\n").arg(entreComillas(conParametros(c.value("colorTexto").toString(), p)));
    if (torta) {
        prop("grosor", "grosor");
        prop("etiquetas", "etiquetas");
    } else {
        prop("orientacion", "orientacion");
        prop("maximo", "maximo");
        prop("escalonado", "escalonado");
        prop("anchoEtiquetas", "anchoEtiquetas");
    }
    // `aparece` en una torta o unas barras es cuándo arrancan a crecer (no un
    // fundido, como en las demás capas): crecer ya es la entrada.
    if (c.contains("aparece")) props += QString("        aparece: %1\n").arg(num(c, "aparece", 0));
    QString anteriores = "null";
    if (!modo.anteriores.isEmpty()) {
        const QJsonValue antes = crudoDe(c, modo.anteriores);
        if (!antes.isUndefined() && !antes.isNull()) anteriores = literal(normalizarDatos(antes));
    }
    return QString(R"(        %1 {
        id: %2
        width: %3
        height: %4
        datos: %5
        anteriores: %6
%7%8    }
)")
        .arg(torta ? "Torta" : "Barras")
        .arg(id)
        .arg(num(c, "ancho", torta ? 420 : 900))
        .arg(num(c, "alto", torta ? 420 : 360))
        .arg(literal(normalizarDatos(crudoDe(c, p))))
        .arg(anteriores)
        .arg(props)
        .arg(posicion(c));
}

} // namespace

QJsonArray normalizarDatos(const QJsonValue& v) {
    QJsonArray out;
    const auto numero = [](const QJsonValue& x) -> double {
        if (x.isDouble()) return x.toDouble();
        QString t = x.toVariant().toString().trimmed().remove(' ');
        // «12,5» es doce y medio para quien escribe en castellano; «1.234,5» también.
        if (t.contains(',')) t = t.remove('.').replace(',', '.');
        return t.toDouble();
    };
    const auto agregar = [&](const QString& etiqueta, double valor, const QString& color) {
        QJsonObject o{{"etiqueta", etiqueta}, {"valor", valor}};
        if (!color.isEmpty()) o["color"] = color;
        out.append(o);
    };
    if (v.isArray()) {
        int i = 0;
        for (const QJsonValue& e : v.toArray()) {
            ++i;
            if (e.isObject()) {
                const QJsonObject o = e.toObject();
                const QJsonValue val = o.contains("valor") ? o.value("valor") : o.value("value");
                agregar(o.contains("etiqueta") ? o.value("etiqueta").toVariant().toString()
                                               : o.value("label").toVariant().toString(),
                        numero(val), o.value("color").toString());
            } else if (e.isArray()) {
                const QJsonArray par = e.toArray();
                agregar(par.at(0).toVariant().toString(), numero(par.at(1)), par.at(2).toString());
            } else {
                agregar(QString::number(i), numero(e), {});
            }
        }
        return out;
    }
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        for (auto it = o.begin(); it != o.end(); ++it) agregar(it.key(), numero(it.value()), {});
        return out;
    }
    if (v.isDouble()) {
        agregar("1", v.toDouble(), {});
        return out;
    }
    const QString texto = v.toString().trimmed();
    if (texto.isEmpty()) return out;
    if (texto.startsWith('[') || texto.startsWith('{')) {
        const QJsonDocument d = QJsonDocument::fromJson(texto.toUtf8());
        if (d.isArray()) return normalizarDatos(d.array());
        if (d.isObject()) return normalizarDatos(d.object());
    }
    // El texto de una persona: «Etiqueta: valor #color», separados por «;» o renglones.
    static const QRegularExpression separador(R"([;\n])");
    static const QRegularExpression entrada(R"(^(.*?)\s*[:=]\s*([-+]?[0-9][0-9.,\s]*)\s*(#[0-9A-Fa-f]{3,8})?\s*$)");
    for (const QString& parte : texto.split(separador, Qt::SkipEmptyParts)) {
        const auto m = entrada.match(parte.trimmed());
        if (m.hasMatch()) agregar(m.captured(1).trimmed(), numero(m.captured(2)), m.captured(3));
    }
    return out;
}

// Lo lee el asistente (la ayuda de los gráficos declarativos): en el idioma del operador.
// Las claves son las del formato y no se traducen.
QJsonObject repertorio() {
    return QJsonObject{
        {"capas",
         QJsonObject{
             {"barra", QString::fromStdString(ba::i18n::traducir("asistente", "Un rectángulo de color. Para la barrita de acento de un zócalo."))},
             {"panel", QString::fromStdString(ba::i18n::traducir("asistente", "La caja de fondo. Con «ajustaA» crece con el ancho de los textos que nombre."))},
             {"texto", QString::fromStdString(ba::i18n::traducir("asistente", "Un texto. «valor» acepta {parametros} entre llaves. Con «desplaza» (píxeles por "
                                                                              "segundo) cruza la pantalla sin parar, que es un ticker. Con «ancho» corta por "
                                                                              "palabras en vez de crecer en una sola línea."))},
             {"medio", QString::fromStdString(ba::i18n::traducir("asistente", "Un GIF animado, una imagen o una secuencia de cuadros (un video con alfa importado con "
                                                                              "grafico_medio_agregar). «archivo» relativo a la carpeta del gráfico (medios/...), «ancho» y "
                                                                              "«alto», «inicio» (ms del gráfico en que arranca: antes no se ve), «bucle» (true de fábrica; "
                                                                              "false = se queda en el último cuadro), «velocidad» (1 = normal) y «ajuste» (ajustar, llenar, "
                                                                              "estirar). Su tiempo es el del gráfico: la vista previa y el aire muestran el mismo cuadro."))},
             {"torta", QString::fromStdString(ba::i18n::traducir("asistente", "Un gráfico de torta (o de anillo, con «grosor» > 0) con datos. «datos»: «{parametro}» o una "
                                                                              "lista [{etiqueta, valor, color}]. Entra girando; con «aparece» (ms) arranca más tarde y "
                                                                              "«duracion» es lo que tarda. «mostrar»: porcentaje, valor, ambos o nada; «etiquetas»; "
                                                                              "«tamanoTexto», «colorTexto», «decimales», «sufijo». Al cambiarle los valores al aire, las "
                                                                              "porciones se mueven de los de antes a los nuevos («cambio»: ms, 700 de fábrica)."))},
             {"barras", QString::fromStdString(ba::i18n::traducir("asistente", "Barras con datos, con su etiqueta y su valor. «orientacion» vertical u horizontal, «maximo» "
                                                                               "(0 = el mayor de los datos), «escalonado» (ms entre una barra y la siguiente al entrar), "
                                                                               "«anchoEtiquetas» (horizontal: la parte del ancho para los nombres, 0.3), y lo mismo que la "
                                                                               "torta: datos, aparece, duracion, cambio, mostrar (valor, porcentaje, ambos, nada), "
                                                                               "tamanoTexto, colorTexto, decimales, sufijo."))}}},
        {"datos", QString::fromStdString(ba::i18n::traducir("asistente", "Un parámetro de tipo «datos» (en grafico.json: \"tipo\": \"datos\") lleva los datos de una torta o "
                                                                         "unas barras. Se escribe como lista [{\"etiqueta\": \"Sí\", \"valor\": 34, \"color\": \"#2e7d32\"}], "
                                                                         "como objeto {\"Sí\": 34, \"No\": 12}, o como texto en un campo: «Sí: 34; No: 12 #c62828; "
                                                                         "Abstenciones: 3»."))},
        {"cambio", QString::fromStdString(ba::i18n::traducir("asistente", "Cambiar los valores de un gráfico que ya está al aire (grafico_actualizar, o «Al aire» de nuevo "
                                                                          "en Rótulos con el mismo gráfico) NO lo hace entrar de nuevo: se dibuja sin la entrada y las "
                                                                          "tortas y barras se mueven de los valores de antes a los nuevos."))},
        {"posicion", QString::fromStdString(ba::i18n::traducir("asistente", "Cada capa se ubica con «izquierda» o «derecha» y «arriba» o «abajo», en píxeles desde "
                                                                            "ese borde del gráfico."))},
        {"aparece", QString::fromStdString(ba::i18n::traducir("asistente", "Milisegundos desde el principio en que esa capa se funde a la vista."))},
        {"entrada", QJsonObject{{"desliza-izquierda", QString::fromStdString(ba::i18n::traducir("asistente", "Entra desde afuera por la izquierda."))},
                                {"desliza-derecha", QString::fromStdString(ba::i18n::traducir("asistente", "Entra desde afuera por la derecha."))},
                                {"aparece", QString::fromStdString(ba::i18n::traducir("asistente", "Solo se funde, sin moverse."))}}},
        {"salida", QJsonObject{{"desenfoca", QString::fromStdString(ba::i18n::traducir("asistente", "Se va desenfocándose y desvaneciéndose."))},
                               {"desaparece", QString::fromStdString(ba::i18n::traducir("asistente", "Solo se funde."))}}}};
}

Traduccion traducir(const QJsonObject& d, const QJsonObject& parametros, const Modo& modo) {
    Traduccion t;
    const QJsonArray capas = d.value("capas").toArray();
    if (capas.isEmpty()) {
        t.error = QCoreApplication::translate("graficos", "el gráfico no tiene ninguna capa");
        return t;
    }

    const double ancho = num(d, "ancho", 900);
    const double alto = num(d, "alto", 220);

    QString cuerpo;
    QString animaciones;
    int n = 0;
    for (const QJsonValue& v : capas) {
        QJsonObject c = v.toObject();
        const QString tipo = c.value("tipo").toString();
        // Ya en pantalla: lo que se fundía al entrar está, y punto.
        if (modo.sinEntrada) c.remove("aparece");
        // El id sale del que le pusieron o de la posición: hace falta para
        // poder animarla y para que un panel pueda ajustarse a un texto.
        const QString id = "cap_" + (c.contains("id") ? c.value("id").toString() : QString("n%1").arg(++n));
        if (tipo == "barra") {
            cuerpo += capaBarra(c, id, parametros);
        } else if (tipo == "texto") {
            cuerpo += capaTexto(c, id, parametros);
        } else if (tipo == "panel") {
            cuerpo += capaPanel(c, id, parametros);
        } else if (tipo == "medio") {
            cuerpo += capaMedio(c, id, parametros);
        } else if (tipo == "torta" || tipo == "barras") {
            cuerpo += capaDatos(tipo, c, id, parametros, modo);
            animaciones += desplazamiento(c, id);
            continue; // su `aparece` es cuándo crecen, no un fundido
        } else {
            t.error = QCoreApplication::translate("graficos", "no sé qué es una capa de tipo «%1». Las que hay: barra, "
                                                              "panel, texto, medio, torta, barras.")
                          .arg(tipo);
            return t;
        }
        animaciones += apariciónDeCapa(c, id);
        animaciones += desplazamiento(c, id);
    }

    // La entrada y la salida se aplican al EFECTO, que es lo que se dibuja:
    // mover el item de origen no movería nada (ver docs/funciones/GRAFICOS.md).
    const QJsonObject entrada = d.value("entrada").toObject();
    const QString efectoEntrada = entrada.value("efecto").toString("desliza-izquierda");
    const double durEntrada = num(entrada, "duracion", 450);
    QString desdeX = "0";
    if (efectoEntrada == "desliza-izquierda")
        desdeX = QString("-%1").arg(ancho * 0.5);
    else if (efectoEntrada == "desliza-derecha")
        desdeX = QString("%1").arg(ancho * 0.5);
    // Un cambio con el gráfico en pantalla no vuelve a entrar: arranca donde la
    // entrada termina (x 0, opacidad 1) y la animación no mueve nada.
    if (modo.sinEntrada) desdeX = "0";

    const QJsonObject salida = d.value("salida").toObject();
    const bool desenfocaAlSalir = salida.value("efecto").toString("desenfoca") == "desenfoca";
    const double durSalida = num(salida, "duracion", 320);

    t.qml = QString(R"(// GENERADO a partir de la descripción del gráfico. No editar a mano:
// se vuelve a generar en cada dibujo. Para cambiarlo, cambiá la descripción.
import QtQuick
import QtQuick.Effects
import BA.Grafico

Item {
    id: raiz
    width: %1
    height: %2

    /// Cuándo se va, en ms desde que apareció. 0 = se queda.
    property int salidaEn: 0

    // El origen del efecto no se mueve nunca; quien se desliza es el efecto.
    // Se esconde con hideSource y no con visible: false: un Item invisible no
    // se «pule» (updatePolish), y las formas (Shape: las tortas) se arman ahí;
    // escondido así, la torta no se dibujaba.
    Item {
        id: contenido
        width: raiz.width
        height: raiz.height
%3    }
    ShaderEffectSource {
        id: fuenteContenido
        sourceItem: contenido
        hideSource: true
        visible: false
        width: raiz.width
        height: raiz.height
    }

    MultiEffect {
        id: efecto
        source: fuenteContenido
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
            from: %10; to: 1
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
                .arg(durSalida)
                .arg(modo.sinEntrada ? 1.0 : 0.0);
    return t;
}

} // namespace ba::grafico
