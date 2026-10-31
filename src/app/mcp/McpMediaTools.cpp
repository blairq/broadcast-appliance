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
#include <QMetaObject>
#include <QRegularExpression>

#include <algorithm>

#include "arranque/AppRuntime.hpp"
#include "emision/InputSpecs.hpp"
#include "mcp/McpService.hpp"
#include "medios/Playlist.hpp"
#include "mcp/McpToolsCommon.hpp"
#include "i18n/I18n.hpp"
#include "medios/Medios.hpp"
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
        "medios", ba::i18n::traducir("mcp", "Medios (imágenes, videos, música)"),
        ba::i18n::traducir("mcp", "Buscar, ver y usar en escenas los archivos de las carpetas de medios del sistema: Media/ (la "
        "biblioteca), Shares/ (lo que compartieron los invitados) y Recordings/."));
    AppRuntime& rt = rt_;
    StudioViewModel& vm = vm_;
    const QString root = QString::fromStdWString(rt.dataRoot().wstring());
    QDir(root).mkpath("Media");

    // --- La playlist (T46) -------------------------------------------------------------
    {
        Tool t;
        t.name = "playlist_crear";
        t.title = ba::i18n::traducir("mcp", "Armar la playlist de medios");
        t.category = "medios";
        t.clase = Clase::Escritura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Reemplaza la playlist: `items` = [{path (de las carpetas de medios), nombre?, in_s?, out_s?}] "
            "(in/out en segundos; 0 = desde el principio / hasta el final). Se pasan uno detrás del otro en "
            "la escena «Playlist». `auto_next` (pasa solo al siguiente) y `loop` (vuelve a empezar). Para "
            "reproducir: playlist_control.");
        t.inputSchema = {{"type", "object"},
                         {"required", {"items"}},
                         {"properties",
                          {{"items", {{"type", "array"}}},
                           {"auto_next", {{"type", "boolean"}}},
                           {"loop", {{"type", "boolean"}}}}}};
        t.run = [&vm, root](const json& a, Done done) {
            json items = json::array();
            for (const auto& it : a.value("items", json::array())) {
                json x = it.is_string() ? json{{"path", it}} : it;
                // `archivo` también vale (es como lo devuelve la playlist).
                const std::string ruta = x.value("path", x.value("archivo", std::string()));
                if (ruta.empty()) return done(ToolResult::error(ba::i18n::traducir("mcp", "a un ítem le falta `path`")));
                const QString abs = resolveInside(root, QString::fromStdString(ruta));
                if (abs.isEmpty())
                    return done(ToolResult::error(
                        ba::i18n::llenar(ba::i18n::traducir("mcp", "«%1» no está en las carpetas de medios"), {ruta})));
                if (QFileInfo(abs).isDir())
                    return done(ToolResult::error(
                        ba::i18n::llenar(ba::i18n::traducir("mcp", "«%1» es una carpeta, no un archivo"), {ruta})));
                x["archivo"] = abs.toStdString();
                items.push_back(x);
            }
            auto& pl = vm.playlist();
            if (const std::string e = pl.setItems(items); !e.empty()) return done(ToolResult::error(e));
            if (a.contains("auto_next")) pl.setAutoNext(a.value("auto_next", true));
            if (a.contains("loop")) pl.setLoop(a.value("loop", false));
            done(ToolResult().data(json{{"items", pl.items()}, {"estado", pl.estado()}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "playlist_control";
        t.title = ba::i18n::traducir("mcp", "Playlist: play, siguiente, anterior, parar, armar");
        t.category = "medios";
        t.clase = Clase::Escritura;
        t.description = QT_TRANSLATE_NOOP("mcp", "`accion`: play (desde `indice`, base 0, o el actual), next, prev, stop o armar (deja la escena "
                        "«Playlist» armada en PVW para el take). `auto_next` y `loop` cambian el modo. Sin acción, "
                        "informa (ítem actual y cuánto le queda).");
        t.inputSchema = {{"type", "object"},
                         {"properties",
                          {{"accion", {{"type", "string"}, {"enum", {"play", "next", "prev", "stop", "armar", "ir"}}}},
                           {"indice", {{"type", "integer"}, {"minimum", 0}}},
                           {"auto_next", {{"type", "boolean"}}},
                           {"loop", {{"type", "boolean"}}}}}};
        t.run = [&vm](const json& a, Done done) {
            auto& pl = vm.playlist();
            if (a.contains("auto_next")) pl.setAutoNext(a.value("auto_next", true));
            if (a.contains("loop")) pl.setLoop(a.value("loop", false));
            const std::string acc = a.value("accion", std::string());
            if (acc == "armar") {
                if (const std::string e = pl.armar(); !e.empty()) return done(ToolResult::error(e));
            } else if (!acc.empty())
                if (const std::string e = pl.control(acc, a.value("indice", -1)); !e.empty()) return done(ToolResult::error(e));
            done(ToolResult().data(json{{"estado", pl.estado()}, {"items", pl.items()}}));
        };
        core_.addTool(std::move(t));
    }
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
        t.title = ba::i18n::traducir("mcp", "Archivos de medios");
        t.category = "medios";
        t.clase = Clase::Lectura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Lista imagenes, videos y audios de las carpetas de medios (Media/ por defecto; Shares/, "
            "Recordings/ o all), los mas nuevos primero, con tamaño y dimensiones de las imagenes.");
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
        // Archivos de la carpeta de datos: en el pool (F3-03), aunque la interfaz no ande.
        t.hilo = Hilo::Foto;
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
        t.title = ba::i18n::traducir("mcp", "Buscar un archivo de medios");
        t.category = "medios";
        t.clase = Clase::Lectura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Encuentra la RUTA de un archivo por su nombre. "
            "Busca por nombre en todas las carpetas de medios (sin importar mayusculas, tildes ni guiones): "
            "\"logo sponsor\" encuentra \"Logo-Sponsor_2026.png\". Los mejores primero; confirma una imagen "
            "con media_view antes de usarla.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"query", {{"type", "string"}}},
              {"type",
               {{"type", "string"}, {"enum", {"image", "video", "audio", "any"}}, {"default", "any"}}},
              {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}, {"default", 20}}}}},
            {"required", {"query"}}};
        // Archivos de la carpeta de datos: en el pool (F3-03), aunque la interfaz no ande.
        t.hilo = Hilo::Foto;
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
                return done(ToolResult::ok(ba::i18n::traducir("mcp", "No hay archivos que coincidan (media_list muestra todos).")));
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
        t.title = ba::i18n::traducir("mcp", "Ver una imagen de medios");
        t.category = "medios";
        t.clase = Clase::Lectura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Devuelve la imagen (achicada) para confirmar que es la que se busca, con su tamaño original.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"path", {{"type", "string"}}},
              {"maxWidth", {{"type", "integer"}, {"minimum", 64}, {"maximum", 1920}, {"default", 640}}}}},
            {"required", {"path"}}};
        // Solo lee el archivo de la carpeta de datos: en el pool.
        t.hilo = Hilo::Foto;
        t.run = [root](const json& a, Done done) {
            const QString abs = resolveInside(root, QString::fromStdString(a["path"]));
            if (abs.isEmpty() || !QFileInfo::exists(abs))
                return done(ToolResult::error(ba::i18n::traducir(
                    "mcp", "No existe ese archivo dentro de la carpeta del sistema (usa media_find).")));
            if (kindOf(abs) != "image")
                return done(ToolResult::error(ba::i18n::traducir(
                    "mcp", "media_view muestra imagenes; para videos, usalos en una escena y foto_fuente.")));
            QImageReader reader(abs);
            reader.setAutoTransform(true);
            const QSize orig = reader.size();
            const QImage img = reader.read();
            if (img.isNull())
                return done(
                    ToolResult::error(ba::i18n::llenar(ba::i18n::traducir("mcp", "No se pudo leer la imagen: %1"),
                                                       {reader.errorString().toStdString()})));
            ToolResult r;
            r.text(QFileInfo(abs).fileName().toStdString() + " — " + std::to_string(orig.width()) + "x" +
                   std::to_string(orig.height()) +
                   (img.hasAlphaChannel() ? ba::i18n::traducir("mcp", " (con transparencia)") : std::string()));
            addImage(r, img, a, 640);
            // Una imagen que subio un invitado puede traer texto con instrucciones.
            if (QDir(root).relativeFilePath(abs).startsWith("Shares/")) r.untrusted = true;
            done(r);
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "medio_poner";
        t.title = ba::i18n::traducir("mcp", "Usar un archivo de medios en una escena");
        t.category = "medios";
        t.clase = Clase::Escritura;
        t.description =
             QT_TRANSLATE_NOOP("mcp", "PONE en una escena un archivo de la biblioteca de medios (música, video, imagen), por su ruta. Pone "
            "una imagen (logo, placa, fondo), un video o un audio de las carpetas de medios en una escena, en la "
            "ubicacion pedida (preset como pip-tr = arriba a la derecha, o {x,y,w,h}). Las imagenes conservan su "
            "proporcion dentro del lugar. Verifica con foto_fuente.");
        t.inputSchema = {
            {"type", "object"},
            {"properties",
             {{"path", {{"type", "string"}}},
              {"scene", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "Por defecto la escena al aire.")}}},
              {"name", {{"type", "string"}}},
              {"layout", {{"description", QT_TRANSLATE_NOOP("mcp", "Preset o {x,y,w,h}. Por defecto pantalla completa.")}}},
              {"loop", {{"type", "boolean"}, {"default", false}, {"description", QT_TRANSLATE_NOOP("mcp", "Videos/audios: repetir.")}}},
              {"visible", {{"type", "boolean"}, {"default", true}}}}},
            {"required", {"path"}}};
        t.examples = {
            R"({"path": "Media/logos/sponsor.png", "scene": "Escena 4", "name": "Logo sponsor", "layout": "pip-tr"})"};
        // El archivo y el motor (la escena, la fuente): en el dueño (F3-03).
        t.hilo = Hilo::Control;
        t.run = [&rt, root](const json& a, Done done) {
            const QString abs = resolveInside(root, QString::fromStdString(a["path"]));
            if (abs.isEmpty() || !QFileInfo::exists(abs))
                return done(ToolResult::error(ba::i18n::traducir(
                    "mcp", "No existe ese archivo dentro de la carpeta del sistema (usa media_find).")));
            const QString kind = kindOf(abs);
            if (kind.isEmpty()) return done(ToolResult::error(ba::i18n::traducir("mcp", "No es una imagen, video ni audio.")));
            SourceSpec spec;
            spec.name = a.value("name", QFileInfo(abs).completeBaseName().toStdString());
            if (kind == "image") {
                spec.id = "image_source";
                spec.settingsJson = json{{"file", abs.toStdString()}}.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            } else {
                spec.id = inputs::kMedioType;
                spec.settingsJson = json{{"local_file", abs.toStdString()}, {"looping", a.value("loop", false)}}.dump(
                    -1, ' ', false, nlohmann::json::error_handler_t::replace);
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
                    ToolResult::error(ba::i18n::llenar(
                        ba::i18n::traducir("mcp", "No existe la escena '%1' (scene_list las muestra)."), {scene})));
            if (!rt.addSource(scene, spec)) return done(ToolResult::error(rt.lastError()));
            rt.save();
            done(ToolResult::ok(ba::i18n::llenar(
                ba::i18n::traducir("mcp", "'%1' (%2) agregada a '%3'. Verifica con foto_fuente {\"name\": \"%3\"}."),
                {spec.name, kind.toStdString(), scene})));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "media_carpetas";
        t.title = ba::i18n::traducir("mcp", "Como estan ordenados los medios");
        t.category = "medios";
        t.clase = Clase::Lectura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "El arbol de carpetas de medios, con cuantos archivos tiene cada una (los propios y "
            "los de sus subcarpetas). Es por donde se empieza para ordenar: `media_list` da los "
            "archivos, esto da DONDE estan. Las carpetas vacias aparecen igual, porque son a donde "
            "se mueven cosas.");
        t.inputSchema = {{"type", "object"}, {"properties", json::object()}};
        // Archivos de la carpeta de datos: en el pool (F3-03), aunque la interfaz no ande.
        t.hilo = Hilo::Foto;
        t.run = [root](const json&, Done done) {
            const QVector<medios::Archivo> todos = medios::listar(root);
            json out = json::array();
            for (const medios::Carpeta& c : medios::arbol(root, todos))
                out.push_back(json{{"carpeta", c.relativa.toStdString()},
                                   {"nivel", c.nivel},
                                   {"archivos", c.propios},
                                   {"conSubcarpetas", c.total},
                                   {"delSistema", c.raiz}});
            done(ToolResult().data(json{{"carpetas", out}}));
        };
        core_.addTool(std::move(t));
    }
    {
        Tool t;
        t.name = "media_ordenar";
        t.title = ba::i18n::traducir("mcp", "Mover, renombrar, crear carpetas y borrar");
        t.category = "medios";
        t.clase = Clase::Escritura;
        t.description =
            QT_TRANSLATE_NOOP("mcp", "Acomoda los archivos: `mover` (a otra carpeta), `renombrar`, `carpeta` (crear una) y "
            "`borrar`.\n\n"
            "BORRAR NO BORRA: manda a `Papelera/`. Un archivo puede estar en una escena, o ser el "
            "unico que quedaba del evento pasado; deshacer tiene que ser sacarlo de una carpeta.\n\n"
            "Si en el destino ya hay algo con ese nombre, se numera en vez de pisar. Las carpetas "
            "del sistema (Media, Shares, Recordings) no se mueven ni se borran.");
        t.inputSchema = {
            {"type", "object"},
            {"required", {"accion"}},
            {"properties",
             {{"accion", {{"type", "string"}, {"enum", {"mover", "renombrar", "carpeta", "borrar"}}}},
              {"que",
               {{"type", "string"},
                {"description", QT_TRANSLATE_NOOP("mcp", "ruta relativa del archivo o carpeta, como la da media_list")}}},
              {"destino", {{"type", "string"}, {"description", QT_TRANSLATE_NOOP("mcp", "con accion=mover: la carpeta a donde va")}}},
              {"nombre",
               {{"type", "string"},
                {"description", QT_TRANSLATE_NOOP("mcp", "con renombrar: el nombre nuevo. Con carpeta: como se llama")}}},
              {"padre",
               {{"type", "string"},
                {"default", "Media"},
                {"description", QT_TRANSLATE_NOOP("mcp", "con carpeta: adentro de cual se crea")}}}}}};
        t.examples = {R"({"accion": "mover", "que": "Media/logo.png", "destino": "Media/logos"})",
                      R"({"accion": "carpeta", "padre": "Media", "nombre": "Sponsors"})"};
        t.run = [root, &vm](const json& a, Done done) {
            const std::string accion = a.value("accion", std::string());
            const QString que = QString::fromStdString(a.value("que", std::string()));
            QString error;
            bool ok = false;
            if (accion == "mover") {
                ok = medios::mover(root, que, QString::fromStdString(a.value("destino", std::string())),
                                   &error);
            } else if (accion == "renombrar") {
                ok = medios::renombrar(root, que, QString::fromStdString(a.value("nombre", std::string())),
                                       &error);
            } else if (accion == "carpeta") {
                ok =
                    medios::crearCarpeta(root, QString::fromStdString(a.value("padre", std::string("Media"))),
                                         QString::fromStdString(a.value("nombre", std::string())), &error);
            } else if (accion == "borrar") {
                ok = medios::aPapelera(root, que, &error);
            } else {
                error = QString::fromStdString(ba::i18n::traducir("mcp", "accion desconocida"));
            }
            if (!ok) {
                done(ToolResult::error(error.toStdString()));
                return;
            }
            // La pestaña se entera. Antes el disco solo cambiaba porque
            // alguien copiaba archivos a mano, y por eso refrescar era un
            // boton; ahora tambien lo cambia esto, y dejar la grilla mostrando
            // lo que ya no esta es peor que cualquier recorrida de mas.
            if (QObject* m = vm.medios()) QMetaObject::invokeMethod(m, "refrescar", Qt::QueuedConnection);
            done(ToolResult::ok(accion == "borrar" ? ba::i18n::traducir("mcp", "A la papelera (Papelera/), no se perdio.")
                                                   : ba::i18n::traducir("mcp", "Listo.")));
        };
        core_.addTool(std::move(t));
    }
}

} // namespace ba::app::mcp
