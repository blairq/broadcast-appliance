// ============================================================================
// src/app/graficos/Emisor.cpp — ver Emisor.hpp
// ============================================================================
#include "graficos/Emisor.hpp"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include <QCoreApplication>
#include <QJsonDocument>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>

#include "graficos/GraficoSource.hpp"
#include "graficos/VideoEnGrafico.hpp"

namespace ba::app::graficos {
namespace {

/// Cuántos cuadros por segundo se pre-renderizan. 30 alcanza para un gráfico:
/// no es video, es un movimiento corto.
constexpr int kFps = 30;
/// De a cuántos se le piden al renderizador. Viajan en base64, así que una
/// tanda muy grande hincha el mensaje sin ganar nada.
constexpr int kPorTanda = 12;
/// La primera, para el aire: con dos cuadros ya hay qué mostrar.
constexpr int kPrimeraTanda = 2;

} // namespace

/// DIBUJAR LO QUE SALE AL AIRE, EN OTRO HILO. Un zócalo de 15 s son 450 cuadros que
/// `ba-grafico` dibuja de a tandas; hecho en el hilo de la interfaz la congelaba hasta
/// terminar (36 s con «Siguiente» en el guion del evento, y el supervisor la mataba).
/// Acá, con SU renderizador (el del editor y las vistas previas sigue en la interfaz):
/// cada tanda vuelve a la interfaz apenas está, y la primera ya sale al aire.
class DibujoAire {
   public:
    struct Pedido {
        std::uint64_t fuente = 0;
        int generacion = 0;
        QJsonObject pedido;
        /// Un cuadro por entrada: el milisegundo a dibujar, o -1 = el mismo de antes
        /// (lo quieto entre la entrada y la salida: no se dibuja ni ocupa memoria).
        std::vector<int> plan;
    };
    /// Una tanda dibujada (o el error), en el hilo del dibujo.
    using Tanda = std::function<void(const Pedido&, std::vector<QImage>, bool fin, QString error)>;

    DibujoAire(QString binario, Tanda tanda) : binario_(std::move(binario)), tanda_(std::move(tanda)) {
        hilo_ = std::thread([this]() { trabajar(); });
    }
    ~DibujoAire() {
        {
            std::lock_guard<std::mutex> l(mu_);
            fin_ = true;
        }
        cv_.notify_all();
        hilo_.join();
    }
    void pedir(Pedido p) {
        {
            std::lock_guard<std::mutex> l(mu_);
            // Lo que estaba esperando para la misma fuente ya no va.
            cola_.erase(std::remove_if(cola_.begin(), cola_.end(), [&](const Pedido& x) { return x.fuente == p.fuente; }),
                        cola_.end());
            vigente_[p.fuente] = p.generacion;
            cola_.push_back(std::move(p));
        }
        cv_.notify_all();
    }

   private:
    bool vigente(const Pedido& p) {
        std::lock_guard<std::mutex> l(mu_);
        return !fin_ && vigente_[p.fuente] == p.generacion;
    }
    void trabajar() {
        // El renderizador se crea en este hilo: su proceso se usa solo desde acá. Se
        // levanta ya (repertorio no dibuja nada): el primer gráfico no espera su arranque.
        GraficoProceso proc(binario_);
        if (proc.disponible()) proc.repertorio();
        for (;;) {
            Pedido p;
            {
                std::unique_lock<std::mutex> l(mu_);
                cv_.wait(l, [this]() { return fin_ || !cola_.empty(); });
                if (fin_) return;
                p = std::move(cola_.front());
                cola_.pop_front();
            }
            const int total = static_cast<int>(p.plan.size());
            QImage ultimo;
            for (int desde = 0; desde < total;) {
                if (!vigente(p)) break; // otro pedido para esa fuente lo reemplazó
                // La primera tanda, chica: el gráfico entra enseguida y el resto llega andando.
                // Una tanda junta hasta `tanda` cuadros a DIBUJAR (los repetidos van gratis).
                const int tanda = desde == 0 ? kPrimeraTanda : kPorTanda;
                const int inicio = desde;
                QVector<int> momentos;
                while (desde < total && momentos.size() < tanda) {
                    if (p.plan[static_cast<std::size_t>(desde)] >= 0) momentos.append(p.plan[static_cast<std::size_t>(desde)]);
                    ++desde;
                }
                // Los repetidos que siguen, en la misma tanda (no esperan a otro dibujo).
                while (desde < total && p.plan[static_cast<std::size_t>(desde)] < 0) ++desde;
                QString err;
                const auto parte = momentos.isEmpty() ? QVector<Cuadro>{} : proc.tira(p.pedido, momentos, &err);
                std::vector<QImage> cuadros;
                int k = 0;
                for (int i = inicio; i < desde && err.isEmpty(); ++i) {
                    if (p.plan[static_cast<std::size_t>(i)] >= 0 && k < parte.size()) ultimo = parte[k++].imagen;
                    if (!ultimo.isNull()) cuadros.push_back(ultimo); // QImage comparte los datos
                }
                tanda_(p, std::move(cuadros), desde >= total || !err.isEmpty(), err);
                if (!err.isEmpty()) break;
            }
        }
    }

    QString binario_;
    Tanda tanda_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Pedido> cola_;
    std::map<std::uint64_t, int> vigente_;
    bool fin_ = false;
    std::thread hilo_;
};

struct Emisor::Impl {
    explicit Impl(const std::filesystem::path& raizDatos) : catalogo(raizDatos), medios(catalogo) {}
    GraficoProceso proc;
    Catalogo catalogo;
    Medios medios;
    ProveedorPlugin proveedor;
    /// Lo último que contestaron los plugins, por (gráfico, valores, tamaño):
    /// la vista previa pide un cuadro por vez y el plugin no tiene por qué
    /// volver a armar todo en cada uno. Se borra cuando un plugin se redeclara.
    std::map<QString, QJsonObject> dePlugins;
    std::unique_ptr<DibujoAire> aire; ///< se crea con el primer gráfico al aire
    std::map<std::uint64_t, int> generacion; ///< por fuente: el último pedido
    std::map<std::uint64_t, bool> empezado;  ///< por fuente: ya salió la primera tanda
    std::map<std::uint64_t, bool> repetir;
    /// Lo último que se disparó en cada fuente, para poder cambiarle un texto
    /// sin tener que volver a escribir todos los valores.
    struct Ultimo {
        QString grafico;
        QJsonObject valores;
    };
    std::map<std::uint64_t, Ultimo> ultimos;
    CapasDeVideo capasVideo;
    /// Por fuente: el video que acompaña al gráfico que se está por ver (o que se ve).
    struct Video {
        std::string nombre; ///< la fuente de escena (para sacarlo aunque ya no esté)
        int generacion = 0; ///< la del pedido que lo puso
        int inicioMs = 0, finMs = 0;
    };
    std::map<std::uint64_t, Video> video;
};

Emisor::Emisor(const std::filesystem::path& raizDatos, QObject* padre)
    : QObject(padre), d_(std::make_unique<Impl>(raizDatos)) {
    // El dibujo para el aire, listo de entrada (su ba-grafico arranca en el fondo).
    if (d_->proc.disponible()) prepararAire();
}

void Emisor::prepararAire() {
    if (d_->aire) return;
    QPointer<Emisor> yo(this);
    d_->aire = std::make_unique<DibujoAire>(
        d_->proc.binario(), [yo](const DibujoAire::Pedido& pd, std::vector<QImage> cuadros, bool fin, QString err) {
            QMetaObject::invokeMethod(
                qApp,
                [yo, pd, cuadros = std::move(cuadros), fin, err]() mutable {
                    if (yo) yo->recibirTanda(pd.fuente, pd.generacion, std::move(cuadros), fin, err);
                },
                Qt::QueuedConnection);
        });
}

Emisor::~Emisor() = default;

bool Emisor::disponible() const { return d_->proc.disponible(); }
GraficoProceso& Emisor::proceso() { return d_->proc; }
Catalogo& Emisor::catalogo() { return d_->catalogo; }
const Catalogo& Emisor::catalogo() const { return d_->catalogo; }
Medios& Emisor::medios() { return d_->medios; }
QStringList Emisor::nombres() const { return d_->catalogo.nombres(); }

void Emisor::setProveedorPlugin(ProveedorPlugin p) { d_->proveedor = std::move(p); }
void Emisor::setCapasDeVideo(CapasDeVideo c) { d_->capasVideo = std::move(c); }

std::string Emisor::nombreDe(std::uint64_t fuente) {
    for (const auto& i : instancias())
        if (i.id == fuente) return i.nombre;
    return {};
}

void Emisor::prepararVideo(std::uint64_t fuente, const QString& grafico, const QJsonObject& pedido, int generacion,
                           int inicioMs, int salidaEn, int salidaDur, int finMs) {
    if (!d_->capasVideo.poner) return;
    QString err;
    const auto capas = video::leer(ficha(grafico), pedido, &err);
    if (!err.isEmpty()) emit fallo(tr("El video del gráfico: %1").arg(err));
    if (capas.empty()) return quitarVideo(fuente); // este gráfico no lleva (el de antes, quizás sí)
    const std::string nombre = nombreDe(fuente);
    // Se ponen YA, quietas en su primera clave (con alfa 0 casi siempre): la rama de la cámara
    // tarda en dar su primer cuadro y el gráfico, en dibujar los suyos; así, cuando sale el
    // primero del gráfico, el video ya está ahí para arrancar.
    const std::string e = d_->capasVideo.poner(nombre, video::aJson(capas, salidaEn, salidaDur, inicioMs));
    if (!e.empty()) {
        d_->video.erase(fuente);
        emit fallo(tr("El video del gráfico no salió: %1").arg(QString::fromStdString(e)));
        return;
    }
    d_->video[fuente] = {nombre, generacion, inicioMs, finMs};
}

void Emisor::quitarVideo(std::uint64_t fuente) {
    const auto it = d_->video.find(fuente);
    if (it == d_->video.end()) return;
    const std::string nombre = it->second.nombre;
    d_->video.erase(it);
    if (d_->capasVideo.quitar) d_->capasVideo.quitar(nombre);
}

QString Emisor::registrarDePlugin(const QString& plugin, const QJsonObject& declaracion) {
    QString nombre;
    const QString err = d_->catalogo.guardarDePlugin(plugin, declaracion, &nombre);
    if (!err.isEmpty()) return err;
    // El plugin pudo haber cambiado su código: lo que contestó antes no vale.
    for (auto it = d_->dePlugins.begin(); it != d_->dePlugins.end();)
        it = it->first.startsWith(nombre + "\n") ? d_->dePlugins.erase(it) : std::next(it);
    emit catalogoCambio();
    return {};
}

QSize Emisor::tamanoDe(std::uint64_t fuente) {
    for (const auto& i : instancias())
        if (i.id == fuente) return {i.ancho, i.alto};
    return {};
}

bool Emisor::enPantalla(std::uint64_t fuente) {
    for (const auto& i : instancias())
        if (i.id == fuente) return i.reproduciendo;
    return false;
}

void Emisor::resolver(const QString& grafico, const QJsonObject& valores, QSize tam,
                      std::function<void(QJsonObject, QString)> listo) {
    const QJsonObject f = ficha(grafico);
    const QString plugin = Catalogo::pluginDe(f);
    if (plugin.isEmpty()) {
        QString err;
        QJsonObject p = d_->catalogo.pedido(grafico, valores, &err);
        return listo(err.isEmpty() ? p : QJsonObject{}, err);
    }
    if (!d_->proveedor) return listo({}, tr("los plugins no están disponibles"));
    if (!tam.isValid()) tam = QSize(f.value("ancho").toInt(900), f.value("alto").toInt(220));
    // Lo que no venga, del porDefecto de la ficha (igual que un gráfico normal).
    QJsonObject v = Catalogo::valoresPorDefecto(f);
    for (auto it = valores.begin(); it != valores.end(); ++it) v[it.key()] = it.value();
    const QString clave = grafico + "\n" + QString::fromUtf8(QJsonDocument(v).toJson(QJsonDocument::Compact)) + "\n" +
                          QString::number(tam.width()) + "x" + QString::number(tam.height());
    const auto armar = [f, v, tam](const QJsonObject& r, QString* err) -> QJsonObject {
        // Lo que puede contestar un plugin, y cómo sigue cada cosa por el camino de siempre.
        if (r.contains("declarativo")) return QJsonObject{{"declarativo", r.value("declarativo")}, {"parametros", v}};
        // Un QML de un plugin ya trae los valores adentro: no se le pasan (no los declara).
        if (r.contains("qml"))
            return QJsonObject{{"fuente", r.value("qml")}, {"parametros", QJsonObject{}}, {"ancho", tam.width()}, {"alto", tam.height()}};
        if (r.contains("secuencia")) {
            // Los cuadros que dibujó: un Medio que los muestra con el reloj del gráfico.
            const QString url = QUrl::fromLocalFile(r.value("secuencia").toString()).toString();
            const QString qml = QString("import QtQuick\nimport BA.Grafico\nItem {\n    Medio {\n        anchors.fill: parent\n"
                                        "        source: \"%1\"\n        bucle: %2\n        ajuste: Medio.Estirar\n    }\n}\n")
                                    .arg(url, f.value("bucle").toBool() ? "true" : "false");
            return QJsonObject{{"fuente", qml}, {"parametros", QJsonObject{}}, {"ancho", tam.width()}, {"alto", tam.height()}};
        }
        *err = QCoreApplication::translate("graficos", "el plugin no devolvió ni «declarativo», ni «qml», ni «secuencia»");
        return {};
    };
    if (const auto it = d_->dePlugins.find(clave); it != d_->dePlugins.end()) {
        QString err;
        const QJsonObject p = armar(it->second, &err);
        return listo(p, err);
    }
    const QJsonObject args{{"nombre", f.value("graficoPlugin").toString()},
                           {"valores", v},
                           {"ancho", tam.width()},
                           {"alto", tam.height()},
                           {"fps", kFps},
                           {"duracion_ms", f.value("duracionMs").toInt(2000)}};
    QPointer<Emisor> yo(this);
    d_->proveedor(plugin, args, [yo, clave, armar, listo, plugin](QJsonObject r, QString e) {
        if (!e.isEmpty()) return listo({}, tr("el plugin «%1»: %2").arg(plugin, e));
        QString err;
        const QJsonObject p = armar(r, &err);
        if (yo && err.isEmpty()) {
            if (yo->d_->dePlugins.size() > 32) yo->d_->dePlugins.clear();
            yo->d_->dePlugins[clave] = r;
        }
        listo(p, err);
    });
}

QString Emisor::conPedido(const QString& grafico, const QJsonObject& valores, QSize tam,
                          std::function<QString(QJsonObject)> usar) {
    struct Estado {
        bool esperando = true; ///< todavía adentro de resolver()
        bool listo = false;
        QJsonObject pedido;
        QString error;
    };
    auto st = std::make_shared<Estado>();
    QPointer<Emisor> yo(this);
    resolver(grafico, valores, tam, [st, yo, usar](QJsonObject p, QString e) {
        if (st->esperando) { // contestó en el momento: sigue quien llamó
            st->listo = true;
            st->pedido = std::move(p);
            st->error = std::move(e);
            return;
        }
        if (!yo) return;
        if (e.isEmpty()) e = usar(std::move(p));
        if (!e.isEmpty()) emit yo->fallo(tr("El gráfico no salió: %1").arg(e));
    });
    st->esperando = false;
    if (!st->listo) return {}; // lo va a contestar un plugin
    if (!st->error.isEmpty()) return st->error;
    return usar(std::move(st->pedido));
}
QJsonObject Emisor::ficha(const QString& grafico) const { return d_->catalogo.ficha(grafico); }

std::uint64_t Emisor::fuentePorNombre(const QString& nombre) const {
    for (const auto& i : instancias())
        if (QString::fromStdString(i.nombre) == nombre) return i.id;
    return 0;
}

QString Emisor::mostrar(const QString& grafico, const QJsonObject& valores, std::uint64_t fuente,
                        int duracionMs, bool repetir) {
    if (duracionMs <= 0) duracionMs = ficha(grafico).value("duracionMs").toInt(2000);
    const QSize tam = tamanoDe(fuente);
    if (!tam.isValid()) return tr("esa fuente de gráfico ya no está en ninguna escena");
    return conPedido(grafico, valores, tam, [this, grafico, valores, fuente, duracionMs, repetir](QJsonObject p) {
        return reproducir(std::move(p), grafico, valores, fuente, duracionMs, repetir);
    });
}

QString Emisor::mostrarOCambiar(const QString& grafico, const QJsonObject& valores, std::uint64_t fuente,
                                bool repetir) {
    if (Catalogo::sabeCambiar(ficha(grafico)) && ultimoGrafico(fuente) == grafico && enPantalla(fuente))
        return actualizar(grafico, valores, fuente);
    return mostrar(grafico, valores, fuente, 0, repetir);
}

QString Emisor::reproducir(QJsonObject p, const QString& grafico, const QJsonObject& valores,
                           std::uint64_t fuente, int duracionMs, bool repetir) {
    if (!d_->proc.disponible()) return tr("el renderizador de gráficos no está en este bundle");

    // El tamaño lo manda la fuente de escena, no la ficha: es el tamaño al que
    // se va a ver.
    bool existe = false;
    for (const auto& i : instancias())
        if (i.id == fuente) {
            p["ancho"] = i.ancho;
            p["alto"] = i.alto;
            existe = true;
        }
    if (!existe) return tr("esa fuente de gráfico ya no está en ninguna escena");

    // EL PLAN DE CUADROS. Con «cuándo se va» (salidaEn, un parámetro del gráfico): la
    // entrada cuadro a cuadro, el cuadro quieto repetido hasta salidaEn y la salida
    // entera. Antes se dibujaban solo `duracionMs` (2 s si la ficha no dice): una placa
    // con salidaEn 4500 nunca llegaba a irse y quedaba congelada en pantalla. Sin
    // salidaEn (o 0: se queda hasta que la saquen), `duracionMs` como siempre.
    std::vector<int> plan;
    const int salidaEn = p.value("parametros").toObject().value("salidaEn").toInt(0);
    if (salidaEn > 0) {
        const int salidaDur = std::clamp(p.value("declarativo").toObject().value("salida").toObject().value("duracion").toInt(800), 0, 5000);
        const int salidaHasta = std::min(salidaEn, 120000) + salidaDur + 150;
        // Lo que se mueve al entrar: hasta 3 s (o hasta que se va, si es antes).
        const int entradaHasta = std::min(salidaEn, std::min(kMaxSegundos * 1000, 3000));
        const int cuadros = salidaHasta * kFps / 1000 + 1;
        for (int i = 0; i < cuadros; ++i) {
            const int ms = i * 1000 / kFps;
            plan.push_back(ms <= entradaHasta || ms >= salidaEn ? ms : -1);
        }
    } else {
        duracionMs = std::clamp(duracionMs, 100, kMaxSegundos * 1000);
        const int total = std::max(1, duracionMs * kFps / 1000);
        for (int i = 0; i < total; ++i) plan.push_back(i * 1000 / kFps);
    }
    // Se dibuja en otro hilo (DibujoAire): sale al aire con la primera tanda y el resto
    // se suma mientras se reproduce. Lo que falle después se avisa con `fallo`.
    prepararAire();
    const int gen = ++d_->generacion[fuente];
    d_->empezado[fuente] = false;
    d_->repetir[fuente] = repetir;
    // El video en vivo que lo acompaña (si declara): entra, queda y se va con él.
    {
        const int salidaDur = std::clamp(p.value("declarativo").toObject().value("salida").toObject().value("duracion").toInt(800), 0, 5000);
        const int finMs = salidaEn > 0 && !repetir ? static_cast<int>(plan.size()) * 1000 / kFps : 0;
        prepararVideo(fuente, grafico, p, gen, 0, salidaEn, salidaDur, finMs);
    }
    d_->aire->pedir({fuente, gen, std::move(p), std::move(plan)});
    d_->ultimos[fuente] = {grafico, valores};
    return {};
}

void Emisor::recibirTanda(std::uint64_t fuente, int generacion, std::vector<QImage> cuadros, bool fin, const QString& error) {
    if (d_->generacion[fuente] != generacion) return; // ya hay otro gráfico para esa fuente
    if (!error.isEmpty()) {
        if (d_->empezado[fuente]) graficos::agregar(fuente, {}, true); // queda lo que llegó
        quitarVideo(fuente);
        emit fallo(tr("El gráfico no se pudo dibujar: %1").arg(error));
        return;
    }
    if (!d_->empezado[fuente]) {
        if (cuadros.empty()) return;
        // Con video adentro: su animación arranca con el primer cuadro que sale (el dibujo
        // tarda lo que tarda; el reloj del gráfico empieza recién ahí).
        std::function<void(std::int64_t)> alEmpezar;
        if (const auto v = d_->video.find(fuente); v != d_->video.end() && v->second.generacion == generacion) {
            QPointer<Emisor> yo(this);
            alEmpezar = [yo, fuente, generacion](std::int64_t ns) {
                QMetaObject::invokeMethod(
                    qApp,
                    [yo, fuente, generacion, ns]() {
                        if (!yo) return;
                        const auto it = yo->d_->video.find(fuente);
                        if (it == yo->d_->video.end() || it->second.generacion != generacion) return;
                        if (yo->d_->capasVideo.empezar) yo->d_->capasVideo.empezar(it->second.nombre, ns);
                        // Un gráfico que se va solo: al terminar su salida, el video también.
                        if (const int dura = it->second.finMs - it->second.inicioMs; it->second.finMs > 0)
                            QTimer::singleShot(dura + 300, yo, [yo, fuente, generacion]() {
                                if (!yo) return;
                                const auto v2 = yo->d_->video.find(fuente);
                                if (v2 != yo->d_->video.end() && v2->second.generacion == generacion) yo->quitarVideo(fuente);
                            });
                    },
                    Qt::QueuedConnection);
            };
        }
        if (!graficos::reproducir(fuente, std::move(cuadros), kFps, d_->repetir[fuente], !fin, std::move(alEmpezar))) {
            emit fallo(tr("No se pudo reproducir el gráfico: esa fuente ya no está"));
            return;
        }
        d_->empezado[fuente] = true;
        emit cambio();
        return;
    }
    graficos::agregar(fuente, std::move(cuadros), fin);
}

QString Emisor::actualizar(const QString& grafico, const QJsonObject& valores, std::uint64_t fuente) {
    if (!d_->proc.disponible()) return tr("el renderizador de gráficos no está en este bundle");
    const QSize tam = tamanoDe(fuente);
    if (!tam.isValid()) return tr("esa fuente de gráfico ya no está en ninguna escena");
    const QJsonObject f = ficha(grafico);
    // ¿Se anima el cambio? Solo si sabe y si lo que hay en pantalla es ESTE gráfico:
    // los valores de antes tienen que ser los suyos.
    const bool animar = Catalogo::sabeCambiar(f) && ultimoGrafico(fuente) == grafico && enPantalla(fuente);
    QJsonObject antes = Catalogo::valoresPorDefecto(f);
    if (animar) {
        const QJsonObject u = ultimosValores(fuente);
        for (auto it = u.begin(); it != u.end(); ++it) antes[it.key()] = it.value();
    }
    d_->ultimos[fuente] = {grafico, valores};
    return conPedido(grafico, valores, tam, [this, f, fuente, animar, antes, tam](QJsonObject p) -> QString {
        p["ancho"] = tam.width();
        p["alto"] = tam.height();
        if (animar) {
            // EL CAMBIO, sin volver a entrar: el renderizador lo dibuja sin la entrada y
            // con los valores de antes (src/grafico: Modo), y las tortas y barras se
            // mueven a los nuevos. Queda puesto: un cambio no programa la salida.
            QJsonObject par = p.value("parametros").toObject();
            QJsonObject ant = antes;
            if (par.contains("salidaEn")) par["salidaEn"] = 0;
            if (ant.contains("salidaEn")) ant["salidaEn"] = 0;
            p["parametros"] = par;
            p["anteriores"] = ant;
            p["sinEntrada"] = true;
            std::vector<int> plan;
            const int dur = Catalogo::duracionCambio(f) + 150;
            for (int ms = 0; ms <= dur; ms += 1000 / kFps) plan.push_back(ms);
            prepararAire();
            const int gen = ++d_->generacion[fuente];
            d_->empezado[fuente] = false;
            d_->repetir[fuente] = false;
            d_->aire->pedir({fuente, gen, std::move(p), std::move(plan)});
            return {};
        }
        // El cuadro estable: pasada la animación de entrada (la duración de la ficha,
        // o 2 s), antes de cualquier salida automática.
        QString err;
        const int ms = std::clamp(f.value("duracionMs").toInt(2000) - 1000 / kFps, 0, kMaxSegundos * 1000);
        const auto cuadro = d_->proc.tira(p, QVector<int>{ms}, &err);
        if (!err.isEmpty()) return err;
        if (cuadro.empty()) return tr("el renderizador no devolvió el cuadro");
        std::vector<QImage> uno{cuadro.front().imagen};
        if (!graficos::reproducir(fuente, std::move(uno), kFps, false)) return tr("no se pudo actualizar esa fuente");
        return {};
    });
}

QString Emisor::ocultar(std::uint64_t fuente) {
    quitarVideo(fuente); // el video del gráfico se va con él (aunque la fuente ya no esté)
    if (!graficos::ocultar(fuente)) return tr("esa fuente de gráfico ya no está");
    emit cambio();
    return {};
}

int Emisor::generacion(std::uint64_t fuente) const {
    const auto it = d_->generacion.find(fuente);
    return it == d_->generacion.end() ? 0 : it->second;
}

QString Emisor::sacar(std::uint64_t fuente, int siSigue) {
    if (siSigue >= 0 && generacion(fuente) != siSigue) return {}; // ya hay otra cosa ahí
    const auto it = d_->ultimos.find(fuente);
    if (it == d_->ultimos.end() || !d_->proc.disponible()) return ocultar(fuente);
    const QString grafico = it->second.grafico;
    bool sabeIrse = false;
    for (const QJsonValue& prm : ficha(grafico).value("parametros").toArray())
        sabeIrse = sabeIrse || prm.toObject().value("clave").toString() == "salidaEn";
    if (!sabeIrse) return ocultar(fuente);
    // Ya entrado (a los 3 s la entrada terminó) y la salida empezando ahí mismo: el plan
    // arranca en ese instante, así que el primer cuadro es el gráfico entero y quieto.
    constexpr int kDesde = 3000;
    QJsonObject v = it->second.valores;
    v["salidaEn"] = kDesde;
    QString err;
    QJsonObject p = d_->catalogo.pedido(grafico, v, &err);
    if (!err.isEmpty()) return ocultar(fuente);
    bool existe = false;
    for (const auto& i : instancias())
        if (i.id == fuente) {
            p["ancho"] = i.ancho;
            p["alto"] = i.alto;
            existe = true;
        }
    if (!existe) return tr("esa fuente de gráfico ya no está en ninguna escena");
    const int dur = std::clamp(p.value("declarativo").toObject().value("salida").toObject().value("duracion").toInt(800), 0, 3000) + 150;
    std::vector<int> plan;
    for (int ms = kDesde; ms <= kDesde + dur; ms += 1000 / kFps) plan.push_back(ms);
    prepararAire();
    const int gen = ++d_->generacion[fuente];
    d_->empezado[fuente] = false;
    d_->repetir[fuente] = false;
    // El video, desde el mismo punto (ya entrado) hasta irse; lo saca el ocultar del final.
    if (d_->video.count(fuente) > 0) prepararVideo(fuente, grafico, p, gen, kDesde, kDesde, dur - 150, 0);
    d_->aire->pedir({fuente, gen, std::move(p), std::move(plan)});
    d_->ultimos.erase(fuente);
    // Al terminar la salida, fuera (si nadie puso otra cosa mientras tanto).
    QPointer<Emisor> yo(this);
    QTimer::singleShot(dur + 700, this, [yo, fuente, gen]() {
        if (yo && yo->generacion(fuente) == gen) yo->ocultar(fuente);
    });
    return {};
}

QJsonObject Emisor::ultimosValores(std::uint64_t fuente) const {
    const auto it = d_->ultimos.find(fuente);
    return it == d_->ultimos.end() ? QJsonObject{} : it->second.valores;
}

QString Emisor::ultimoGrafico(std::uint64_t fuente) const {
    const auto it = d_->ultimos.find(fuente);
    return it == d_->ultimos.end() ? QString{} : it->second.grafico;
}

} // namespace ba::app::graficos
