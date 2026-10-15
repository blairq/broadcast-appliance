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

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "arranque/AppRuntime.hpp"
#include "graficos/Emisor.hpp"
#include "graficos/GraficoProceso.hpp"
#include "graficos/GraficoSource.hpp"
#include "graficos/Vinculos.hpp"
#include "escenas/Conmutador.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {
namespace {

/// De Qt a json (lo que devuelve el catálogo).
json aJsonGrafico(const QJsonArray& a) {
    return json::parse(QJsonDocument(a).toJson(QJsonDocument::Compact).toStdString(), nullptr, false);
}

} // namespace

void McpService::registerGraficoTools() {
    core_.addCategory("graficos", "Gráficos animados",
                      "Zócalos, placas y tickers: escribirlos, verlos y corregirlos.");

    AppRuntime& rt = rt_;
    // El MISMO emisor que usan la interfaz, el guion y el rotulador. Tenia el
    // suyo propio —otro `ba-grafico` y otra memoria de lo ultimo que salio al
    // aire— y eso hacia justo lo que no puede pasar: el asistente sacaba un
    // grafico al aire y la tarjeta de Rotulos seguia mostrando lo anterior.
    auto* em = vm_.emisor();
    auto* proc = em != nullptr ? &em->proceso() : nullptr;

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
            "casi siempre alcanza.\n\n"
            "`acciones` son los botones de pedido ya escrito que ese gráfico le ofrece al operador en "
            "la pantalla de Rótulos. Cuando te llegue uno de esos pedidos vas a recibirlo con el "
            "contexto puesto (escena, fuente y gráfico). Si un gráfico no tiene ninguno, proponelos: "
            "se escriben en su `grafico.json` y estan explicados en docs/funciones/GRAFICOS.md.";
        t.run = [em, proc](const json&, Done done) {
            if (em == nullptr) return done(ToolResult::error("No hay motor de gráficos."));
            json out = aJsonGrafico(em->catalogo().lista());
            done(ToolResult().data(json{{"graficos", out}, {"motorDisponible", proc->disponible()}}));
        };
        core_.addTool(std::move(t));
    }

    {
        Tool t;
        t.name = "grafico_probar";
        t.title = "Ver cómo queda un gráfico";
        t.category = "graficos";
        t.readOnly = true;
        t.description = "Dibuja el gráfico en varios instantes de su animación y devuelve las imágenes. "
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
              {"momentos", {{"type", "array"}, {"items", {{"type", "integer"}, {"minimum", 0}}}, {"description", "instantes de la animación, en milisegundos desde que entra"}}}}},
            {"required", {"nombre"}}};
        t.examples = {
            R"({"nombre": "zocalo", "parametros": {"nombre": "Jorge Ortega", "cargo": "Intendente"}, "momentos": [0, 150, 300, 450, 900]})"};
        t.run = [em, proc](const json& a, Done done) {
            if (em == nullptr) return done(ToolResult::error("No hay motor de gráficos."));
            if (!proc->disponible())
                return done(ToolResult::error("El renderizador de gráficos no está en este bundle (" +
                                              proc->binario().toStdString() + ")."));
            QString err;
            QJsonObject par;
            if (a.contains("parametros"))
                par = QJsonDocument::fromJson(QByteArray::fromStdString(a["parametros"].dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object();
            // El mismo pedido que sale al aire (Emisor): lo que se prueba es lo que se ve.
            const QJsonObject pedido =
                em->catalogo().pedido(QString::fromStdString(a.value("nombre", std::string())), par, &err);
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
        t.description = "Dibuja el gráfico y lo reproduce en una fuente «Gráfico animado» de una escena. "
                        "ESTO SE VE AL AIRE si esa escena está en el programa.";
        t.details =
            "Antes de usar esto hay que haber mirado el gráfico con grafico_probar. Una vez que sale, "
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
            "se sostiene. Con `repetir`, vuelve a empezar (un ticker).";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"fuente", {{"type", "integer"}, {"description", "Id de la fuente (grafico_en_escena)."}}},
              {"nombre", {{"type", "string"}, {"description", "Qué gráfico mostrar."}}},
              {"parametros",
               {{"type", "object"},
                {"description", "Los campos del gráfico (grafico_listar los dice), incluido `salidaEn` (ms: cuándo se va; 0 = se queda)."}}},
              {"duracionMs",
               {{"type", "integer"},
                {"minimum", 100},
                {"maximum", 15000},
                {"description", "Solo para un gráfico SIN `salidaEn`: cuánta animación dibujar (si no, lo de su ficha)."}}},
              {"fps", {{"type", "integer"}, {"minimum", 10}, {"maximum", 60}, {"default", 30}}},
              {"repetir", {{"type", "boolean"}, {"default", false}}},
              {"armar",
               {{"type", "boolean"},
                {"default", false},
                {"description", "Armarlo en el PREVIEW en vez de al aire: lo dibuja en una escena que no está al "
                                "aire y la deja armada; sale con TAKE (scene_take)."}}}}},
            {"required", {"fuente", "nombre"}}};
        t.run = [em, &rt, this](const json& a, Done done) {
            if (em == nullptr || !em->disponible())
                return done(ToolResult::error("El renderizador de gráficos no está en este bundle."));
            const auto id = static_cast<std::uint64_t>(a["fuente"].get<long long>());
            // Armar: la escena que tiene esa fuente y NO está al aire.
            std::string escenaArmar;
            if (a.value("armar", false)) {
                std::string nombreFuente;
                for (const auto& i : graficos::instancias())
                    if (i.id == id) nombreFuente = i.nombre;
                if (nombreFuente.empty()) return done(ToolResult::error("No hay una fuente de gráfico con ese id (grafico_en_escena)."));
                // La escena del programa solo frena si el programa sale (se transmite o graba).
                bool alAire = false;
                const bool sale = rt.engine().haySalida();
                for (const std::string& sc : rt.engine().sceneNames()) {
                    const auto ss = rt.engine().sceneSources(sc);
                    if (std::find(ss.begin(), ss.end(), nombreFuente) == ss.end()) continue;
                    if (sale && sc == rt.engine().currentScene()) alAire = true;
                    else if (escenaArmar.empty()) escenaArmar = sc;
                }
                if (alAire)
                    return done(ToolResult::error("«" + nombreFuente + "» está en la escena al aire: armarlo se vería. Usá una "
                                                  "fuente de gráfico de otra escena, o mostralo sin armar."));
                if (escenaArmar.empty())
                    return done(ToolResult::error("«" + nombreFuente + "» no está en ninguna escena."));
            }
            QJsonObject par;
            if (a.contains("parametros"))
                par = QJsonDocument::fromJson(QByteArray::fromStdString(a["parametros"].dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))).object();
            // Por el emisor, no por abajo: asi queda anotado con que valores
            // salio esta fuente y la tarjeta de Rotulos muestra lo que hay al
            // aire en vez de lo anterior.
            const QString err = em->mostrar(QString::fromStdString(a["nombre"].get<std::string>()), par, id,
                                            a.value("duracionMs", 0), a.value("repetir", false));
            if (!err.isEmpty()) return done(ToolResult::error(err.toStdString()));
            if (!escenaArmar.empty()) {
                vm_.conmutador().armar(escenaArmar);
                return done(ToolResult::ok("«" + a["nombre"].get<std::string>() + "» armado en el preview («" + escenaArmar +
                                           "»): sale con TAKE."));
            }
            done(ToolResult::ok("«" + a["nombre"].get<std::string>() + "» al aire."));
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
        t.run = [em](const json& a, Done done) {
            if (em == nullptr) return done(ToolResult::error("No hay motor de gráficos."));
            const auto id = static_cast<std::uint64_t>(a["fuente"].get<long long>());
            const QString err = em->ocultar(id);
            if (!err.isEmpty()) return done(ToolResult::error(err.toStdString()));
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
        t.title = "Rotulos que se escriben solos";
        t.category = "graficos";
        t.readOnly = true;
        t.description = "Los vinculos que hay y, sobre todo, los ORIGENES que se pueden usar.";
        t.details = "Un vinculo dice de donde sale cada campo de un grafico, una sola vez, y despues se "
                    "mantiene solo: cuando lo que resuelve cambia (cambio el orador, cambio la escena), el "
                    "grafico vuelve a salir al aire con lo nuevo.\n\n"
                    "`origenes` es lo que se puede poner entre llaves, CON SU VALOR DE AHORA. Miralo antes "
                    "de escribir un vinculo: si el origen que ibas a usar no esta en esa lista, no existe, y "
                    "el vinculo no se va a guardar.\n\n"
                    "De cada vinculo, `resuelve` es lo que daria si se disparara ahora mismo y `problema` "
                    "dice por que no puede. `fuenteExiste` en false significa que esa fuente de escena no "
                    "esta en ninguna escena en este momento: el vinculo se queda quieto hasta que aparezca.";
        t.run = [vin, aJson](const json&, Done done) {
            if (vin == nullptr) return done(ToolResult::error("No hay motor de graficos."));
            done(ToolResult().data(json{{"vinculos", aJson(QJsonDocument(vin->lista()))},
                                        {"origenes", aJson(QJsonDocument(vin->origenes()))}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "rotulo_vincular";
        t.title = "Que un rotulo se escriba solo";
        t.category = "graficos";
        t.description = "Pone el nombre y el cargo de quien habla en pantalla, y lo mantiene al dia solo. "
                        "Ata los campos de un grafico a origenes que cambian solos.";
        t.details =
            "Ejemplo, el caso tipico —el zocalo del que esta hablando—:\n\n"
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
            "{orador.restante}: lo que le queda, o «+m:ss» pasado).";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"fuente",
               {{"type", "string"}, {"description", "Nombre de la fuente «Grafico animado» de la escena."}}},
              {"grafico", {{"type", "string"}, {"description", "Cual de los graficos guardados."}}},
              {"valores",
               {{"type", "object"}, {"description", "clave del parametro -> texto con {origenes}."}}},
              {"dispara", {{"type", "boolean"}, {"description", "Si sale al aire solo. Por defecto si."}}},
              {"repetir", {{"type", "boolean"}, {"description", "En bucle (para un ticker)."}}},
              {"vivo",
               {{"type", "boolean"},
                {"description", "Campos que cambian cada segundo ({hora_s}, {orador.cronometro}, {orador.restante}): "
                                "con el gráfico al aire se actualiza sin volver a entrar."}}},
              {"id", {{"type", "string"}, {"description", "Para reemplazar uno que ya existe."}}}}},
            {"required", {"fuente", "grafico", "valores"}}};
        t.run = [vin, aQJson](const json& a, Done done) {
            if (vin == nullptr) return done(ToolResult::error("No hay motor de graficos."));
            QString id;
            const QString err = vin->poner(aQJson(a), &id);
            if (!err.isEmpty()) return done(ToolResult::error(err.toStdString()));
            done(ToolResult::ok("Vinculo guardado (" + id.toStdString() +
                                "). Se va a mantener solo de ahora en mas."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "rotulo_desvincular";
        t.title = "Sacar o apagar un vinculo";
        t.category = "graficos";
        t.description = "Quita un vinculo, o lo apaga sin borrarlo.";
        t.details = "En vivo, APAGARLO es mas seguro que borrarlo: se vuelve a prender con una sola "
                    "cosa y no hay que acordarse de como estaba escrito. Al prenderlo otra vez sale "
                    "al aire con lo que haya en ese momento, aunque sea lo mismo de antes.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"id", {{"type", "string"}}},
              {"activo",
               {{"type", "boolean"}, {"description", "Presente = prende o apaga. Ausente = lo borra."}}}}},
            {"required", {"id"}}};
        t.run = [vin](const json& a, Done done) {
            if (vin == nullptr) return done(ToolResult::error("No hay motor de graficos."));
            const QString id = QString::fromStdString(a.value("id", std::string()));
            if (a.contains("activo")) {
                const bool act = a["activo"].get<bool>();
                if (!vin->activar(id, act)) return done(ToolResult::error("No existe ese vinculo."));
                return done(ToolResult::ok(act ? "Vinculo prendido." : "Vinculo apagado."));
            }
            if (!vin->quitar(id)) return done(ToolResult::error("No existe ese vinculo."));
            done(ToolResult::ok("Vinculo borrado."));
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
        t.details = "Lo declara el propio motor, así que esto no se desincroniza del código.\n\n"
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
