// SPDX-License-Identifier: GPL-2.0-or-later
//
// ba-tablero: el dashboard de la app en el navegador (Qt para WebAssembly), en /control/app/.
//
// La MISMA QML que la app (src/app/qml: DashboardPage, las tarjetas, los controles, Theme),
// compilada acá sin copiarla: tocar una tarjeta cambia la app y la web a la vez. Lo que en la
// app es el modelo (Studio) acá es su réplica (ClienteReplica/ObjetoReplica), que la app
// mantiene al día por el WebSocket del control web (control/ReplicaPublicador). El video de
// las vistas llega en cuadros JPEG (VideoItemWeb, control/CuadrosWeb).
//
// El WebSocket lo abre la página (puente.js, con el canal y el token de siempre: el aparato
// se emparejó con el QR/PIN en /control/). Esto le habla por dos funciones:
//   JS → C++: Module.baRecibir(texto)  (cada mensaje de la app con type "replica")
//             Module.baConectado()     (autenticado: pedir el modelo)
//   C++ → JS: window.baPuente.enviar(texto), window.baPuente.estado(qué, detalle)
// ============================================================================
#include <QDirIterator>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QJsonDocument>

#include "ClienteReplica.hpp"
#include "Falsos.hpp"
#include "ObjetoReplica.hpp"
#include "Pagina.hpp"
#include "ui/DashboardModel.hpp"
#include "VideoItemWeb.hpp"
#include "pantallas/BuscadorCatalogo.hpp"
#include "ui/ChatHilo.hpp"
#include "ui/FilaViva.hpp"
#include "ui/FiltroCatalogo.hpp"
#include "ui/FotosPersonas.hpp"
#include "ui/ImagenViva.hpp"
#include "ui/ListaObjetos.hpp"
#include "ui/Reloj.hpp"
#include "ui/Vumetro.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/bind.h>
#include <emscripten/val.h>
#endif

namespace {
ClienteReplica* g_cliente = nullptr;
ba::app::DashboardModel* g_tablero = nullptr;
QString g_vista; ///< ?vista=… : una vista sola en su pestaña (TableroPestana.qml); vacío = el tablero
QQmlApplicationEngine* g_motor = nullptr;

void avisarPagina(const char* que, const QString& detalle) {
#ifdef __EMSCRIPTEN__
    const QByteArray d = detalle.toUtf8();
    EM_ASM({ if (window.baPuente) window.baPuente.estado(UTF8ToString($0), UTF8ToString($1)); }, que, d.constData());
#else
    Q_UNUSED(que);
    Q_UNUSED(detalle);
#endif
}

void cargarVista() {
    static bool cargada = false;
    if (cargada || g_cliente->raiz() == nullptr) return;
    cargada = true;
    qmlRegisterSingletonInstance("BA", 1, 0, "Studio", g_cliente->raiz());
    g_motor->load(QUrl(g_vista.isEmpty() ? QStringLiteral("qrc:/web/TableroWeb.qml") : QStringLiteral("qrc:/web/TableroPestana.qml")));
    avisarPagina(g_motor->rootObjects().isEmpty() ? "error" : "listo",
                 g_motor->rootObjects().isEmpty() ? QStringLiteral("no se pudo cargar la vista") : QString());
}

#ifdef __EMSCRIPTEN__
void baRecibir(std::string texto) {
    if (g_cliente) g_cliente->recibirTexto(QByteArray::fromStdString(texto));
}
void baConectado() {
    // Ya llegaron las preferencias del aparato (con su disposición del tablero).
    if (g_tablero) g_tablero->recargar();
    if (g_cliente) g_cliente->hola();
}
void baPantallaCompleta(bool si) { Pagina::la().setPantallaCompleta(si); }
/// La pestaña del navegador se ve (o se escondió): sin verse, la app no manda nada.
void baVisible(bool si) {
    if (g_cliente && g_cliente->listo()) g_cliente->pausar(!si);
}
/// Un cuadro que descomprimió el navegador (RGBA): una sola copia de JS a la memoria del módulo.
void baCuadro(std::string clave, emscripten::val pixeles, int w, int h, double t, double serial) {
    if (!g_cliente || w <= 0 || h <= 0) return;
    QImage img(w, h, QImage::Format_RGBA8888);
    if (img.sizeInBytes() != static_cast<qsizetype>(w) * h * 4) return;
    emscripten::val(emscripten::typed_memory_view(static_cast<size_t>(img.sizeInBytes()), img.bits())).call<void>("set", pixeles);
    g_cliente->entregarCuadro(QString::fromStdString(clave), img, static_cast<qint64>(t), static_cast<quint64>(serial));
}
void baPedirWebrtc() {
    if (g_cliente) g_cliente->pedirWebrtc();
}
#endif
} // namespace

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_BINDINGS(ba_tablero) {
    emscripten::function("baRecibir", &baRecibir);
    emscripten::function("baConectado", &baConectado);
    emscripten::function("baPedirWebrtc", &baPedirWebrtc);
    emscripten::function("baCuadro", &baCuadro);
    emscripten::function("baPantallaCompleta", &baPantallaCompleta);
    emscripten::function("baVisible", &baVisible);
}
#endif

int main(int argc, char** argv) {
    QQuickStyle::setStyle(QStringLiteral("Basic")); // como la app (arranque/GuiMode.cpp)
    auto* app = new QGuiApplication(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("Broadcast Appliance"));
    // Las letras de la app (Theme.font, Theme.mono), adentro del .wasm.
    for (QDirIterator it(QStringLiteral(":/fuentes"), {QStringLiteral("*.ttf")}); it.hasNext();)
        QFontDatabase::addApplicationFont(it.next());
    // Lo que no está en esas (los símbolos de los botones), de DejaVu Sans.
    QFont::insertSubstitutions(QStringLiteral("Noto Sans"), {QStringLiteral("DejaVu Sans")});
    QFont::insertSubstitutions(QStringLiteral("JetBrains Mono"), {QStringLiteral("DejaVu Sans")});

    g_motor = new QQmlApplicationEngine();
    g_cliente = new ClienteReplica(app);
    g_cliente->setMotor(g_motor);
    VideoItemWeb::setCliente(g_cliente);
    g_motor->rootContext()->setContextProperty(QStringLiteral("Replica"), g_cliente);
    g_motor->rootContext()->setContextProperty(QStringLiteral("Pagina"), &Pagina::la());
    // El tablero de ESTE aparato: el mismo modelo que el de la app, pero su disposición se
    // guarda en el registro del aparato (preferencias del control web) y nunca toca la de la
    // app. Studio.dashboard, en el navegador, es este.
    ba::app::DashboardModel::Almacen almacen;
#ifdef __EMSCRIPTEN__
    almacen.leer = []() {
        emscripten::val puente = emscripten::val::global("baPuente");
        if (puente.isUndefined()) return QJsonObject();
        return QJsonDocument::fromJson(QByteArray::fromStdString(puente.call<std::string>("tablero"))).object();
    };
    almacen.guardar = [](const QJsonObject& o) {
        emscripten::val puente = emscripten::val::global("baPuente");
        if (!puente.isUndefined())
            puente.call<void>("guardarTablero", QJsonDocument(o).toJson(QJsonDocument::Compact).toStdString());
    };
#endif
    g_tablero = new ba::app::DashboardModel(almacen, app);
    QQmlEngine::setObjectOwnership(g_tablero, QQmlEngine::CppOwnership);
    g_cliente->reemplazar(QStringLiteral("Studio"), QStringLiteral("dashboard"), g_tablero);
    // ?hud=1: cuánto tarda cada monitor en mostrar el cuadro (para medir la latencia).
    bool hud = false;
#ifdef __EMSCRIPTEN__
    hud = emscripten::val::global("location")["search"].as<std::string>().find("hud=1") != std::string::npos;
#endif
    g_motor->rootContext()->setContextProperty(QStringLiteral("HudWeb"), hud);
#ifdef __EMSCRIPTEN__
    {
        const std::string q = emscripten::val::global("location")["search"].as<std::string>();
        const auto i = q.find("vista=");
        if (i != std::string::npos) {
            std::string v = q.substr(i + 6);
            v = v.substr(0, v.find('&'));
            g_vista = QString::fromStdString(v);
        }
    }
#endif
    g_motor->rootContext()->setContextProperty(QStringLiteral("VistaWeb"), g_vista);
    // Lo que en la pantalla de la app es de la VISTA (qué pestaña del Mezclador, qué está
    // desplegado): en el navegador es de esta pestaña, no de la ventana de la app.
    g_cliente->propiedadDeLaVista(QStringLiteral("Studio.mezclador"), QStringLiteral("pestana"));
    g_cliente->propiedadDeLaVista(QStringLiteral("Studio.mezclador"), QStringLiteral("matrizAbierta"));
    g_cliente->propiedadDeLaVista(QStringLiteral("Studio.matriz"), QStringLiteral("pistasPlegadas"));
    g_cliente->propiedadDeLaVista(QStringLiteral("Studio.matriz"), QStringLiteral("mostrarEquipo"));

    // Los mismos tipos que registra la app (arranque/GuiMode.cpp); los de video y los que
    // no tienen con qué andar en el navegador, con su versión web.
    static ba::app::Reloj reloj;
    qmlRegisterSingletonInstance("BA", 1, 0, "Reloj", &reloj);
    qmlRegisterSingletonInstance("BA", 1, 0, "Fotos", &ba::app::FotosPersonas::las());
    qmlRegisterType<VideoItemWeb>("BA", 1, 0, "VideoItem");
    qmlRegisterType<TerminalWeb>("BA", 1, 0, "TerminalView");
    qmlRegisterType<ba::app::Vumetro>("BA", 1, 0, "Vumetro");
    qmlRegisterType<ba::app::ImagenViva>("BA", 1, 0, "ImagenViva");
    qmlRegisterType<MonitorBusWeb>("BA", 1, 0, "MonitorBus");
    qmlRegisterType<ScopeWeb>("BA", 1, 0, "Scope");
    qmlRegisterType<ba::app::ListaObjetos>("BA", 1, 0, "ListaConClave");
    qmlRegisterType<ba::app::FiltroCatalogo>("BA", 1, 0, "FiltroCatalogo");
    qmlRegisterType<ba::app::BuscadorCatalogo>("BA", 1, 0, "BuscadorCatalogo");
    qmlRegisterType<ba::app::ChatHilo>("BA", 1, 0, "ChatHilo");
    qmlRegisterType<ba::app::FilaViva>("BA", 1, 0, "FilaViva");
    qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/qml/Theme.qml")), "BA", 1, 0, "Theme");

    QObject::connect(g_cliente, &ClienteReplica::listoChanged, app, []() {
        if (g_cliente->listo()) cargarVista();
    });
    QObject::connect(g_cliente, &ClienteReplica::errorChanged, app,
                     []() { avisarPagina("error", g_cliente->error()); });
#ifdef __EMSCRIPTEN__
    g_cliente->enviar = [](const QByteArray& t) {
        EM_ASM({ if (window.baPuente) window.baPuente.enviar(UTF8ToString($0)); }, t.constData());
    };
    // El enlace WebRTC del programa (para comparar la latencia con el de los cuadros).
    QObject::connect(g_cliente, &ClienteReplica::enlaceWebrtc, app, [](const QString& url, const QString& error) {
        const QByteArray u = url.toUtf8(), e = error.toUtf8();
        EM_ASM({
            window.baWebrtc = new Object();
            window.baWebrtc.url = UTF8ToString($0);
            window.baWebrtc.error = UTF8ToString($1);
        }, u.constData(), e.constData());
    });
    avisarPagina("arrancado", QString());
#endif
    return app->exec();
}
