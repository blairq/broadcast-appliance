// ============================================================================
// src/app/mcp/McpMediaTools.cpp — Carpetas de medios del sistema
//
// "Busca la imagen del sponsor y ponela de logo arriba a la derecha de la
// escena 4": el asistente busca en las carpetas de medios del sistema portable
// (Media/, lo que compartieron los invitados en Shares/, las grabaciones), mira
// la imagen para confirmar que es la correcta y la usa en una escena. Solo
// dentro de la carpeta del sistema: nunca rutas de afuera.
// ============================================================================

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QRegularExpression>

#include <algorithm>

#include "AppRuntime.hpp"
#include "mcp/McpService.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "ui/StudioViewModel.hpp"

namespace ba::app::mcp {

namespace {

const QStringList kImages{"png", "jpg", "jpeg", "gif", "bmp", "tga", "webp", "psd", "svg"};
const QStringList kVideos{"mp4", "mkv", "mov", "webm", "avi", "flv", "ts", "m4v", "mpg", "mpeg", "wmv"};
const QStringList kAudios{"mp3", "wav", "ogg", "flac", "m4a", "aac", "opus", "wma"};
const QStringList kFolders{"Media", "Shares", "Recordings"};

QString kindOf(const QString& file) {
    const QString ext = QFileInfo(file).suffix().toLower();
    if (kImages.contains(ext)) return "image";
    if (kVideos.contains(ext)) return "video";
    if (kAudios.contains(ext)) return "audio";
    return {};
}

/// Sin tildes y en minusculas (buscar "logo camion" encuentra "Logo_Camión.png").
QString fold(const QString& s) {
    QString out = s.normalized(QString::NormalizationForm_D).toLower();
    out.remove(QRegularExpression("[\\x{0300}-\\x{036f}]"));
    out.replace(QRegularExpression("[_\\-.]+"), " ");
    return out;
}

/// Ruta dentro de la carpeta del sistema (relativa o absoluta) -> absoluta, o
/// vacio si sale de la carpeta.
QString resolveInside(const QString& root, const QString& p) {
    const QString abs = QDir::cleanPath(QDir(root).absoluteFilePath(p));
    const QString r = QDir::cleanPath(root);
    return (abs == r || abs.startsWith(r + "/")) ? abs : QString();
}

} // namespace

void McpService::registerMediaTools() {
    core_.addCategory(
        "medios", "Medios (imagenes, videos, musica)",
        "Buscar, ver y usar en escenas los archivos de las carpetas de medios del sistema: Media/ (la "
        "biblioteca), Shares/ (lo que compartieron los invitados) y Recordings/.");
    AppRuntime& rt = rt_;
    StudioViewModel& vm = vm_;
    const QString root = QString::fromStdWString(rt.dataRoot().wstring());
    QDir(root).mkpath("Media");

    const auto scan = [root](const QString& folder, const QString& kind) {
        std::vector<QFileInfo> out;
        const QStringList folders = folder.isEmpty() || folder == "all" ? kFolders : QStringList{folder};
        for (const QString& f : folders) {
            QDirIterator it(root + "/" + f, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QFileInfo fi(it.next());
                const QString k = kindOf(fi.fileName());
                if (k.isEmpty() || (!kind.isEmpty() && kind != "any" && kind != k)) continue;
                out.push_back(fi);
                if (out.size() > 20000) return out;
            }
        }
        return out;
    };
    const auto describe = [root](const QFileInfo& fi) {
        json e{{"path", QDir(root).relativeFilePath(fi.absoluteFilePath()).toStdString()},
               {"name", fi.fileName().toStdString()},
               {"type", kindOf(fi.fileName()).toStdString()},
               {"sizeBytes", fi.size()},
               {"modified", fi.lastModified().toString(Qt::ISODate).toStdString()}};
        if (kindOf(fi.fileName()) == "image") {
            const QSize s = QImageReader(fi.absoluteFilePath()).size();
            if (s.isValid()) {
                e["width"] = s.width();
                e["height"] = s.height();
            }
        }
        return e;
    };

    {
        Tool t;
        t.name = "media_list";
        t.title = "Archivos de medios";
        t.category = "medios";
        t.readOnly = true;
        t.description =
            "Lista imagenes, videos y audios de las carpetas de medios (Media/ por defecto; Shares/, "
            "Recordings/ o all), los mas nuevos primero, con tamaño y dimensiones de las imagenes.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"folder",
               {{"type", "string"},
                {"enum", {"Media", "Shares", "Recordings", "all"}},
                {"default", "Media"}}},
              {"type",
               {{"type", "string"}, {"enum", {"image", "video", "audio", "any"}}, {"default", "any"}}},
              {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 500}, {"default", 100}}}}}};
        t.run = [scan, describe, root](const json& a, Done done) {
            auto files = scan(QString::fromStdString(a.value("folder", std::string("Media"))),
                              QString::fromStdString(a.value("type", std::string("any"))));
            std::sort(files.begin(), files.end(), [](const QFileInfo& x, const QFileInfo& y) {
                return x.lastModified() > y.lastModified();
            });
            json out = json::array();
            for (const auto& fi : files) {
                if (static_cast<int>(out.size()) >= a.value("limit", 100)) break;
                out.push_back(describe(fi));
            }
            ToolResult r;
            r.data(json{{"root", root.toStdString()}, {"total", files.size()}, {"files", out}});
            for (const auto& f : out)
                if (f.value("path", "").rfind("Shares/", 0) == 0)
                    r.untrusted = true; // nombres puestos por invitados
            done(r);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "media_find";
        t.title = "Buscar un archivo de medios";
        t.category = "medios";
        t.readOnly = true;
        t.description =
            "Busca por nombre en todas las carpetas de medios (sin importar mayusculas, tildes ni guiones): "
            "\"logo sponsor\" encuentra \"Logo-Sponsor_2026.png\". Los mejores primero; confirma una imagen "
            "con media_view antes de usarla.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"query", {{"type", "string"}}},
              {"type",
               {{"type", "string"}, {"enum", {"image", "video", "audio", "any"}}, {"default", "any"}}},
              {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}, {"default", 20}}}}},
            {"required", {"query"}}};
        t.run = [scan, describe](const json& a, Done done) {
            const QStringList words = fold(QString::fromStdString(a["query"])).split(' ', Qt::SkipEmptyParts);
            std::vector<std::pair<int, QFileInfo>> hits;
            for (const auto& fi : scan("all", QString::fromStdString(a.value("type", std::string("any"))))) {
                const QString name = fold(fi.completeBaseName());
                const QString path = fold(fi.absolutePath());
                int score = 0;
                for (const QString& w : words) {
                    if (name.contains(w))
                        score += name.split(' ').contains(w) ? 3 : 2;
                    else if (path.contains(w))
                        score += 1;
                    else {
                        score = -1;
                        break;
                    }
                }
                if (score > 0) hits.emplace_back(score, fi);
            }
            std::sort(hits.begin(), hits.end(), [](const auto& x, const auto& y) {
                return x.first != y.first ? x.first > y.first
                                          : x.second.lastModified() > y.second.lastModified();
            });
            json out = json::array();
            for (const auto& [score, fi] : hits) {
                if (static_cast<int>(out.size()) >= a.value("limit", 20)) break;
                json e = describe(fi);
                e["score"] = score;
                out.push_back(e);
            }
            if (out.empty())
                return done(ToolResult::ok("No hay archivos que coincidan (media_list muestra todos)."));
            ToolResult r;
            r.data(json{{"matches", out}});
            for (const auto& f : out)
                if (f.value("path", "").rfind("Shares/", 0) == 0) r.untrusted = true;
            done(r);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "media_view";
        t.title = "Ver una imagen de medios";
        t.category = "medios";
        t.readOnly = true;
        t.description =
            "Devuelve la imagen (achicada) para confirmar que es la que se busca, con su tamaño original.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"path", {{"type", "string"}}},
              {"maxWidth", {{"type", "integer"}, {"minimum", 64}, {"maximum", 1920}, {"default", 640}}}}},
            {"required", {"path"}}};
        t.run = [root](const json& a, Done done) {
            const QString abs = resolveInside(root, QString::fromStdString(a["path"]));
            if (abs.isEmpty() || !QFileInfo::exists(abs))
                return done(ToolResult::error(
                    "No existe ese archivo dentro de la carpeta del sistema (usa media_find)."));
            if (kindOf(abs) != "image")
                return done(ToolResult::error(
                    "media_view muestra imagenes; para videos, usalos en una escena y capture_source."));
            QImageReader reader(abs);
            reader.setAutoTransform(true);
            const QSize orig = reader.size();
            const QImage img = reader.read();
            if (img.isNull())
                return done(
                    ToolResult::error("No se pudo leer la imagen: " + reader.errorString().toStdString()));
            ToolResult r;
            r.text(QFileInfo(abs).fileName().toStdString() + " — " + std::to_string(orig.width()) + "x" +
                   std::to_string(orig.height()) + (img.hasAlphaChannel() ? " (con transparencia)" : ""));
            addImage(r, img, a, 640);
            // Una imagen que subio un invitado puede traer texto con instrucciones.
            if (QDir(root).relativeFilePath(abs).startsWith("Shares/")) r.untrusted = true;
            done(r);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "media_use";
        t.title = "Usar un archivo de medios en una escena";
        t.category = "medios";
        t.description =
            "Pone una imagen (logo, placa, fondo), un video o un audio de las carpetas de medios en una "
            "escena, "
            "en la ubicacion pedida (preset como pip-tr = arriba a la derecha, o {x,y,w,h}). Las imagenes "
            "conservan su proporcion dentro del lugar. Verifica con capture_source.";
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"path", {{"type", "string"}}},
              {"scene", {{"type", "string"}, {"description", "Por defecto la escena al aire."}}},
              {"name", {{"type", "string"}}},
              {"layout", {{"description", "Preset o {x,y,w,h}. Por defecto pantalla completa."}}},
              {"loop", {{"type", "boolean"}, {"default", false}, {"description", "Videos/audios: repetir."}}},
              {"visible", {{"type", "boolean"}, {"default", true}}}}},
            {"required", {"path"}}};
        t.examples = {
            R"({"path": "Media/logos/sponsor.png", "scene": "Escena 4", "name": "Logo sponsor", "layout": "pip-tr"})"};
        t.run = [&rt, &vm, root](const json& a, Done done) {
            const QString abs = resolveInside(root, QString::fromStdString(a["path"]));
            if (abs.isEmpty() || !QFileInfo::exists(abs))
                return done(ToolResult::error(
                    "No existe ese archivo dentro de la carpeta del sistema (usa media_find)."));
            const QString kind = kindOf(abs);
            if (kind.isEmpty()) return done(ToolResult::error("No es una imagen, video ni audio."));
            SourceSpec spec;
            spec.name = a.value("name", QFileInfo(abs).completeBaseName().toStdString());
            if (kind == "image") {
                spec.id = "image_source";
                spec.settingsJson = json{{"file", abs.toStdString()}}.dump();
            } else {
                spec.id = "ffmpeg_source";
                spec.settingsJson = json{
                    {"is_local_file", true},
                    {"local_file", abs.toStdString()},
                    {"looping", a.value("loop", false)},
                    {"restart_on_activate",
                     true}}.dump();
            }
            spec.visible = a.value("visible", true);
            spec.rect = CanvasRect{0.0f, 0.0f, 1.0f, 1.0f};
            std::string err;
            if (a.contains("layout") &&
                !parseLayout(a["layout"], spec.rect.x, spec.rect.y, spec.rect.w, spec.rect.h, err))
                return done(ToolResult::error(err));
            const std::string scene = a.value("scene", rt.engine().currentScene());
            const auto scenes = rt.engine().sceneNames();
            if (std::find(scenes.begin(), scenes.end(), scene) == scenes.end())
                return done(
                    ToolResult::error("No existe la escena '" + scene + "' (scene_list las muestra)."));
            if (!rt.addSource(scene, spec)) return done(ToolResult::error(rt.lastError()));
            rt.save();
            vm.refreshModels();
            done(ToolResult::ok("'" + spec.name + "' (" + kind.toStdString() + ") agregada a '" + scene +
                                "'. Verifica con capture_source {\"name\": \"" + scene + "\"}."));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
