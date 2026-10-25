// test_qml_carga.cpp — cada .qml de la interfaz COMPILA (no solo parsea)
//
// qmllint (test_qml_syntax) agarra los errores de sintaxis, pero no los que
// aparecen al compilar el componente: una propiedad asignada dos veces
// («Property value set multiple times»), un tipo que no existe, un id repetido.
// Uno de esos dejó la app sin interfaz: arrancaba, no podía cargar Main.qml, se
// cerraba y el launcher la relanzaba en bucle. Acá se compila cada archivo con
// el motor de QML de verdad, con los tipos de la app (Studio, VideoItem,
// TerminalView) reemplazados por unos de mentira: las propiedades de Studio se
// resuelven al ejecutar, así que para compilar alcanza con que exista.
#include "ui/Vumetro.hpp"
#include <QDirIterator>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QImage>
#include <QQuickItem>
#include <QUrl>

#include <QSize>

#include <cstdio>

// Las mismas propiedades y funciones que ven los QML (ui/VideoItem.hpp,
// term/TerminalView.hpp), sin nada adentro: solo para compilar.
class VideoItemFalso : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QString source MEMBER source_ NOTIFY cambio)
    Q_PROPERTY(FillMode fillMode MEMBER fillMode_ NOTIFY cambio)
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY cambio)
    Q_PROPERTY(QSize frameSize READ frameSize NOTIFY cambio)
   public:
    enum FillMode { Fit, Cover };
    Q_ENUM(FillMode)
    bool hasFrame() const { return false; }
    QSize frameSize() const { return {}; }
   signals:
    void cambio();

   private:
    QString source_;
    FillMode fillMode_ = Fit;
};

class TerminalFalsa : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QObject* session MEMBER session_ NOTIFY cambio)
    Q_PROPERTY(int fontSize MEMBER fontSize_ NOTIFY cambio)
    Q_PROPERTY(QString fontFamily MEMBER fontFamily_ NOTIFY cambio)
    Q_PROPERTY(int scrollOffset READ cero NOTIFY cambio)
    Q_PROPERTY(bool hasSelection READ no NOTIFY cambio)
   public:
    Q_INVOKABLE void copySelection() {}
    Q_INVOKABLE void pasteClipboard() {}
    Q_INVOKABLE void scrollToBottom() {}
    int cero() const { return 0; }
    bool no() const { return false; }
   signals:
    void cambio();

   private:
    QObject* session_ = nullptr;
    int fontSize_ = 12;
    QString fontFamily_;
};

class ImagenVivaFalsa : public QQuickItem { // ui/ImagenViva.hpp
    Q_OBJECT
    Q_PROPERTY(QImage imagen MEMBER imagen_ NOTIFY cambio)
    Q_PROPERTY(bool ajustar MEMBER ajustar_ NOTIFY cambio)
    Q_PROPERTY(bool hayImagen READ no NOTIFY cambio)
   public:
    bool no() const { return false; }
   signals:
    void cambio();

   private:
    QImage imagen_;
    bool ajustar_ = true;
};

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    const QString dir = QStringLiteral(BA_QML_DIR);
    static QObject studio;
    qmlRegisterSingletonInstance("BA", 1, 0, "Studio", &studio);
    qmlRegisterType<VideoItemFalso>("BA", 1, 0, "VideoItem");
    qmlRegisterType<TerminalFalsa>("BA", 1, 0, "TerminalView");
    qmlRegisterType<ImagenVivaFalsa>("BA", 1, 0, "ImagenViva");
    qmlRegisterType<ba::app::Vumetro>("BA", 1, 0, "Vumetro");
    qmlRegisterSingletonType(QUrl::fromLocalFile(dir + "/Theme.qml"), "BA", 1, 0, "Theme");

    QQmlEngine engine;
    int n = 0, malos = 0;
    QDirIterator it(dir, {"*.qml"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        ++n;
        QQmlComponent c(&engine, QUrl::fromLocalFile(f), QQmlComponent::PreferSynchronous);
        if (c.isError()) {
            ++malos;
            for (const auto& e : c.errors()) std::printf("%s\n", qPrintable(e.toString()));
        }
    }
    std::printf("test_qml_carga: %d archivos, %d no compilan\n", n, malos);
    return malos == 0 ? 0 : 1;
}

#include "test_qml_carga.moc"
