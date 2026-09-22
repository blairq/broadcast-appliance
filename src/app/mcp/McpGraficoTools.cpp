// ============================================================================
// src/app/mcp/McpGraficoTools.cpp — gráficos animados, para el asistente
//
// El bucle que hace que valga la pena: escribir, validar, mirar una TIRA de
// cuadros, corregir. Sin ese bucle el asistente escribe a ciegas y el
// operador se entera al aire.
//
// Los gráficos viven en Graficos/ del bundle (los de fábrica) y en
// Eventos/<evento>/Graficos/ (los del evento, que viajan con él).
// ============================================================================
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "AppRuntime.hpp"
#include "graficos/GraficoProceso.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"

namespace ba::app::mcp {
namespace {

using graficos::GraficoProceso;

/// Dónde puede vivir un gráfico. Nunca fuera de estas dos carpetas: un nombre
/// con ".." no tiene por qué llegar a leer el disco.
QStringList carpetas(AppRuntime& rt) {
    return {QString::fromStdString((rt.dataRoot() / "Graficos").string())};
}

QString carpetaDe(AppRuntime& rt, const QString& nombre, QString* err) {
    if (nombre.isEmpty() || nombre.contains("..") || nombre.contains('/') || nombre.contains('\\')) {
        if (err != nullptr) *err = "nombre de gráfico inválido";
        return {};
    }
    for (const QString& base : carpetas(rt)) {
        const QString c = QDir(base).filePath(nombre);
        if (QFileInfo(c).isDir()) return c;
    }
    if (err != nullptr) *err = "no existe el gráfico «" + nombre + "» (grafico_listar los muestra)";
    return {};
}

/// El grafico.json de un gráfico, o un objeto vacío.
QJsonObject ficha(const QString& carpeta) {
    QFile f(QDir(carpeta).filePath("grafico.json"));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

/// Arma el pedido para el renderizador a partir de lo que hay en la carpeta:
/// si tiene "declarativo" en su ficha va por ahí; si tiene grafico.qml, por el
/// archivo.
QJsonObject pedidoDe(const QString& carpeta, const QJsonObject& parametros, QString* err) {
    const QJsonObject f = ficha(carpeta);
    QJsonObject p{{"parametros", parametros}};
    if (f.contains("declarativo")) {
        p["declarativo"] = f.value("declarativo");
        return p;
    }
    const QString qml = QDir(carpeta).filePath("grafico.qml");
    if (!QFileInfo::exists(qml)) {
        if (err != nullptr)
            *err = "el gráfico no tiene ni «declarativo» en su grafico.json ni un grafico.qml";
        return {};
    }
    p["qml"] = qml;
    if (f.contains("ancho")) p["ancho"] = f.value("ancho");
    if (f.contains("alto")) p["alto"] = f.value("alto");
    return p;
}

} // namespace

void McpService::registerGraficoTools() {
    core_.addCategory("graficos", "Gráficos animados",
                      "Zócalos, placas y tickers: escribirlos, verlos y corregirlos.");

    AppRuntime& rt = rt_;
    auto* proc = new GraficoProceso({}, this);

    {
        Tool t;
        t.name = "grafico_listar";
        t.title = "Gráficos que hay";
        t.category = "graficos";
        t.readOnly = true;
        t.description = "Los gráficos disponibles, con los parámetros que acepta cada uno.";
        t.details =
            "Un gráfico es una carpeta con `grafico.json` (qué parámetros acepta) y, o bien una "
            "descripción declarativa adentro de ese mismo archivo, o bien un `grafico.qml` escrito a "
            "mano.\n\n"
            "Antes de inventar uno nuevo, mirá si alguno de estos sirve cambiándole los parámetros: "
            "casi siempre alcanza.";
        t.run = [&rt, proc](const json&, Done done) {
            json out = json::array();
            for (const QString& base : carpetas(rt)) {
                for (const QString& d : QDir(base).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                    const QJsonObject f = ficha(QDir(base).filePath(d));
                    json g{{"nombre", d.toStdString()},
                           {"titulo", f.value("nombre").toString(d).toStdString()},
                           {"descripcion", f.value("descripcion").toString().toStdString()},
                           {"declarativo", f.contains("declarativo")}};
                    json params = json::array();
                    for (const QJsonValue& v : f.value("parametros").toArray()) {
                        const QJsonObject p = v.toObject();
                        params.push_back(json{{"clave", p.value("clave").toString().toStdString()},
                                              {"titulo", p.value("titulo").toString().toStdString()},
                                              {"tipo", p.value("tipo").toString().toStdString()},
                                              {"ayuda", p.value("ayuda").toString().toStdString()}});
                    }
                    g["parametros"] = params;
                    out.push_back(g);
                }
            }
            done(ToolResult().data(json{{"graficos", out},
                                        {"motorDisponible", proc->disponible()}}));
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "grafico_probar";
        t.title = "Ver cómo queda un gráfico";
        t.category = "graficos";
        t.readOnly = true;
        t.description =
            "Dibuja el gráfico en varios instantes de su animación y devuelve las imágenes. "
            "No sale al aire.";
        t.details =
            "ESTA es la herramienta que hay que usar antes de dar un gráfico por bueno. Una animación "
            "NO se puede juzgar mirando un cuadro: hay que ver la entrada a medio camino, el momento "
            "en que se asienta y, si tiene salida, cómo se va.\n\n"
            "`momentos` en milisegundos desde que el gráfico aparece. Para una entrada de 450 ms, algo "
            "como [0, 150, 300, 450, 900] muestra lo que importa. Hasta 12.\n\n"
            "Mirá las imágenes de verdad: ahí se ve si el texto entra tarde, si la caja tapa el nombre, "
            "si un nombre largo se sale, o si el color no se lee sobre el video.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"nombre", {{"type", "string"}, {"description", "Cuál gráfico (ver grafico_listar)."}}},
              {"parametros", {{"type", "object"}, {"description", "Los valores a probar."}}},
              {"momentos", {{"type", "array"}, {"items", {{"type", "integer"}}}}}}},
            {"required", {"nombre"}}};
        t.examples = {
            R"({"nombre": "zocalo", "parametros": {"nombre": "Jorge Ortega", "cargo": "Intendente"}, "momentos": [0, 150, 300, 450, 900]})"};
        t.run = [&rt, proc](const json& a, Done done) {
            if (!proc->disponible())
                return done(ToolResult::error(
                    "El renderizador de gráficos no está en este bundle (" +
                    proc->binario().toStdString() + ")."));
            QString err;
            const QString carpeta =
                carpetaDe(rt, QString::fromStdString(a.value("nombre", std::string())), &err);
            if (carpeta.isEmpty()) return done(ToolResult::error(err.toStdString()));

            QJsonObject par;
            if (a.contains("parametros"))
                par = QJsonDocument::fromJson(QByteArray::fromStdString(a["parametros"].dump()))
                          .object();
            QJsonObject pedido = pedidoDe(carpeta, par, &err);
            if (pedido.isEmpty()) return done(ToolResult::error(err.toStdString()));

            QVector<int> momentos;
            for (const auto& m : a.value("momentos", json::array()))
                momentos.append(m.get<int>());
            if (momentos.isEmpty()) momentos = {0, 150, 300, 450, 900};

            const auto cuadros = proc->tira(pedido, momentos, &err);
            if (!err.isEmpty()) return done(ToolResult::error(err.toStdString()));

            ToolResult r;
            for (const auto& c : cuadros) {
                r.text(std::to_string(c.ms) + " ms");
                addImage(r, c.imagen, a, 1200);
            }
            return done(r);
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "grafico_repertorio";
        t.title = "Qué se puede usar en un gráfico declarativo";
        t.category = "graficos";
        t.readOnly = true;
        t.description = "Las capas, las formas de ubicarlas y los efectos de entrada y salida.";
        t.details =
            "Lo declara el propio motor, así que esto no se desincroniza del código.\n\n"
            "Con este repertorio se arma un gráfico SIN escribir código: es lo que conviene hacer "
            "siempre que alcance. Escribir QML a mano solo cuando el repertorio de verdad no dé, "
            "porque ahí reaparecen las trampas (ver `help {\"topic\": \"graficos\"}`).";
        t.run = [proc](const json&, Done done) {
            if (!proc->disponible())
                return done(ToolResult::error("El renderizador de gráficos no está en este bundle."));
            QString err;
            const QJsonObject rep = proc->repertorio(&err);
            if (!err.isEmpty()) return done(ToolResult::error(err.toStdString()));
            done(ToolResult().data(
                json::parse(QString(QJsonDocument(rep).toJson()).toStdString(), nullptr, false)));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
