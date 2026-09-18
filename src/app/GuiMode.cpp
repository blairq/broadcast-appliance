// ============================================================================
// src/app/GuiMode.cpp — ba-app con el dashboard QML (ver GuiMode.hpp)
//
// Arma las tres capas y las conecta:
//   modelo     AppRuntime (motor + configuracion)
//   viewmodel  StudioViewModel (+ submodelos, FrameService)
//   vista      qrc:/qml/Main.qml
// La vista solo ve el singleton `Studio` y el tipo `VideoItem`.
// ============================================================================

#include "GuiMode.hpp"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>

#include <csignal>
#include <cstdio>

#include "AppRuntime.hpp"
#include "ui/FrameService.hpp"
#include "ui/StudioViewModel.hpp"
#include "ui/VideoItem.hpp"

namespace ba::app {

namespace {

extern "C" void onGuiStopSignal(int) { AppRuntime::requestStop(); }

constexpr int kPreviewWidth = 1280; // vista previa/fondo: 720p alcanza y es barata

} // namespace

int runGui(int argc, char** argv) {
    std::signal(SIGINT, onGuiStopSignal);
    std::signal(SIGTERM, onGuiStopSignal);
#if !defined(_WIN32)
    std::signal(SIGPIPE, SIG_IGN);
#endif
    std::setvbuf(stdout, nullptr, _IOLBF, 0);

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("Broadcast Appliance");
    QQuickStyle::setStyle("Basic"); // controles propios encima; nada nativo que desentone

    AppRuntime rt;
    rt.configure(argc, argv);
    const int rc = rt.start();
    if (rc != 0) {
        std::printf("ba-app: el motor no pudo arrancar: %s\n", rt.lastError().c_str());
        return rc;
    }
    const VideoConfig& v = rt.config().video;
    rt.engine().enablePreview(kPreviewWidth,
                              (kPreviewWidth * v.outputHeight / std::max(1, v.outputWidth)) & ~1);

    int result = 0;
    {
        StudioViewModel vm(rt);
        auto* frames = static_cast<FrameService*>(vm.frames());
        VideoItem::setService(frames);
        qmlRegisterSingletonInstance("BA", 1, 0, "Studio", &vm);
        qmlRegisterType<VideoItem>("BA", 1, 0, "VideoItem");
        qmlRegisterSingletonType(QUrl("qrc:/qml/Theme.qml"), "BA", 1, 0, "Theme");

        QQmlApplicationEngine engine;
        QObject::connect(&vm, &StudioViewModel::quitRequested, &app, &QGuiApplication::quit);
        engine.load(QUrl("qrc:/qml/Main.qml"));
        if (engine.rootObjects().isEmpty()) {
            std::printf("ba-app: no se pudo cargar la interfaz QML\n");
            rt.stop();
            return 3;
        }
        // Diagnostico: --ui-snapshot=<png> guarda una captura a los 6 s
        // (--ui-snapshot-delay=<ms> para cambiarlo).
        int snapshotDelayMs = 6000;
        for (int i = 1; i < argc; ++i) {
            const QString a = QString::fromLocal8Bit(argv[i]);
            if (a.startsWith("--ui-snapshot-delay=")) snapshotDelayMs = a.mid(20).toInt();
        }
        for (int i = 1; i < argc; ++i) {
            const QString a = QString::fromLocal8Bit(argv[i]);
            if (a.startsWith("--ui-page=")) {
                engine.rootObjects().first()->setProperty("page", a.mid(10).toInt());
            }
            if (a.startsWith("--ui-snapshot=")) {
                const QString path = a.mid(QString("--ui-snapshot=").size());
                auto* win = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
                QTimer::singleShot(snapshotDelayMs, win, [win, path]() {
                    const bool ok = win->grabWindow().save(path);
                    std::printf("ba-app: captura de la ventana %s: %s\n", ok ? "guardada" : "FALLO",
                                path.toLocal8Bit().constData());
                    // Las ventanas de salida, como <path>-salida-N.png.
                    int n = 1;
                    for (QWindow* w : QGuiApplication::topLevelWindows()) {
                        auto* q = qobject_cast<QQuickWindow*>(w);
                        if (q == nullptr || q == win || !q->isVisible()) {
                            continue;
                        }
                        const QString p = QString(path).replace(".png", QString("-salida-%1.png").arg(n++));
                        std::printf("ba-app: captura de ventana de salida %s: %s\n",
                                    q->grabWindow().save(p) ? "guardada" : "FALLO",
                                    p.toLocal8Bit().constData());
                    }
                });
            }
        }
        result = app.exec();
    }
    rt.stop();
    return result;
}

} // namespace ba::app
