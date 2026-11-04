// bench_sala_conferencia.cpp — Banco de la vista «En la conferencia» con mucha gente
//
// La grilla de la sala (qml/controls/SalaParticipantes.qml, la de verdad) con N
// lugares falsos en un JsonListModel (el mismo modelo que conf.inside), VideoItem de
// verdad y un servicio de cuadros falso (ProveedorCuadros) que, como FrameService,
// avisa a lo sumo una vez por vuelta de 16 ms con los cuadros que llegaron (cada
// invitado a `--cps`, con fases distintas, como llegan de B). Mientras tanto:
//   - los roles cambian a `--roles-hz` (el semáforo de algunos, a veces un micrófono),
//   - cada `--entradas-s` segundos alguien sale y otro entra (inserta y quita filas),
//   - con `--desplazar`, la grilla se desplaza de arriba abajo sin parar.
// Mide:
//   - el hilo de la interfaz: el hueco más largo y el p99 entre latidos de un timer de
//     2 ms (lo que se «traba»), lo que tarda cada cambio de roles (setItems + todos los
//     bindings que despierta, sincrónico) y cada aviso de cuadros (todos los refresh),
//   - el scene graph: lo que tarda cada sincronización (la interfaz espera) y cada
//     render, y cuántos cuadros se dibujaron,
//   - VideoItem::contadores(): cuántos VideoItem se crearon (delegados nuevos), cuántos
//     viven, cuántos refresh, pedidos, cuadros tomados y texturas subidas,
//   - el modelo: cuántos (fila, rol) avisó dataChanged.
// Los bindings uno por uno: con el perfilador de QML (compila con QT_QML_DEBUG):
//     qmlprofiler -o /tmp/sala.qtd -- ./bench_sala_conferencia --n 120 --segundos 15
// y la traza se abre en Qt Creator (Analyze → QML Profiler → Load).
//
// Antes / después: el mismo binario contra los QML de otro árbol
//     git worktree add /tmp/sala-antes e0d71306
//     ./bench_sala_conferencia --qml /tmp/sala-antes/src/app/qml
// (los QML de antes usan VideoItem.enVista igual; el VideoItem es el de este árbol).
//
// Uso: bench_sala_conferencia [--n 120] [--segundos 20] [--cps 15] [--roles-hz 10]
//        [--entradas-s 3] [--desplazar] [--lista] [--ancho 1600 --alto 900]
//        [--qml <dir de qml>] [--offscreen] [--json]
// Sin --offscreen abre una ventana de verdad (la GPU de la máquina: lo que cuesta subir
// las texturas); con --offscreen, sin GPU (QSG_RHI_BACKEND=null): solo el hilo de la
// interfaz y la sincronización. NO es un test de ctest: es para medir a mano.
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRandomGenerator>
#include <QTimer>
#include <QUrl>

#if defined(QT_QML_DEBUG)
#include <QQmlDebuggingEnabler>
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>
#include <vector>

#include "ui/JsonListModel.hpp"
#include "ui/ProveedorCuadros.hpp"
#include "ui/VideoItem.hpp"

#ifndef BA_QML_DIR // lo pone CMake (src/app/qml); sin él, --qml
#define BA_QML_DIR "src/app/qml"
#endif

using ba::app::JsonListModel;
using ba::app::ProveedorCuadros;
using ba::app::VideoItem;

namespace {

// --- Tiempos ------------------------------------------------------------------------
// Una serie de duraciones (ms) con su resumen. Con candado: la sincronización y el render
// se miden en el hilo de render.
class Serie {
   public:
    void agregar(double ms) {
        std::lock_guard<std::mutex> l(mu_);
        v_.push_back(ms);
    }
    QJsonObject resumen() const {
        std::lock_guard<std::mutex> l(mu_);
        std::vector<double> v = v_;
        std::sort(v.begin(), v.end());
        const auto pct = [&v](double p) {
            if (v.empty()) return 0.0;
            return v[std::min(v.size() - 1, static_cast<std::size_t>(p / 100.0 * static_cast<double>(v.size())))];
        };
        double suma = 0;
        for (double x : v) suma += x;
        const auto r = [](double x) { return std::round(x * 1000.0) / 1000.0; };
        return QJsonObject{{"n", static_cast<int>(v.size())},
                           {"total_ms", r(suma)},
                           {"p50_ms", r(pct(50))},
                           {"p99_ms", r(pct(99))},
                           {"max_ms", r(v.empty() ? 0.0 : v.back())}};
    }

   private:
    mutable std::mutex mu_;
    std::vector<double> v_;
};

// --- El servicio de cuadros falso ---------------------------------------------------
// Como FrameService: los cuadros llegan de B a su ritmo con fases distintas; una vuelta
// cada 16 ms los recoge y avisa una vez (framesUpdated) si llegó alguno. Las imágenes
// están hechas de antemano (dos por lugar, alternadas: sin pintar en la medición).
class ProveedorFalso : public ProveedorCuadros {
   public:
    ProveedorFalso(int lugares, int cps) : periodoUs_(cps > 0 ? 1'000'000 / cps : 0) {
        for (int i = 1; i <= lugares; ++i) {
            Lugar& l = lugares_[QStringLiteral("guest:%1").arg(i)];
            for (int k = 0; k < 2; ++k) {
                QImage img(640, 360, QImage::Format_RGB32);
                img.fill(QColor::fromHsv((i * 37) % 360, 90, 70 + 40 * k));
                QPainter p(&img);
                p.setBrush(QColor(217, 169, 136));
                p.setPen(Qt::NoPen);
                p.drawEllipse(QPointF(320 + 40 * k, 160), 70, 85);
                l.imagenes[k] = img;
            }
            l.faseUs = QRandomGenerator::global()->bounded(static_cast<qint64>(std::max<std::int64_t>(1, periodoUs_)));
        }
        reloj_.start();
    }

    bool ultimo(const QString& clave, QImage& out, std::uint64_t& serial, std::int64_t intervaloUs) const override {
        ++pedidos_;
        if (intervaloUs > 0) ++pedidosConIntervalo_;
        const auto it = lugares_.find(clave);
        if (it == lugares_.end() || it->second.serial == 0 || it->second.serial == serial) return false;
        out = it->second.imagenes[it->second.serial % 2];
        serial = it->second.serial;
        return true;
    }

    /// Una vuelta (como FrameService::poll): los que tienen cuadro nuevo y un aviso.
    /// Devuelve lo que tardaron los que escuchan (ms) o -1 si no hubo cuadros.
    double vuelta() {
        if (periodoUs_ <= 0) return -1;
        const std::int64_t ahora = reloj_.nsecsElapsed() / 1000;
        bool alguno = false;
        for (auto& [clave, l] : lugares_) {
            const std::int64_t n = (ahora + l.faseUs) / periodoUs_;
            if (n != l.ultimoN) {
                l.ultimoN = n;
                ++l.serial;
                alguno = true;
            }
        }
        if (!alguno) return -1;
        QElapsedTimer t;
        t.start();
        emit framesUpdated();
        return static_cast<double>(t.nsecsElapsed()) / 1e6;
    }

    std::uint64_t pedidos() const { return pedidos_; }
    std::uint64_t pedidosConIntervalo() const { return pedidosConIntervalo_; }

   private:
    struct Lugar {
        QImage imagenes[2];
        std::uint64_t serial = 0;
        std::int64_t faseUs = 0;
        std::int64_t ultimoN = -1;
    };
    std::int64_t periodoUs_;
    std::map<QString, Lugar> lugares_;
    QElapsedTimer reloj_;
    mutable std::uint64_t pedidos_ = 0, pedidosConIntervalo_ = 0;
};

// --- La gente de la sala ------------------------------------------------------------
const char* const kNombres[] = {"Ana Ferreyra",  "Bruno Sosa",      "Carla Giménez", "Diego Romero",   "Elena Acosta",
                                "Facundo Ruiz",  "Gabriela Benítez", "Hernán Medina", "Inés Herrera",   "Julián Aguirre",
                                "Karina Pereyra", "Lucas Gutiérrez", "Olga Ríos",     "Pablo Morales",  "Rocío Ortiz"};

QJsonObject persona(int id, int slot) {
    const QString uid = QStringLiteral("p%1").arg(id);
    const QString nombre = QString::fromUtf8(kNombres[id % (sizeof(kNombres) / sizeof(kNombres[0]))]) + QStringLiteral(" %1").arg(id);
    return QJsonObject{{"id", uid},          {"uid", uid},           {"name", nombre},        {"slot", slot},
                       {"connected", true},  {"mediaUp", true},      {"mic", id % 5 != 2},    {"cam", id % 9 != 4},
                       {"screen", id == 3},  {"hand", id == 8},      {"admittedMs", 0},       {"presenter", id == 0},
                       {"speaker", id == 1 ? "onair" : "none"},      {"info", id % 3 == 0 ? "Consejera, bloque A" : ""},
                       {"contact", ""},      {"talk", false},        {"prueba", false},
                       {"semaforo", "verde"}, {"sinRetorno", id == 10}};
}

QString valor(const QStringList& args, const QString& nombre, const QString& omision) {
    const int i = args.indexOf(nombre);
    return i >= 0 && i + 1 < args.size() ? args.at(i + 1) : omision;
}

QQuickItem* buscarVista(QObject* raiz, bool lista) {
    for (QQuickItem* it : raiz->findChildren<QQuickItem*>()) {
        if (!it->isVisible()) continue;
        if (it->inherits(lista ? "QQuickListView" : "QQuickGridView")) return it;
    }
    return nullptr;
}

} // namespace

int main(int argc, char** argv) {
    QStringList args;
    for (int i = 1; i < argc; ++i) args << QString::fromLocal8Bit(argv[i]);
    const bool offscreen = args.contains("--offscreen");
    if (offscreen) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        qputenv("QSG_RHI_BACKEND", "null");
    }
#if defined(QT_QML_DEBUG)
    QQmlDebuggingEnabler::enableDebugging(true);
#endif
    QGuiApplication app(argc, argv);

    const int n = valor(args, "--n", "120").toInt();
    const double segundos = valor(args, "--segundos", "20").toDouble();
    const int cps = valor(args, "--cps", "15").toInt();
    const double rolesHz = valor(args, "--roles-hz", "10").toDouble();
    const double entradasS = valor(args, "--entradas-s", "3").toDouble();
    const bool desplazar = args.contains("--desplazar");
    const bool lista = args.contains("--lista");
    const bool json = args.contains("--json");
    const int ancho = valor(args, "--ancho", "1600").toInt();
    const int alto = valor(args, "--alto", "900").toInt();
    const QString dirQml = valor(args, "--qml", QStringLiteral(BA_QML_DIR));

    // Los tipos de la app que usa la sala: Studio (sin nada: solo lo tocan los clics),
    // Theme (el de verdad), VideoItem (el de verdad, con el servicio falso).
    static QObject studio;
    qmlRegisterSingletonInstance("BA", 1, 0, "Studio", &studio);
    qmlRegisterSingletonType(QUrl::fromLocalFile(dirQml + "/Theme.qml"), "BA", 1, 0, "Theme");
    qmlRegisterType<VideoItem>("BA", 1, 0, "VideoItem");
    ProveedorFalso proveedor(n + 8, cps);
    VideoItem::setService(&proveedor);

    // El modelo: los mismos roles que ConferenceViewModel::inside.
    JsonListModel modelo(QStringList{"uid", "name", "slot", "connected", "mediaUp", "mic", "cam", "screen", "hand", "admittedMs",
                                     "presenter", "speaker", "info", "contact", "talk", "prueba", "semaforo", "sinRetorno"});
    QJsonArray gente;
    int proximoId = 0;
    for (; proximoId < n; ++proximoId) gente.append(persona(proximoId, proximoId + 1));
    std::uint64_t avisosRoles = 0, filasInsertadas = 0, filasQuitadas = 0;
    QObject::connect(&modelo, &QAbstractItemModel::dataChanged, [&avisosRoles](const QModelIndex& a, const QModelIndex& b, const QList<int>& roles) {
        avisosRoles += static_cast<std::uint64_t>(b.row() - a.row() + 1) * static_cast<std::uint64_t>(std::max<qsizetype>(1, roles.size()));
    });
    QObject::connect(&modelo, &QAbstractItemModel::rowsInserted, [&filasInsertadas](const QModelIndex&, int a, int b) { filasInsertadas += b - a + 1; });
    QObject::connect(&modelo, &QAbstractItemModel::rowsRemoved, [&filasQuitadas](const QModelIndex&, int a, int b) { filasQuitadas += b - a + 1; });

    QQmlEngine engine;
    engine.rootContext()->setContextProperty("modeloSala", &modelo);
    const QByteArray fuente = QStringLiteral(R"(
import QtQuick
import BA
import "file:%1/controls"
Window {
    width: %2; height: %3
    visible: true
    color: "#101014"
    SalaParticipantes {
        anchors { fill: parent; margins: 12 }
        modelo: modeloSala
        modo: "%4"
    }
}
)")
                                  .arg(dirQml)
                                  .arg(ancho)
                                  .arg(alto)
                                  .arg(lista ? "lista" : "grilla")
                                  .toUtf8();
    QQmlComponent componente(&engine);
    componente.setData(fuente, QUrl::fromLocalFile(dirQml + "/bench-sala.qml"));
    QElapsedTimer cargar;
    cargar.start();
    modelo.setItems(gente);
    QObject* raiz = componente.create();
    if (raiz == nullptr) {
        for (const auto& e : componente.errors()) std::fprintf(stderr, "%s\n", qPrintable(e.toString()));
        return 1;
    }
    const double cargaMs = static_cast<double>(cargar.nsecsElapsed()) / 1e6;
    auto* ventana = qobject_cast<QQuickWindow*>(raiz);
    if (ventana == nullptr) {
        std::fprintf(stderr, "bench_sala_conferencia: la raíz no es una ventana\n");
        return 1;
    }

    // --- Lo que se mide ---
    Serie sync, render, roles, avisos, latido;
    std::atomic<std::uint64_t> cuadrosDibujados{0};
    // Hilo de render (o el de la interfaz con el bucle básico): directo, sin cola.
    thread_local QElapsedTimer tSync, tRender;
    QObject::connect(ventana, &QQuickWindow::beforeSynchronizing, ventana, [] { tSync.start(); }, Qt::DirectConnection);
    QObject::connect(ventana, &QQuickWindow::afterSynchronizing, ventana,
                     [&sync] { sync.agregar(static_cast<double>(tSync.nsecsElapsed()) / 1e6); }, Qt::DirectConnection);
    QObject::connect(ventana, &QQuickWindow::beforeRendering, ventana, [] { tRender.start(); }, Qt::DirectConnection);
    QObject::connect(ventana, &QQuickWindow::afterRendering, ventana,
                     [&render] { render.agregar(static_cast<double>(tRender.nsecsElapsed()) / 1e6); }, Qt::DirectConnection);
    QObject::connect(ventana, &QQuickWindow::frameSwapped, ventana, [&cuadrosDibujados] { ++cuadrosDibujados; }, Qt::DirectConnection);

    // Los contadores desde ahora (la carga aparte).
    auto& c = VideoItem::contadores();
    const std::uint64_t creados0 = c.creados.load(), refrescos0 = c.refrescos.load(), pedidos0 = c.pedidos.load(),
                        cuadros0 = c.cuadros.load(), subidas0 = c.subidas.load();
    const std::uint64_t avisosRoles0 = avisosRoles;

    // El latido: cuánto se atrasa un timer de 2 ms (el hilo de la interfaz ocupado).
    QElapsedTimer reloj;
    reloj.start();
    qint64 ultimoLatido = reloj.nsecsElapsed();
    QTimer latidos;
    latidos.setTimerType(Qt::PreciseTimer);
    latidos.setInterval(2);
    QObject::connect(&latidos, &QTimer::timeout, [&] {
        const qint64 ahora = reloj.nsecsElapsed();
        latido.agregar(static_cast<double>(ahora - ultimoLatido) / 1e6);
        ultimoLatido = ahora;
    });
    latidos.start();

    // La vuelta de los cuadros (FrameService::poll, ~60 Hz).
    QTimer vueltas;
    vueltas.setTimerType(Qt::PreciseTimer);
    vueltas.setInterval(16);
    QObject::connect(&vueltas, &QTimer::timeout, [&] {
        const double ms = proveedor.vuelta();
        if (ms >= 0) avisos.agregar(ms);
    });
    vueltas.start();

    // Los roles: el semáforo de ~10 % y, a veces, un micrófono o una mano.
    QTimer cambios;
    cambios.setInterval(rolesHz > 0 ? static_cast<int>(1000.0 / rolesHz) : 1000);
    auto* azar = QRandomGenerator::global();
    QObject::connect(&cambios, &QTimer::timeout, [&] {
        if (rolesHz <= 0) return;
        static const char* const kSemaforos[] = {"verde", "ambar", "rojo"};
        for (int i = 0; i < gente.size(); ++i) {
            QJsonObject o = gente.at(i).toObject();
            bool cambio = false;
            if (azar->bounded(10) == 0) {
                o["semaforo"] = kSemaforos[azar->bounded(3)];
                cambio = true;
            }
            if (azar->bounded(100) == 0) {
                o["mic"] = !o.value("mic").toBool();
                cambio = true;
            }
            if (azar->bounded(300) == 0) {
                o["hand"] = !o.value("hand").toBool();
                cambio = true;
            }
            if (cambio) gente.replace(i, o);
        }
        QElapsedTimer t;
        t.start();
        modelo.setItems(gente);
        roles.agregar(static_cast<double>(t.nsecsElapsed()) / 1e6);
    });
    cambios.start();

    // Entra y sale gente: uno se va de un lugar al azar y entra otro al final.
    QTimer entradas;
    entradas.setInterval(entradasS > 0 ? static_cast<int>(entradasS * 1000.0) : 1000);
    QObject::connect(&entradas, &QTimer::timeout, [&] {
        if (entradasS <= 0 || gente.isEmpty()) return;
        const int i = static_cast<int>(azar->bounded(static_cast<quint32>(gente.size())));
        const int slot = gente.at(i).toObject().value("slot").toInt();
        gente.removeAt(i);
        gente.append(persona(proximoId++, slot));
        QElapsedTimer t;
        t.start();
        modelo.setItems(gente);
        roles.agregar(static_cast<double>(t.nsecsElapsed()) / 1e6);
    });
    entradas.start();

    // Desplazar de arriba abajo y de vuelta (un ciclo cada 6 s).
    QTimer desplazamiento;
    desplazamiento.setInterval(16);
    QObject::connect(&desplazamiento, &QTimer::timeout, [&] {
        QQuickItem* vista = buscarVista(raiz, lista);
        if (vista == nullptr) return;
        const double recorrido = vista->property("contentHeight").toDouble() - vista->height();
        if (recorrido <= 0) return;
        const double fase = static_cast<double>(reloj.elapsed() % 6000) / 6000.0;
        const double y = vista->property("originY").toDouble() + recorrido * 0.5 * (1.0 - std::cos(fase * 6.283185307179586));
        vista->setProperty("contentY", y);
    });
    if (desplazar) desplazamiento.start();

    QTimer::singleShot(static_cast<int>(segundos * 1000.0), &app, [&] {
        const double dur = static_cast<double>(reloj.nsecsElapsed()) / 1e9;
        QJsonObject r{
            {"n", n},
            {"segundos", std::round(dur * 100.0) / 100.0},
            {"modo", lista ? "lista" : "grilla"},
            {"desplazar", desplazar},
            {"qml", dirQml},
            {"carga_ms", std::round(cargaMs * 10.0) / 10.0},
            {"interfaz", QJsonObject{{"latido_2ms", latido.resumen()},
                                     {"cambio_de_roles", roles.resumen()},
                                     {"aviso_de_cuadros", avisos.resumen()}}},
            {"scene_graph", QJsonObject{{"sincronizacion", sync.resumen()},
                                        {"render", render.resumen()},
                                        {"cuadros_por_s", std::round(static_cast<double>(cuadrosDibujados.load()) / dur * 10.0) / 10.0}}},
            {"video", QJsonObject{{"vivos", static_cast<double>(c.vivos.load())},
                                  {"creados", static_cast<double>(c.creados.load() - creados0)},
                                  {"refrescos_por_s", std::round(static_cast<double>(c.refrescos.load() - refrescos0) / dur)},
                                  {"pedidos_por_s", std::round(static_cast<double>(c.pedidos.load() - pedidos0) / dur)},
                                  {"cuadros_por_s", std::round(static_cast<double>(c.cuadros.load() - cuadros0) / dur)},
                                  {"subidas_por_s", std::round(static_cast<double>(c.subidas.load() - subidas0) / dur)},
                                  {"pedidos_con_tope", static_cast<double>(proveedor.pedidosConIntervalo())}}},
            {"modelo", QJsonObject{{"filas_roles_avisados", static_cast<double>(avisosRoles - avisosRoles0)},
                                   {"filas_insertadas", static_cast<double>(filasInsertadas)},
                                   {"filas_quitadas", static_cast<double>(filasQuitadas)}}},
        };
        const QByteArray texto = QJsonDocument(r).toJson(json ? QJsonDocument::Compact : QJsonDocument::Indented);
        std::printf("%s\n", texto.constData());
        if (!json) {
            const QJsonObject l = r["interfaz"].toObject()["latido_2ms"].toObject();
            const QJsonObject s = r["scene_graph"].toObject()["sincronizacion"].toObject();
            std::printf("bench_sala_conferencia: %d lugares, interfaz trabada como mucho %.1f ms (p99 %.1f), "
                        "sincronización p99 %.2f ms, %.0f texturas/s\n",
                        n, l["max_ms"].toDouble(), l["p99_ms"].toDouble(), s["p99_ms"].toDouble(),
                        r["video"].toObject()["subidas_por_s"].toDouble());
        }
        delete raiz;
        QGuiApplication::quit();
    });
    return QGuiApplication::exec();
}
