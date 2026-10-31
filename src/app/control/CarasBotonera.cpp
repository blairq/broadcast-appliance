// src/app/control/CarasBotonera.cpp — ver CarasBotonera.hpp

#include "control/CarasBotonera.hpp"

#include "arranque/AppRuntime.hpp"
#include "emision/BroadcastEngine.hpp"
#include "platform/Hilo.hpp"

#include <gst/app/gstappsink.h>
#include <gst/gst.h>

#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QPointer>
#include <QRandomGenerator>
#include <QTimer>
#include <QUrl>

#include <memory>
#include <thread>

namespace ba::app::control {

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
QString rutaDe(const QString& archivo) {
    const QUrl u(archivo);
    return u.isLocalFile() ? u.toLocalFile() : archivo;
}

QImage cuadrado(const QImage& img, int lado) {
    const int l = std::min(img.width(), img.height());
    return img.copy((img.width() - l) / 2, (img.height() - l) / 2, l, l)
        .scaled(lado, lado, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
        .convertToFormat(QImage::Format_RGB888);
}

QImage deRgba(const std::vector<std::uint8_t>& rgba, int w, int h) {
    if (w <= 0 || h <= 0 || rgba.size() < static_cast<size_t>(w) * h * 4) return {};
    return QImage(rgba.data(), w, h, w * 4, QImage::Format_RGBA8888).copy();
}

/// Los primeros `max` cuadros de un video, a 8 cuadros/s, con GStreamer.
QVector<QImage> cuadrosDeVideo(const QString& ruta, int max, QString& error) {
    QVector<QImage> out;
    gchar* uri = gst_filename_to_uri(ruta.toUtf8().constData(), nullptr);
    if (uri == nullptr) {
        error = QCoreApplication::translate("caras", "No entiendo la ruta %1").arg(ruta);
        return out;
    }
    const std::string desc = std::string("uridecodebin uri=\"") + uri +
                             "\" ! videoconvert ! videoscale ! videorate ! "
                             "video/x-raw,format=RGBA,height=240,pixel-aspect-ratio=1/1,framerate=" +
                             std::to_string(CarasBotonera::kFps) + "/1 ! appsink name=s sync=false max-buffers=" +
                             std::to_string(max + 4);
    g_free(uri);
    GError* err = nullptr;
    GstElement* p = gst_parse_launch(desc.c_str(), &err);
    if (p == nullptr || err != nullptr) {
        error = QCoreApplication::translate("caras", "No se puede leer el video: %1").arg(err ? err->message : "?");
        if (err) g_error_free(err);
        if (p) gst_object_unref(p);
        return out;
    }
    GstElement* s = gst_bin_get_by_name(GST_BIN(p), "s");
    gst_element_set_state(p, GST_STATE_PLAYING);
    while (out.size() < max) {
        GstSample* m = gst_app_sink_try_pull_sample(GST_APP_SINK(s), 8 * GST_SECOND);
        if (m == nullptr) break; // fin del archivo, error o nada en 8 s
        GstCaps* caps = gst_sample_get_caps(m);
        GstStructure* st = caps ? gst_caps_get_structure(caps, 0) : nullptr;
        int w = 0, h = 0;
        if (st) {
            gst_structure_get_int(st, "width", &w);
            gst_structure_get_int(st, "height", &h);
        }
        GstBuffer* b = gst_sample_get_buffer(m);
        GstMapInfo mi;
        if (w > 0 && h > 0 && b && gst_buffer_map(b, &mi, GST_MAP_READ)) {
            // El paso de fila puede tener relleno: se copia de a fila.
            const int paso = static_cast<int>(mi.size / h);
            QImage img(w, h, QImage::Format_RGBA8888);
            for (int y = 0; y < h; ++y) memcpy(img.scanLine(y), mi.data + y * paso, static_cast<size_t>(w) * 4);
            out.push_back(img);
            gst_buffer_unmap(b, &mi);
        }
        gst_sample_unref(m);
    }
    if (out.isEmpty()) {
        GstBus* bus = gst_element_get_bus(p);
        if (GstMessage* msg = gst_bus_pop_filtered(bus, GST_MESSAGE_ERROR)) {
            GError* e = nullptr;
            gst_message_parse_error(msg, &e, nullptr);
            error = QCoreApplication::translate("caras", "No se puede leer el video: %1").arg(e ? e->message : "?");
            if (e) g_error_free(e);
            gst_message_unref(msg);
        } else {
            error = QCoreApplication::translate("caras", "El archivo no tiene video que se pueda leer");
        }
        gst_object_unref(bus);
    }
    gst_element_set_state(p, GST_STATE_NULL);
    gst_object_unref(s);
    gst_object_unref(p);
    return out;
}
} // namespace

CarasBotonera::CarasBotonera(AppRuntime& rt, fs::path dataRoot, QObject* parent)
    : QObject(parent), rt_(rt), raiz_(std::move(dataRoot)) {}

fs::path CarasBotonera::carpeta(const QString& id) const {
    return raiz_ / "Media" / "botonera" / id.toStdWString();
}

QString CarasBotonera::raizUrl() const {
    return QUrl::fromLocalFile(QString::fromStdWString((raiz_ / "Media" / "botonera").wstring()) + "/").toString();
}

QString CarasBotonera::nuevoId() const {
    return QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + "-" +
           QString::number(QRandomGenerator::global()->bounded(0x10000), 16);
}

void CarasBotonera::empezar(const QString& texto) {
    ++enCurso_;
    estado_ = texto;
    emit cambio();
}

void CarasBotonera::terminar(const QString& id, const QString& error) {
    enCurso_ = std::max(0, enCurso_ - 1);
    estado_ = error;
    emit cambio();
    emit lista(id, error);
}

QString CarasBotonera::guardar(const QString& id, const QVector<QImage>& cuadros, const QImage& grande,
                               const QString& original) {
    if (cuadros.isEmpty()) return QCoreApplication::translate("caras", "No llegó ninguna imagen");
    const fs::path dir = carpeta(id);
    std::error_code ec;
    fs::create_directories(dir, ec);
    const QString base = QString::fromStdWString(dir.wstring());
    for (int i = 0; i < cuadros.size(); ++i)
        if (!cuadrado(cuadros[i], kLado).save(base + "/" + QString::fromStdString(archivoCuadro(i)), "JPG", 82))
            return QCoreApplication::translate("caras", "No se pudo guardar en %1").arg(base);
    if (!original.isEmpty()) {
        const QString ext = QFileInfo(original).suffix().toLower();
        QFile::copy(original, base + "/medio." + (ext.isEmpty() ? QStringLiteral("bin") : ext));
    } else if (!grande.isNull()) {
        const QImage g = grande.width() > 1280 ? grande.scaledToWidth(1280, Qt::SmoothTransformation) : grande;
        g.convertToFormat(QImage::Format_RGB888).save(base + "/medio.jpg", "JPG", 90);
    }
    return {};
}

QVariantMap CarasBotonera::importar(const QString& archivo) {
    const QString ruta = rutaDe(archivo);
    if (!QFileInfo::exists(ruta)) return {{"error", QCoreApplication::translate("caras", "No existe %1").arg(ruta)}};
    const QString id = nuevoId();
    empezar(QCoreApplication::translate("caras", "Leyendo %1…").arg(QFileInfo(ruta).fileName()));
    QPointer<CarasBotonera> yo(this);
    std::thread([yo, ruta, id]() {
        ba::platform::nombrarHilo("caras-botonera");
        QVector<QImage> cuadros;
        QImage grande;
        QString error;
        QImageReader r(ruta);
        if (r.canRead()) {
            if (r.supportsAnimation() && r.imageCount() != 1) {
                // GIF/WebP animado: un cuadro cada 1/8 s según las demoras propias.
                int t = 0, siguiente = 0;
                while (cuadros.size() < kFps * kMaxSegundos) {
                    const QImage img = r.read();
                    if (img.isNull()) break;
                    if (t >= siguiente) {
                        cuadros.push_back(img);
                        siguiente += 1000 / kFps;
                    }
                    t += std::max(20, r.nextImageDelay());
                    if (!r.canRead()) break;
                }
            } else {
                r.setAutoTransform(true);
                grande = r.read();
                if (!grande.isNull()) cuadros.push_back(grande);
            }
            if (cuadros.isEmpty()) error = QCoreApplication::translate("caras", "No se pudo leer la imagen: %1").arg(r.errorString());
        } else {
            cuadros = cuadrosDeVideo(ruta, kFps * kMaxSegundos, error);
        }
        QMetaObject::invokeMethod(
            qApp,
            [yo, id, cuadros, ruta, error]() {
                if (!yo) return;
                QString e = error;
                if (e.isEmpty()) e = yo->guardar(id, cuadros, {}, ruta);
                yo->terminar(id, e);
            },
            Qt::QueuedConnection);
    }).detach();
    return {{"id", id}};
}

QVariantMap CarasBotonera::capturarFoto(const QString& entrada) {
    if (entrada.isEmpty()) return {{"error", QCoreApplication::translate("caras", "Elegí una cámara")}};
    const QString id = nuevoId();
    empezar(QCoreApplication::translate("caras", "Sacando la foto…"));
    QPointer<CarasBotonera> yo(this);
    rt_.engine().capturarAparte(
        entrada.toStdString(), 1280, [yo, id, entrada](const std::string& e, std::vector<std::uint8_t> rgba, int w, int h) {
            const QImage img = e.empty() ? deRgba(rgba, w, h) : QImage();
            QMetaObject::invokeMethod(
                qApp,
                [yo, id, img, entrada]() {
                    if (!yo) return;
                    if (img.isNull()) {
                        yo->terminar(id, QCoreApplication::translate("caras", "«%1» no da imagen ahora").arg(entrada));
                        return;
                    }
                    yo->terminar(id, yo->guardar(id, {img}, img));
                },
                Qt::QueuedConnection);
        });
    return {{"id", id}};
}

QVariantMap CarasBotonera::grabar(const QString& entrada, double segundos) {
    if (entrada.isEmpty()) return {{"error", QCoreApplication::translate("caras", "Elegí una cámara")}};
    const int n = std::clamp(static_cast<int>(std::lround(segundos * kFps)), kFps, kFps * kMaxSegundos);
    const QString id = nuevoId();
    empezar(QCoreApplication::translate("caras", "Grabando %1 s…").arg(n / kFps));
    // Un cuadro cada 1/8 s; cada uno llega cuando llega (en otro hilo).
    struct Toma {
        QVector<QImage> cuadros;
        int pedidos = 0, llegados = 0;
        bool cerrada = false;
    };
    auto toma = std::make_shared<Toma>();
    toma->cuadros.resize(n);
    QPointer<CarasBotonera> yo(this);
    auto cerrar = [yo, toma, id, entrada]() {
        if (!yo || toma->cerrada) return;
        toma->cerrada = true;
        QVector<QImage> buenos;
        for (const QImage& i : toma->cuadros)
            if (!i.isNull()) buenos.push_back(i);
        if (buenos.isEmpty()) {
            yo->terminar(id, QCoreApplication::translate("caras", "«%1» no da imagen ahora").arg(entrada));
            return;
        }
        yo->terminar(id, yo->guardar(id, buenos, {}));
    };
    auto* reloj = new QTimer(this);
    reloj->setInterval(1000 / kFps);
    connect(reloj, &QTimer::timeout, this, [this, reloj, toma, n, entrada, cerrar, yo]() {
        if (toma->pedidos >= n) {
            reloj->deleteLater();
            // Lo que no llegó en 3 s, no llega: se guarda lo que hay.
            QTimer::singleShot(3000, this, cerrar);
            return;
        }
        const int i = toma->pedidos++;
        estado_ = QCoreApplication::translate("caras", "Grabando… %1 s").arg((n - i + kFps - 1) / kFps);
        emit cambio();
        rt_.engine().capturarAparte(
            entrada.toStdString(), 320, [yo, toma, i, n, cerrar](const std::string& e, std::vector<std::uint8_t> rgba, int w, int h) {
                const QImage img = e.empty() ? deRgba(rgba, w, h) : QImage();
                QMetaObject::invokeMethod(
                    qApp,
                    [toma, i, n, img, cerrar]() {
                        toma->cuadros[i] = img;
                        if (++toma->llegados == n) cerrar();
                    },
                    Qt::QueuedConnection);
            });
    });
    reloj->start();
    return {{"id", id}};
}

QVariantMap CarasBotonera::info(const QString& id) const {
    const QString base = QString::fromStdWString(carpeta(id).wstring());
    if (id.isEmpty() || !QFileInfo::exists(base + "/0000.jpg")) return {};
    QDir d(base);
    const int n = static_cast<int>(d.entryList({"[0-9][0-9][0-9][0-9].jpg"}, QDir::Files).size());
    const QStringList medio = d.entryList({"medio.*"}, QDir::Files);
    const qint64 v = QFileInfo(base + "/0000.jpg").lastModified().toMSecsSinceEpoch();
    return {{"id", id},
            {"url", QUrl::fromLocalFile(base + "/0000.jpg").toString() + "?v=" + QString::number(v)},
            {"base", QUrl::fromLocalFile(base + "/").toString()},
            {"cuadros", n},
            {"video", n > 1},
            {"medio", medio.isEmpty() ? QString() : "Media/botonera/" + id + "/" + medio.first()}};
}

std::string CarasBotonera::rutaPoster(const std::string& id) const {
    const fs::path p = carpeta(QString::fromStdString(id)) / "0000.jpg";
    std::error_code ec;
    return fs::exists(p, ec) ? QString::fromStdWString(p.wstring()).toStdString() : std::string();
}

std::string CarasBotonera::archivoCuadro(int n) { return QString("%1.jpg").arg(n, 4, 10, QChar('0')).toStdString(); }

json CarasBotonera::cara(const std::string& id) const {
    auto it = cuantos_.find(id);
    if (it == cuantos_.end() || it->second == 0) {
        const QString base = QString::fromStdWString(carpeta(QString::fromStdString(id)).wstring());
        const int n = static_cast<int>(QDir(base).entryList({"[0-9][0-9][0-9][0-9].jpg"}, QDir::Files).size());
        if (n == 0) return nullptr; // todavía no (o la borraron): no se recuerda
        if (cuantos_.size() > 512) cuantos_.clear();
        it = cuantos_.emplace(id, n).first;
    }
    return {{"id", id}, {"cuadros", it->second}, {"fps", kFps}};
}

} // namespace ba::app::control
