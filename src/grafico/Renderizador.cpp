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
#include <QJsonDocument>
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
#include "diag/Registro.hpp"

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
    {"Qt5Compat.GraphicalEffects", "no viaja en este bundle. Usá «import QtQuick.Effects» con MultiEffect."},
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

/// Los avisos de QML (una propiedad que no existe en un componente, un medio
/// que no está) al registro del renderizador: no rompen el dibujo, así que
/// nadie se enteraba y el gráfico salía con un pedazo de menos.
void avisosAlRegistro(QQmlEngine& motor) {
    QObject::connect(&motor, &QQmlEngine::warnings, [](const QList<QQmlError>& avisos) {
        for (const QQmlError& e : avisos) ba::diag::registro().aviso("qml: " + e.toString().toStdString());
    });
}

/// Lo que el sistema le pasa a cualquier gráfico, pero que el gráfico puede no
/// declarar: se asigna solo si existe la propiedad. A diferencia de un
/// parámetro, que falte no es un nombre mal escrito: es un gráfico que no
/// sabe animar un cambio, y entonces se dibuja como siempre.
void ponerOpcionales(QObject* raiz, const QJsonObject& opcionales) {
    for (auto it = opcionales.begin(); it != opcionales.end(); ++it) {
        const QByteArray nombre = it.key().toUtf8();
        if (raiz->metaObject()->indexOfProperty(nombre.constData()) >= 0)
            raiz->setProperty(nombre.constData(), aVariant(it.value()));
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
    // La instancia de la tira que sigue (ver Renderizador::tira). El objeto antes que su motor.
    std::unique_ptr<QQmlEngine> sesionMotor;
    std::unique_ptr<QObject> sesionObj;
    QString sesionClave;
    int sesionMs = -1;
    void cerrarSesion() {
        if (auto* item = qobject_cast<QQuickItem*>(sesionObj.get())) item->setParentItem(nullptr);
        sesionObj.reset();
        sesionMotor.reset();
        sesionClave.clear();
        sesionMs = -1;
    }

    ~Impl() {
        if (gl != nullptr && superficie != nullptr) gl->makeCurrent(superficie);
        cerrarSesion();
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
            error =
                QString("no se pudo crear el destino de dibujo de %1x%2").arg(tam.width()).arg(tam.height());
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

Renderizador::Resultado Renderizador::dibujar(const QString& qml, const QJsonObject& parametros, QSize tam,
                                              int ms, const QJsonObject& opcionales) {
    return dibujarComun(QUrl::fromLocalFile(qml), {}, parametros, tam, ms, opcionales);
}

Renderizador::Resultado Renderizador::dibujarFuente(const QString& fuente, const QJsonObject& parametros,
                                                    QSize tam, int ms, const QUrl& base,
                                                    const QJsonObject& opcionales) {
    return dibujarComun(base, fuente, parametros, tam, ms, opcionales);
}

Renderizador::Resultado Renderizador::dibujarComun(const QUrl& url, const QString& fuente,
                                                   const QJsonObject& parametros, QSize tam, int ms,
                                                   const QJsonObject& opcionales) {
    Resultado r;
    if (!d_->preparar(r.error)) return r;
    d_->cerrarSesion(); // un dibujo suelto no convive con la instancia de una tira en la ventana

    // El primer dibujo de la sesión sale con el reloj sin arrancar del todo:
    // la animación queda en su instante cero sin importar lo que se pida. No
    // encontré la razón exacta adentro de Qt, y adivinar sale caro, así que se
    // hace UN dibujo de descarte y desde ahí todos salen bien. Cuesta una vez
    // por proceso.
    if (!d_->calentado) {
        d_->calentado = true;
        dibujarComun(url, fuente, parametros, tam, ms, opcionales);
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
    avisosAlRegistro(motor);
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
    ponerOpcionales(raiz, opcionales);

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

std::vector<Renderizador::Resultado> Renderizador::tira(const QUrl& url, const QString& fuente,
                                                       const QJsonObject& parametros, QSize tam,
                                                       const std::vector<int>& momentos,
                                                       const QJsonObject& opcionales) {
    std::vector<Resultado> out;
    Resultado base;
    if (momentos.empty()) return out;
    if (!d_->preparar(base.error)) {
        out.push_back(base);
        return out;
    }
    // El dibujo de descarte de la primera vez (ver dibujarComun).
    if (!d_->calentado) dibujarComun(url, fuente, parametros, tam, momentos.front(), opcionales);

    tam = tam.expandedTo(QSize(2, 2)).boundedTo(QSize(3840, 2160));
    d_->gl->makeCurrent(d_->superficie);
    const QString clave = url.toString() + "\n" + fuente + "\n" +
                          QString::fromUtf8(QJsonDocument(parametros).toJson(QJsonDocument::Compact)) + "\n" +
                          QString::fromUtf8(QJsonDocument(opcionales).toJson(QJsonDocument::Compact)) + "\n" +
                          QString::number(tam.width()) + "x" + QString::number(tam.height());
    const auto dibujarCuadro = [this]() {
        d_->control->polishItems();
        d_->control->beginFrame();
        d_->control->sync();
        d_->control->render();
        d_->control->endFrame();
    };
    // Otra cosa, o para atrás en el tiempo: una instancia nueva (como dibujarComun).
    if (clave != d_->sesionClave || !d_->sesionObj || momentos.front() < d_->sesionMs) {
        d_->cerrarSesion();
        d_->ventana->setGeometry(0, 0, tam.width(), tam.height());
        if (!d_->destino(tam, base.error)) {
            out.push_back(base);
            return out;
        }
        d_->sesionMotor = std::make_unique<QQmlEngine>();
        d_->sesionMotor->setOutputWarningsToStandardError(false);
        avisosAlRegistro(*d_->sesionMotor);
        QQmlComponent componente(d_->sesionMotor.get());
        if (fuente.isEmpty())
            componente.loadUrl(url);
        else
            componente.setData(fuente.toUtf8(), url);
        if (componente.isError()) {
            base.error = textoDeErrores(componente.errors());
            d_->cerrarSesion();
            out.push_back(base);
            return out;
        }
        d_->sesionObj.reset(componente.create());
        auto* raiz = qobject_cast<QQuickItem*>(d_->sesionObj.get());
        if (raiz == nullptr) {
            base.error = d_->sesionObj ? QString("la raíz del gráfico tiene que ser un Item (algo que se pueda dibujar)")
                                       : (componente.isError() ? textoDeErrores(componente.errors())
                                                               : QString("el gráfico no devolvió ningún objeto"));
            d_->cerrarSesion();
            out.push_back(base);
            return out;
        }
        for (auto it = parametros.begin(); it != parametros.end(); ++it) {
            const QByteArray nombre = it.key().toUtf8();
            if (raiz->metaObject()->indexOfProperty(nombre.constData()) < 0) {
                base.error = QString("el gráfico no declara el parámetro «%1»").arg(it.key());
                d_->cerrarSesion();
                out.push_back(base);
                return out;
            }
            raiz->setProperty(nombre.constData(), aVariant(it.value()));
        }
        ponerOpcionales(raiz, opcionales);
        raiz->setParentItem(d_->ventana->contentItem());
        raiz->setWidth(tam.width());
        raiz->setHeight(tam.height());
        QCoreApplication::processEvents();
        dibujarCuadro(); // el asentamiento en el instante cero (ver dibujarComun)
        d_->reloj->avanzar(0);
        d_->sesionClave = clave;
        d_->sesionMs = 0;
    }
    for (const int m : momentos) {
        if (m > d_->sesionMs) {
            d_->reloj->avanzar(m - d_->sesionMs);
            d_->sesionMs = m;
        }
        dibujarCuadro();
        Resultado r;
        r.imagen = d_->fbo->toImage();
        if (r.imagen.isNull()) r.error = "el renderizador no devolvió ninguna imagen";
        else if (r.imagen.format() != QImage::Format_ARGB32_Premultiplied)
            r.imagen = r.imagen.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        out.push_back(std::move(r));
        if (!out.back().error.isEmpty()) break;
    }
    return out;
}

} // namespace ba::grafico
