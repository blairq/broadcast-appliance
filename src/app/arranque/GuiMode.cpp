// ============================================================================
// src/app/arranque/GuiMode.cpp — ba-app con el dashboard QML (ver GuiMode.hpp)
//
// Arma las tres capas y las conecta:
//   modelo     AppRuntime (motor + configuracion)
//   viewmodel  StudioViewModel (+ submodelos, FrameService)
//   vista      qrc:/qml/Main.qml
// La vista solo ve el singleton `Studio` y el tipo `VideoItem`.
// ============================================================================

#include "arranque/GuiMode.hpp"
#include "ajustes/Ajustes.hpp"
#include "arranque/ArranqueAviso.hpp"
#include "platform/Hilo.hpp"
#include "platform/Paths.hpp"

#include <QDesktopServices>
#include <QDir>
#include <QMouseEvent>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickWindow>

#include "diag/DiarioDeArranque.hpp"
#include "diag/Registro.hpp"
#include "i18n/I18n.hpp"
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QSGRendererInterface>
#include <QThread>
#include <QUrl>

#include <csignal>
#include <cstdio>
#include <memory>

#include "agent/AgentViewModel.hpp"
#include "agent/ModelosLocales.hpp"
#include "arranque/AppRuntime.hpp"
#include "agent/AlertEngine.hpp"
#include "arranque/PlanoControl.hpp"
#include "arranque/TraductorQt.hpp"
#include "mcp/McpService.hpp"
#include "control/Botonera.hpp"
#include "control/Acciones.hpp"
#include "Candado.hpp"
#include "plugins/Plugins.hpp"
#include "term/TerminalView.hpp"
#include "ui/VigiaInterfaz.hpp"
#include "ui/Vumetro.hpp"
#include "ui/ImagenViva.hpp"
#include "ui/FrameService.hpp"
#include "ui/InputsViewModel.hpp"
#include "ui/QrViewModel.hpp"
#include "ui/StudioViewModel.hpp"
#include "ui/VideoItem.hpp"
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QPointer>
#include <QRasterWindow>
#include <QScreen>

namespace ba::app {

namespace {

extern "C" void onGuiStopSignal(int) { AppRuntime::requestStop(); }

constexpr int kPreviewWidth = 1280; // vista previa/fondo: 720p alcanza y es barata

#ifndef BA_APP_VERSION
#define BA_APP_VERSION ""
#endif

// --- El cartel de carga -----------------------------------------------------------
// Lo primero que se ve, a las décimas de segundo de abrir la app: antes del
// motor, del modelo y del QML (que juntos son varios segundos). Por eso es una
// ventana pintada a mano con QPainter y no QML: no espera a nada. El motor
// arranca en este mismo hilo, así que el cartel se repinta solo cuando una
// etapa avisa (ArranqueAviso.hpp); se va cuando la ventana principal dibujó.
class CartelDeCarga : public QRasterWindow {
    static constexpr const char* kRepo = "github.com/blairq/broadcast-appliance";
    static constexpr int kMargen = 36;
    /// El motor arranca en este hilo: mientras el cartel es la única ventana, sus clics se
    /// atienden (el enlace); con la principal ya creándose, no (que no se meta en la carga).
    static QEventLoop::ProcessEventsFlags sinUsuarioSiHayOtras() {
        return QGuiApplication::topLevelWindows().size() <= 1 ? QEventLoop::AllEvents : QEventLoop::ExcludeUserInputEvents;
    }

   public:
    CartelDeCarga() {
        setFlags(Qt::SplashScreen | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        setTitle("Broadcast Appliance");
        resize(560, 330);
        if (QScreen* pantalla = QGuiApplication::primaryScreen())
            setPosition(pantalla->availableGeometry().center() - QPoint(width() / 2, height() / 2));
        logo_ = QImage(":/icons/broadcast-appliance-256.png");
    }

    void ponerEstado(const QString& que, const QString& detalle = {}, double parte = -1) {
        estado_ = que;
        detalle_ = detalle;
        parte_ = parte;
        update();
        // El hilo está ocupado arrancando el motor: se pinta ya, sin esperar al lazo.
        QCoreApplication::processEvents(sinUsuarioSiHayOtras());
    }

    /// Lo muestra y espera (como mucho 300 ms) a que esté de verdad en pantalla.
    void mostrar() {
        show();
        QElapsedTimer t;
        t.start();
        while (!pintado_ && t.elapsed() < 300) QCoreApplication::processEvents(sinUsuarioSiHayOtras(), 20);
    }

   protected:
    void mouseMoveEvent(QMouseEvent* e) override {
        const bool sobre = enlace_.contains(e->position().toPoint());
        if (sobre != sobreEnlace_) {
            sobreEnlace_ = sobre;
            setCursor(sobre ? Qt::PointingHandCursor : Qt::ArrowCursor);
            update();
        }
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && enlace_.contains(e->position().toPoint()))
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://") + kRepo));
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        pintar(p);
        if (!pintado_) {
            pintado_ = true;
            ba::diag::registro().info(ba::i18n::traducir("arranque", "arranque: el cartel de carga ya se ve"));
        }
        // Para mirarlo sin captura de pantalla (pruebas): BA_CARTEL_PNG=archivo.png.
        if (const QByteArray png = qgetenv("BA_CARTEL_PNG"); !png.isEmpty()) {
            QImage im(size() * devicePixelRatio(), QImage::Format_ARGB32);
            im.setDevicePixelRatio(devicePixelRatio());
            QPainter pi(&im);
            pintar(pi);
            pi.end();
            im.save(QString::fromUtf8(png));
        }
    }

    void pintar(QPainter& p) {
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QColor fondo("#0a0a0c"), borde("#26262c"), texto("#f4f4f6"), tenue("#a2a2aa"),
            apagado("#6c6c74"), acento("#ff6a2b");
        p.fillRect(QRect(0, 0, width(), height()), fondo);
        p.setPen(QPen(borde, 1));
        p.drawRect(QRect(0, 0, width() - 1, height() - 1));
        // Una franja del color de la marca arriba, como la píldora de AIRE.
        p.fillRect(QRect(0, 0, width(), 3), acento);

        const int m = kMargen;
        if (!logo_.isNull()) p.drawImage(QRect(m, 44, 88, 88), logo_);
        QFont f = p.font();
        f.setPixelSize(30);
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(texto);
        p.drawText(QRect(m + 108, 52, width() - m * 2 - 108, 40), Qt::AlignLeft | Qt::AlignVCenter,
                   "Broadcast Appliance");
        f.setPixelSize(14);
        f.setWeight(QFont::Normal);
        p.setFont(f);
        p.setPen(tenue);
        const QString lema = QCoreApplication::translate("arranque", "estudio de transmisión todo en uno");
        const QString version =
            QString(BA_APP_VERSION).isEmpty()
                ? lema
                : QCoreApplication::translate("arranque", "Versión %1 · %2").arg(QString(BA_APP_VERSION), lema);
        p.drawText(QRect(m + 108, 92, width() - m * 2 - 108, 22), Qt::AlignLeft | Qt::AlignVCenter, version);

        // Lo que está haciendo, con una barra que avanza con cada etapa.
        f.setPixelSize(13);
        p.setFont(f);
        p.setPen(texto);
        p.drawText(QRect(m, 168, width() - m * 2, 20), Qt::AlignLeft | Qt::AlignVCenter,
                   estado_.isEmpty() ? QCoreApplication::translate("arranque", "Cargando…") : estado_);
        const QRect barra(m, 196, width() - m * 2, 4);
        p.setPen(Qt::NoPen);
        p.setBrush(borde);
        p.drawRoundedRect(barra, 2, 2);
        p.setBrush(acento);
        const double avance = std::min(1.0, 0.08 + 0.92 * (1.0 - 1.0 / (1.0 + pasos_ * 0.12)));
        p.drawRoundedRect(QRect(barra.x(), barra.y(), static_cast<int>(barra.width() * avance), barra.height()), 2, 2);
        // Una etapa larga que se puede contar (los plugins de video: «37 de 312»):
        // qué está haciendo, en chico, y cuánto lleva en una barra fina propia.
        if (!detalle_.isEmpty()) {
            f.setPixelSize(12);
            p.setFont(f);
            p.setPen(tenue);
            p.drawText(QRect(m, 206, width() - m * 2, 18), Qt::AlignLeft | Qt::AlignVCenter,
                       p.fontMetrics().elidedText(detalle_, Qt::ElideMiddle, width() - m * 2));
        }
        if (parte_ >= 0) {
            const QRect fina(m, 228, width() - m * 2, 2);
            p.setPen(Qt::NoPen);
            p.setBrush(borde);
            p.drawRect(fina);
            p.setBrush(tenue);
            p.drawRect(QRect(fina.x(), fina.y(), static_cast<int>(fina.width() * std::min(1.0, parte_)), fina.height()));
        }

        // Quién lo hizo.
        f.setPixelSize(12);
        p.setFont(f);
        p.setPen(tenue);
        p.drawText(QRect(m, 244, width() - m * 2, 18), Qt::AlignLeft | Qt::AlignVCenter,
                   QCoreApplication::translate("arranque", "Hecho por %1").arg("Jorge Ortega"));
        p.setPen(acento);
        QFont fe = p.font();
        fe.setUnderline(sobreEnlace_); // es un enlace: subrayado con el mouse encima
        p.setFont(fe);
        p.drawText(QRect(m, 264, width() - m * 2, 18), Qt::AlignLeft | Qt::AlignVCenter, kRepo);
        enlace_ = QRect(m, 264, p.fontMetrics().horizontalAdvance(kRepo), 18);
        fe.setUnderline(false);
        p.setFont(fe);
        p.setPen(apagado);
        p.drawText(QRect(m, 284, width() - m * 2, 18), Qt::AlignLeft | Qt::AlignVCenter,
                   QCoreApplication::translate("arranque", "Software libre (GPL-3.0) · GStreamer, Qt y ONNX Runtime"));
    }

   public:
    int pasos_ = 0;

   private:
    QImage logo_;
    QString estado_;
    QString detalle_;
    double parte_ = -1;
    bool pintado_ = false;
    bool sobreEnlace_ = false;
    QRect enlace_; ///< donde quedó pintada la dirección del repositorio (un clic la abre)
};

} // namespace

int runGui(int argc, char** argv) {
    std::signal(SIGINT, onGuiStopSignal);
    std::signal(SIGTERM, onGuiStopSignal);
#if !defined(_WIN32)
    std::signal(SIGPIPE, SIG_IGN);
#endif

    QGuiApplication app(argc, argv);
    // Este hilo nunca tiene que esperar al motor: desde acá, el motor avisa en el
    // registro cada vez que lo hace esperar (ver motor/Candado.hpp).
    ba::motor::marcarHiloInterfaz();
    ba::platform::nombrarHilo("interfaz");
    // Y cuánto se demora su bucle de eventos (y quién lo demora): ui/VigiaInterfaz.
    new ba::app::VigiaInterfaz(&app);
    // Con --test, cada vez que la interfaz tome el candado del motor (no tiene que
    // tomarlo nunca) queda como ERROR en el registro, aunque sea de un instante.
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--test" || std::string(argv[i]) == "--debug") ba::motor::vigiaEstricto(true);

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
    // El idioma, antes del cartel de carga: ya sale traducido.
    instalarIdioma();

    // El cartel de carga, ANTES que todo lo demás (ver CartelDeCarga).
    auto cartel = std::make_unique<CartelDeCarga>();
    cartel->mostrar();
    arranque::oyente() = [c = QPointer<CartelDeCarga>(cartel.get())](const arranque::Aviso& a) {
        if (!c) return; // un arranque que falló y salió antes: el cartel ya no está
        if (a.paso) ++c->pasos_;
        c->ponerEstado(QString::fromStdString(a.que), QString::fromStdString(a.detalle), a.parte);
    };

    AppRuntime rt;
    {
        // Lo que tarda leer la configuración: la retoma tras una caída cuenta todo (F3-10).
        QElapsedTimer reloj;
        reloj.start();
        rt.configure(argc, argv);
        ba::diag::registro().info(ba::i18n::llenar(ba::i18n::traducir("arranque", "arranque: la configuración, %1 ms"),
                                                   {std::to_string(reloj.elapsed())}));
    }
    const int rc = rt.start();
    if (rc != 0) {
        std::printf("ba-app: %s\n",
                    ba::i18n::llenar(ba::i18n::traducir("arranque", "el motor no pudo arrancar: %1"), {rt.lastError()})
                        .c_str());
        return rc;
    }
    // Las entradas de audio que el sistema tardó en abrir (BroadcastEngine::
    // addGlobalAudio) se enganchan cuando llegan.
    QTimer audioPendiente;
    QObject::connect(&audioPendiente, &QTimer::timeout, [&rt]() { rt.engine().adoptPendingAudio(); });
    audioPendiente.start(1000);
    const VideoConfig& v = rt.config().video;
    rt.engine().enablePreview(kPreviewWidth,
                              (kPreviewWidth * v.outputHeight / std::max(1, v.outputWidth)) & ~1);
    // EL PLANO DE CONTROL (F3-03): su hilo con el canal del launcher, el documento de aire,
    // el MCP y las alarmas; y el reloj del motor, que es del runtime y no de una vista.
    PlanoControl plano({[&rt]() { rt.ticControl(); }, [&rt]() { rt.ticMotor(); }});
    plano.setCierre([]() { return AppRuntime::stopRequested(); });
    // El documento de aire lo vuelca su dueño, en su hilo (y en cada vuelta del control).
    rt.setDuenoDelAire([&plano, &rt]() { plano.encolar([&rt]() { rt.documentoAire().volcar(); }); });
    plano.arrancar();

    int result = 0;
    {
        QElapsedTimer relojGui;
        relojGui.start();
        arranque::avisar(ba::i18n::traducir("arranque", "Cargando la configuración del estudio…"));
        StudioViewModel vm(rt);
        ba::diag::registro().info(ba::i18n::llenar(ba::i18n::traducir("arranque", "arranque: el modelo de la app, %1 ms"),
                                                   {std::to_string(relojGui.restart())}));
        // El vigía de las alarmas, al plano de control: con la interfaz trabada siguen
        // saliendo (lo que lee de la interfaz le llega como foto, ver agent/EntradasAlarmas).
        plano.adoptar(&vm.alerts());
        auto* frames = static_cast<FrameService*>(vm.frames());
        VideoItem::setService(frames);
        qmlRegisterSingletonInstance("BA", 1, 0, "Studio", &vm);
        qmlRegisterType<VideoItem>("BA", 1, 0, "VideoItem");
        qmlRegisterType<term::TerminalView>("BA", 1, 0, "TerminalView");
        qmlRegisterType<Vumetro>("BA", 1, 0, "Vumetro");
        qmlRegisterType<ImagenViva>("BA", 1, 0, "ImagenViva");
        // Fuente de la terminal del Asistente (viaja en el bundle: data/fonts).
        for (const QString& dir :
             {QString::fromStdWString((ba::platform::bundleRoot() / "data" / "fonts").wstring()),
              QCoreApplication::applicationDirPath() + "/../../data/fonts"}) {
            const QStringList fonts = QDir(dir).entryList({"*.ttf", "*.otf"}, QDir::Files);
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
        // Un cierre pedido (el launcher, una señal) pasa por la vista: así no pregunta.
        QObject::connect(&plano, &PlanoControl::cierrePedido, &vm, &StudioViewModel::quitRequested);
        relojGui.restart();
        arranque::avisar(ba::i18n::traducir("arranque", "Abriendo la interfaz…"));
        engine.load(QUrl("qrc:/qml/Main.qml"));
        ba::diag::registro().info(ba::i18n::llenar(ba::i18n::traducir("arranque", "arranque: la interfaz (QML), %1 ms"),
                                                   {std::to_string(relojGui.restart())}));
        if (engine.rootObjects().isEmpty()) {
            std::printf("ba-app: %s\n", ba::i18n::traducir("arranque", "no se pudo cargar la interfaz QML").c_str());
            plano.detener();
            rt.setDuenoDelAire({});
            rt.stop();
            return 3;
        }
        // El arranque se da por terminado cuando la interfaz DIBUJO, no cuando
        // termino de construirse: entre una cosa y la otra es donde se cuelga
        // un QML roto, y eso es justo lo que el diario tiene que agarrar.
        if (auto* w = qobject_cast<QQuickWindow*>(engine.rootObjects().first())) {
            QObject::connect(
                w, &QQuickWindow::frameSwapped, w,
                [&cartel]() {
                    static bool avisado = false;
                    if (avisado) return;
                    avisado = true;
                    // La ventana de verdad ya se ve: el cartel se va.
                    arranque::oyente() = nullptr;
                    cartel.reset();
                    ba::diag::diario().arranqueOk();
                    ba::diag::registro().info(ba::i18n::traducir("arranque", "arranque terminado: la interfaz dibujo"));
                },
                Qt::QueuedConnection);
            // Con qué dibuja Qt y dónde: el backend (D3D11, OpenGL, Metal, Vulkan…) y si
            // el dibujo va en un hilo propio o en el principal (entonces la subida de los
            // cuadros de video le come tiempo a la interfaz). Para diagnosticar.
            QObject::connect(
                w, &QQuickWindow::sceneGraphInitialized, w,
                [w]() {
                    static const char* kApi[] = {"desconocido", "software", "OpenVG", "OpenGL", "Direct3D 11",
                                                 "Vulkan", "Metal", "Null", "Direct3D 12"};
                    const int api = w->rendererInterface() ? static_cast<int>(w->rendererInterface()->graphicsApi()) : 0;
                    const bool hiloPropio = QThread::currentThread() != qApp->thread();
                    const std::string cual = api >= 0 && api < 9 ? kApi[api] : ba::i18n::traducir("arranque", "otro");
                    ba::diag::registro().info(ba::i18n::llenar(
                        hiloPropio ? ba::i18n::traducir("arranque", "dibujo de Qt: %1, en su hilo (QSGRenderThread)")
                                   : ba::i18n::traducir("arranque", "dibujo de Qt: %1, en el hilo principal"),
                        {cual}));
                },
                Qt::DirectConnection);
        }

        // Servidor MCP: un asistente de IA maneja y ve toda la app (127.0.0.1 + token).
        relojGui.restart();
        mcp::McpService mcpService(rt, vm);
        // Cada herramienta en su hilo y el transporte en el del plano de control (F3-03).
        mcpService.setPlano(plano);
        ba::diag::registro().info(ba::i18n::llenar(ba::i18n::traducir("arranque", "arranque: el servidor MCP, %1 ms"),
                                                   {std::to_string(relojGui.restart())}));
        // Modo prueba: el MCP ejecuta todo sin esperar la aprobación de una persona.
        bool modoTest = false;
        for (int i = 1; i < argc; ++i)
            modoTest = modoTest || std::string(argv[i]) == "--test" || std::string(argv[i]) == "--debug";
        if (modoTest) {
            mcpService.core().setSinAprobaciones(true);
            vm.setModoTest(true);
            ba::diag::registro().linea(ba::diag::Nivel::Aviso, "mcp", 0,
                                       ba::i18n::traducir("mcp", "MODO PRUEBA (--test): el MCP no pide aprobaciones, ni siquiera las de aire"));
        }
        mcpService.setMainWindow([&engine]() -> QQuickWindow* {
            return engine.rootObjects().isEmpty() ? nullptr
                                                  : qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        });
        // Con qué herramientas se entrenó cada modelo local (su modelo.json) se
        // compara contra las de hoy: el esquema de cada una, tal cual lo ve el modelo.
        if (auto* ml = qobject_cast<agent::ModelosLocales*>(vm.modelos())) {
            ml->setHerramientas([&mcpService]() {
                nlohmann::json m = nlohmann::json::object();
                for (const auto& t : mcpService.core().tools())
                    m[t.name] = t.inputSchema;
                return m;
            });
        }
        for (QObject* o : {vm.agent(), vm.agenteAutomatizacion()}) {
            auto* agentVm = static_cast<agent::AgentViewModel*>(o);
            agentVm->setSystemPrompt([&mcpService]() { return mcpService.agentPrompt(); });
            agentVm->setMcpReady([&mcpService]() { return mcpService.port() != 0; });
        }
        // Los plugins de Python llaman herramientas del servidor (lo mismo que
        // puede el asistente) y arrancan los que estaban prendidos.
        if (auto* pl = vm.plugins()) {
            pl->setLlamador([&mcpService](const std::string& plugin, const std::string& h, const nlohmann::json& args,
                                          std::function<void(nlohmann::json)> listo) {
                // Origen «plugin:<id>»: queda en el registro con su nombre y la guarda de aire lo trata
                // como lo que es (algo automático, no una persona).
                mcpService.core().callTool({{"name", h}, {"arguments", args}}, std::move(listo),
                                           mcp::CallContext{"plugin:" + plugin, "", "plugin:" + plugin});
            });
            QTimer::singleShot(ba::ajustes::entero("arranque.pluginsMs", 3000), pl, [pl]() { pl->arrancarGuardados(); });
        }
        // La botonera ejecuta por el MISMO camino que el asistente (validación,
        // registro, sincronizar la vista), como «botonera:<aparato>». Es una
        // persona apretando: la guarda de aire no la frena, porque la botonera
        // ya pidió la confirmación en el propio aparato.
        {
            auto& g = vm.botonera().ganchos();
            g.ejecutar = [&mcpService](const std::string& h, const nlohmann::json& args, const std::string& dev,
                                       const std::string& perfil, const std::string& salida,
                                       control::Botonera::Listo listo) {
                // El aparato «asistente» (botonera_presionar) no es una persona.
                mcp::CallContext ctx{"botonera:" + dev, "", "botonera:" + perfil, dev != "asistente", {}};
                ctx.salida = salida; // la que maneja el aparato (PLAN-SALIDAS)
                mcpService.core().callTool({{"name", h}, {"arguments", args}},
                                           [listo](nlohmann::json r) {
                                               std::string texto;
                                               for (const auto& c : r.value("content", nlohmann::json::array()))
                                                   if (c.value("type", "") == "text") {
                                                       texto = c.value("text", std::string());
                                                       break;
                                                   }
                                               if (texto.size() > 160) texto = texto.substr(0, 157) + "...";
                                               listo(!r.value("isError", false), texto);
                                           },
                                           ctx);
            };
            g.deAireAhora = [&mcpService](const std::string& h, const nlohmann::json& args) {
                return mcpService.core().hayAire() && mcpService.core().aire().esDeAire(mcpService.core().nombreActual(h), args);
            };
            g.herramientas = [&mcpService]() {
                std::set<std::string> out;
                for (const auto& t : mcpService.core().tools()) {
                    out.insert(t.name);
                    out.insert(t.antes.begin(), t.antes.end()); // celdas guardadas con un nombre viejo
                }
                return out;
            };
            g.catalogo = [&mcpService]() { return mcpService.core().catalogo(); };
            g.buscar = [&mcpService](const std::string& q) { return mcpService.core().buscarHerramientas(q); };
        }
        // Los atajos de teclado: una persona apretando una tecla (como la botonera).
        vm.acciones().ganchos().ejecutar = [&mcpService, &vm](const std::string& h, const nlohmann::json& args,
                                                              const std::string& salida, control::Acciones::Listo listo) {
            mcp::CallContext ctx{"atajo", "", "atajo", true, {}};
            ctx.salida = salida; // la que manejan los atajos (o la que dice la acción)
            mcpService.core().callTool({{"name", h}, {"arguments", args}},
                                       [listo, &vm](nlohmann::json r) {
                                           const bool ok = !r.value("isError", false);
                                           std::string texto;
                                           for (const auto& c : r.value("content", nlohmann::json::array()))
                                               if (c.value("type", "") == "text") {
                                                   texto = c.value("text", std::string());
                                                   break;
                                               }
                                           if (!ok) emit vm.notify(QString::fromStdString(texto.substr(0, 200)), "error");
                                           listo(ok, texto);
                                       },
                                       ctx);
        };
        if (rt.config().mcpEnabled) {
            if (mcpService.start(static_cast<quint16>(rt.config().mcpPort))) {
                std::printf("ba-app: %s\n",
                            ba::i18n::llenar(ba::i18n::traducir("arranque", "servidor MCP en %1 (token: bin/64bit/ba-mcp --token; stdio: ba-mcp)"),
                                             {mcpService.url().toStdString()})
                                .c_str());
            } else {
                std::printf("ba-app: %s\n",
                            ba::i18n::llenar(ba::i18n::traducir("arranque", "el servidor MCP no pudo abrir el puerto %1"),
                                             {std::to_string(rt.config().mcpPort)})
                                .c_str());
            }
        }
        // El servidor arranca DESPUES de que la interfaz se dibujo, asi que hay
        // que avisar: si no, la pestaña Asistente se queda con el cartel de
        // «MCP apagado» que evaluo antes de tiempo.
        for (QObject* o : {vm.agent(), vm.agenteAutomatizacion()})
            static_cast<agent::AgentViewModel*>(o)->avisarMcpCambio();
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
                    std::printf("ba-app: %s\n",
                                ba::i18n::llenar(ok ? ba::i18n::traducir("arranque", "captura de la ventana guardada: %1")
                                                    : ba::i18n::traducir("arranque", "captura de la ventana FALLO: %1"),
                                                 {path.toStdString()})
                                    .c_str());
                    // Las ventanas de salida, como <path>-salida-N.png.
                    int n = 1;
                    for (QWindow* w : QGuiApplication::topLevelWindows()) {
                        auto* q = qobject_cast<QQuickWindow*>(w);
                        if (q == nullptr || q == win || !q->isVisible()) {
                            continue;
                        }
                        const QString p = QString(path).replace(".png", QString("-salida-%1.png").arg(n++));
                        std::printf("ba-app: %s\n",
                                    ba::i18n::llenar(q->grabWindow().save(p)
                                                         ? ba::i18n::traducir("arranque", "captura de ventana de salida guardada: %1")
                                                         : ba::i18n::traducir("arranque", "captura de ventana de salida FALLO: %1"),
                                                     {p.toStdString()})
                                        .c_str());
                    }
                });
            }
        }
        result = app.exec();
        // Lo que el plano adoptó (el MCP, las alarmas) vuelve a este hilo antes de que lo
        // borren sus dueños; el documento de aire lo vuelca rt.stop en el acto.
        plano.detener();
        rt.setDuenoDelAire({});
    }
    plano.detener();
    rt.setDuenoDelAire({});
    rt.stop();
    return result;
}

} // namespace ba::app
