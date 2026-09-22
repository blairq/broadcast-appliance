// ============================================================================
// src/grafico/main.cpp — `ba-grafico`: dibuja gráficos animados, aparte
//
// POR QUÉ ES OTRO PROCESO. Un gráfico es código que escribe la LLM o el
// operador mientras edita, o sea código roto todo el tiempo: un bucle
// infinito, una recursión, memoria pedida sin freno. Si eso corriera adentro
// de la app, un gráfico mal escrito colgaría el programa que está al aire.
// Acá, el peor caso es que este proceso muera y la app lo vuelva a levantar.
//
// De paso resuelve el otro problema: es la caja de arena. Acotar un motor de
// QML desde adentro no alcanza; un proceso aparte sí.
//
// PROTOCOLO. Una petición JSON por línea en la entrada, una respuesta JSON por
// línea en la salida. Las imágenes viajan en PNG codificado en base64.
//
// Eso alcanza para lo que hace hoy: vistas previas (un cuadro, o una tira de
// cuadros para mirar una animación). Para componer EN VIVO dentro de una
// escena a 30 cuadros por segundo, base64 no sirve y habrá que pasar los
// cuadros por memoria compartida, como ya hacen A y B.
//
// Peticiones:
//   {"op":"validar",  "qml":"...", "parametros":{...}}
//   {"op":"dibujar",  "qml":"...", "parametros":{...}, "ancho":900, "alto":220, "ms":400}
//   {"op":"tira",     "qml":"...", "parametros":{...}, "ancho":900, "alto":220,
//                     "momentos":[0,200,400,1500]}
//   {"op":"salir"}
//
// Respuestas: {"ok":true, ...} o {"ok":false, "error":"..."}.
// ============================================================================

#include <cstdio>
#include <iostream>
#include <string>

#include <QBuffer>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickWindow>

#include "Renderizador.hpp"

namespace {

using ba::grafico::Renderizador;

QString aBase64Png(const QImage& img) {
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return QString::fromLatin1(bytes.toBase64());
}

void responder(const QJsonObject& o) {
    // Una linea, sin saltos adentro: el que lee del otro lado lee por lineas.
    std::fputs(QJsonDocument(o).toJson(QJsonDocument::Compact).constData(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

void responderError(const QString& e) { responder(QJsonObject{{"ok", false}, {"error", e}}); }

QJsonObject atender(Renderizador& r, const QJsonObject& pedido) {
    const QString op = pedido.value("op").toString();
    const QString qml = pedido.value("qml").toString();
    const QJsonObject par = pedido.value("parametros").toObject();
    const QSize tam(pedido.value("ancho").toInt(900), pedido.value("alto").toInt(220));

    if (op == "validar") {
        const Renderizador::Resultado res = r.validar(qml, par);
        if (!res.error.isEmpty()) return QJsonObject{{"ok", false}, {"error", res.error}};
        return QJsonObject{{"ok", true}};
    }
    if (op == "dibujar") {
        const Renderizador::Resultado res = r.dibujar(qml, par, tam, pedido.value("ms").toInt(0));
        if (!res.error.isEmpty()) return QJsonObject{{"ok", false}, {"error", res.error}};
        return QJsonObject{{"ok", true}, {"ms", pedido.value("ms").toInt(0)},
                           {"png", aBase64Png(res.imagen)}};
    }
    if (op == "tira") {
        const QJsonArray momentos = pedido.value("momentos").toArray();
        if (momentos.isEmpty())
            return QJsonObject{{"ok", false}, {"error", "la tira necesita al menos un momento"}};
        // Un tope: una tira de doscientos cuadros no la mira nadie y llenaria
        // la respuesta de megabytes.
        if (momentos.size() > 12)
            return QJsonObject{{"ok", false}, {"error", "hasta 12 momentos por tira"}};
        QJsonArray cuadros;
        for (const QJsonValue& m : momentos) {
            const Renderizador::Resultado res = r.dibujar(qml, par, tam, m.toInt());
            if (!res.error.isEmpty()) return QJsonObject{{"ok", false}, {"error", res.error}};
            cuadros.append(QJsonObject{{"ms", m.toInt()}, {"png", aBase64Png(res.imagen)}});
        }
        return QJsonObject{{"ok", true}, {"cuadros", cuadros}};
    }
    return QJsonObject{{"ok", false}, {"error", "no sé qué es la operación «" + op + "»"}};
}

} // namespace

int main(int argc, char** argv) {
    // Sin ventana de verdad: se dibuja fuera de pantalla. Si no hay pantalla
    // (un servidor, CI), Qt igual puede rasterizar.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") && qEnvironmentVariableIsEmpty("DISPLAY") &&
        qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QGuiApplication app(argc, argv);

    Renderizador renderizador;
    std::string linea;
    while (std::getline(std::cin, linea)) {
        if (linea.empty()) continue;
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(linea), &err);
        if (!doc.isObject()) {
            responderError("no entendí el pedido: " + err.errorString());
            continue;
        }
        if (doc.object().value("op").toString() == "salir") break;
        responder(atender(renderizador, doc.object()));
    }
    return 0;
}
