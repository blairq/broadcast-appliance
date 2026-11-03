// test_sala_grilla.cpp — La grilla de la sala con 120 adentro (SalaParticipantes de verdad)
//
// Sin GPU (offscreen, QSG_RHI_BACKEND=null), con el VideoItem de verdad y un servicio de
// cuadros que no da ninguno. Comprueba lo que trababa la vista con mucha gente:
//   - sin «Binding loop» (el de GrillaAdaptable.disp al entrar y salir gente),
//   - no se crea un VideoItem por persona: solo las celdas que se ven y una fila de
//     reserva a cada lado,
//   - solo las celdas que se ven tienen enVista (las de reserva no piden cuadros),
//   - entran 30 de golpe y se van 60: la grilla sigue sana (columnas, sin avisos).
#include <QEventLoop>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>

#include <cstdio>

#include "ui/JsonListModel.hpp"
#include "ui/ProveedorCuadros.hpp"
#include "ui/VideoItem.hpp"

#ifndef BA_QML_DIR // lo pone CMake (src/app/qml)
#define BA_QML_DIR "src/app/qml"
#endif

using ba::app::JsonListModel;
using ba::app::VideoItem;

namespace {

class SinCuadros : public ba::app::ProveedorCuadros {
   public:
    bool ultimo(const QString&, QImage&, std::uint64_t&, std::int64_t) const override { return false; }
};

int fallas = 0;
int bucles = 0;
QtMessageHandler anterior = nullptr;

void mensajes(QtMsgType t, const QMessageLogContext& c, const QString& m) {
    if (m.contains(QLatin1String("Binding loop"))) ++bucles;
    if (anterior != nullptr) anterior(t, c, m);
}

void esperar(int ms) {
    QEventLoop bucle;
    QTimer::singleShot(ms, &bucle, &QEventLoop::quit);
    bucle.exec();
}

void comprobar(bool ok, const char* que) {
    std::printf("%s %s\n", ok ? "ok  " : "MAL ", que);
    if (!ok) ++fallas;
}

QJsonArray gente(int desde, int cuantos) {
    QJsonArray a;
    for (int i = desde; i < desde + cuantos; ++i) {
        const QString uid = QStringLiteral("p%1").arg(i);
        a.append(QJsonObject{{"id", uid}, {"uid", uid}, {"name", QStringLiteral("Integrante %1").arg(i)}, {"slot", i + 1},
                             {"connected", true}, {"mediaUp", true}, {"mic", true}, {"cam", true}, {"screen", false},
                             {"hand", false}, {"admittedMs", 0}, {"presenter", false}, {"speaker", "none"}, {"info", ""},
                             {"contact", ""}, {"talk", false}, {"prueba", false}, {"semaforo", "verde"}, {"sinRetorno", false}});
    }
    return a;
}

/// Los VideoItem que existen (visibles en la escena) y cuántos tienen enVista.
void contarVideos(QObject* raiz, int& existen, int& enVista) {
    existen = enVista = 0;
    for (QQuickItem* it : raiz->findChildren<QQuickItem*>()) {
        auto* v = qobject_cast<VideoItem*>(it);
        if (v == nullptr || !v->isVisible()) continue;
        ++existen;
        if (v->enVista()) ++enVista;
    }
}

} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QSG_RHI_BACKEND", "null");
    QGuiApplication app(argc, argv);
    anterior = qInstallMessageHandler(mensajes);
    const QString dir = QStringLiteral(BA_QML_DIR);

    static QObject studio;
    qmlRegisterSingletonInstance("BA", 1, 0, "Studio", &studio);
    qmlRegisterSingletonType(QUrl::fromLocalFile(dir + "/Theme.qml"), "BA", 1, 0, "Theme");
    qmlRegisterType<VideoItem>("BA", 1, 0, "VideoItem");
    SinCuadros proveedor;
    VideoItem::setService(&proveedor);

    JsonListModel modelo(QStringList{"uid", "name", "slot", "connected", "mediaUp", "mic", "cam", "screen", "hand", "admittedMs",
                                     "presenter", "speaker", "info", "contact", "talk", "prueba", "semaforo", "sinRetorno"});
    modelo.setItems(gente(0, 120));

    QQmlEngine engine;
    engine.rootContext()->setContextProperty("modeloSala", &modelo);
    QQmlComponent comp(&engine);
    comp.setData(QStringLiteral("import QtQuick\nimport BA\nimport \"file:%1/controls\"\n"
                                "Window { width: 1400; height: 700; visible: true\n"
                                "  SalaParticipantes { anchors.fill: parent; modelo: modeloSala } }\n")
                     .arg(dir)
                     .toUtf8(),
                 QUrl::fromLocalFile(dir + "/test-sala-grilla.qml"));
    QObject* raiz = comp.create();
    if (raiz == nullptr) {
        for (const auto& e : comp.errors()) std::printf("%s\n", qPrintable(e.toString()));
        return 1;
    }
    esperar(400);

    // 1400 × 700 con celdas de 160: ~8 columnas y ~7 filas a la vista (+1 de reserva a cada lado).
    int existen = 0, enVista = 0;
    contarVideos(raiz, existen, enVista);
    std::printf("con 120: %d VideoItem, %d en vista\n", existen, enVista);
    comprobar(existen > 0, "hay celdas con video");
    comprobar(existen < 100, "no hay un VideoItem por persona (solo lo que se ve y la reserva)");
    comprobar(enVista <= existen && enVista < 80, "la reserva no pide cuadros (enVista falso fuera de la vista)");
    comprobar(bucles == 0, "sin binding loop al abrir");

    // Entran 30 de golpe y después se van 60.
    QJsonArray mas = gente(0, 120);
    for (const auto& v : gente(1000, 30)) mas.append(v);
    modelo.setItems(mas);
    esperar(200);
    comprobar(bucles == 0, "sin binding loop al entrar 30");
    modelo.setItems(gente(30, 60)); // quedan 60: se fueron los 30 primeros y los 30 nuevos
    esperar(200);
    comprobar(modelo.count() == 60, "quedan 60");
    comprobar(bucles == 0, "sin binding loop al salir 60");
    contarVideos(raiz, existen, enVista);
    std::printf("con 60: %d VideoItem, %d en vista\n", existen, enVista);
    comprobar(enVista > 0 && enVista <= 60, "las celdas siguen al modelo");

    // Con 4: celdas grandes, todas a la vista y sin reserva.
    modelo.setItems(gente(0, 4));
    esperar(200);
    contarVideos(raiz, existen, enVista);
    std::printf("con 4: %d VideoItem, %d en vista\n", existen, enVista);
    comprobar(existen >= 4 && enVista == 4, "con 4, las 4 a la vista");
    comprobar(bucles == 0, "sin binding loop con pocos");

    delete raiz;
    std::printf("test_sala_grilla: %s\n", fallas == 0 ? "bien" : "con fallas");
    return fallas == 0 ? 0 : 1;
}
