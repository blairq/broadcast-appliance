// src/app/tests/test_graficos_medios.cpp — los medios de un gráfico (graficos/Medios)
//
//   1. Una imagen se copia a medios/ y la lista la muestra (con su tipo).
//   2. Una carpeta de PNG (una secuencia) se copia entera.
//   3. Lo que no es imagen ni video, o no existe, es un error que se entiende.
//   4. Quitar lo manda a la papelera del gráfico, y nunca sale de medios/.
//   5. Un video se pasa a cuadros PNG con su secuencia.json (GStreamer): con alfa,
//      la transparencia llega a los cuadros; sin alfa, se avisa.
//
// En una carpeta temporal; los videos de prueba los arma el propio GStreamer
// (sin GStreamer o sin sus codificadores, esa parte se saltea).

#include "graficos/Catalogo.hpp"
#include "graficos/Medios.hpp"

#if defined(BA_HAVE_GST)
#include <gst/gst.h>

#include "video/CatalogoGst.hpp"
#endif

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

using ba::app::graficos::Catalogo;
using ba::app::graficos::Medios;

namespace {

int fallos = 0;

#define CHECK(cond, msg)                                    \
    do {                                                    \
        if (!(cond)) {                                      \
            std::fprintf(stderr, "FALLA: %s\n", msg);       \
            ++fallos;                                       \
        }                                                   \
    } while (0)

/// importar() y esperar la respuesta (un video la da desde otro hilo).
QJsonObject importar(Medios& m, const QString& grafico, const QString& archivo, const QJsonObject& op, QString* err) {
    QEventLoop bucle;
    QJsonObject r;
    bool listo = false;
    m.importar(grafico, archivo, op, [&](QJsonObject res, QString e) {
        r = res;
        *err = e;
        listo = true;
        bucle.quit();
    });
    if (!listo) {
        QTimer::singleShot(60000, &bucle, &QEventLoop::quit);
        bucle.exec();
    }
    return r;
}

QImage cuadro(int w, int h, QColor c) {
    QImage i(w, h, QImage::Format_ARGB32);
    i.fill(c);
    return i;
}

#if defined(BA_HAVE_GST)
/// Un video corto armado por GStreamer. false si este GStreamer no tiene con qué.
bool armarVideo(const QString& desc) {
    GError* e = nullptr;
    GstElement* p = gst_parse_launch(desc.toUtf8().constData(), &e);
    if (p == nullptr || e != nullptr) {
        if (e) g_error_free(e);
        if (p) gst_object_unref(p);
        return false;
    }
    gst_element_set_state(p, GST_STATE_PLAYING);
    GstBus* bus = gst_element_get_bus(p);
    GstMessage* m = gst_bus_timed_pop_filtered(bus, 20 * GST_SECOND,
                                               static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
    const bool ok = m != nullptr && GST_MESSAGE_TYPE(m) == GST_MESSAGE_EOS;
    if (m) gst_message_unref(m);
    gst_object_unref(bus);
    gst_element_set_state(p, GST_STATE_NULL);
    gst_object_unref(p);
    return ok;
}
#endif

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "carpeta temporal");
    Catalogo cat(tmp.path().toStdString());
    QString creado, err;
    CHECK(cat.crear("logo", true, &creado).isEmpty(), "un gráfico para probar");
    Medios m(cat);
    int cambios = 0;
    QObject::connect(&m, &Medios::cambio, [&](const QString& g) { cambios += g == "logo" ? 1 : 0; });

    // 1) Una imagen: se copia, con su tipo y si tiene alfa.
    const QString png = QDir(tmp.path()).filePath("bandera.png");
    cuadro(40, 20, QColor(255, 0, 0, 128)).save(png);
    QJsonObject r = importar(m, "logo", png, {}, &err);
    CHECK(err.isEmpty() && r.value("archivo").toString() == "medios/bandera.png", "imagen: queda en medios/");
    CHECK(r.value("tipo").toString() == "imagen" && r.value("alfa").toBool(), "imagen: tipo y alfa");
    CHECK(QFile::exists(QDir(cat.carpeta("logo")).filePath("medios/bandera.png")), "imagen: el archivo está");
    CHECK(cambios == 1, "imagen: avisa el cambio");
    r = importar(m, "logo", png, QJsonObject{{"nombre", "Otra Bandera!"}}, &err);
    CHECK(err.isEmpty() && r.value("archivo").toString() == "medios/Otra-Bandera.png", "imagen: con otro nombre, limpio");

    // 2) Una secuencia ya hecha (carpeta de PNG).
    const QString sec = QDir(tmp.path()).filePath("cuadros");
    QDir().mkpath(sec);
    for (int i = 0; i < 5; ++i) cuadro(16, 16, QColor(i * 50, 0, 0, 200)).save(QDir(sec).filePath(QString("c%1.png").arg(i)));
    r = importar(m, "logo", sec, {}, &err);
    CHECK(err.isEmpty() && r.value("tipo").toString() == "secuencia" && r.value("cuadros").toInt() == 5,
          "secuencia: se copian los cinco");
    const QJsonArray lista = m.lista("logo");
    CHECK(lista.size() == 3, "lista: los tres medios");
    bool haySecuencia = false;
    for (const QJsonValue& v : lista)
        haySecuencia = haySecuencia || (v.toObject().value("tipo").toString() == "secuencia" &&
                                        v.toObject().value("cuadros").toInt() == 5 &&
                                        v.toObject().value("archivo").toString() == "medios/cuadros");
    CHECK(haySecuencia, "lista: la secuencia, con sus cuadros");

    // 3) Errores que se entienden.
    importar(m, "logo", QDir(tmp.path()).filePath("no-esta.gif"), {}, &err);
    CHECK(err.contains("no existe"), "un archivo que no está");
    QFile txt(QDir(tmp.path()).filePath("notas.txt"));
    txt.open(QIODevice::WriteOnly);
    txt.write("hola");
    txt.close();
    importar(m, "logo", txt.fileName(), {}, &err);
    CHECK(err.contains("no sé usar"), "algo que no es imagen ni video");
    importar(m, "no-existe", png, {}, &err);
    CHECK(!err.isEmpty(), "un gráfico que no existe");

    // 4) Quitar: a la papelera, y solo adentro de medios/.
    CHECK(m.quitar("logo", "medios/bandera.png").isEmpty(), "quitar");
    CHECK(!QFile::exists(QDir(cat.carpeta("logo")).filePath("medios/bandera.png")), "quitar: ya no está");
    CHECK(!QDir(QDir(cat.carpeta("logo")).filePath("medios/.papelera")).isEmpty(), "quitar: va a la papelera");
    CHECK(m.lista("logo").size() == 2, "quitar: la lista se achica (la papelera no cuenta)");
    CHECK(!m.quitar("logo", "../grafico.json").isEmpty() && QFile::exists(QDir(cat.carpeta("logo")).filePath("grafico.json")),
          "quitar: nunca fuera de medios/");

    // 5) Videos a cuadros.
#if defined(BA_HAVE_GST)
#if defined(BA_TEST_GST_RAIZ)
    g_setenv("BA_GST_RAIZ", BA_TEST_GST_RAIZ, FALSE); // el vendoreado, no el del sistema
#endif
    std::string e;
    if (!ba::app::video::iniciarGStreamer(&e)) {
        std::printf("(sin GStreamer: se saltean los videos: %s)\n", e.c_str());
    } else {
        // Sin alfa: 12 cuadros a 30 por segundo, en VP8.
        const QString webm = QDir(tmp.path()).filePath("opaco.webm");
        if (armarVideo("videotestsrc num-buffers=12 ! video/x-raw,width=64,height=48,framerate=30/1 ! vp8enc ! "
                       "webmmux ! filesink location=\"" + webm + "\"")) {
            r = importar(m, "logo", webm, {}, &err);
            CHECK(err.isEmpty(), qUtf8Printable("video: se convierte (" + err + ")"));
            CHECK(r.value("tipo").toString() == "secuencia" && r.value("cuadros").toInt() == 12, "video: los 12 cuadros");
            CHECK(qAbs(r.value("fps").toDouble() - 30) < 0.01, "video: su framerate");
            CHECK(!r.value("alfa").toBool(), "video sin alfa: se avisa (alfa: false)");
            const QJsonObject meta = QJsonDocument::fromJson([&] {
                QFile f(QDir(cat.carpeta("logo")).filePath("medios/opaco/secuencia.json"));
                f.open(QIODevice::ReadOnly);
                return f.readAll();
            }()).object();
            CHECK(meta.value("cuadros").toInt() == 12 && meta.value("formato").toString() == "png", "video: secuencia.json");
            CHECK(QFile::exists(QDir(cat.carpeta("logo")).filePath("medios/opaco/00011.png")), "video: el último cuadro");
            // Achicado.
            r = importar(m, "logo", webm, QJsonObject{{"nombre", "chico"}, {"ancho", 32}}, &err);
            CHECK(err.isEmpty() && r.value("ancho").toInt() == 32 && r.value("alto").toInt() == 24, "video: achicado");
            CHECK(QImage(QDir(cat.carpeta("logo")).filePath("medios/chico/00000.png")).width() == 32,
                  "video: los cuadros achicados");
            // Cortado a un máximo de segundos.
            const QJsonObject corto =
                Medios::videoACuadros(webm, QDir(tmp.path()).filePath("corto"), 0, 0, &err); // 0 -> 1 cuadro como mínimo
            CHECK(err.isEmpty() && corto.value("cuadros").toInt() >= 1, "videoACuadros: con tope");
        } else {
            std::printf("(este GStreamer no codifica VP8: se saltea el video opaco)\n");
        }
        // Con alfa: PNG con transparencia -> qtmux con png (lo decodifica cualquier GStreamer, con alfa).
        const QString mov = QDir(tmp.path()).filePath("alfa.mov");
        if (armarVideo("videotestsrc num-buffers=6 pattern=ball background-color=0x00000000 ! "
                       "video/x-raw,format=RGBA,width=64,height=48,framerate=30/1 ! pngenc ! qtmux ! filesink location=\"" +
                       mov + "\"")) {
            r = importar(m, "logo", mov, {}, &err);
            CHECK(err.isEmpty() && r.value("cuadros").toInt() == 6, qUtf8Printable("video con alfa: se convierte (" + err + ")"));
            CHECK(r.value("alfa").toBool(), "video con alfa: la transparencia llega");
            const QImage c0(QDir(cat.carpeta("logo")).filePath("medios/alfa/00000.png"));
            CHECK(c0.hasAlphaChannel() && qAlpha(c0.pixel(0, 0)) < 255, "video con alfa: el cuadro es transparente");
        } else {
            std::printf("(este GStreamer no arma un .mov con PNG: se saltea el video con alfa)\n");
        }
        importar(m, "logo", txt.fileName() + ".webm", {}, &err);
        CHECK(!err.isEmpty(), "un video que no está");
    }
#else
    std::printf("(compilado sin GStreamer: se saltean los videos)\n");
#endif

    std::printf(fallos == 0 ? "OK: medios de los gráficos\n" : "%d fallos\n", fallos);
    return fallos == 0 ? 0 : 1;
}
