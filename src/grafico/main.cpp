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
// Para las vistas previas alcanza. Para el AIRE (la tira de una animación a 30
// cuadros por segundo) no: comprimir PNG se llevaba tres cuartos del tiempo y el
// gráfico llegaba tarde y trabado. Con `"shm":true` la tira deja los píxeles crudos
// en memoria compartida y la respuesta solo dice dónde: {"ok":true, "shm":"<clave>",
// "ancho", "alto", "bpl", "cuadros":[ms...]}, BGRA premultiplicado, un cuadro atrás
// del otro. El segmento vive hasta el pedido siguiente (el que lo pidió lo copia
// antes de mandar otro).
//
// Peticiones:
//   {"op":"validar",  "qml":"...", "parametros":{...}}
//   {"op":"dibujar",  "qml":"...", "parametros":{...}, "ancho":900, "alto":220, "ms":400}
//   {"op":"tira",     "qml":"...", "parametros":{...}, "ancho":900, "alto":220,
//                     "momentos":[0,200,400,1500], "shm":false}
//   {"op":"salir"}
//
// Respuestas: {"ok":true, ...} o {"ok":false, "error":"..."}.
// ============================================================================

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
#include <iostream>
#include <string>

#include <QBuffer>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickWindow>

#include "Declarativo.hpp"
#include "Renderizador.hpp"

#include "diag/Caidas.hpp"
#include "diag/Registro.hpp"
#include "platform/Paths.hpp"
#include "platform/Segmento.hpp"

namespace {

using ba::grafico::Renderizador;

QString aBase64Png(const QImage& img) {
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return QString::fromLatin1(bytes.toBase64());
}

// La tira en memoria compartida (ver el encabezado). Se guarda el segmento de la
// respuesta anterior hasta acá: el que pidió ya lo copió, porque no manda otro pedido
// sin haber leído la respuesta.
ba::platform::Segmento g_segmento;
int g_segmentos = 0;

QJsonObject enMemoria(const std::vector<Renderizador::Resultado>& res, const std::vector<int>& ms) {
    const QImage& primero = res.front().imagen;
    const size_t porCuadro = static_cast<size_t>(primero.sizeInBytes());
    for (const auto& c : res)
        if (c.imagen.size() != primero.size() || static_cast<size_t>(c.imagen.sizeInBytes()) != porCuadro)
            return QJsonObject{{"ok", false}, {"error", "los cuadros de la tira no tienen el mismo tamaño"}};
    g_segmento.cerrar();
    // Una clave corta: en macOS el nombre de un segmento no pasa de 31 letras.
    const std::string clave =
        "bag" + std::to_string(QCoreApplication::applicationPid()) + "n" + std::to_string(++g_segmentos);
    std::string err;
    if (!g_segmento.crear(clave, porCuadro * res.size(), &err))
        return QJsonObject{{"ok", false}, {"error", "no se pudo crear la memoria compartida: " + QString::fromStdString(err)}};
    auto* destino = static_cast<uchar*>(g_segmento.datos());
    QJsonArray cuadros;
    for (size_t i = 0; i < res.size(); ++i) {
        std::memcpy(destino + porCuadro * i, res[i].imagen.constBits(), porCuadro);
        cuadros.append(ms[i]);
    }
    return QJsonObject{{"ok", true},
                       {"shm", QString::fromStdString(clave)},
                       {"ancho", primero.width()},
                       {"alto", primero.height()},
                       {"bpl", static_cast<int>(primero.bytesPerLine())},
                       {"cuadros", cuadros}};
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
    QSize tam(pedido.value("ancho").toInt(900), pedido.value("alto").toInt(220));

    if (op == "repertorio") return QJsonObject{{"ok", true}, {"repertorio", ba::grafico::repertorio()}};

    // Un gráfico puede venir de tres lados: un archivo (`qml`), texto suelto
    // (`fuente`, que usa el IDE mientras se edita) o una descripción
    // declarativa (`declarativo`), que se traduce a QML acá mismo.
    QString fuente = pedido.value("fuente").toString();
    if (pedido.contains("declarativo")) {
        const QJsonObject d = pedido.value("declarativo").toObject();
        const ba::grafico::Traduccion tr = ba::grafico::traducir(d, par);
        if (!tr.error.isEmpty()) return QJsonObject{{"ok", false}, {"error", tr.error}};
        fuente = tr.qml;
        // La descripción manda su propio tamaño salvo que se lo pisen.
        if (!pedido.contains("ancho") && d.contains("ancho")) tam.setWidth(d.value("ancho").toInt(900));
        if (!pedido.contains("alto") && d.contains("alto")) tam.setHeight(d.value("alto").toInt(220));
    }
    // El QML GENERADO a partir de una descripción ya trae los valores metidos
    // en el texto, así que no se le pasan de nuevo: como no los declara como
    // propiedades, pasárselos sería un error.
    //
    // Ojo con la condición: no es «vino como texto», es «lo generamos
    // nosotros». Un gráfico escrito a mano también viaja como texto —es lo que
    // manda el editor mientras se escribe, y también el emisor cuando lo saca
    // al aire— y ESE sí declara sus parámetros y los necesita. Con la
    // condición equivocada, un zócalo salía al aire siempre con el nombre de
    // ejemplo, sin decir nada.
    const QJsonObject parReales = pedido.contains("declarativo") ? QJsonObject{} : par;
    const auto dibujar = [&](int ms) {
        return fuente.isEmpty() ? r.dibujar(qml, parReales, tam, ms)
                                : r.dibujarFuente(fuente, parReales, tam, ms);
    };

    if (op == "validar") {
        const Renderizador::Resultado res = dibujar(0);
        if (!res.error.isEmpty()) return QJsonObject{{"ok", false}, {"error", res.error}};
        return QJsonObject{{"ok", true}};
    }
    if (op == "dibujar") {
        const Renderizador::Resultado res = dibujar(pedido.value("ms").toInt(0));
        if (!res.error.isEmpty()) return QJsonObject{{"ok", false}, {"error", res.error}};
        return QJsonObject{
            {"ok", true}, {"ms", pedido.value("ms").toInt(0)}, {"png", aBase64Png(res.imagen)}};
    }
    if (op == "tira") {
        const QJsonArray momentos = pedido.value("momentos").toArray();
        if (momentos.isEmpty())
            return QJsonObject{{"ok", false}, {"error", "la tira necesita al menos un momento"}};
        // Un tope: una tira de doscientos cuadros no la mira nadie y llenaria
        // la respuesta de megabytes.
        if (momentos.size() > 12) return QJsonObject{{"ok", false}, {"error", "hasta 12 momentos por tira"}};
        // Una sola instancia para toda la tira (y la siguiente, si sigue la misma animación).
        std::vector<int> ms;
        for (const QJsonValue& m : momentos) ms.push_back(m.toInt());
        for (size_t i = 1; i < ms.size(); ++i)
            if (ms[i] < ms[i - 1]) return QJsonObject{{"ok", false}, {"error", "los momentos van en orden"}};
        const QUrl url = fuente.isEmpty() ? QUrl::fromLocalFile(qml) : QUrl("grafico.qml");
        const std::vector<Renderizador::Resultado> res = r.tira(url, fuente, parReales, tam, ms);
        for (const auto& c : res)
            if (!c.error.isEmpty()) return QJsonObject{{"ok", false}, {"error", c.error}};
        if (res.size() != ms.size()) return QJsonObject{{"ok", false}, {"error", "la tira salió incompleta"}};
        if (pedido.value("shm").toBool()) return enMemoria(res, ms);
        QJsonArray cuadros;
        for (size_t i = 0; i < res.size(); ++i)
            cuadros.append(QJsonObject{{"ms", ms[i]}, {"png", aBase64Png(res[i].imagen)}});
        return QJsonObject{{"ok", true}, {"cuadros", cuadros}};
    }
    return QJsonObject{{"ok", false}, {"error", "no sé qué es la operación «" + op + "»"}};
}

} // namespace

int main(int argc, char** argv) {
    // Sin ventana de verdad: se dibuja a una textura. Aun asi hace falta un
    // contexto de OpenGL, asi que la plataforma de Qt importa.
    //
    // SOLO en X11/Wayland se cae a `offscreen`, y solo cuando NO hay pantalla
    // (un servidor, el CI): ahi Qt rasteriza igual. En Windows y macOS no se
    // toca: `DISPLAY` y `WAYLAND_DISPLAY` nunca estan puestas, asi que la
    // condicion de abajo daria verdadera SIEMPRE y forzaria `offscreen` aunque
    // el escritorio este ahi. Y la plataforma offscreen de Windows no da
    // contexto WGL: el renderizador no podria crear el suyo y los graficos no
    // se dibujarian nunca, sin que nada lo explique.
#if !defined(_WIN32) && !defined(__APPLE__)
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") && qEnvironmentVariableIsEmpty("DISPLAY") &&
        qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
#endif
    QGuiApplication app(argc, argv);

    // El registro: este proceso lo levanta la app por una tuberia, asi que su
    // stderr no lo lee nadie. Sin archivo propio, un gráfico que revienta no
    // deja rastro en ningun lado.
    ba::diag::instalarManejadorDeCaidas("graficos");
    auto& reg = ba::diag::registro();
    reg.setComponentePorDefecto("graficos");
    reg.abrir(ba::platform::dataRoot() / "Logs", "graficos.log");
    reg.info("renderizador de graficos listo");

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
        const QJsonObject pedido = doc.object();
        if (pedido.value("op").toString() == "salir") break;
        const QJsonObject r = atender(renderizador, pedido);
        // Lo que sale mal se registra SIEMPRE, aunque el que pidio ya reciba
        // el error: en vivo, quien pide es la app y el error se muestra un
        // segundo y se va.
        if (!r.value("ok").toBool())
            ba::diag::registro().error(
                (pedido.value("op").toString() + ": " + r.value("error").toString()).toStdString());
        else
            ba::diag::registro().depuracion(pedido.value("op").toString().toStdString() + " ok");
        responder(r);
    }
    return 0;
}
