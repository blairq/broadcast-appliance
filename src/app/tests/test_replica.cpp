// test_replica.cpp — la réplica del tablero web: la app (control/ReplicaPublicador) y el
// navegador (tablero-web/ClienteReplica + ObjetoReplica) hablando por JSON, sin red.
//
//   el esquema sale del QMetaObject (sin secretos); la QML enlazada a la réplica ve los
//   valores y se actualiza cuando cambian en la app (propiedades y filas de un modelo, sin
//   rehacer las filas que no cambiaron); los objetos hijos se abren solos al leerlos; las
//   funciones de los botones llegan a la app; las que devuelven algo responden en el
//   binding; escribir una propiedad desde la QML llega a la app; un perfil que no es
//   «todo» no ve nada; lo negado no sale; un cuadro de video se comprime y se descomprime.

#include <QAbstractListModel>
#include <QBuffer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTimer>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "ClienteReplica.hpp"
#include "ObjetoReplica.hpp"
#include "control/CuadrosWeb.hpp"
#include "control/ReplicaPublicador.hpp"

using ba::app::control::ReplicaPublicador;
using json = nlohmann::json;

namespace {
int g_fallas = 0;
#define CHECK(c, m)                                             \
    do {                                                        \
        if (!(c)) {                                             \
            std::printf("FALLO: %s (linea %d)\n", m, __LINE__); \
            ++g_fallas;                                         \
        } else {                                                \
            std::printf("ok: %s\n", m);                         \
        }                                                       \
    } while (0)
} // namespace

class Hijo : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString texto MEMBER texto NOTIFY cambio)
   public:
    QString texto = "hola";
   signals:
    void cambio();
};

class Lista : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int columnas MEMBER columnas NOTIFY cambio)
   public:
    struct Fila {
        QString titulo;
        int valor;
    };
    std::vector<Fila> filas{{"uno", 1}, {"dos", 2}};
    int columnas = 12;
    int rowCount(const QModelIndex& = {}) const override { return static_cast<int>(filas.size()); }
    QVariant data(const QModelIndex& i, int rol) const override {
        const Fila& f = filas[static_cast<std::size_t>(i.row())];
        return rol == Qt::UserRole + 1 ? QVariant(f.titulo) : QVariant(f.valor);
    }
    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole + 1, "titulo"}, {Qt::UserRole + 2, "valor"}}; }
    void agregar(const QString& t, int v) {
        beginInsertRows({}, rowCount(), rowCount());
        filas.push_back({t, v});
        endInsertRows();
    }
    void cambiarValor(int fila, int v) {
        filas[static_cast<std::size_t>(fila)].valor = v;
        emit dataChanged(index(fila), index(fila));
    }
    Q_INVOKABLE void quitarPrimera() {
        beginRemoveRows({}, 0, 0);
        filas.erase(filas.begin());
        endRemoveRows();
    }
   signals:
    void cambio();
};

class Raiz : public QObject {
    Q_OBJECT
    Q_PROPERTY(int contador READ contador NOTIFY contadorCambio)
    Q_PROPERTY(QString nombre READ nombre WRITE setNombre NOTIFY nombreCambio)
    Q_PROPERTY(QString streamKey READ nombre CONSTANT)
    Q_PROPERTY(QString pinTunel READ nombre CONSTANT)
    Q_PROPERTY(QObject* hijo READ hijo CONSTANT)
    Q_PROPERTY(QObject* lista READ lista CONSTANT)
    Q_PROPERTY(QObject* ventanas READ ventanas CONSTANT)
    Q_PROPERTY(QVariantList cosas READ cosas NOTIFY contadorCambio)
   public:
    int c = 1;
    QString n = "inicial";
    Hijo h;
    Lista l;
    Hijo v;
    int contador() const { return c; }
    QString nombre() const { return n; }
    void setNombre(const QString& x) {
        n = x;
        emit nombreCambio();
    }
    QObject* hijo() { return &h; }
    QObject* lista() { return &l; }
    QObject* ventanas() { return &v; }
    QVariantList cosas() const { return {c, QVariantMap{{"a", "b"}, {"hijo", QVariant::fromValue<QObject*>(const_cast<Hijo*>(&h))}}}; }
    Q_INVOKABLE void sumar(int k) {
        c += k;
        emit contadorCambio();
    }
    Q_INVOKABLE QString titulo(const QString& k) const { return "título de " + k + " (" + QString::number(c) + ")"; }
    Q_INVOKABLE void copiar(const QString&) { ++copias; }
    Q_INVOKABLE void restar(int k) {
        c -= k;
        emit contadorCambio();
    }
    int copias = 0;
   signals:
    void contadorCambio();
    void nombreCambio();
};

namespace {
void bombear(ReplicaPublicador& pub, ClienteReplica& cli, int vueltas = 6) {
    for (int i = 0; i < vueltas; ++i) {
        cli.vaciar();
        pub.vaciar();
        QCoreApplication::processEvents();
    }
}
} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QGuiApplication app(argc, argv);

    Raiz raiz;
    ReplicaPublicador pub(&raiz);
    pub.negar({"ventanas"});
    pub.negarFunciones({"copiar"});
    // La lista permitida (en la app sale de la QML del dashboard): «restar» no está.
    pub.setPermitidas({{"Studio", "sumar", false, "herr_aire"},
                       {"Studio", "nombre", true, ""},
                       {"Studio", "titulo", false, "herr_lectura"},
                       {"Studio.lista", "quitarPrimera", false, ""}});
    bool hayAire = false;
    std::vector<std::string> autorizadas;
    pub.ganchos().autorizar = [&](const std::string&, const std::string& h, bool lectura, bool confirmado) -> json {
        autorizadas.push_back(h + (lectura ? ":ver" : ""));
        if (h == "herr_aire" && hayAire && !confirmado) return {{"ok", false}, {"confirmar", true}, {"texto", "¿seguro?"}};
        return {{"ok", true}};
    };
    ClienteReplica cli;
    QQmlEngine engine;
    cli.setMotor(&engine);

    std::vector<json> recibidos;
    json perfil = json::array({"*"});
    pub.ganchos().enviar = [&](const std::string& cid, const json& m) {
        CHECK(cid == "c1", "la app le contesta a la conexión que preguntó");
        recibidos.push_back(m);
        cli.recibirTexto(QByteArray::fromStdString(m.dump()));
    };
    std::vector<std::string> anotado;
    pub.ganchos().anotar = [&](const std::string&, const std::string& t) { anotado.push_back(t); };
    json videoPedido;
    pub.ganchos().video = [&](const std::string&, const json& c) { videoPedido = c; };
    cli.enviar = [&](const QByteArray& t) {
        const json m = json::parse(t.toStdString(), nullptr, false);
        CHECK(m.is_object() && m.value("type", "") == "replica", "el navegador manda {type:replica}");
        pub.entrada("c1", "ap1", perfil, m);
    };

    // Una propiedad local del navegador (como Studio.dashboard: la disposición del aparato).
    QObject tableroLocal;
    tableroLocal.setObjectName("local");
    cli.reemplazar("Studio", "ventanas", &tableroLocal);

    // --- Un perfil que no es «todo»: nada.
    perfil = json::array({"escenas"});
    cli.hola();
    bombear(pub, cli);
    CHECK(!cli.listo() && !cli.error().isEmpty(), "un perfil que no es «todo» no recibe el modelo (y se le dice por qué)");
    perfil = json::array({"*"});

    // --- Empezar.
    cli.hola();
    bombear(pub, cli);
    CHECK(cli.listo(), "con «todo» llega la raíz con sus valores");
    ObjetoReplica* r = cli.raiz();
    CHECK(r != nullptr && r->valor("contador").toInt() == 1, "el valor de una propiedad llegó");
    CHECK(r != nullptr && !r->valor("streamKey").isValid(), "una propiedad con nombre de secreto no se publica (streamKey)");
    CHECK(r != nullptr && !r->valor("pinTunel").isValid(), "ni el PIN");
    CHECK(r != nullptr && r->valor("ventanas").value<QObject*>() == &tableroLocal,
          "una propiedad local del navegador es la local, no la de la app");
    CHECK(r != nullptr && !r->valor("copiar").isValid(), "una función negada no se publica");

    qmlRegisterSingletonInstance("BA", 1, 0, "Studio", r);
    QQmlComponent comp(&engine);
    comp.setData(R"(
        import QtQuick
        import BA
        Item {
            property int c: Studio.contador
            property string n: Studio.nombre
            property string t: Studio.hijo.texto
            property int cols: Studio.lista.columnas
            property int filas: rep.count
            property string primera: rep.count > 0 ? rep.itemAt(0).titulo : ""
            property int creadas: 0
            property string tit: Studio.titulo("x") || ""
            property var cosas: Studio.cosas
            Repeater {
                id: rep
                model: Studio.lista
                Item { required property string titulo; required property int valor; Component.onCompleted: parent.creadas++ }
            }
            function hacer() { Studio.sumar(5); Studio.nombre = "desde la web"; Studio.lista.quitarPrimera() }
            function sumarUno() { Studio.sumar(1) }
        })",
                 QUrl("qrc:/prueba.qml"));
    std::unique_ptr<QObject> vista(comp.create());
    if (!vista) std::printf("%s\n", qPrintable(comp.errorString()));
    CHECK(vista != nullptr, "la QML enlazada a la réplica carga");
    if (!vista) return 1;
    bombear(pub, cli);
    CHECK(vista->property("c").toInt() == 1, "la QML ve el valor");
    CHECK(vista->property("t").toString() == "hola", "un objeto hijo se abre solo al leerlo");
    CHECK(vista->property("cols").toInt() == 12, "un modelo también tiene sus propiedades");
    CHECK(vista->property("filas").toInt() == 2 && vista->property("primera").toString() == "uno",
          "las filas del modelo, con sus roles");
    CHECK(vista->property("tit").toString() == "título de x (1)", "una función que devuelve algo responde en el binding");
    const QVariantList cosas = vista->property("cosas").toList();
    CHECK(cosas.size() == 2 && cosas.at(1).toMap().value("hijo").value<QObject*>() == cli.objeto("Studio.hijo"),
          "un objeto adentro de una lista llega como la misma réplica");

    // --- Cambios en la app (lo que devuelven las funciones se vuelve a pedir una vez por segundo como mucho).
    {
        QEventLoop espera;
        QTimer::singleShot(1100, &espera, &QEventLoop::quit);
        espera.exec();
    }
    const int creadasAntes = vista->property("creadas").toInt();
    raiz.sumar(2);
    raiz.h.texto = "chau";
    emit raiz.h.cambio();
    raiz.l.agregar("tres", 3);
    raiz.l.cambiarValor(0, 10);
    bombear(pub, cli);
    CHECK(vista->property("c").toInt() == 3, "un cambio en la app llega a la QML");
    CHECK(vista->property("t").toString() == "chau", "también en un hijo");
    CHECK(vista->property("filas").toInt() == 3, "una fila nueva");
    CHECK(vista->property("creadas").toInt() == creadasAntes + 1, "sin rehacer las filas que ya estaban");
    ObjetoReplica* lista = cli.objeto("Studio.lista");
    CHECK(lista != nullptr && lista->data(lista->index(0), Qt::UserRole + 2).toInt() == 10, "un dato que cambió en una fila");
    CHECK(vista->property("tit").toString() == "título de x (3)", "lo que devuelve una función se vuelve a pedir si cambió el objeto");

    // --- Desde la web.
    anotado.clear();
    QMetaObject::invokeMethod(vista.get(), "hacer");
    bombear(pub, cli);
    CHECK(raiz.c == 8, "una función de un botón llega a la app");
    CHECK(raiz.n == "desde la web", "escribir una propiedad desde la QML llega a la app");
    CHECK(raiz.l.filas.size() == 2, "una función de un modelo también");
    CHECK(vista->property("c").toInt() == 8 && vista->property("filas").toInt() == 2, "y vuelve a la vista");
    CHECK(anotado.size() >= 2, "lo que cambia algo queda en el registro");

    // --- Lo que no se puede.
    const std::size_t antes = recibidos.size();
    cli.enviar(QByteArray(R"({"type":"replica","op":"llamar","id":999,"o":"Studio","m":"copiar","a":["x"]})"));
    bombear(pub, cli);
    CHECK(raiz.copias == 0, "una función negada no se llama aunque la pidan a mano");
    bool rechazo = false;
    for (std::size_t i = antes; i < recibidos.size(); ++i)
        for (const auto& m : recibidos[i].value("m", json::array()))
            if (m.value("op", "") == "resultado" && m.value("id", 0) == 999 && !m.value("ok", true)) rechazo = true;
    CHECK(rechazo, "y se contesta que no");
    anotado.clear();
    cli.enviar(QByteArray(R"({"type":"replica","op":"llamar","id":998,"o":"Studio","m":"restar","a":[1]})"));
    bombear(pub, cli);
    CHECK(raiz.c == 8, "una función fuera de la lista permitida no se llama");
    CHECK(!anotado.empty() && anotado[0].find("RECHAZADA") != std::string::npos, "y queda en el registro");
    CHECK(std::find(autorizadas.begin(), autorizadas.end(), "herr_lectura:ver") != autorizadas.end(),
          "lo que devuelve algo se autoriza como lectura de su herramienta");

    // --- De aire con aire: pide confirmación, y recién confirmada se hace.
    hayAire = true;
    int idConfirmar = -1;
    QObject::connect(&cli, &ClienteReplica::confirmar, [&](int id, const QString&) { idConfirmar = id; });
    QMetaObject::invokeMethod(vista.get(), "sumarUno");
    bombear(pub, cli);
    CHECK(raiz.c == 8 && idConfirmar > 0, "algo de aire con aire no se hace: se pide confirmar");
    cli.confirmarLlamada(idConfirmar);
    bombear(pub, cli);
    CHECK(raiz.c == 9, "confirmado, se hace");
    hayAire = false;
    cli.enviar(QByteArray(R"({"type":"replica","op":"poner","o":"Studio","p":"contador","v":99})"));
    bombear(pub, cli);
    CHECK(raiz.c == 9, "una propiedad de solo lectura no se escribe");

    // --- Video.
    cli.pedirVideo(&cli, "program", 640, 25);
    QEventLoop l;
    QTimer::singleShot(150, &l, &QEventLoop::quit);
    l.exec();
    bombear(pub, cli);
    CHECK(videoPedido.is_array() && videoPedido.size() == 1 && videoPedido[0].value("clave", "") == "program" &&
              videoPedido[0].value("fps", 0) == 25,
          "el pedido de video llega a la app");
    QImage img(1920, 1080, QImage::Format_RGB32);
    img.fill(Qt::red);
    int w = 0, h = 0;
    const QByteArray jpg = ba::app::control::CuadrosWeb::comprimir(img, 640, 72, &w, &h);
    CHECK(w == 640 && h == 360 && jpg.size() > 100, "un cuadro se achica y se comprime");
    bool llego = false;
    QObject::connect(&cli, &ClienteReplica::cuadro, [&](const QString& clave, const QImage& im, qint64 t, quint64 s) {
        llego = clave == "program" && im.width() == 640 && t == 1234 && s == 7;
    });
    cli.recibirTexto(QByteArray::fromStdString(json{{"type", "replica"},
                                                    {"op", "cuadro"},
                                                    {"clave", "program"},
                                                    {"s", 7},
                                                    {"t", 1234},
                                                    {"w", w},
                                                    {"h", h},
                                                    {"jpg", jpg.toBase64().toStdString()}}
                                                   .dump()));
    CHECK(llego, "y del otro lado se descomprime");

    pub.cerrada("c1");
    CHECK(pub.conexiones() == 0 && videoPedido.empty(), "al irse la conexión se suelta todo (también el video)");

    std::printf("%s (%d fallas)\n", g_fallas ? "FALLO" : "OK", g_fallas);
    return g_fallas ? 1 : 0;
}

#include "test_replica.moc"
