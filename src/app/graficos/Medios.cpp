// ============================================================================
// src/app/graficos/Medios.cpp — ver Medios.hpp
// ============================================================================
#include "graficos/Medios.hpp"

#include "graficos/Catalogo.hpp"

#include <gst/app/gstappsink.h>
#include <gst/gst.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <thread>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QMetaObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>

namespace ba::app::graficos {
namespace {

const QStringList& extensionesDeImagen() {
    static const QStringList l{"gif", "png", "jpg", "jpeg", "webp", "apng"};
    return l;
}

/// Un nombre que sirve como archivo o carpeta adentro de medios/.
QString limpio(const QString& n) {
    return QString(n).replace(' ', '-').remove(QRegularExpression("[^A-Za-z0-9_.-]")).left(64);
}

QJsonObject leerJson(const QString& ruta) {
    QFile f(ruta);
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject{};
}

/// Copia los cuadros de una carpeta (una secuencia que ya viene hecha).
bool copiarCarpeta(const QString& desde, const QString& hasta, int* cuadros) {
    QDir().mkpath(hasta);
    int n = 0;
    for (const QFileInfo& fi : QDir(desde).entryInfoList(QDir::Files)) {
        const QString ext = fi.suffix().toLower();
        if (!extensionesDeImagen().contains(ext) && fi.fileName() != "secuencia.json") continue;
        const QString dst = QDir(hasta).filePath(fi.fileName());
        QFile::remove(dst);
        if (!QFile::copy(fi.filePath(), dst)) return false;
        if (ext == "png") ++n;
    }
    if (cuadros != nullptr) *cuadros = n;
    return true;
}

} // namespace

struct Medios::Impl {
    explicit Impl(Catalogo& c) : catalogo(c) {}
    Catalogo& catalogo;
    std::atomic<int> enCurso{0};
};

Medios::Medios(Catalogo& catalogo, QObject* padre) : QObject(padre), d_(std::make_unique<Impl>(catalogo)) {}
Medios::~Medios() = default;

bool Medios::importando() const { return d_->enCurso.load() > 0; }

bool Medios::esVideo(const QString& archivo) {
    static const QStringList l{"webm", "mov", "mp4", "m4v", "mkv", "avi", "mxf", "mpg", "mpeg", "ts", "ogv"};
    return l.contains(QFileInfo(archivo).suffix().toLower());
}

QJsonArray Medios::lista(const QString& grafico) const {
    QJsonArray out;
    const QString c = d_->catalogo.carpeta(grafico);
    if (c.isEmpty()) return out;
    const QDir dir(QDir(c).filePath("medios"));
    for (const QFileInfo& fi : dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (fi.fileName().startsWith('.')) continue;
        QJsonObject m{{"archivo", "medios/" + fi.fileName()}, {"nombre", fi.completeBaseName()}};
        if (fi.isDir()) {
            const QJsonObject meta = leerJson(QDir(fi.filePath()).filePath("secuencia.json"));
            const int cuadros = meta.contains("cuadros") ? meta.value("cuadros").toInt()
                                                         : static_cast<int>(QDir(fi.filePath()).entryList({"*.png"}, QDir::Files).size());
            m["nombre"] = fi.fileName();
            m["tipo"] = "secuencia";
            m["cuadros"] = cuadros;
            m["fps"] = meta.value("fps").toDouble(30);
            if (meta.contains("alfa")) m["alfa"] = meta.value("alfa");
        } else if (extensionesDeImagen().contains(fi.suffix().toLower())) {
            QImageReader r(fi.filePath());
            const bool animado = r.supportsAnimation() && r.imageCount() != 1;
            m["tipo"] = animado ? "animado" : "imagen";
            if (r.imageCount() > 0) m["cuadros"] = r.imageCount();
        } else {
            continue;
        }
        out.append(m);
    }
    return out;
}

QString Medios::quitar(const QString& grafico, const QString& archivo) {
    QString err;
    const QString c = d_->catalogo.carpeta(grafico, &err);
    if (c.isEmpty()) return err;
    // Solo lo que está adentro de medios/: «../grafico.json» no se borra.
    const QString nombre = QFileInfo(archivo).fileName();
    if (nombre.isEmpty() || nombre.startsWith('.') || archivo.contains("..")) return tr("ese medio no sirve");
    const QString origen = QDir(QDir(c).filePath("medios")).filePath(nombre);
    if (!QFileInfo::exists(origen)) return tr("el gráfico no tiene «%1»").arg(nombre);
    const QString papelera = QDir(c).filePath("medios/.papelera");
    QDir().mkpath(papelera);
    const QString destino = QDir(papelera).filePath(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-") + nombre);
    if (!QDir().rename(origen, destino)) return tr("no se pudo sacar «%1»").arg(nombre);
    emit cambio(grafico);
    return {};
}

void Medios::importar(const QString& grafico, const QString& archivoPedido, const QJsonObject& opciones,
                      std::function<void(QJsonObject, QString)> listo) {
    QString err;
    const QString c = d_->catalogo.carpeta(grafico, &err);
    if (c.isEmpty()) return listo({}, err);
    // Viene como ruta o como URL (el diálogo de archivos de la interfaz).
    QString archivo = archivoPedido;
    if (archivo.startsWith("file:")) archivo = QUrl(archivo).toLocalFile();
    const QFileInfo fi(archivo);
    if (!fi.exists()) return listo({}, tr("no existe «%1»").arg(archivo));
    const QString medios = QDir(c).filePath("medios");
    QDir().mkpath(medios);
    const QString base = limpio(opciones.value("nombre").toString(fi.isDir() ? fi.fileName() : fi.completeBaseName()));
    if (base.isEmpty()) return listo({}, tr("ese nombre no sirve"));

    if (fi.isDir()) {
        int cuadros = 0;
        const QString destino = QDir(medios).filePath(base);
        if (!copiarCarpeta(fi.filePath(), destino, &cuadros)) return listo({}, tr("no se pudo copiar la secuencia"));
        if (cuadros == 0) return listo({}, tr("la carpeta no tiene cuadros PNG"));
        emit cambio(grafico);
        return listo(QJsonObject{{"archivo", "medios/" + base}, {"tipo", "secuencia"}, {"cuadros", cuadros}}, {});
    }
    const QString ext = fi.suffix().toLower();
    if (extensionesDeImagen().contains(ext)) {
        const QString destino = QDir(medios).filePath(base + "." + ext);
        QFile::remove(destino);
        if (!QFile::copy(fi.filePath(), destino)) return listo({}, tr("no se pudo copiar «%1»").arg(fi.fileName()));
        QImageReader r(destino);
        const QImage primero = r.read();
        if (primero.isNull()) {
            QFile::remove(destino);
            return listo({}, tr("«%1» no es una imagen que se pueda leer").arg(fi.fileName()));
        }
        emit cambio(grafico);
        return listo(QJsonObject{{"archivo", "medios/" + base + "." + ext},
                                 {"tipo", r.supportsAnimation() && r.imageCount() != 1 ? "animado" : "imagen"},
                                 {"ancho", primero.width()},
                                 {"alto", primero.height()},
                                 {"alfa", primero.hasAlphaChannel()}},
                     {});
    }
    if (!esVideo(archivo))
        return listo({}, tr("no sé usar «%1» en un gráfico (GIF, PNG, JPG, WebP, una carpeta de PNG o un video)").arg(fi.fileName()));

    // Un video: a cuadros, en otro hilo (un clip de 10 s a 1080p son varios segundos de PNG).
    const QString destino = QDir(medios).filePath(base);
    const int ancho = opciones.value("ancho").toInt(0);
    const int maxSeg = std::clamp(opciones.value("maxSegundos").toInt(20), 1, 120);
    ++d_->enCurso;
    emit importandoChanged();
    QPointer<Medios> yo(this);
    std::thread([yo, archivo, destino, base, ancho, maxSeg, grafico, listo = std::move(listo)]() mutable {
        QString e;
        QJsonObject r = videoACuadros(archivo, destino, ancho, maxSeg, &e);
        if (e.isEmpty()) r["archivo"] = "medios/" + base;
        if (QCoreApplication::instance() == nullptr) return; // la app se cerró mientras tanto
        QMetaObject::invokeMethod(
            qApp,
            [yo, r, e, grafico, listo = std::move(listo)]() {
                if (yo) {
                    --yo->d_->enCurso;
                    emit yo->importandoChanged();
                    if (e.isEmpty()) emit yo->cambio(grafico);
                }
                listo(r, e);
            },
            Qt::QueuedConnection);
    }).detach();
}

QJsonObject Medios::videoACuadros(const QString& video, const QString& destino, int ancho, int maxSegundos,
                                  QString* error) {
    const auto fallar = [error](const QString& m) {
        if (error != nullptr) *error = m;
        return QJsonObject{};
    };
    if (!gst_is_initialized()) gst_init(nullptr, nullptr);
    gchar* uri = gst_filename_to_uri(video.toUtf8().constData(), nullptr);
    if (uri == nullptr) return fallar(QCoreApplication::translate("graficos", "no entiendo la ruta %1").arg(video));
    // RGBA con su alfa. decodebin elige solo el decodificador con alfa cuando el
    // archivo lo trae: vp9alphadecodebin para un WebM VP9 con alfa (codecalpha),
    // avdec_prores para un ProRes 4444.
    const std::string desc = std::string("uridecodebin uri=\"") + uri +
                             "\" ! videoconvert ! video/x-raw,format=RGBA ! appsink name=s sync=false max-buffers=4";
    g_free(uri);
    GError* gerr = nullptr;
    GstElement* p = gst_parse_launch(desc.c_str(), &gerr);
    if (p == nullptr || gerr != nullptr) {
        const QString m = QCoreApplication::translate("graficos", "no se puede leer el video: %1").arg(gerr ? gerr->message : "?");
        if (gerr) g_error_free(gerr);
        if (p) gst_object_unref(p);
        return fallar(m);
    }
    // Lo que había con ese nombre se reemplaza entero (no quedan cuadros viejos de más).
    QDir(destino).removeRecursively();
    QDir().mkpath(destino);
    GstElement* s = gst_bin_get_by_name(GST_BIN(p), "s");
    gst_element_set_state(p, GST_STATE_PLAYING);
    int n = 0, w = 0, h = 0, fpsN = 0, fpsD = 1, tope = 0;
    bool alfa = false;
    QString errEscritura;
    for (;;) {
        GstSample* m = gst_app_sink_try_pull_sample(GST_APP_SINK(s), 8 * GST_SECOND);
        if (m == nullptr) break; // fin del video, error o nada en 8 s
        if (n == 0) {
            if (GstCaps* caps = gst_sample_get_caps(m)) {
                const GstStructure* st = gst_caps_get_structure(caps, 0);
                gst_structure_get_int(st, "width", &w);
                gst_structure_get_int(st, "height", &h);
                gst_structure_get_fraction(st, "framerate", &fpsN, &fpsD);
            }
            if (fpsN <= 0 || fpsD <= 0) fpsN = 30, fpsD = 1; // un video sin framerate fijo
            tope = std::max(1, maxSegundos * fpsN / fpsD);
        }
        GstBuffer* b = gst_sample_get_buffer(m);
        GstMapInfo mi;
        if (w > 0 && h > 0 && b != nullptr && gst_buffer_map(b, &mi, GST_MAP_READ)) {
            const size_t paso = mi.size / static_cast<size_t>(h); // la fila puede venir con relleno
            QImage img(w, h, QImage::Format_RGBA8888);
            for (int y = 0; y < h; ++y)
                std::memcpy(img.scanLine(y), mi.data + paso * static_cast<size_t>(y), static_cast<size_t>(w) * 4);
            gst_buffer_unmap(b, &mi);
            // ¿Trae alfa de verdad? Si el decodificador no lo entendió, todo sale opaco:
            // se avisa, porque un «video con alfa» que tapa todo es lo peor al aire.
            if (!alfa)
                for (int y = 0; y < h && !alfa; y += 7) {
                    const uchar* fila = img.constScanLine(y);
                    for (int x = 0; x < w; x += 7)
                        if (fila[x * 4 + 3] < 250) {
                            alfa = true;
                            break;
                        }
                }
            if (ancho > 0 && ancho < w) img = img.scaledToWidth(ancho, Qt::SmoothTransformation);
            if (!img.save(QDir(destino).filePath(QString("%1.png").arg(n, 5, 10, QChar('0'))), "PNG")) {
                errEscritura = QCoreApplication::translate("graficos", "no se pudo escribir el cuadro %1").arg(n);
                gst_sample_unref(m);
                break;
            }
            ++n;
        }
        gst_sample_unref(m);
        if (n >= tope) break;
    }
    QString errBus;
    if (n == 0) {
        GstBus* bus = gst_element_get_bus(p);
        if (GstMessage* msg = gst_bus_pop_filtered(bus, GST_MESSAGE_ERROR)) {
            GError* e = nullptr;
            gst_message_parse_error(msg, &e, nullptr);
            errBus = QCoreApplication::translate("graficos", "no se puede leer el video: %1").arg(e ? e->message : "?");
            if (e) g_error_free(e);
            gst_message_unref(msg);
        }
        gst_object_unref(bus);
    }
    gst_element_set_state(p, GST_STATE_NULL);
    gst_object_unref(s);
    gst_object_unref(p);
    if (!errEscritura.isEmpty()) return fallar(errEscritura);
    if (n == 0) {
        QDir(destino).removeRecursively();
        return fallar(errBus.isEmpty() ? QCoreApplication::translate("graficos", "el archivo no tiene video que se pueda leer") : errBus);
    }
    const double fps = static_cast<double>(fpsN) / fpsD;
    const int anchoFinal = ancho > 0 && ancho < w ? ancho : w;
    const int altoFinal = ancho > 0 && ancho < w ? h * ancho / w : h;
    const QJsonObject meta{{"fps", fps},       {"formato", "png"}, {"cuadros", n}, {"ancho", anchoFinal},
                           {"alto", altoFinal}, {"alfa", alfa},     {"origen", QFileInfo(video).fileName()},
                           {"cortado", n >= tope}};
    QSaveFile f(QDir(destino).filePath("secuencia.json"));
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(meta).toJson()) < 0 || !f.commit())
        return fallar(QCoreApplication::translate("graficos", "no se pudo escribir secuencia.json"));
    if (error != nullptr) error->clear();
    QJsonObject r = meta;
    r["tipo"] = "secuencia";
    return r;
}

} // namespace ba::app::graficos
