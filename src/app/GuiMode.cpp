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

#include <QDir>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickWindow>

#include "diag/DiarioDeArranque.hpp"
#include "diag/Registro.hpp"
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>

#include <csignal>
#include <cstdio>

#include "agent/AgentViewModel.hpp"
#include "ui/QrViewModel.hpp"
#include "AppRuntime.hpp"
#include "mcp/McpService.hpp"
#include "term/TerminalView.hpp"
#include "ui/FrameService.hpp"
#include "ui/InputsViewModel.hpp"
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

    QGuiApplication app(argc, argv);

    QGuiApplication::setApplicationName("Broadcast Appliance");
    // El logo en la barra de tareas, el Dock y el selector de ventanas. Varios
    // tamaños: el sistema elige (los chicos son el dibujo sin las ondas).
    {
        QIcon icono;
        for (int lado : {16, 24, 32, 48, 64, 128, 256, 512})
            icono.addFile(QString(":/icons/broadcast-appliance-%1.png").arg(lado), QSize(lado, lado));
        QGuiApplication::setWindowIcon(icono);
    }
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
        qmlRegisterType<term::TerminalView>("BA", 1, 0, "TerminalView");
        // Fuente de la terminal del Asistente (viaja en el bundle: data/fonts).
        for (const QString& dir : {QString::fromStdWString((rt.dataRoot() / "data" / "fonts").wstring()),
                                   QCoreApplication::applicationDirPath() + "/../../data/fonts"}) {
            const QStringList fonts = QDir(dir).entryList({"*.ttf"}, QDir::Files);
            for (const QString& f : fonts)
                QFontDatabase::addApplicationFont(dir + "/" + f);
            if (!fonts.isEmpty()) break;
        }
        qmlRegisterSingletonType(QUrl("qrc:/qml/Theme.qml"), "BA", 1, 0, "Theme");

        QQmlApplicationEngine engine;
        engine.addImageProvider("inputs", new InputThumbProvider(rt)); // miniaturas de la pagina Entradas
        // La vista previa del generador de QR: rasteriza el MISMO SVG que se
        // exporta, asi lo que se ve es exactamente lo que sale.
        engine.addImageProvider("qr", new QrImageProvider(vm.qrVm()));
        QObject::connect(&vm, &StudioViewModel::quitRequested, &app, &QGuiApplication::quit);
        engine.load(QUrl("qrc:/qml/Main.qml"));
        if (engine.rootObjects().isEmpty()) {
            std::printf("ba-app: no se pudo cargar la interfaz QML\n");
            rt.stop();
            return 3;
        }
        // El arranque se da por terminado cuando la interfaz DIBUJO, no cuando
        // termino de construirse: entre una cosa y la otra es donde se cuelga
        // un QML roto, y eso es justo lo que el diario tiene que agarrar.
        if (auto* w = qobject_cast<QQuickWindow*>(engine.rootObjects().first())) {
            QObject::connect(
                w, &QQuickWindow::frameSwapped, w,
                []() {
                    static bool avisado = false;
                    if (avisado) return;
                    avisado = true;
                    ba::diag::diario().arranqueOk();
                    ba::diag::registro().info("arranque terminado: la interfaz dibujo");
                },
                Qt::QueuedConnection);
        }

        // Servidor MCP: un asistente de IA maneja y ve toda la app (127.0.0.1 + token).
        mcp::McpService mcpService(rt, vm);
        mcpService.setMainWindow([&engine]() -> QQuickWindow* {
            return engine.rootObjects().isEmpty() ? nullptr
                                                  : qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        });
        auto* agentVm = static_cast<agent::AgentViewModel*>(vm.agent());
        agentVm->setSystemPrompt([&mcpService]() { return mcpService.agentPrompt(); });
        agentVm->setMcpReady([&mcpService]() { return mcpService.port() != 0; });
        if (rt.config().mcpEnabled) {
            if (mcpService.start(static_cast<quint16>(rt.config().mcpPort))) {
                std::printf("ba-app: servidor MCP en %s (token: bin/64bit/ba-mcp --token; stdio: ba-mcp)\n",
                            mcpService.url().toLocal8Bit().constData());
            } else {
                std::printf("ba-app: el servidor MCP no pudo abrir el puerto %d\n", rt.config().mcpPort);
            }
        }
        // El servidor arranca DESPUES de que la interfaz se dibujo, asi que hay
        // que avisar: si no, la pestaña Asistente se queda con el cartel de
        // «MCP apagado» que evaluo antes de tiempo.
        agentVm->avisarMcpCambio();
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
