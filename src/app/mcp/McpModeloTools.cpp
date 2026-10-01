// ============================================================================
// src/app/mcp/McpModeloTools.cpp — los modelos locales y la voz, por MCP
//
// Lo mismo que Configuración -> Modelos locales: qué hay para esta máquina,
// bajarlo, activarlo, ajustarlo y probarlo. Operan sobre los MISMOS objetos
// que la página (ModelosLocales, Voz): lo que se cambia por acá se ve en la
// interfaz en el acto, y al revés.
//
// VAN EN «agentes», a propósito: el modelo reactivo se entrena con las fichas
// de las herramientas de SU perfil (audio, escenas, fuentes...). Una
// herramienta nueva en una de esas categorías cambiaría su prompt y lo dejaría
// viejo. Estas son de administración: no son suyas, las deriva.
// ============================================================================

#include <QJsonDocument>
#include <QJsonObject>

#include "agent/ModelosLocales.hpp"
#include "agent/Voz.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {

namespace {

json aJson(const QVariantMap& m) {
    return json::parse(
        QJsonDocument(QJsonObject::fromVariantMap(m)).toJson(QJsonDocument::Compact).toStdString(), nullptr,
        false);
}

json estado(agent::ModelosLocales& ml, agent::Voz* voz) {
    json catalogo = json::array();
    for (const QVariant& v : ml.catalogo())
        catalogo.push_back(aJson(v.toMap()));
    QVariantMap info = ml.info();
    info.remove("pruebas"); // las frases de prueba: largas y no hacen falta acá
    json r{{"estado", ml.estado().toStdString()},
           {"detalle", ml.detalle().toStdString()},
           {"activo", ml.activo().toStdString()},
           {"motor", ml.motor().toStdString()},
           {"hay_motor", ml.hayMotor()},
           {"info", aJson(info)},
           {"sesgo", ml.sesgo()},
           {"mtp", ml.mtp().toStdString()},
           {"al_arrancar", ml.alArrancar()},
           {"descarga",
            {{"en_curso", ml.descargando()},
             {"progreso", ml.progreso()},
             {"texto", ml.textoDescarga().toStdString()}}},
           {"catalogo", catalogo}};
    if (voz != nullptr)
        r["voz"] = {{"disponible", voz->disponible()},
                    {"lista", voz->lista()},
                    {"estado", voz->estado().toStdString()},
                    {"tecla", voz->tecla().toStdString()},
                    {"ultimo", voz->ultimo().toStdString()}};
    return r;
}

/// El repo tiene que estar en el catálogo de esta plataforma.
bool enCatalogo(agent::ModelosLocales& ml, const std::string& repo, std::string* tipo = nullptr) {
    for (const QVariant& v : ml.catalogo()) {
        const QVariantMap m = v.toMap();
        if (m.value("repo").toString().toStdString() == repo) {
            if (tipo != nullptr) *tipo = m.value("tipo").toString().toStdString();
            return true;
        }
    }
    return false;
}

json esquemaRepo(const char* que) {
    return {{"type", "object"},
            {"required", {"repo"}},
            {"properties", {{"repo", {{"type", "string"}, {"description", que}}}}}};
}

} // namespace

void McpService::registerModeloTools() {
    agent::ModelosLocales* mlp = vm_.modelosLocales();
    if (mlp == nullptr) return;
    agent::ModelosLocales& ml = *mlp;
    auto* voz = qobject_cast<agent::Voz*>(vm_.voz());

    {
        Tool t;
        t.name = "modelo_local_estado";
        t.title = "Modelos locales: que hay y como esta";
        t.category = "agentes";
        t.readOnly = true;
        t.description = "El modelo chico que corre en esta maquina (elige la herramienta para una frase "
                        "de cabina y, si trae vision, mira imagenes) y la voz (push-to-talk): cual esta "
                        "cargado, ajustes, descarga en curso, el catalogo para esta plataforma (cuales "
                        "estan bajados) y el estado de la voz.";
        t.run = [&ml, voz](const json&, Done done) { done(ToolResult().data(estado(ml, voz))); };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "modelo_local_bajar";
        t.title = "Bajar un modelo local";
        t.category = "agentes";
        t.idempotent = true;
        t.openWorld = true;
        t.description = "Baja un modelo del catalogo (modelo_local_estado lo lista) a la carpeta portable, "
                        "verificando cada archivo. Si ya esta, lo actualiza (lo verificado no se rebaja). "
                        "Vuelve enseguida: el avance se ve en modelo_local_estado (descarga).";
        t.details = "Los modelos de lenguaje pesan 2-3 GB y los de voz ~0,6 GB: la red del estudio se usa "
                    "mientras baja. Una descarga a la vez. Nada se usa sin verificar, y una descarga a "
                    "medias nunca pisa el modelo que andaba.";
        t.inputSchema = esquemaRepo("el repo, tal cual aparece en el catalogo");
        t.run = [&ml](const json& a, Done done) {
            const std::string repo = a.value("repo", std::string());
            if (!enCatalogo(ml, repo))
                return done(ToolResult::error("`" + repo + "` no esta en el catalogo de esta plataforma."));
            if (ml.descargando()) return done(ToolResult::error("Ya hay una descarga en curso."));
            ml.descargar(QString::fromStdString(repo));
            done(ToolResult::ok("Bajando `" + repo + "`: el avance, en modelo_local_estado."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "modelo_local_cancelar_descarga";
        t.title = "Cancelar la descarga";
        t.category = "agentes";
        t.idempotent = true;
        t.description = "Corta la descarga en curso. Lo que estaba instalado queda como estaba.";
        t.run = [&ml](const json&, Done done) {
            ml.cancelarDescarga();
            done(ToolResult::ok(ml.descargando() ? "Cancelando." : "No habia ninguna descarga."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "modelo_local_borrar";
        t.title = "Borrar un modelo bajado";
        t.category = "agentes";
        t.destructive = true;
        t.description = "Borra un modelo bajado (libera gigas). Si es el activo, lo apaga. Se puede volver "
                        "a bajar.";
        t.inputSchema = esquemaRepo("el repo a borrar");
        t.run = [&ml](const json& a, Done done) {
            const std::string repo = a.value("repo", std::string());
            if (!enCatalogo(ml, repo))
                return done(ToolResult::error("`" + repo + "` no esta en el catalogo de esta plataforma."));
            ml.borrar(QString::fromStdString(repo));
            done(ToolResult::ok("Borrado `" + repo + "`."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "modelo_local_activar";
        t.title = "Cargar un modelo local";
        t.category = "agentes";
        t.idempotent = true;
        t.description = "Carga un modelo de lenguaje bajado (apaga el que hubiera). Tarda unos segundos "
                        "en leer las fichas: el estado pasa a `listo` en modelo_local_estado. Los de voz "
                        "no se activan: se usan solos apenas estan bajados.";
        t.inputSchema = esquemaRepo("el repo del modelo de lenguaje");
        t.run = [&ml](const json& a, Done done) {
            const std::string repo = a.value("repo", std::string());
            std::string tipo;
            if (!enCatalogo(ml, repo, &tipo))
                return done(ToolResult::error("`" + repo + "` no esta en el catalogo de esta plataforma."));
            if (tipo != "reactivo")
                return done(
                    ToolResult::error("Los modelos de voz no se activan: se usan solos si estan bajados."));
            if (!ml.hayMotor())
                return done(ToolResult::error("Sin motor en esta plataforma: " + ml.motor().toStdString()));
            ml.activar(QString::fromStdString(repo));
            done(ToolResult::ok("Cargando `" + repo + "`."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "modelo_local_apagar";
        t.title = "Apagar el modelo local";
        t.category = "agentes";
        t.idempotent = true;
        t.description = "Apaga el modelo cargado (libera la memoria y la GPU). Queda elegido para la "
                        "proxima vez.";
        t.run = [&ml](const json&, Done done) {
            ml.apagar();
            done(ToolResult::ok("Apagado."));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "modelo_local_ajustes";
        t.title = "Ajustes del modelo local";
        t.category = "agentes";
        t.idempotent = true;
        t.description = "Cambia `sesgo` (cuanto deriva al asistente grande: -4 atiende mas, +4 deriva mas), "
                        "`mtp` (no | auto | q8 | fp16, al proximo arranque) y `al_arrancar` (cargarlo al "
                        "abrir la app). Lo que no se manda no se toca.";
        t.details =
            "Derivar de mas cuesta segundos; elegir mal cuesta al aire. Con +2 pasa al grande mas de "
            "9 de cada 10 pedidos que no son suyos y atiende un poco menos de lo propio. La cabeza MTP "
            "esta apagada por defecto: con la actual genera mas lento que sin ella.";
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"sesgo", {{"type", "number"}, {"minimum", -8}, {"maximum", 8}}},
                           {"mtp", {{"type", "string"}, {"enum", {"no", "auto", "q8", "fp16"}}}},
                           {"al_arrancar", {{"type", "boolean"}}}}}};
        t.run = [&ml, voz](const json& a, Done done) {
            if (a.contains("sesgo") && a["sesgo"].is_number()) ml.setSesgo(a["sesgo"].get<double>());
            if (a.contains("mtp") && a["mtp"].is_string())
                ml.setMtp(QString::fromStdString(a["mtp"].get<std::string>()));
            if (a.contains("al_arrancar") && a["al_arrancar"].is_boolean())
                ml.setAlArrancar(a["al_arrancar"].get<bool>());
            done(ToolResult::ok("Ajustes guardados.").data(estado(ml, voz)));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "modelo_local_elegir";
        t.title = "Probar el modelo local con una frase";
        t.category = "agentes";
        t.readOnly = true;
        t.description = "Que herramienta elige el modelo local para una frase de cabina (o si la deriva), "
                        "con la probabilidad y lo que tardo. NO ejecuta nada: es para probarlo. Queda en "
                        "la lista de pruebas de la pagina.";
        t.inputSchema = {
            {"type", "object"}, {"required", {"frase"}}, {"properties", {{"frase", {{"type", "string"}}}}}};
        t.examples = {R"({"frase": "bajale al mic 2"})", R"({"frase": "poneme un cafe con leche"})"};
        t.run = [&ml](const json& a, Done done) {
            const QString frase = QString::fromStdString(a.value("frase", std::string())).trimmed();
            if (frase.isEmpty()) return done(ToolResult::error("Falta la frase."));
            if (!ml.listo())
                return done(
                    ToolResult::error("No hay un modelo local cargado (" + ml.estado().toStdString() + ")."));
            ml.probar(frase); // que se vea en la pagina
            ml.elegir(frase, [done](const json& j) {
                if (j.contains("error")) return done(ToolResult::error(j.value("error", std::string())));
                done(ToolResult().data(json{{"herramienta", j.value("herramienta", std::string())},
                                            {"derivar", j.value("derivar", false)},
                                            {"prob", j.value("prob", 0.0)},
                                            {"ms", j.value("ms", 0.0)}}));
            });
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
