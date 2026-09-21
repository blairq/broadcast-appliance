// ============================================================================
// src/app/mcp/McpEventoTools.cpp — el guion del evento, para el asistente
//
// Lo mismo que hace el operador en la pantalla del guion, pero para que lo
// pueda hacer la IA: listar y abrir eventos, armar bloques y cues, revisar que
// no falte nada y correr el guion.
//
// Por que existe: armar un guion de 40 cues a mano es el trabajo aburrido de
// un evento, y es justo lo que un asistente hace bien a partir de una
// descripcion ("acto por el 25 de mayo, 40 minutos, himno, dos discursos,
// video institucional, cierre"). Correrlo es otra cosa: eso lo decide el
// operador, y por eso `evento_correr` NO es de solo lectura.
//
// Cada herramienta trae en `details` para que sirve y como se usa, porque el
// asistente las descubre sin nadie que se las explique.
// ============================================================================
#include "mcp/McpEventoTools.hpp"

#include <algorithm>

#include <QJsonDocument>
#include <QJsonObject>

#include "mcp/McpCore.hpp"
#include "ui/EventoViewModel.hpp"

namespace ba::app::mcp {
namespace {

/// El guion completo tal como lo ve la interfaz, para que el asistente vea lo
/// mismo que el operador (incluido lo que le falta a cada cue).
json guionDe(EventoViewModel& ev) {
    const QVariantMap g = ev.guion();
    return json::parse(QString(QJsonDocument(QJsonObject::fromVariantMap(g)).toJson()).toStdString(),
                       nullptr, false);
}

QVariantMap aVariantMap(const json& j) {
    const QJsonDocument d = QJsonDocument::fromJson(QByteArray::fromStdString(j.dump()));
    return d.object().toVariantMap();
}

} // namespace

void registerEventoTools(McpCore& core, EventoViewModel& ev) {
    core.addCategory("evento", "Guion del evento",
                     "El storyboard: bloques, cues, tiempos previstos y el recorrido en vivo.");

    // --- Ver ------------------------------------------------------------------
    {
        Tool t;
        t.name = "evento_listar";
        t.title = "Eventos que hay";
        t.category = "evento";
        t.readOnly = true;
        t.description = "Los eventos guardados, con su duracion prevista y cuantos cues tiene cada uno.";
        t.details =
            "Un evento es el guion de lo que va a pasar: bloques (apertura, discurso, cierre) y dentro "
            "de cada uno los cues concretos (poner una escena, arrancar un tema, dar la palabra).\n\n"
            "Cada evento vive en su propia carpeta dentro de Eventos/, con su musica, sus videos y sus "
            "discursos adentro, para poder llevarselo entero a otra maquina.\n\n"
            "Usalo para saber sobre que evento trabajar antes de llamar a evento_abrir.";
        t.run = [&ev](const json&, Done done) {
            json out = json::array();
            for (const QVariant& v : ev.eventos()) {
                const QVariantMap m = v.toMap();
                out.push_back(json{{"carpeta", m["carpeta"].toString().toStdString()},
                                   {"nombre", m["nombre"].toString().toStdString()},
                                   {"inicio", m["inicio"].toString().toStdString()},
                                   {"duracionSeg", m["duracionSeg"].toInt()},
                                   {"bloques", m["bloques"].toInt()},
                                   {"cues", m["cues"].toInt()}});
            }
            done(ToolResult().data(json{{"eventos", out},
                                        {"abierto", ev.abierto().toStdString()}}));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "evento_ver";
        t.title = "El guion abierto";
        t.category = "evento";
        t.readOnly = true;
        t.description =
            "El guion completo del evento abierto: bloques con su hora prevista y sus cues, cada uno con "
            "tipo, duracion, si encadena solo y que le falta para poder ejecutarse.";
        t.details =
            "`falta` es lo importante: dice por que ese cue no se podria ejecutar (una escena sin nombre, "
            "un orador sin tiempo, un video sin archivo). Un guion sano tiene todos los `falta` vacios.\n\n"
            "`offsetSeg` de cada cue son los segundos desde el arranque del evento: con `inicio` sale la "
            "hora prevista de cada cosa.\n\n"
            "Si no hay ningun evento abierto, `bloques` viene vacio: abri uno con evento_abrir.";
        t.run = [&ev](const json&, Done done) {
            done(ToolResult().data(json{{"abierto", ev.abierto().toStdString()},
                                        {"sinGuardar", ev.sucio()},
                                        {"guion", guionDe(ev)}}));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "evento_abrir";
        t.title = "Abrir un evento";
        t.category = "evento";
        t.description = "Abre un evento guardado, o crea uno nuevo si se pasa `nombre` en vez de `carpeta`.";
        t.details =
            "Con `carpeta` abre uno que ya existe (el nombre exacto que devuelve evento_listar).\n"
            "Con `nombre` crea uno nuevo con ese titulo y lo deja abierto; si ya hay uno con ese nombre "
            "NO lo pisa, crea otro al lado.\n\n"
            "Lo que se este editando sin guardar se guarda antes de cambiar de evento.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"carpeta", {{"type", "string"}, {"description", "Un evento que ya existe."}}},
                           {"nombre", {{"type", "string"}, {"description", "Crear uno nuevo con este titulo."}}}}}};
        t.run = [&ev](const json& a, Done done) {
            if (a.contains("nombre")) {
                const QString c = ev.crear(QString::fromStdString(a["nombre"].get<std::string>()));
                if (c.isEmpty()) return done(ToolResult::error("No se pudo crear el evento."));
                return done(ToolResult::ok("Evento creado y abierto: " + c.toStdString()));
            }
            const std::string c = a.value("carpeta", std::string());
            if (c.empty()) return done(ToolResult::error("Pasa `carpeta` (uno que exista) o `nombre` (uno nuevo)."));
            if (!ev.abrir(QString::fromStdString(c)))
                return done(ToolResult::error("No se pudo abrir '" + c + "' (evento_listar muestra los que hay)."));
            done(ToolResult::ok("Evento abierto: " + c));
        };
        core.addTool(std::move(t));
    }

    // --- Armar ----------------------------------------------------------------
    {
        Tool t;
        t.name = "evento_bloque";
        t.title = "Agregar o cambiar un bloque";
        t.category = "evento";
        t.description = "Agrega un bloque al final del guion, lo renombra, lo mueve o lo saca.";
        t.details =
            "Un bloque es un tramo con nombre propio: «Apertura», «Palabras del intendente», «Cierre». "
            "Sirve para leer el guion de un vistazo y para que los tiempos se sumen por tramo.\n\n"
            "Sin `indice` agrega uno nuevo al final. Con `indice` (base 0) actua sobre ese: `nombre` lo "
            "renombra, `mover` lo cambia de lugar y `quitar` lo saca con todos sus cues.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"indice", {{"type", "integer"}, {"minimum", 0}, {"description", "Cual bloque (base 0)."}}},
              {"nombre", {{"type", "string"}}},
              {"mover", {{"type", "integer"}, {"minimum", 0}, {"description", "Nueva posicion."}}},
              {"quitar", {{"type", "boolean"}}}}}};
        t.run = [&ev](const json& a, Done done) {
            if (!a.contains("indice")) {
                ev.agregarBloque(QString::fromStdString(a.value("nombre", std::string("Bloque nuevo"))));
                return done(ToolResult::ok("Bloque agregado al final."));
            }
            const int i = a["indice"].get<int>();
            if (a.value("quitar", false)) {
                ev.quitarBloque(i);
                return done(ToolResult::ok("Bloque quitado."));
            }
            if (a.contains("nombre")) ev.renombrarBloque(i, QString::fromStdString(a["nombre"]));
            if (a.contains("mover")) ev.moverBloque(i, a["mover"].get<int>());
            done(ToolResult::ok("Bloque actualizado."));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "evento_cue";
        t.title = "Agregar o cambiar un cue";
        t.category = "evento";
        t.description = "Agrega un cue a un bloque, le cambia los datos, lo mueve de lugar o lo saca.";
        t.details =
            "Un cue es una cosa concreta que pasa. Los tipos y lo que necesita cada uno en `datos`:\n\n"
            "  escena        {escena}              pone esa escena al aire\n"
            "  musica        {archivo, accion}     accion: arrancar (por defecto) o parar\n"
            "  video         {archivo, alTerminar}\n"
            "  teleprompter  {discurso}            un archivo de Discursos/\n"
            "  orador        {participante}        ademas necesita duracionSeg > 0\n"
            "  pantalla      {pantalla, contenido}\n"
            "  audio         {accion, fuente}\n"
            "  pausa         -                     espera lo que diga duracionSeg\n"
            "  espera        -                     frena hasta que el operador confirme\n"
            "  nota          -                     no hace nada: es una indicacion\n\n"
            "`texto` es lo que el operador lee cuando toca ese cue: escribilo siempre, aunque el tipo no "
            "lo exija.\n\n"
            "`automatico` encadena con el siguiente sin esperar el clic. Usalo con cuidado: en vivo, que "
            "algo salga solo cuando no lo esperabas es peor que llegar tarde. La revision avisa si hay "
            "seis automaticos seguidos.\n\n"
            "Sin `cue` agrega uno nuevo al final del bloque. Con `cue` actua sobre ese.\n\n"
            "Ojo: hoy el sistema solo EJECUTA los cues de escena y de orador. Los demas se pueden dejar "
            "escritos en el guion (sirven para los tiempos y para que el operador sepa que hacer), pero "
            "cuando toquen se van a anotar como «todavia no se ejecutan».";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"bloque", {{"type", "integer"}, {"minimum", 0}}},
              {"cue", {{"type", "integer"}, {"minimum", 0}, {"description", "Cual cue del bloque (base 0)."}}},
              {"tipo", {{"type", "string"},
                        {"enum", {"escena", "musica", "video", "teleprompter", "orador", "pantalla",
                                  "audio", "pausa", "espera", "nota"}}}},
              {"texto", {{"type", "string"}, {"description", "Lo que lee el operador."}}},
              {"datos", {{"type", "object"}, {"description", "Lo propio del tipo (ver la ayuda)."}}},
              {"duracionSeg", {{"type", "integer"}, {"minimum", 0}}},
              {"automatico", {{"type", "boolean"}}},
              {"moverABloque", {{"type", "integer"}, {"minimum", 0}}},
              {"moverACue", {{"type", "integer"}, {"minimum", 0}}},
              {"quitar", {{"type", "boolean"}}}}},
            {"required", {"bloque"}}};
        t.run = [&ev](const json& a, Done done) {
            const int b = a["bloque"].get<int>();
            if (!a.contains("cue")) {
                const QString id = ev.agregarCue(b, QString::fromStdString(a.value("tipo", std::string("nota"))));
                if (id.isEmpty()) return done(ToolResult::error("No existe el bloque " + std::to_string(b) + "."));
                // Recien creado: es el ultimo del bloque.
                const QVariantMap g = ev.guion();
                const QVariantList bs = g["bloques"].toList();
                const int ultimo = bs[b].toMap()["cues"].toList().size() - 1;
                QVariantMap campos;
                if (a.contains("texto")) campos["texto"] = QString::fromStdString(a["texto"]);
                if (a.contains("datos")) campos["datos"] = aVariantMap(a["datos"]);
                if (a.contains("duracionSeg")) campos["duracionSeg"] = a["duracionSeg"].get<int>();
                if (a.contains("automatico")) campos["automatico"] = a["automatico"].get<bool>();
                if (!campos.isEmpty()) ev.editarCue(b, ultimo, campos);
                return done(ToolResult::ok("Cue agregado en el bloque " + std::to_string(b) + "."));
            }
            const int c = a["cue"].get<int>();
            if (a.value("quitar", false)) {
                ev.quitarCue(b, c);
                return done(ToolResult::ok("Cue quitado."));
            }
            if (a.contains("moverABloque") || a.contains("moverACue")) {
                ev.moverCue(b, c, a.value("moverABloque", b), a.value("moverACue", c));
                return done(ToolResult::ok("Cue movido."));
            }
            QVariantMap campos;
            if (a.contains("tipo")) campos["tipo"] = QString::fromStdString(a["tipo"]);
            if (a.contains("texto")) campos["texto"] = QString::fromStdString(a["texto"]);
            if (a.contains("datos")) campos["datos"] = aVariantMap(a["datos"]);
            if (a.contains("duracionSeg")) campos["duracionSeg"] = a["duracionSeg"].get<int>();
            if (a.contains("automatico")) campos["automatico"] = a["automatico"].get<bool>();
            ev.editarCue(b, c, campos);
            done(ToolResult::ok("Cue actualizado."));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "evento_guardar";
        t.title = "Guardar el guion";
        t.category = "evento";
        t.description = "Escribe el guion en la carpeta del evento, respaldando antes el que estaba.";
        t.details =
            "Guarda en Eventos/<carpeta>/evento.json. El guion anterior queda en Respaldos/ con la fecha, "
            "asi que guardar nunca pierde lo que habia.\n\n"
            "Tambien se puede cambiar el titulo del evento y su hora de arranque (`inicio`, formato HH:MM). "
            "Con la hora puesta, el guion calcula la hora prevista de cada bloque y cada cue.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"nombre", {{"type", "string"}}},
                           {"inicio", {{"type", "string"}, {"description", "HH:MM, hora prevista de arranque."}}}}}};
        t.run = [&ev](const json& a, Done done) {
            if (a.contains("nombre")) ev.setNombre(QString::fromStdString(a["nombre"]));
            if (a.contains("inicio")) ev.setInicio(QString::fromStdString(a["inicio"]));
            if (!ev.guardar()) return done(ToolResult::error("No se pudo guardar (¿hay algun evento abierto?)."));
            done(ToolResult::ok("Guion guardado."));
        };
        core.addTool(std::move(t));
    }

    // --- Revisar y correr -----------------------------------------------------
    {
        Tool t;
        t.name = "evento_revisar";
        t.title = "Revision previa";
        t.category = "evento";
        t.description =
            "Busca lo que impide correr el guion: cues incompletos, escenas que no existen, bloques "
            "vacios, cadenas largas de cues automaticos.";
        t.details =
            "Es lo que evita los problemas del dia del evento. Corrila despues de armar el guion y de "
            "nuevo antes de empezar.\n\n"
            "Cada reparo dice donde esta (`bloque`, `cue`) y si es `grave` (asi no se puede correr) o "
            "solo algo para mirar.\n\n"
            "Revisa tambien los archivos que el guion nombra: que la musica y los videos esten en "
            "Medios/, que los discursos esten en Discursos/, que no esten vacios y que ninguno apunte "
            "fuera de la carpeta del evento.\n\n"
            "Todavia no revisa: que los dispositivos esten conectados, el espacio en disco, ni que los "
            "invitados citados esten en la sala.";
        t.run = [&ev](const json&, Done done) {
            ev.revisar();
            json out = json::array();
            int graves = 0;
            for (const QVariant& v : ev.reparos()) {
                const QVariantMap m = v.toMap();
                if (m["grave"].toBool()) ++graves;
                out.push_back(json{{"bloque", m["bloque"].toInt()},
                                   {"cue", m["cue"].toInt()},
                                   {"grave", m["grave"].toBool()},
                                   {"texto", m["texto"].toString().toStdString()}});
            }
            done(ToolResult().data(json{{"reparos", out}, {"graves", graves},
                                        {"sePuedeCorrer", graves == 0}}));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "evento_ensayo_rapido";
        t.title = "Ensayo acelerado";
        t.category = "evento";
        t.description =
            "Recorre el guion ENTERO de una, sin esperar las duraciones y sin tocar nada, y dice de cada "
            "cue si se podria ejecutar.";
        t.details =
            "La diferencia con evento_revisar es que esto CAMINA el recorrido de verdad: sigue el orden "
            "real de los cues cruzando de bloque en bloque, asi que ademas de los cues incompletos "
            "muestra los bloques vacios que nadie pisa y los cues de un tipo que todavia no se ejecuta.\n\n"
            "No sale al aire nunca, ni siquiera con el ensayo apagado. Es lo que conviene correr despues "
            "de armar un guion largo, antes de probarlo en tiempo real.\n\n"
            "El resultado queda en la bitacora, que se devuelve entera.";
        t.readOnly = true;
        t.run = [&ev](const json&, Done done) {
            ev.ensayoAcelerado();
            json out = json::array();
            for (const QVariant& v : ev.bitacora()) {
                const QVariantMap m = v.toMap();
                out.push_back(json{{"texto", m["texto"].toString().toStdString()},
                                   {"tipo", m["tipo"].toString().toStdString()}});
            }
            // La bitacora se lee de arriba para abajo con lo mas nuevo primero;
            // para un informe conviene el orden en que paso.
            std::reverse(out.begin(), out.end());
            done(ToolResult().data(json{{"recorrido", out}}));
        };
        core.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "evento_correr";
        t.title = "Correr el guion";
        t.category = "evento";
        t.description =
            "Arranca el guion, avanza al cue siguiente, vuelve atras, salta a uno, pausa o para. "
            "Con `ensayo` recorre todo SIN salir al aire.";
        t.details =
            "Esto SALE AL AIRE salvo que `ensayo` este prendido: los cues de escena cambian lo que se ve "
            "y los de orador le dan la palabra a alguien de verdad. Por eso no es de solo lectura y "
            "conviene que el operador sepa lo que se va a hacer.\n\n"
            "`accion`:\n"
            "  arrancar    empieza por el primer cue\n"
            "  siguiente   pasa al que sigue\n"
            "  atras       vuelve al anterior (para repetir algo)\n"
            "  ir          salta a `bloque` y `cue`\n"
            "  pausar      frena el guion; el tiempo pausado NO cuenta como atraso\n"
            "  reanudar\n"
            "  parar       lo deja parado\n\n"
            "La respuesta trae el cue actual, el que sigue y el reloj: `desvioSeg` positivo = vamos tarde.\n\n"
            "Mientras el guion corre sin ensayo, el sistema entra en MODO EVENTO: la app le avisa al "
            "operador antes de cualquier accion de riesgo, y settings_set queda bloqueado (un ajuste que "
            "reinicia la sala sacaria a todos los invitados en el peor momento posible).";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"accion", {{"type", "string"},
                          {"enum", {"arrancar", "siguiente", "atras", "ir", "pausar", "reanudar", "parar"}}}},
              {"bloque", {{"type", "integer"}, {"minimum", 0}}},
              {"cue", {{"type", "integer"}, {"minimum", 0}}},
              {"ensayo", {{"type", "boolean"}, {"description", "Recorrer sin salir al aire."}}}}},
            {"required", {"accion"}}};
        t.run = [&ev](const json& a, Done done) {
            if (a.contains("ensayo")) ev.setEnsayo(a["ensayo"].get<bool>());
            const std::string accion = a["accion"];
            if (accion == "arrancar") ev.arrancar();
            else if (accion == "siguiente") ev.avanzar();
            else if (accion == "atras") ev.retroceder();
            else if (accion == "ir") ev.irA(a.value("bloque", 0), a.value("cue", 0));
            else if (accion == "pausar") ev.pausar();
            else if (accion == "reanudar") ev.reanudar();
            else if (accion == "parar") ev.parar();
            else return done(ToolResult::error("Accion desconocida: " + accion));

            const auto mapa = [](const QVariantMap& m) {
                json o = json::object();
                for (auto it = m.begin(); it != m.end(); ++it) {
                    const QVariant& v = it.value();
                    if (v.typeId() == QMetaType::Int) o[it.key().toStdString()] = v.toInt();
                    else if (v.typeId() == QMetaType::Bool) o[it.key().toStdString()] = v.toBool();
                    else if (v.typeId() == QMetaType::QString) o[it.key().toStdString()] = v.toString().toStdString();
                }
                return o;
            };
            done(ToolResult().data(json{{"estado", ev.estado().toStdString()},
                                        {"ensayo", ev.ensayo()},
                                        {"actual", mapa(ev.actual())},
                                        {"siguiente", mapa(ev.siguiente())},
                                        {"reloj", mapa(ev.reloj())}}));
        };
        core.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
