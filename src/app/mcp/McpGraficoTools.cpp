// ============================================================================
// src/app/mcp/McpGraficoTools.cpp — gráficos animados, para el asistente
//
// El bucle que hace que valga la pena: escribir, validar, mirar una TIRA de
// cuadros, corregir. Sin ese bucle el asistente escribe a ciegas y el
// operador se entera al aire.
//
// Dónde están los gráficos, cómo se leen y cómo se arma su pedido lo sabe
// graficos::Catalogo (el del Emisor): el mismo que usan el editor y el aire.
// ============================================================================
#include <algorithm>
#include <memory>
#include <set>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "arranque/AppRuntime.hpp"
#include "graficos/Emisor.hpp"
#include "graficos/GraficoProceso.hpp"
#include "graficos/GraficoSource.hpp"
#include "graficos/Medios.hpp"
#include "graficos/Vinculos.hpp"
#include "graficos/VideoEnGrafico.hpp"
#include "escenas/Conmutador.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "i18n/I18n.hpp"
#include "emision/VentanillaMotor.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {
namespace {

/// De Qt a json (lo que devuelve el catálogo).
json aJsonGrafico(const QJsonArray& a) {
    return json::parse(QJsonDocument(a).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
}
json aJsonObj(const QJsonObject& o) {
    return json::parse(QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
}
/// De json a Qt (los argumentos de una herramienta).
QJsonObject aQObj(const json& j) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)))
        .object();
}

} // namespace

void McpService::registerGraficoTools() {
    core_.addCategory("graficos", ba::i18n::traducir("mcp", "Gráficos animados"),
                      ba::i18n::traducir("mcp", "Zócalos, placas y tickers: escribirlos, verlos y corregirlos."));

    // El MISMO emisor que usan la interfaz, el guion y el rotulador. Tenia el
    // suyo propio —otro `ba-grafico` y otra memoria de lo ultimo que salio al
    // aire— y eso hacia justo lo que no puede pasar: el asistente sacaba un
    // grafico al aire y la tarjeta de Rotulos seguia mostrando lo anterior.
    auto* em = vm_.emisor();
    auto* proc = em != nullptr ? &em->proceso() : nullptr;

    {
        Tool t;
        t.name = "grafico_listar";
        t.title = ba::i18n::traducir("mcp", "Gráficos que hay");
        t.category = "graficos";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Los gráficos disponibles, con los parámetros que acepta cada uno.");
        t.details =
            QT_TRANSLATE_NOOP("mcp", "Un gráfico es una carpeta con `grafico.json` (qué parámetros acepta) y, o bien una "
            "descripción declarativa adentro de ese mismo archivo, o bien un `grafico.qml` escrito a "
            "mano.\n\n"
            "Antes de inventar uno nuevo, mirá si alguno de estos sirve cambiándole los parámetros: "
            "casi siempre alcanza.\n\n"
            "`acciones` son los botones de pedido ya escrito que ese gráfico le ofrece al operador en "
            "la pantalla de Rótulos. Cuando te llegue uno de esos pedidos vas a recibirlo con el "
            "contexto puesto (escena, fuente y gráfico). Si un gráfico no tiene ninguno, proponelos: "
            "se escriben en su `grafico.json` y estan explicados en docs/funciones/GRAFICOS.md.\n\n"
            "`plugin` no vacío: lo dibuja ese plugin de Python (help {\"topic\": \"graficos\"}, «Gráficos de un "
            "plugin»); se usa igual que los demás. Un parámetro de tipo `datos` lleva los datos de una torta o "
            "unas barras (lista [{etiqueta, valor, color}], objeto {\"Sí\": 3} o texto «Sí: 3; No: 2»).");
        t.run = [em, proc](const json&, Done done) {
            if (em == nullptr) return done(ToolResult::error(ba::i18n::traducir("mcp", "No hay motor de gráficos.")));
            json out = aJsonGrafico(em->catalogo().lista());
            done(ToolResult().data(json{{"graficos", out}, {"motorDisponible", proc->disponible()}}));
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "grafico_probar";
        t.title = ba::i18n::traducir("mcp", "Ver cómo queda un gráfico");
        t.category = "graficos";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Dibuja el gráfico en varios instantes de su animación y devuelve las imágenes. "
                        "No sale al aire.");
        t.details =
            QT_TRANSLATE_NOOP("mcp", "ESTA es la herramienta que hay que usar antes de dar un gráfico por bueno. Una animación "
            "NO se puede juzgar mirando un cuadro: hay que ver la entrada a medio camino, el momento "
            "en que se asienta y, si tiene salida, cómo se va.\n\n"
            "`momentos` en milisegundos desde que el gráfico aparece. Para una entrada de 450 ms, algo "
            "como [0, 150, 300, 450, 900] muestra lo que importa. Hasta 12.\n\n"
            "Mirá las imágenes de verdad: ahí se ve si el texto entra tarde, si la caja tapa el nombre, "
            "si un nombre largo se sale, o si el color no se lee sobre el video.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"nombre", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "Cuál gráfico (ver grafico_listar).")}}},
              {"parametros", {{"type", "object"}, {"description", QT_TRANSLATE_NOOP("mcp", "Los valores a probar.")}}},
              {"momentos", {{"type", "array"}, {"items", {{"type", "integer"}, {"minimum", 0}}}, {"description", QT_TRANSLATE_NOOP("mcp", "instantes de la animación, en milisegundos desde que entra")}}}}},
            {"required", {"nombre"}}};
        t.examples = {
            R"({"nombre": "zocalo", "parametros": {"nombre": "Jorge Ortega", "cargo": "Intendente"}, "momentos": [0, 150, 300, 450, 900]})"};
        t.run = [em, proc](const json& a, Done done) {
            if (em == nullptr) return done(ToolResult::error(ba::i18n::traducir("mcp", "No hay motor de gráficos.")));
            if (!proc->disponible())
                return done(ToolResult::error(ba::i18n::llenar(
                    ba::i18n::traducir("mcp", "El renderizador de gráficos no está en este bundle (%1)."),
                    {proc->binario().toStdString()})));
            const QJsonObject par = a.contains("parametros") ? aQObj(a["parametros"]) : QJsonObject{};
            QVector<int> momentos;
            for (const auto& m : a.value("momentos", json::array()))
                momentos.append(m.get<int>());
            if (momentos.isEmpty()) momentos = {0, 150, 300, 450, 900};
            // El mismo pedido que sale al aire (Emisor::resolver): lo que se prueba es lo que
            // se ve, también si lo dibuja un plugin (que contesta después).
            em->resolver(QString::fromStdString(a.value("nombre", std::string())), par, {},
                         [proc, momentos, a, done](QJsonObject pedido, QString err) {
                             if (!err.isEmpty()) return done(ToolResult::error(err.toStdString()));
                             const auto cuadros = proc->tira(pedido, momentos, &err);
                             if (!err.isEmpty()) return done(ToolResult::error(err.toStdString()));
                             ToolResult r;
                             for (const auto& c : cuadros) {
                                 r.text(std::to_string(c.ms) + " ms");
                                 addImage(r, c.imagen, a, 1200);
                             }
                             done(r);
                         });
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "grafico_mostrar";
        t.porSalida = true; // `armar`: en el preview de esa salida
        t.title = ba::i18n::traducir("mcp", "Poner un gráfico al aire");
        t.category = "graficos";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Dibuja el gráfico y lo reproduce en una fuente «Gráfico animado» de una escena. "
                        "ESTO SE VE AL AIRE si esa escena está en el programa.");
        t.details =
            QT_TRANSLATE_NOOP("mcp", "Antes de usar esto hay que haber mirado el gráfico con grafico_probar. Una vez que sale, "
            "lo ve el público.\n\n"
            "La fuente tiene que existir en una escena: se agrega como cualquier otra, con tipo "
            "«Gráfico animado» (ba_grafico). `grafico_en_escena` las lista con su id.\n\n"
            "Se dibuja en otro proceso y sale al aire con los primeros cuadros; el resto llega andando.\n\n"
            "CUÁNTO QUEDA EN PANTALLA lo dice el parámetro del gráfico `salidaEn` («Cuándo se va», en "
            "milisegundos, dentro de `parametros`): a ese momento arranca la animación de salida y el "
            "gráfico se va solo (una placa de apertura: 4000-5000). Con 0, o si el gráfico no lo tiene, "
            "queda puesto hasta grafico_ocultar (un zócalo que se sostiene). Lo quieto entre la entrada "
            "y la salida no se dibuja: puede quedarse 60 s sin costo. `duracionMs` NO hace falta "
            "(solo cuenta para un gráfico sin `salidaEn`: cuánta animación dibujar).\n\n"
            "EN EL GUION es distinto: una cue de gráfico dice `seVa` (al-avanzar: títulos y placas se "
            "van solos cuando el guion pasa a otra cue; sola; queda) y si no lo dice manda la ficha del "
            "gráfico (`seVa`). Para eventos con guion, preferí cues a grafico_mostrar.\n\n"
            "Sin `repetir`, el último cuadro queda fijo — que es lo que quiere un zócalo que entra y "
            "se sostiene. Con `repetir`, vuelve a empezar (un ticker).\n\n"
            "VIDEO EN VIVO ADENTRO (la cámara del orador en el zócalo, «Zócalo con cámara»): un gráfico "
            "que tiene un parámetro de tipo «fuente» lleva una fuente de video de la app que entra, queda y "
            "se va con él. Decila con `video` (el nombre de la fuente como lo da scene_list, p. ej. «Cámara 1») o en `parametros`. "
            "Si esa fuente ya está en la escena del gráfico se anima esa y al terminar vuelve a su lugar; si "
            "no, se suma sola mientras dure el gráfico.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"fuente", {{"type", "integer"}, {"description", QT_TRANSLATE_NOOP("mcp", "Id de la fuente (grafico_en_escena).")}}},
              {"nombre", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "Qué gráfico mostrar.")}}},
              {"parametros",
               {{"type", "object"},
                {"description", QT_TRANSLATE_NOOP("mcp", "Los campos del gráfico (grafico_listar los dice), incluido `salidaEn` (ms: cuándo se va; 0 = se queda).")}}},
              {"duracionMs",
               {{"type", "integer"},
                {"minimum", 100},
                {"maximum", 15000},
                {"description", QT_TRANSLATE_NOOP("mcp", "Solo para un gráfico SIN `salidaEn`: cuánta animación dibujar (si no, lo de su ficha).")}}},
              {"fps", {{"type", "integer"}, {"minimum", 10}, {"maximum", 60}, {"default", 30}}},
              {"repetir", {{"type", "boolean"}, {"default", false}}},
              {"video",
               {{"type", "string"},
                {"description", QT_TRANSLATE_NOOP("mcp", "La fuente de video que va adentro del gráfico (como en scene_list): llena sus "
                                "parámetros de tipo «fuente». Solo para gráficos con video en vivo.")}}},
              {"armar",
               {{"type", "boolean"},
                {"default", false},
                {"description", QT_TRANSLATE_NOOP("mcp", "Armarlo en el PREVIEW en vez de al aire: lo dibuja en una escena que no está al "
                                "aire y la deja armada; sale con TAKE (preview_al_aire).")}}}}},
            {"required", {"fuente", "nombre"}}};
        t.run = [em, this](const json& a, Done done) {
            if (em == nullptr || !em->disponible())
                return done(ToolResult::error(ba::i18n::traducir("mcp", "El renderizador de gráficos no está en este bundle.")));
            const auto id = static_cast<std::uint64_t>(a["fuente"].get<long long>());
            // Armar: la escena que tiene esa fuente y NO está al aire.
            std::string escenaArmar;
            if (a.value("armar", false)) {
                std::string nombreFuente;
                for (const auto& i : graficos::instancias())
                    if (i.id == id) nombreFuente = i.nombre;
                if (nombreFuente.empty()) return done(ToolResult::error(ba::i18n::traducir("mcp", "No hay una fuente de gráfico con ese id (grafico_en_escena).")));
                // La escena al aire de una salida solo frena si esa salida sale (transmite o
                // graba); la fuente es de todas, así que cuenta cualquier salida.
                // (De la foto: corre en la interfaz y la lee como las vistas.)
                const FotoMotor& foto = vm_.ventanilla().foto();
                std::set<std::string> alAireQueSale;
                for (const FotoMotor::Salida& sal : foto.salidas)
                    if (sal.sale) alAireQueSale.insert(sal.alAire);
                bool alAire = false;
                for (const FotoMotor::Escena& e : foto.escenas) {
                    const std::string& sc = e.nombre;
                    const auto ss = foto.fuentesDe(sc);
                    if (std::find(ss.begin(), ss.end(), nombreFuente) == ss.end()) continue;
                    if (alAireQueSale.count(sc) > 0) alAire = true;
                    else if (escenaArmar.empty()) escenaArmar = sc;
                }
                if (alAire)
                    return done(ToolResult::error(ba::i18n::llenar(
                        ba::i18n::traducir("mcp", "«%1» está en la escena al aire: armarlo se vería. Usá una "
                                                  "fuente de gráfico de otra escena, o mostralo sin armar."),
                        {nombreFuente})));
                if (escenaArmar.empty())
                    return done(ToolResult::error(
                        ba::i18n::llenar(ba::i18n::traducir("mcp", "«%1» no está en ninguna escena."), {nombreFuente})));
            }
            QJsonObject par;
            if (a.contains("parametros"))
                par = QJsonDocument::fromJson(QByteArray::fromStdString(a["parametros"].dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object();
            // El video de adentro: a sus parámetros de tipo «fuente».
            if (const std::string video = a.value("video", std::string()); !video.empty()) {
                const QStringList claves =
                    graficos::video::parametrosDeFuente(em->ficha(QString::fromStdString(a["nombre"].get<std::string>())));
                if (claves.isEmpty())
                    return done(ToolResult::error(ba::i18n::llenar(
                        ba::i18n::traducir("mcp", "«%1» no lleva video en vivo (no tiene un parámetro de tipo «fuente»)."),
                        {a["nombre"].get<std::string>()})));
                if (vm_.ventanilla().foto().tipoDe(video).empty())
                    return done(ToolResult::error(ba::i18n::llenar(
                        ba::i18n::traducir("mcp", "No hay ninguna fuente «%1» (scene_list las dice)."), {video})));
                for (const QString& k : claves) par[k] = QString::fromStdString(video);
            }
            // Por el emisor, no por abajo: asi queda anotado con que valores
            // salio esta fuente y la tarjeta de Rotulos muestra lo que hay al
            // aire en vez de lo anterior.
            const QString err = em->mostrar(QString::fromStdString(a["nombre"].get<std::string>()), par, id,
                                            a.value("duracionMs", 0), a.value("repetir", false));
            if (!err.isEmpty()) return done(ToolResult::error(err.toStdString()));
            if (!escenaArmar.empty()) {
                conmutadorDe(core_, vm_, a).armar(escenaArmar); // en el preview de su salida
                return done(ToolResult::ok(
                    ba::i18n::llenar(ba::i18n::traducir("mcp", "«%1» armado en el preview («%2»): sale con TAKE."),
                                     {a["nombre"].get<std::string>(), escenaArmar})));
            }
            done(ToolResult::ok(
                ba::i18n::llenar(ba::i18n::traducir("mcp", "«%1» al aire."), {a["nombre"].get<std::string>()})));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "grafico_ocultar";
        t.title = ba::i18n::traducir("mcp", "Sacar un gráfico");
        t.category = "graficos";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Saca del aire un gráfico animado: deja la fuente (`fuente`, el id de grafico_en_escena) transparente, sin "
                                                 "borrarla de la escena.");
        t.details = QT_TRANSLATE_NOOP("mcp", "No borra la fuente de la escena: la deja vacía. Volver a mostrarla es "
                    "grafico_mostrar de nuevo.");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"fuente", {{"type", "integer"}}}}},
                         {"required", {"fuente"}}};
        t.run = [em](const json& a, Done done) {
            if (em == nullptr) return done(ToolResult::error(ba::i18n::traducir("mcp", "No hay motor de gráficos.")));
            const auto id = static_cast<std::uint64_t>(a["fuente"].get<long long>());
            const QString err = em->ocultar(id);
            if (!err.isEmpty()) return done(ToolResult::error(err.toStdString()));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Gráfico oculto.")));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "grafico_en_escena";
        t.title = ba::i18n::traducir("mcp", "Fuentes de gráfico que hay");
        t.category = "graficos";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Las fuentes «Gráfico animado» puestas en escenas, con su id y su tamaño.");
        t.details = QT_TRANSLATE_NOOP("mcp", "El `id` es lo que piden grafico_mostrar y grafico_ocultar. Si está vacío, hay "
                    "que agregar una fuente de tipo ba_grafico a alguna escena primero.");
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
        t.name = "grafico_actualizar";
        t.title = ba::i18n::traducir("mcp", "Cambiar los valores de un gráfico al aire");
        t.category = "graficos";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Le cambia los valores al gráfico que ya está en pantalla en una fuente SIN hacerlo entrar de "
                        "nuevo. Con tortas y barras, los valores se mueven de los de antes a los nuevos (una votación "
                        "en vivo).");
        t.details =
            QT_TRANSLATE_NOOP("mcp", "Para lo que cambia mientras el gráfico está puesto: un conteo que avanza, un resultado que se completa. "
            "Mandá solo lo que cambia en `parametros`: lo demás queda como estaba en esa fuente.\n\n"
            "Si el gráfico sabe animar un cambio (tiene una torta, unas barras, un parámetro de tipo `datos`, o "
            "`\"cambio\"` en su ficha), se dibuja la transición (`cambio.duracion` de la ficha, 700 ms de fábrica). Si "
            "no, se reemplaza su cuadro quieto. Si en esa fuente no está ese gráfico, lo saca al aire de cero "
            "(como grafico_mostrar). Un cambio deja el gráfico puesto: no programa la salida.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"fuente", {{"type", "integer"}, {"description", QT_TRANSLATE_NOOP("mcp", "Id de la fuente (grafico_en_escena).")}}},
              {"nombre", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "Qué gráfico (si no, el que ya está en esa fuente).")}}},
              {"parametros", {{"type", "object"}, {"description", QT_TRANSLATE_NOOP("mcp", "Solo lo que cambia.")}}}}},
            {"required", {"fuente", "parametros"}}};
        t.examples = {
            R"({"fuente": 12, "parametros": {"votos": [{"etiqueta": "Sí", "valor": 21}, {"etiqueta": "No", "valor": 9}]}})"};
        t.run = [em](const json& a, Done done) {
            if (em == nullptr || !em->disponible())
                return done(ToolResult::error(ba::i18n::traducir("mcp", "El renderizador de gráficos no está en este bundle.")));
            const auto id = static_cast<std::uint64_t>(a["fuente"].get<long long>());
            QString nombre = QString::fromStdString(a.value("nombre", std::string()));
            if (nombre.isEmpty()) nombre = em->ultimoGrafico(id);
            if (nombre.isEmpty())
                return done(ToolResult::error(ba::i18n::traducir("mcp", "Esa fuente no tiene ningún gráfico puesto: decí cuál (`nombre`).")));
            // Lo nuevo encima de lo que ya tenía (si es el mismo gráfico).
            QJsonObject valores = em->ultimoGrafico(id) == nombre ? em->ultimosValores(id) : QJsonObject{};
            const QJsonObject nuevos = aQObj(a.value("parametros", json::object()));
            for (auto it = nuevos.begin(); it != nuevos.end(); ++it) valores[it.key()] = it.value();
            bool enPantalla = false;
            for (const auto& i : graficos::instancias())
                if (i.id == id) enPantalla = i.reproduciendo;
            const QString err = enPantalla && em->ultimoGrafico(id) == nombre ? em->actualizar(nombre, valores, id)
                                                                             : em->mostrar(nombre, valores, id);
            if (!err.isEmpty()) return done(ToolResult::error(err.toStdString()));
            done(ToolResult::ok(ba::i18n::llenar(ba::i18n::traducir("mcp", "«%1» actualizado."), {nombre.toStdString()})));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "grafico_medios";
        t.title = ba::i18n::traducir("mcp", "Medios de un gráfico");
        t.category = "graficos";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Los GIF, imágenes y videos (ya pasados a cuadros) que tiene un gráfico en su carpeta medios/.");
        t.details = QT_TRANSLATE_NOOP("mcp", "`archivo` es lo que va en la capa `medio` del declarativo (o en `Medio { source }` de un QML). "
                    "`alfa: false` en un video: salió opaco (va a tapar lo de atrás).");
        t.inputSchema = {{"type", "object"},
                         {"properties", {{"nombre", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "Qué gráfico.")}}}}},
                         {"required", {"nombre"}}};
        t.run = [em](const json& a, Done done) {
            if (em == nullptr) return done(ToolResult::error(ba::i18n::traducir("mcp", "No hay motor de gráficos.")));
            const QString nombre = QString::fromStdString(a.value("nombre", std::string()));
            QString err;
            if (em->catalogo().carpeta(nombre, &err).isEmpty()) return done(ToolResult::error(err.toStdString()));
            done(ToolResult().data(json{{"medios", aJsonGrafico(em->medios().lista(nombre))},
                                        {"importando", em->medios().importando()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "grafico_medio_agregar";
        t.title = ba::i18n::traducir("mcp", "Agregar un GIF o un video con alfa a un gráfico");
        t.category = "graficos";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Copia un GIF, una imagen o una carpeta de PNG al gráfico, o convierte un video (WebM VP9 con "
                        "alfa, ProRes 4444, MP4…) a cuadros con su transparencia. Después va en una capa `medio`.");
        t.details =
            QT_TRANSLATE_NOOP("mcp", "Queda en la carpeta del gráfico (`medios/`), así que viaja con él. Un video se pasa UNA vez a cuadros PNG "
            "(con GStreamer) y en el gráfico se lee cuadro por cuadro con el reloj del gráfico: la vista previa, "
            "grafico_probar y el aire muestran el mismo cuadro en el mismo instante. Puede tardar (un clip de 10 s a "
            "1080p son varios segundos): la respuesta llega al terminar.\n\n"
            "Mirá `alfa` en la respuesta: false = el video no trae transparencia (o el decodificador no la vio) y va "
            "a tapar todo lo de atrás. `ancho` achica los cuadros (un elemento chico no necesita 1920). "
            "`maxSegundos` (20 de fábrica) corta un video largo: un gráfico dura segundos.\n\n"
            "Después, en la descripción: {\"tipo\": \"medio\", \"archivo\": \"<archivo de la respuesta>\", \"ancho\": …, "
            "\"alto\": …, \"inicio\": ms, \"bucle\": true|false} (grafico_repertorio).");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"nombre", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "A qué gráfico.")}}},
              {"archivo", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "Ruta del GIF, imagen, carpeta de PNG o video.")}}},
              {"como", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "Cómo se llama adentro (si no, el del archivo).")}}},
              {"ancho", {{"type", "integer"}, {"minimum", 16}, {"description", QT_TRANSLATE_NOOP("mcp", "Achicar los cuadros de un video a este ancho.")}}},
              {"maxSegundos", {{"type", "integer"}, {"minimum", 1}, {"maximum", 120}}}}},
            {"required", {"nombre", "archivo"}}};
        t.run = [em](const json& a, Done done) {
            if (em == nullptr) return done(ToolResult::error(ba::i18n::traducir("mcp", "No hay motor de gráficos.")));
            QJsonObject op;
            if (a.contains("como")) op["nombre"] = QString::fromStdString(a["como"].get<std::string>());
            if (a.contains("ancho")) op["ancho"] = a["ancho"].get<int>();
            if (a.contains("maxSegundos")) op["maxSegundos"] = a["maxSegundos"].get<int>();
            em->medios().importar(QString::fromStdString(a.value("nombre", std::string())),
                                  QString::fromStdString(a.value("archivo", std::string())), op,
                                  [done](QJsonObject r, QString err) {
                                      if (!err.isEmpty()) return done(ToolResult::error(err.toStdString()));
                                      done(ToolResult().data(aJsonObj(r)));
                                  });
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "grafico_medio_quitar";
        t.title = ba::i18n::traducir("mcp", "Sacar un medio de un gráfico");
        t.category = "graficos";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Saca un GIF o un video de la carpeta medios/ del gráfico (va a su papelera, medios/.papelera).");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"nombre", {{"type", "string"}}},
                           {"archivo", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "El `archivo` de grafico_medios (medios/…).")}}}}},
                         {"required", {"nombre", "archivo"}}};
        t.run = [em](const json& a, Done done) {
            if (em == nullptr) return done(ToolResult::error(ba::i18n::traducir("mcp", "No hay motor de gráficos.")));
            const QString err = em->medios().quitar(QString::fromStdString(a.value("nombre", std::string())),
                                                    QString::fromStdString(a.value("archivo", std::string())));
            if (!err.isEmpty()) return done(ToolResult::error(err.toStdString()));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Medio quitado.")));
        };
        core_.addTool(std::move(t));
    }

    // --- Vinculos: que el rotulo se escriba solo ----------------------------
    auto* vin = vm_.vinculos();
    const auto aJson = [](const QJsonDocument& d) {
        return json::parse(d.toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
    };
    const auto aQJson = [](const json& j) {
        return QJsonDocument::fromJson(QByteArray::fromStdString(j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object();
    };

    {
        Tool t;
        t.name = "rotulo_vinculos";
        t.title = ba::i18n::traducir("mcp", "Rotulos que se escriben solos");
        t.category = "graficos";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Los vinculos que hay y, sobre todo, los ORIGENES que se pueden usar.");
        t.details = QT_TRANSLATE_NOOP("mcp", "Un vinculo dice de donde sale cada campo de un grafico, una sola vez, y despues se "
                    "mantiene solo: cuando lo que resuelve cambia (cambio el orador, cambio la escena), el "
                    "grafico vuelve a salir al aire con lo nuevo.\n\n"
                    "`origenes` es lo que se puede poner entre llaves, CON SU VALOR DE AHORA. Miralo antes "
                    "de escribir un vinculo: si el origen que ibas a usar no esta en esa lista, no existe, y "
                    "el vinculo no se va a guardar.\n\n"
                    "De cada vinculo, `resuelve` es lo que daria si se disparara ahora mismo y `problema` "
                    "dice por que no puede. `fuenteExiste` en false significa que esa fuente de escena no "
                    "esta en ninguna escena en este momento: el vinculo se queda quieto hasta que aparezca.");
        t.run = [vin, aJson](const json&, Done done) {
            if (vin == nullptr) return done(ToolResult::error(ba::i18n::traducir("mcp", "No hay motor de graficos.")));
            done(ToolResult().data(json{{"vinculos", aJson(QJsonDocument(vin->lista()))},
                                        {"origenes", aJson(QJsonDocument(vin->origenes()))}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "rotulo_vincular";
        t.title = ba::i18n::traducir("mcp", "Que un rotulo se escriba solo");
        t.category = "graficos";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Pone el nombre y el cargo de quien habla en pantalla, y lo mantiene al dia solo. "
                        "Ata los campos de un grafico a origenes que cambian solos.");
        t.details =
            QT_TRANSLATE_NOOP("mcp", "Ejemplo, el caso tipico —el zocalo del que esta hablando—:\n\n"
            "    { \"fuente\": \"Zocalo\", \"grafico\": \"zocalo\",\n"
            "      \"valores\": { \"nombre\": \"{orador.nombre}\", \"cargo\": \"{orador.cargo}\" } }\n\n"
            "Lo que hay que saber:\n\n"
            "  * La FUENTE se nombra, no se numera: el id interno de una fuente de escena "
            "cambia cada vez que se abre el programa. El nombre no.\n"
            "  * Un valor es TEXTO con origenes entre llaves, asi que se pueden mezclar: "
            "\"{orador.nombre} - {escena}\" o \"Nos escribe {invitado2.nombre}\".\n"
            "  * Un origen que no existe hace fallar el vinculo ACA, al guardarlo, y no en vivo. "
            "Mira primero rotulo_vinculos.\n"
            "  * Los campos que no menciones quedan en el valor por defecto del grafico.\n"
            "  * `dispara` en false lo deja resolviendo pero sin sacar nada al aire: sirve para "
            "dejarlo escrito y probarlo antes de un evento.\n"
            "  * Mandar el mismo `id` reemplaza el vinculo entero, que es como se corrige uno.\n\n"
            "No se redispara si lo que resuelve no cambio: si no, la animacion parpadearia una vez "
            "por segundo.\n\n"
            "CAMPOS VIVOS: con `vivo: true`, cuando cambia lo que resuelve y el grafico ya esta al "
            "aire en esa fuente, se vuelve a dibujar SOLO su cuadro estable (sin la entrada): para "
            "un reloj con segundos ({hora_s}) o el cronometro del orador ({orador.cronometro}, "
            "{orador.restante}: lo que le queda, o «+m:ss» pasado).");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"fuente",
               {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "Nombre de la fuente «Grafico animado» de la escena.")}}},
              {"grafico", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "Cual de los graficos guardados.")}}},
              {"valores",
               {{"type", "object"}, {"description", QT_TRANSLATE_NOOP("mcp", "clave del parametro -> texto con {origenes}.")}}},
              {"dispara", {{"type", "boolean"}, {"description", QT_TRANSLATE_NOOP("mcp", "Si sale al aire solo. Por defecto si.")}}},
              {"repetir", {{"type", "boolean"}, {"description", QT_TRANSLATE_NOOP("mcp", "En bucle (para un ticker).")}}},
              {"vivo",
               {{"type", "boolean"},
                {"description", QT_TRANSLATE_NOOP("mcp", "Campos que cambian cada segundo ({hora_s}, {orador.cronometro}, {orador.restante}): "
                                "con el gráfico al aire se actualiza sin volver a entrar.")}}},
              {"id", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "Para reemplazar uno que ya existe.")}}}}},
            {"required", {"fuente", "grafico", "valores"}}};
        t.run = [vin, aQJson](const json& a, Done done) {
            if (vin == nullptr) return done(ToolResult::error(ba::i18n::traducir("mcp", "No hay motor de graficos.")));
            QString id;
            const QString err = vin->poner(aQJson(a), &id);
            if (!err.isEmpty()) return done(ToolResult::error(err.toStdString()));
            done(ToolResult::ok(ba::i18n::llenar(
                ba::i18n::traducir("mcp", "Vinculo guardado (%1). Se va a mantener solo de ahora en mas."),
                {id.toStdString()})));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "rotulo_desvincular";
        t.title = ba::i18n::traducir("mcp", "Sacar o apagar un vinculo");
        t.category = "graficos";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Quita un vínculo de rótulo (`id`, de rotulo_vinculos) o, con `activo`, lo apaga o lo prende sin borrarlo.");
        t.details = QT_TRANSLATE_NOOP("mcp", "En vivo, APAGARLO es mas seguro que borrarlo: se vuelve a prender con una sola "
                    "cosa y no hay que acordarse de como estaba escrito. Al prenderlo otra vez sale "
                    "al aire con lo que haya en ese momento, aunque sea lo mismo de antes.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"id", {{"type", "string"}}},
              {"activo",
               {{"type", "boolean"}, {"description", QT_TRANSLATE_NOOP("mcp", "Presente = prende o apaga. Ausente = lo borra.")}}}}},
            {"required", {"id"}}};
        t.run = [vin](const json& a, Done done) {
            if (vin == nullptr) return done(ToolResult::error(ba::i18n::traducir("mcp", "No hay motor de graficos.")));
            const QString id = QString::fromStdString(a.value("id", std::string()));
            if (a.contains("activo")) {
                const bool act = a["activo"].get<bool>();
                if (!vin->activar(id, act)) return done(ToolResult::error(ba::i18n::traducir("mcp", "No existe ese vinculo.")));
                return done(ToolResult::ok(act ? ba::i18n::traducir("mcp", "Vinculo prendido.")
                                               : ba::i18n::traducir("mcp", "Vinculo apagado.")));
            }
            if (!vin->quitar(id)) return done(ToolResult::error(ba::i18n::traducir("mcp", "No existe ese vinculo.")));
            done(ToolResult::ok(ba::i18n::traducir("mcp", "Vinculo borrado.")));
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "grafico_repertorio";
        t.title = ba::i18n::traducir("mcp", "Qué se puede usar en un gráfico declarativo");
        t.category = "graficos";
        t.clase = Clase::Lectura;
        t.description = QT_TRANSLATE_NOOP("mcp", "Las capas, las formas de ubicarlas y los efectos de entrada y salida.");
        t.details = QT_TRANSLATE_NOOP("mcp", "Lo declara el propio motor, así que esto no se desincroniza del código.\n\n"
                    "Con este repertorio se arma un gráfico SIN escribir código: es lo que conviene hacer "
                    "siempre que alcance. Escribir QML a mano solo cuando el repertorio de verdad no dé, "
                    "porque ahí reaparecen las trampas (ver `help {\"topic\": \"graficos\"}`).");
        t.run = [proc](const json&, Done done) {
            if (!proc->disponible())
                return done(ToolResult::error(ba::i18n::traducir("mcp", "El renderizador de gráficos no está en este bundle.")));
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
