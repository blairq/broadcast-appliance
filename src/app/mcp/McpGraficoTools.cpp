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
#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "AppRuntime.hpp"
#include "graficos/GraficoProceso.hpp"
#include "graficos/GraficoSource.hpp"
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
        t.name = "grafico_mostrar";
        t.title = "Poner un gráfico al aire";
        t.category = "graficos";
        t.description =
            "Dibuja el gráfico y lo reproduce en una fuente «Gráfico animado» de una escena. "
            "ESTO SE VE AL AIRE si esa escena está en el programa.";
        t.details =
            "Antes de usar esto hay que haber mirado el gráfico con grafico_probar. Una vez que sale, "
            "lo ve el público.\n\n"
            "La fuente tiene que existir en una escena: se agrega como cualquier otra, con tipo "
            "«Gráfico animado» (ba_grafico). `grafico_en_escena` las lista con su id.\n\n"
            "Los cuadros se dibujan TODOS de antemano y después se reproducen, así que el primer "
            "disparo tarda un poco (medio segundo para una animación corta) y a partir de ahí no hay "
            "nada que pueda trabarse. Por eso hay tope de duración: más de unos segundos no es un "
            "gráfico animado, es un video.\n\n"
            "Sin `repetir`, el último cuadro queda fijo — que es lo que quiere un zócalo que entra y "
            "se sostiene. Con `repetir`, vuelve a empezar (un ticker).";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"fuente", {{"type", "integer"}, {"description", "Id de la fuente (grafico_en_escena)."}}},
              {"nombre", {{"type", "string"}, {"description", "Qué gráfico mostrar."}}},
              {"parametros", {{"type", "object"}}},
              {"duracionMs",
               {{"type", "integer"}, {"minimum", 100}, {"maximum", 15000}, {"default", 1500},
                {"description", "Cuánta animación dibujar."}}},
              {"fps", {{"type", "integer"}, {"minimum", 10}, {"maximum", 60}, {"default", 30}}},
              {"repetir", {{"type", "boolean"}, {"default", false}}}}},
            {"required", {"fuente", "nombre"}}};
        t.run = [&rt, proc](const json& a, Done done) {
            if (!proc->disponible())
                return done(ToolResult::error("El renderizador de gráficos no está en este bundle."));
            const auto id = static_cast<std::uint64_t>(a["fuente"].get<long long>());
            int ancho = 900;
            int alto = 220;
            bool existe = false;
            for (const auto& i : graficos::instancias())
                if (i.id == id) {
                    ancho = i.ancho;
                    alto = i.alto;
                    existe = true;
                }
            if (!existe)
                return done(ToolResult::error(
                    "No hay ninguna fuente de gráfico con ese id (grafico_en_escena las lista)."));

            QString err;
            const QString carpeta =
                carpetaDe(rt, QString::fromStdString(a["nombre"].get<std::string>()), &err);
            if (carpeta.isEmpty()) return done(ToolResult::error(err.toStdString()));
            QJsonObject par;
            if (a.contains("parametros"))
                par = QJsonDocument::fromJson(QByteArray::fromStdString(a["parametros"].dump()))
                          .object();
            QJsonObject pedido = pedidoDe(carpeta, par, &err);
            if (pedido.isEmpty()) return done(ToolResult::error(err.toStdString()));
            pedido["ancho"] = ancho;
            pedido["alto"] = alto;

            const int fps = std::clamp(a.value("fps", 30), 10, 60);
            const int dur = std::clamp(a.value("duracionMs", 1500), 100, graficos::kMaxSegundos * 1000);
            // Se piden los cuadros de a tandas: el protocolo del renderizador
            // acota cuantos entran en una respuesta.
            std::vector<QImage> todos;
            const int total = std::max(1, dur * fps / 1000);
            for (int desde = 0; desde < total;) {
                QVector<int> momentos;
                for (int k = 0; k < 12 && desde < total; ++k, ++desde)
                    momentos.append(desde * 1000 / fps);
                const auto parte = proc->tira(pedido, momentos, &err);
                if (!err.isEmpty()) return done(ToolResult::error(err.toStdString()));
                for (const auto& c : parte)
                    todos.push_back(c.imagen);
            }
            if (!graficos::reproducir(id, std::move(todos), fps, a.value("repetir", false)))
                return done(ToolResult::error("no se pudo reproducir en esa fuente"));
            done(ToolResult::ok("«" + a["nombre"].get<std::string>() + "» al aire (" +
                                std::to_string(total) + " cuadros)."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "grafico_ocultar";
        t.title = "Sacar un gráfico";
        t.category = "graficos";
        t.description = "Deja la fuente transparente.";
        t.details = "No borra la fuente de la escena: la deja vacía. Volver a mostrarla es "
                    "grafico_mostrar de nuevo.";
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"fuente", {{"type", "integer"}}}}},
                         {"required", {"fuente"}}};
        t.run = [](const json& a, Done done) {
            const auto id = static_cast<std::uint64_t>(a["fuente"].get<long long>());
            if (!graficos::ocultar(id))
                return done(ToolResult::error("No hay ninguna fuente de gráfico con ese id."));
            done(ToolResult::ok("Gráfico oculto."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "grafico_en_escena";
        t.title = "Fuentes de gráfico que hay";
        t.category = "graficos";
        t.readOnly = true;
        t.description = "Las fuentes «Gráfico animado» puestas en escenas, con su id y su tamaño.";
        t.details = "El `id` es lo que piden grafico_mostrar y grafico_ocultar. Si está vacío, hay "
                    "que agregar una fuente de tipo ba_grafico a alguna escena primero.";
        t.run = [](const json&, Done done) {
            json out = json::array();
            for (const auto& i : graficos::instancias())
                out.push_back(json{{"id", i.id},
                                   {"fuente", i.nombre},
                                   {"grafico", i.grafico},
                                   {"ancho", i.ancho},
                                   {"alto", i.alto},
                                   {"reproduciendo", i.reproduciendo},
                                   {"cuadros", i.cuadros}});
            done(ToolResult().data(json{{"fuentes", out}}));
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
