// ============================================================================
// src/grafico/Componentes.cpp — ver Componentes.hpp
// ============================================================================
#include "Componentes.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <utility>
#include <vector>

#include <QAbstractAnimation>
#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJSValue>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSGImageNode>

#include "Declarativo.hpp"

namespace ba::grafico {

namespace {

/// Cuánta memoria puede ocupar un GIF ya decodificado. Un GIF de pantalla
/// completa con cientos de cuadros no es un gráfico: es un video, y se importa
/// como secuencia (que se lee de a un cuadro).
constexpr qint64 kTopeAnimadaBytes = 512LL * 1024 * 1024;
constexpr int kTopeCuadros = 5000;

const QStringList& extensionesDeVideo() {
    static const QStringList l{"webm", "mov", "mp4", "m4v", "mkv", "avi", "mxf", "mpg", "mpeg", "ts", "ogv"};
    return l;
}

} // namespace

// --- De dónde salen los cuadros ------------------------------------------------

class FuenteDeCuadros {
   public:
    virtual ~FuenteDeCuadros() = default;
    virtual int cantidad() const = 0;
    /// ms que dura entera (0 = una imagen quieta).
    virtual int duracion() const = 0;
    /// El cuadro que se ve en el ms `ms` (0 <= ms < duracion).
    virtual int indiceEn(int ms) const = 0;
    virtual QImage cuadro(int i) = 0;
};

namespace {

/// Un GIF (o WebP animado, o una imagen quieta): se decodifica entero al
/// abrir, con la duración de cada cuadro tal como la trae el archivo.
class Animada : public FuenteDeCuadros {
   public:
    static std::unique_ptr<Animada> abrir(const QString& ruta, QString& error) {
        QImageReader r(ruta);
        if (!r.canRead()) {
            error = QCoreApplication::translate("graficos", "no se puede leer «%1»: %2").arg(QFileInfo(ruta).fileName(), r.errorString());
            return nullptr;
        }
        auto a = std::make_unique<Animada>();
        qint64 bytes = 0;
        while (static_cast<int>(a->cuadros_.size()) < kTopeCuadros) {
            QImage img = r.read();
            if (img.isNull()) break;
            int dur = r.nextImageDelay();
            // Los navegadores toman 0 y 10 ms como 100: así se ven los GIF que
            // circulan, y así tienen que verse acá.
            if (dur <= 10) dur = 100;
            bytes += img.sizeInBytes();
            if (bytes > kTopeAnimadaBytes) {
                error = QCoreApplication::translate("graficos", "«%1» es demasiado grande para un gráfico (más de %2 MB "
                                                                "decodificado): importalo como secuencia de cuadros")
                            .arg(QFileInfo(ruta).fileName())
                            .arg(kTopeAnimadaBytes / (1024 * 1024));
                return nullptr;
            }
            a->cuadros_.push_back(img.convertToFormat(QImage::Format_ARGB32_Premultiplied));
            a->desde_.push_back(a->total_);
            a->total_ += dur;
            if (!r.supportsAnimation()) break;
        }
        if (a->cuadros_.empty()) {
            error = QCoreApplication::translate("graficos", "«%1» no tiene ninguna imagen que se pueda leer").arg(QFileInfo(ruta).fileName());
            return nullptr;
        }
        if (a->cuadros_.size() == 1) a->total_ = 0; // una imagen quieta no dura
        return a;
    }
    int cantidad() const override { return static_cast<int>(cuadros_.size()); }
    int duracion() const override { return total_; }
    int indiceEn(int ms) const override {
        // El último cuadro que empezó antes de `ms`.
        const auto it = std::upper_bound(desde_.begin(), desde_.end(), ms);
        return std::max(0, static_cast<int>(it - desde_.begin()) - 1);
    }
    QImage cuadro(int i) override { return cuadros_.at(static_cast<size_t>(std::clamp(i, 0, cantidad() - 1))); }

   private:
    std::vector<QImage> cuadros_;
    std::vector<int> desde_;
    int total_ = 0;
};

/// Una carpeta de cuadros: los PNG de un video importado (con su alfa) o los
/// cuadros crudos que dibujó un plugin. Se leen de a uno, cuando hacen falta.
///
/// secuencia.json (opcional): {"fps": 30, "formato": "png"} o, crudos,
/// {"fps", "formato": "crudo", "archivo": "cuadros.bgra", "ancho", "alto",
///  "cuadros"} — BGRA premultiplicado, un cuadro atrás del otro.
class Secuencia : public FuenteDeCuadros {
   public:
    static std::unique_ptr<Secuencia> abrir(const QString& dir, qreal fpsPorDefecto, QString& error) {
        auto s = std::make_unique<Secuencia>();
        QJsonObject meta;
        QFile fm(QDir(dir).filePath("secuencia.json"));
        if (fm.open(QIODevice::ReadOnly)) meta = QJsonDocument::fromJson(fm.readAll()).object();
        s->fps_ = std::clamp(meta.value("fps").toDouble(fpsPorDefecto), 1.0, 240.0);
        if (meta.value("formato").toString() == "crudo") {
            s->ancho_ = meta.value("ancho").toInt();
            s->alto_ = meta.value("alto").toInt();
            s->n_ = meta.value("cuadros").toInt();
            s->crudo_ = std::make_unique<QFile>(QDir(dir).filePath(meta.value("archivo").toString("cuadros.bgra")));
            const qint64 porCuadro = static_cast<qint64>(s->ancho_) * s->alto_ * 4;
            if (s->ancho_ <= 0 || s->alto_ <= 0 || s->n_ <= 0 || !s->crudo_->open(QIODevice::ReadOnly) ||
                s->crudo_->size() < porCuadro * s->n_) {
                error = QCoreApplication::translate("graficos", "la secuencia de cuadros crudos está incompleta (%1)").arg(dir);
                return nullptr;
            }
            s->mapa_ = s->crudo_->map(0, porCuadro * s->n_);
            if (s->mapa_ == nullptr) {
                error = QCoreApplication::translate("graficos", "no se pudo leer la secuencia de cuadros crudos (%1)").arg(dir);
                return nullptr;
            }
            return s;
        }
        // Ordenados como los ordena una persona: «cuadro2» antes que «cuadro10».
        QStringList archivos = QDir(dir).entryList({"*.png", "*.PNG", "*.webp", "*.jpg", "*.jpeg"}, QDir::Files);
        QCollator orden;
        orden.setNumericMode(true);
        std::sort(archivos.begin(), archivos.end(), [&](const QString& a, const QString& b) { return orden.compare(a, b) < 0; });
        for (const QString& a : archivos) s->archivos_ << QDir(dir).filePath(a);
        s->n_ = static_cast<int>(s->archivos_.size());
        if (s->n_ == 0) {
            error = QCoreApplication::translate("graficos", "la carpeta «%1» no tiene cuadros (PNG)").arg(QFileInfo(dir).fileName());
            return nullptr;
        }
        return s;
    }
    int cantidad() const override { return n_; }
    int duracion() const override { return n_ <= 1 ? 0 : static_cast<int>(std::lround(n_ * 1000.0 / fps_)); }
    int indiceEn(int ms) const override {
        return std::clamp(static_cast<int>(std::floor(ms * fps_ / 1000.0)), 0, n_ - 1);
    }
    QImage cuadro(int i) override {
        i = std::clamp(i, 0, n_ - 1);
        if (mapa_ != nullptr) {
            const qint64 porCuadro = static_cast<qint64>(ancho_) * alto_ * 4;
            // Copia: el mapa se va con la fuente y la imagen puede quedar más.
            return QImage(mapa_ + porCuadro * i, ancho_, alto_, ancho_ * 4, QImage::Format_ARGB32_Premultiplied).copy();
        }
        // Los últimos pocos, a mano: la tira pide en orden y la barra vuelve
        // seguido al mismo lugar.
        for (const auto& [k, img] : cache_)
            if (k == i) return img;
        QImage img(archivos_.at(i));
        img = img.isNull() ? QImage() : img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        cache_.emplace_back(i, img);
        if (cache_.size() > 6) cache_.pop_front();
        return img;
    }
    ~Secuencia() override {
        if (mapa_ != nullptr) crudo_->unmap(mapa_);
    }

   private:
    qreal fps_ = 30;
    int n_ = 0;
    QStringList archivos_;
    std::deque<std::pair<int, QImage>> cache_;
    std::unique_ptr<QFile> crudo_;
    uchar* mapa_ = nullptr;
    int ancho_ = 0, alto_ = 0;
};

} // namespace

// --- El reloj: una animación de Qt, o sea el reloj del renderizador ----------------

class Medio::Reloj : public QAbstractAnimation {
   public:
    explicit Reloj(Medio* m) : QAbstractAnimation(m), medio_(m) {}
    int duration() const override { return -1; } // corre mientras exista el gráfico

   protected:
    void updateCurrentTime(int t) override { medio_->alAvanzar(t); }

   private:
    Medio* medio_;
};

Medio::Medio(QQuickItem* padre) : QQuickItem(padre) { setFlag(ItemHasContents, true); }
Medio::~Medio() = default;

void Medio::setSource(const QUrl& u) {
    if (u == source_) return;
    source_ = u;
    emit sourceChanged();
    if (isComponentComplete()) cargar();
}
void Medio::setInicio(int ms) {
    if (ms == inicio_) return;
    inicio_ = ms;
    emit inicioChanged();
    elegirCuadro();
}
void Medio::setVelocidad(qreal v) {
    if (qFuzzyCompare(v, velocidad_)) return;
    velocidad_ = v;
    emit velocidadChanged();
    elegirCuadro();
}
void Medio::setBucle(bool b) {
    if (b == bucle_) return;
    bucle_ = b;
    emit bucleChanged();
    elegirCuadro();
}
void Medio::setFps(qreal f) {
    if (qFuzzyCompare(f, fps_) || f <= 0) return;
    fps_ = f;
    emit fpsChanged();
    if (isComponentComplete()) cargar();
}
void Medio::setAjuste(Ajuste a) {
    if (a == ajuste_) return;
    ajuste_ = a;
    emit ajusteChanged();
    update();
}
int Medio::duracion() const { return fuente_ ? fuente_->duracion() : 0; }
int Medio::cuadros() const { return fuente_ ? fuente_->cantidad() : 0; }

int Medio::tiempoDelMedio(int t, int inicio, qreal velocidad, int duracion, bool bucle) {
    if (t < inicio) return -1;
    const int m = static_cast<int>(std::floor((t - inicio) * std::max<qreal>(0.0, velocidad)));
    if (duracion <= 0) return 0;
    return bucle ? m % duracion : std::min(m, duracion - 1);
}

void Medio::componentComplete() {
    QQuickItem::componentComplete();
    cargar();
    // Arranca con el gráfico, como un NumberAnimation con running: true.
    reloj_ = new Reloj(this);
    reloj_->start();
}

void Medio::alAvanzar(int ms) {
    ahora_ = ms;
    elegirCuadro();
}

void Medio::cargar() {
    fuente_.reset();
    error_.clear();
    indice_ = -1;
    imagen_ = {};
    if (!source_.isEmpty()) {
        // En Qt 6 una url de QML no se resuelve sola: «medios/x.gif» es relativo
        // al gráfico (la carpeta que mandó la app), no al directorio de trabajo.
        const QQmlContext* ctx = qmlContext(this);
        const QUrl url = ctx != nullptr ? ctx->resolvedUrl(source_) : source_;
        const QString ruta = url.isLocalFile() ? url.toLocalFile() : url.toString();
        const QFileInfo fi(ruta);
        if (!fi.exists()) {
            error_ = QCoreApplication::translate("graficos", "no está «%1» (se busca en la carpeta del gráfico: medios/…)")
                         .arg(source_.toString());
        } else if (fi.isDir()) {
            fuente_ = Secuencia::abrir(ruta, fps_, error_);
        } else if (extensionesDeVideo().contains(fi.suffix().toLower())) {
            error_ = QCoreApplication::translate("graficos", "«%1» es un video: se importa primero (grafico_medio_agregar, o "
                                                             "«Agregar medio» en Gráficos) y queda como secuencia de cuadros "
                                                             "con su alfa")
                         .arg(fi.fileName());
        } else {
            fuente_ = Animada::abrir(ruta, error_);
        }
    }
    // Un error de un medio se ve en el registro del renderizador (no rompe el gráfico entero).
    if (!error_.isEmpty())
        qWarning("%s", qUtf8Printable(QCoreApplication::translate("graficos", "Medio: %1").arg(error_)));
    if (fuente_) {
        const QImage primero = fuente_->cuadro(0);
        setImplicitSize(primero.width(), primero.height());
    }
    emit cargado();
    elegirCuadro();
    update();
}

void Medio::elegirCuadro() {
    const int t = fuente_ ? tiempoDelMedio(ahora_, inicio_, velocidad_, fuente_->duracion(), bucle_) : -1;
    if (t != tiempo_) {
        tiempo_ = t;
        emit tiempoChanged();
    }
    const int i = t < 0 ? -1 : fuente_->indiceEn(t);
    if (i == indice_) return;
    indice_ = i;
    imagen_ = i < 0 ? QImage() : fuente_->cuadro(i);
    cambio_ = true;
    update();
}

QSGNode* Medio::updatePaintNode(QSGNode* viejo, UpdatePaintNodeData*) {
    auto* n = static_cast<QSGImageNode*>(viejo);
    if (imagen_.isNull() || width() <= 0 || height() <= 0) {
        delete n;
        return nullptr;
    }
    if (n == nullptr) {
        n = window()->createImageNode();
        n->setOwnsTexture(true);
        n->setFiltering(QSGTexture::Linear);
        cambio_ = true;
    }
    if (cambio_) {
        n->setTexture(window()->createTextureFromImage(imagen_));
        cambio_ = false;
    }
    const QSizeF img = imagen_.size();
    const QSizeF caja(width(), height());
    if (ajuste_ == Estirar) {
        n->setSourceRect(QRectF(QPointF(0, 0), img));
        n->setRect(QRectF(QPointF(0, 0), caja));
    } else if (ajuste_ == Llenar) {
        // Llena la caja y recorta lo que sobra, centrado.
        const QSizeF esc = img.scaled(caja, Qt::KeepAspectRatioByExpanding);
        const qreal sx = img.width() * (caja.width() / esc.width());
        const qreal sy = img.height() * (caja.height() / esc.height());
        n->setSourceRect(QRectF((img.width() - sx) / 2, (img.height() - sy) / 2, sx, sy));
        n->setRect(QRectF(QPointF(0, 0), caja));
    } else {
        const QSizeF esc = img.scaled(caja, Qt::KeepAspectRatio);
        n->setSourceRect(QRectF(QPointF(0, 0), img));
        n->setRect(QRectF(QPointF((caja.width() - esc.width()) / 2, (caja.height() - esc.height()) / 2), esc));
    }
    return n;
}

QVariantList DatosQml::normalizar(const QVariant& v) const {
    // Una lista o un objeto escritos en QML llegan como QJSValue, que
    // QJsonValue no entiende (daba null y la torta salía vacía).
    const QVariant x = v.userType() == qMetaTypeId<QJSValue>() ? v.value<QJSValue>().toVariant() : v;
    return normalizarDatos(QJsonValue::fromVariant(x)).toVariantList();
}

void registrarComponentes() {
    qmlRegisterType<Medio>("BA.Grafico", 1, 0, "Medio");
    qmlRegisterSingletonType<DatosQml>("BA.Grafico", 1, 0, "Datos",
                                       [](QQmlEngine*, QJSEngine*) -> QObject* { return new DatosQml(); });
    // Los gráficos de datos son QML (se dibujan con Shape y Text, como
    // cualquier gráfico): viajan adentro del binario, en el recurso :/ba-grafico.
    qmlRegisterType(QUrl("qrc:/ba-grafico/qml/Torta.qml"), "BA.Grafico", 1, 0, "Torta");
    qmlRegisterType(QUrl("qrc:/ba-grafico/qml/Barras.qml"), "BA.Grafico", 1, 0, "Barras");
}

} // namespace ba::grafico
