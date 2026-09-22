// ============================================================================
// src/grafico/Renderizador.cpp — ver Renderizador.hpp
//
// TRES COSAS QUE COSTARON, ANOTADAS PARA NO VOLVER A PERDER EL TIEMPO:
//
// 1. El reloj propio SOLO se puede instalar con QQuickRenderControl. Una
//    QQuickWindow normal arranca el bucle de dibujo de Qt Quick, que instala
//    su propio conductor de animaciones, y entonces Qt rechaza el nuestro
//    ("animation driver already installed"). Sin reloj propio no hay vista
//    previa reproducible: todos los cuadros salen en el instante cero.
//
// 2. Hay que darle a Qt NUESTRO contexto de OpenGL (setGraphicsDevice) antes
//    de inicializar. Si no, Qt arma el suyo, dibuja ahí, y el framebuffer que
//    creamos nosotros vive en otro contexto: al leerlo salen píxeles de
//    basura.
//
// 3. El renderizador por software no sirve: no soporta efectos (MultiEffect),
//    o sea nada de desenfoques ni máscaras, que es la mitad de un gráfico.
// ============================================================================
#include "Renderizador.hpp"

#include <QAnimationDriver>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonValue>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickGraphicsDevice>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QSurfaceFormat>

#include "RelojAnimaciones.hpp"

namespace ba::grafico {
namespace {

/// Los módulos que la gente (y la LLM) escribe por inercia y que en este Qt
/// NO existen. Se detectan al cargar para poder decir qué usar en su lugar,
/// en vez de dejar un error de Qt que no ayuda a nadie.
struct Reemplazo {
    const char* viejo;
    const char* consejo;
};
constexpr Reemplazo kModulosQueNoEstan[] = {
    {"QtGraphicalEffects",
     "ese módulo es de Qt 5 y acá no está. Usá «import QtQuick.Effects» con MultiEffect "
     "(desenfoque, sombra, máscara)."},
    {"Qt5Compat.GraphicalEffects",
     "no viaja en este bundle. Usá «import QtQuick.Effects» con MultiEffect."},
    {"QtQuick.VectorImage",
     "es de Qt 6.8 y acá hay 6.6. Para vector animable usá Shape con PathSvg, que acepta el "
     "atributo «d» de un SVG tal cual."},
};

QVariant aVariant(const QJsonValue& v) {
    switch (v.type()) {
        case QJsonValue::Bool: return v.toBool();
        case QJsonValue::Double: return v.toDouble();
        case QJsonValue::String: return v.toString();
        case QJsonValue::Array: return v.toArray().toVariantList();
        case QJsonValue::Object: return v.toObject().toVariantMap();
        default: return {};
    }
}

QString textoDeErrores(const QList<QQmlError>& errores) {
    QStringList l;
    for (const QQmlError& e : errores) {
        QString linea = e.description();
        if (e.line() > 0) linea = QString("línea %1: %2").arg(e.line()).arg(linea);
        // El consejo va PEGADO al error: quien lo lee no tiene por qué ir a
        // buscar la documentación para entender qué hacer.
        for (const Reemplazo& r : kModulosQueNoEstan)
            if (e.description().contains(QLatin1String(r.viejo)))
                linea += QString(" — %1").arg(QString::fromUtf8(r.consejo));
        l << linea;
    }
    return l.join("\n");
}

} // namespace

struct Renderizador::Impl {
    QOpenGLContext* gl = nullptr;
    QOffscreenSurface* superficie = nullptr;
    QQuickRenderControl* control = nullptr;
    QQuickWindow* ventana = nullptr;
    RelojAnimaciones* reloj = nullptr;
    std::unique_ptr<QOpenGLFramebufferObject> fbo;
    bool listo = false;
    bool calentado = false;

    ~Impl() {
        if (gl != nullptr && superficie != nullptr) gl->makeCurrent(superficie);
        fbo.reset();
        delete ventana;
        delete control;
        if (gl != nullptr) gl->doneCurrent();
        delete superficie;
        delete gl;
        delete reloj;
    }

    bool preparar(QString& error) {
        if (listo) return true;
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

        QSurfaceFormat fmt;
        fmt.setDepthBufferSize(16);
        fmt.setStencilBufferSize(8);
        fmt.setAlphaBufferSize(8); // el gráfico se compone sobre el video

        gl = new QOpenGLContext();
        gl->setFormat(fmt);
        if (!gl->create()) {
            error = "no se pudo crear el contexto de OpenGL";
            return false;
        }
        superficie = new QOffscreenSurface();
        superficie->setFormat(gl->format());
        superficie->create();
        if (!superficie->isValid()) {
            error = "no se pudo crear la superficie fuera de pantalla";
            return false;
        }
        if (!gl->makeCurrent(superficie)) {
            error = "no se pudo activar el contexto de OpenGL";
            return false;
        }

        control = new QQuickRenderControl();
        ventana = new QQuickWindow(control);
        ventana->setColor(Qt::transparent);
        // Que Qt dibuje en NUESTRO contexto (ver el encabezado, punto 2).
        ventana->setGraphicsDevice(QQuickGraphicsDevice::fromOpenGLContext(gl));
        if (!control->initialize()) {
            error = "no se pudo inicializar el renderizador de QML";
            return false;
        }

        // Con RenderControl el tiempo lo manda la aplicación, así que acá sí
        // se acepta nuestro conductor de animaciones.
        reloj = new RelojAnimaciones();
        reloj->install();
        listo = true;
        return true;
    }

    /// Un framebuffer del tamaño pedido (se rehace solo si cambió).
    bool destino(QSize tam, QString& error) {
        if (fbo && fbo->size() == tam) return true;
        QOpenGLFramebufferObjectFormat f;
        f.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        fbo = std::make_unique<QOpenGLFramebufferObject>(tam, f);
        if (!fbo->isValid()) {
            error = QString("no se pudo crear el destino de dibujo de %1x%2")
                        .arg(tam.width())
                        .arg(tam.height());
            fbo.reset();
            return false;
        }
        ventana->setRenderTarget(QQuickRenderTarget::fromOpenGLTexture(fbo->texture(), tam));
        return true;
    }
};

Renderizador::Renderizador() : d_(std::make_unique<Impl>()) {}
Renderizador::~Renderizador() = default;

Renderizador::Resultado Renderizador::validar(const QString& qml, const QJsonObject& parametros) {
    return dibujar(qml, parametros, QSize(64, 64), 0);
}

Renderizador::Resultado Renderizador::dibujar(const QString& qml, const QJsonObject& parametros,
                                              QSize tam, int ms) {
    return dibujarComun(QUrl::fromLocalFile(qml), {}, parametros, tam, ms);
}

Renderizador::Resultado Renderizador::dibujarFuente(const QString& fuente,
                                                    const QJsonObject& parametros, QSize tam, int ms,
                                                    const QString& nombreParaErrores) {
    return dibujarComun(QUrl(nombreParaErrores), fuente, parametros, tam, ms);
}

Renderizador::Resultado Renderizador::dibujarComun(const QUrl& url, const QString& fuente,
                                                   const QJsonObject& parametros, QSize tam, int ms) {
    Resultado r;
    if (!d_->preparar(r.error)) return r;

    // El primer dibujo de la sesión sale con el reloj sin arrancar del todo:
    // la animación queda en su instante cero sin importar lo que se pida. No
    // encontré la razón exacta adentro de Qt, y adivinar sale caro, así que se
    // hace UN dibujo de descarte y desde ahí todos salen bien. Cuesta una vez
    // por proceso.
    if (!d_->calentado) {
        d_->calentado = true;
        dibujarComun(url, fuente, parametros, tam, ms);
    }

    tam = tam.expandedTo(QSize(2, 2)).boundedTo(QSize(3840, 2160));
    d_->gl->makeCurrent(d_->superficie);
    d_->ventana->setGeometry(0, 0, tam.width(), tam.height());
    if (!d_->destino(tam, r.error)) return r;

    // OJO: el reloj NO se reinicia entre dibujos, aunque cada uno empiece de
    // cero desde el punto de vista del gráfico. Qt guarda cuándo fue el último
    // tic y calcula el avance por diferencia: si el reloj volviera a cero, esa
    // diferencia daría negativa y el pedido siguiente no avanzaría nada (se
    // veía como cuadros que iban un paso atrás, alternando). Como el gráfico
    // se crea de nuevo cada vez, sus animaciones arrancan en el instante que
    // lean al crearse, y desde ahí se adelanta lo pedido.

    // Un motor NUEVO por dibujo: así el gráfico arranca siempre desde cero y
    // no hereda ni la caché de componentes ni el estado de la vez anterior.
    QQmlEngine motor;
    motor.setOutputWarningsToStandardError(false);
    QQmlComponent componente(&motor);
    if (fuente.isEmpty())
        componente.loadUrl(url);
    else
        componente.setData(fuente.toUtf8(), url);
    if (componente.isError()) {
        r.error = textoDeErrores(componente.errors());
        return r;
    }

    std::unique_ptr<QObject> obj(componente.create());
    if (obj == nullptr) {
        r.error = componente.isError() ? textoDeErrores(componente.errors())
                                       : QString("el gráfico no devolvió ningún objeto");
        return r;
    }
    auto* raiz = qobject_cast<QQuickItem*>(obj.get());
    if (raiz == nullptr) {
        r.error = "la raíz del gráfico tiene que ser un Item (algo que se pueda dibujar)";
        return r;
    }

    // Los parámetros, ANTES de que arranque el tiempo: si se pusieran después,
    // una animación que depende del texto ya habría empezado con el texto
    // viejo.
    for (auto it = parametros.begin(); it != parametros.end(); ++it) {
        const QByteArray nombre = it.key().toUtf8();
        if (raiz->metaObject()->indexOfProperty(nombre.constData()) < 0) {
            r.error = QString("el gráfico no declara el parámetro «%1». Los parámetros van "
                              "declarados como propiedades del Item raíz y listados en "
                              "grafico.json.")
                          .arg(it.key());
            return r;
        }
        raiz->setProperty(nombre.constData(), aVariant(it.value()));
    }

    raiz->setParentItem(d_->ventana->contentItem());
    raiz->setWidth(tam.width());
    raiz->setHeight(tam.height());

    // Una vuelta de eventos para que las animaciones se den por arrancadas.
    QCoreApplication::processEvents();

    // Un cuadro de asentamiento en el instante cero, ANTES de adelantar. No
    // se usa su imagen: sirve para que Qt registre el arranque de cada
    // animación. Sin él, el primer avance de la sesión se perdía y ese cuadro
    // salía como si el tiempo no hubiera corrido.
    const auto dibujarCuadro = [this]() {
        d_->control->polishItems();
        d_->control->beginFrame();
        d_->control->sync();
        d_->control->render();
        d_->control->endFrame();
    };
    dibujarCuadro();
    d_->reloj->avanzar(0);

    if (ms > 0) d_->reloj->avanzar(ms);
    dibujarCuadro();

    r.imagen = d_->fbo->toImage();
    if (r.imagen.isNull()) {
        r.error = "el renderizador no devolvió ninguna imagen";
        return r;
    }
    // Premultiplicado: es como libobs espera el alfa. Sin esto, el texto
    // blanco sale con un halo oscuro en los bordes sobre el video.
    if (r.imagen.format() != QImage::Format_ARGB32_Premultiplied)
        r.imagen = r.imagen.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    raiz->setParentItem(nullptr);
    return r;
}

} // namespace ba::grafico
