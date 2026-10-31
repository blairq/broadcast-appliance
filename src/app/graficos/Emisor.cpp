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
#include "platform/Hilo.hpp"

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
/// Acá, con SU renderizador (el del editor y las vistas previas sigue en la interfaz).
///
/// Y TAMBIÉN SE ENTREGA ACÁ. Cada tanda va directo a la fuente (GraficoSource: reproducir
/// la primera, agregar las demás), sin pasar por la interfaz: con la interfaz trabada, un
/// gráfico que estaba entrando seguía esperando sus cuadros y quedaba congelado a medio
/// entrar. A la interfaz solo vuelven los avisos (empezó, falló), encolados.
///
/// EL RELOJ DE LOS CAMPOS VIVOS. Un rótulo con la hora con segundos (un vínculo «vivo»
/// con {hora_s}) se redibujaba desde la interfaz una vez por segundo; trabada, el reloj
/// al aire se paraba. Quien lo pone deja acá cómo calcular sus valores AHORA (una función
/// que no lee nada de la interfaz: una foto del contexto y la hora) y este hilo, una vez
/// por segundo, los calcula y, si cambiaron, dibuja el cuadro quieto con ellos.
class DibujoAire {
   public:
    struct Pedido {
        std::uint64_t fuente = 0;
        int generacion = 0; ///< la del Emisor (para los avisos a la interfaz)
        QJsonObject pedido;
        /// Un cuadro por entrada: el milisegundo a dibujar, o -1 = el mismo de antes
        /// (lo quieto entre la entrada y la salida: no se dibuja ni ocupa memoria).
        std::vector<int> plan;
        bool repetir = false;
        /// Avisar a la interfaz cuando salga (un gráfico que entra; no un campo que cambia).
        bool avisar = true;
        /// Con el primer cuadro ya en el motor (ver graficos::reproducir).
        std::function<void(std::int64_t)> alEmpezar;
        std::uint64_t turno = 0; ///< lo pone pedir(): el más nuevo de su fuente manda
    };
    /// Los avisos a la interfaz (se llaman desde el hilo del dibujo).
    struct Avisos {
        std::function<void(std::uint64_t fuente, int generacion)> empezo;
        std::function<void(std::uint64_t fuente, int generacion, QString error)> fallo;
    };
    /// Los valores de AHORA de un campo vivo. Corre en el hilo del dibujo: no lee nada de
    /// la interfaz. Vacío = no se pudo (se deja lo que está).
    using ValoresAhora = std::function<QJsonObject()>;

    DibujoAire(QString binario, Avisos avisos) : binario_(std::move(binario)), avisos_(std::move(avisos)) {
        hilo_ = std::thread([this]() { ba::platform::nombrarHilo("dibujo-aire"); trabajar(); });
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
            relojes_.erase(p.fuente); // lo nuevo manda: el reloj de antes ya no
            encolar(std::move(p));
        }
        cv_.notify_all();
    }
    /// Lo que estaba por salir en esa fuente ya no va (se oculta). Vuelve con la entrega
    /// que estuviera en curso terminada: quien llama oculta después y nada lo pisa.
    void cancelar(std::uint64_t fuente) {
        {
            std::lock_guard<std::mutex> l(mu_);
            cola_.erase(std::remove_if(cola_.begin(), cola_.end(), [&](const Pedido& x) { return x.fuente == fuente; }),
                        cola_.end());
            vigente_[fuente] = 0; // ningún turno es 0
            relojes_.erase(fuente);
        }
        std::lock_guard<std::mutex> e(entregaM_);
    }
    /// El campo vivo de esa fuente: desde ahora, una vez por segundo, `valores` y, si
    /// cambiaron desde `actuales`, el cuadro `ms` de `base` con ellos.
    void conReloj(std::uint64_t fuente, int generacion, QJsonObject base, int ms, QJsonObject actuales,
                  ValoresAhora valores) {
        {
            std::lock_guard<std::mutex> l(mu_);
            relojes_[fuente] = {generacion, std::move(base), ms, std::move(actuales), std::move(valores)};
        }
        cv_.notify_all();
    }
    bool conReloj(std::uint64_t fuente) {
        std::lock_guard<std::mutex> l(mu_);
        return relojes_.count(fuente) > 0;
    }

   private:
    struct Reloj {
        int generacion = 0;
        QJsonObject base;
        int ms = 0;
        QJsonObject ultimos;
        ValoresAhora valores;
    };
    /// Con mu_ tomado.
    void encolar(Pedido p) {
        // Lo que estaba esperando para la misma fuente ya no va.
        cola_.erase(std::remove_if(cola_.begin(), cola_.end(), [&](const Pedido& x) { return x.fuente == p.fuente; }),
                    cola_.end());
        p.turno = ++turnos_;
        vigente_[p.fuente] = p.turno;
        cola_.push_back(std::move(p));
    }
    bool vigente(const Pedido& p) {
        std::lock_guard<std::mutex> l(mu_);
        return !fin_ && vigente_[p.fuente] == p.turno;
    }
    /// Una vuelta del reloj: los campos vivos que cambiaron, a la cola.
    void tictac() {
        std::map<std::uint64_t, Reloj> relojes;
        {
            std::lock_guard<std::mutex> l(mu_);
            relojes = relojes_;
        }
        for (auto& [fuente, r] : relojes) {
            const QJsonObject v = r.valores ? r.valores() : QJsonObject{};
            if (v.isEmpty() || v == r.ultimos) continue;
            QJsonObject p = r.base;
            QJsonObject par = p.value("parametros").toObject();
            for (auto it = v.begin(); it != v.end(); ++it) par[it.key()] = it.value();
            p["parametros"] = par;
            std::lock_guard<std::mutex> l(mu_);
            const auto it = relojes_.find(fuente);
            // Mientras se calculaba, otro pedido lo sacó (o lo puso de nuevo): ese manda.
            if (it == relojes_.end() || it->second.ultimos != r.ultimos) continue;
            it->second.ultimos = v;
            Pedido pd;
            pd.fuente = fuente;
            pd.generacion = r.generacion;
            pd.pedido = std::move(p);
            pd.plan = {r.ms};
            pd.avisar = false;
            encolar(std::move(pd));
        }
    }
    /// Una tanda a la fuente: la primera arranca la reproducción, las demás se suman.
    /// Bajo entregaM_ y solo si sigue vigente (cancelar espera a que termine).
    void entregar(const Pedido& p, std::vector<QImage> cuadros, bool fin, const QString& err, bool& empezado) {
        std::lock_guard<std::mutex> e(entregaM_);
        if (!vigente(p)) return;
        if (!err.isEmpty()) {
            if (empezado) graficos::agregar(p.fuente, {}, true); // queda lo que llegó
            if (avisos_.fallo) avisos_.fallo(p.fuente, p.generacion, err);
            return;
        }
        if (empezado) {
            graficos::agregar(p.fuente, std::move(cuadros), fin);
            return;
        }
        if (cuadros.empty()) return;
        if (!graficos::reproducir(p.fuente, std::move(cuadros), kFps, p.repetir, !fin, p.alEmpezar)) {
            if (avisos_.fallo)
                avisos_.fallo(p.fuente, p.generacion, QCoreApplication::translate("graficos", "esa fuente ya no está"));
            return;
        }
        empezado = true;
        if (p.avisar && avisos_.empezo) avisos_.empezo(p.fuente, p.generacion);
    }
    void trabajar() {
        // El renderizador se crea en este hilo: su proceso se usa solo desde acá. Se
        // levanta ya (repertorio no dibuja nada): el primer gráfico no espera su arranque.
        GraficoProceso proc(binario_);
        if (proc.disponible()) proc.repertorio();
        using Hora = std::chrono::steady_clock;
        auto tic = Hora::now() + std::chrono::seconds(1);
        for (;;) {
            Pedido p;
            bool vuelta = false;
            {
                std::unique_lock<std::mutex> l(mu_);
                // Con campos vivos, se despierta también a la vuelta del reloj.
                if (relojes_.empty())
                    cv_.wait(l, [this]() { return fin_ || !cola_.empty() || !relojes_.empty(); });
                else
                    cv_.wait_until(l, tic, [this]() { return fin_ || !cola_.empty(); });
                if (fin_) return;
                // Un dibujo largo (o una cola que no se vacía) no saltea el reloj.
                vuelta = !relojes_.empty() && Hora::now() >= tic;
                if (!vuelta && !cola_.empty()) {
                    p = std::move(cola_.front());
                    cola_.pop_front();
                }
            }
            if (vuelta) {
                tic = Hora::now() + std::chrono::seconds(1);
                tictac();
                continue;
            }
            if (p.plan.empty()) continue; // despertó un reloj nuevo: espera su vuelta
            const int total = static_cast<int>(p.plan.size());
            QImage ultimo;
            bool empezado = false;
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
                if (err.isEmpty() && !momentos.isEmpty() && parte.isEmpty())
                    err = QCoreApplication::translate("graficos", "el renderizador no devolvió el cuadro");
                std::vector<QImage> cuadros;
                int k = 0;
                for (int i = inicio; i < desde && err.isEmpty(); ++i) {
                    if (p.plan[static_cast<std::size_t>(i)] >= 0 && k < parte.size()) ultimo = parte[k++].imagen;
                    if (!ultimo.isNull()) cuadros.push_back(ultimo); // QImage comparte los datos
                }
                entregar(p, std::move(cuadros), desde >= total || !err.isEmpty(), err, empezado);
                if (!err.isEmpty()) break;
            }
        }
    }

    QString binario_;
    Avisos avisos_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Pedido> cola_;
    std::map<std::uint64_t, std::uint64_t> vigente_; ///< por fuente: el turno que manda
    std::map<std::uint64_t, Reloj> relojes_;
    std::uint64_t turnos_ = 0;
    bool fin_ = false;
    /// Lo toma la entrega a la fuente (y cancelar, para esperarla).
    std::mutex entregaM_;
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
    // Los cuadros ya fueron a la fuente desde el hilo del dibujo; a la interfaz, el aviso.
    DibujoAire::Avisos avisos;
    avisos.empezo = [yo](std::uint64_t fuente, int generacion) {
        QMetaObject::invokeMethod(
            qApp, [yo, fuente, generacion]() { if (yo) yo->salioAlAire(fuente, generacion); }, Qt::QueuedConnection);
    };
    avisos.fallo = [yo](std::uint64_t fuente, int generacion, QString error) {
        QMetaObject::invokeMethod(
            qApp, [yo, fuente, generacion, error]() { if (yo) yo->fallaAlAire(fuente, generacion, error); },
            Qt::QueuedConnection);
    };
    d_->aire = std::make_unique<DibujoAire>(d_->proc.binario(), std::move(avisos));
}

void Emisor::encargar(std::uint64_t fuente, int generacion, QJsonObject pedido, std::vector<int> plan, bool repetir,
                      bool avisar) {
    prepararAire();
    DibujoAire::Pedido pd;
    pd.fuente = fuente;
    pd.generacion = generacion;
    pd.pedido = std::move(pedido);
    pd.plan = std::move(plan);
    pd.repetir = repetir;
    pd.avisar = avisar;
    // Con video adentro: su animación arranca con el primer cuadro que sale (el dibujo
    // tarda lo que tarda; el reloj del gráfico empieza recién ahí). Se llama desde el hilo
    // que entrega los cuadros: a la interfaz, encolado. Un campo que cambia (sin `avisar`)
    // no vuelve a arrancar el video.
    if (const auto v = d_->video.find(fuente); avisar && v != d_->video.end() && v->second.generacion == generacion) {
        QPointer<Emisor> yo(this);
        pd.alEmpezar = [yo, fuente, generacion](std::int64_t ns) {
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
    d_->aire->pedir(std::move(pd));
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
    const int gen = ++d_->generacion[fuente];
    // El video en vivo que lo acompaña (si declara): entra, queda y se va con él.
    {
        const int salidaDur = std::clamp(p.value("declarativo").toObject().value("salida").toObject().value("duracion").toInt(800), 0, 5000);
        const int finMs = salidaEn > 0 && !repetir ? static_cast<int>(plan.size()) * 1000 / kFps : 0;
        prepararVideo(fuente, grafico, p, gen, 0, salidaEn, salidaDur, finMs);
    }
    encargar(fuente, gen, std::move(p), std::move(plan), repetir, true);
    d_->ultimos[fuente] = {grafico, valores};
    return {};
}

void Emisor::salioAlAire(std::uint64_t fuente, int generacion) {
    if (this->generacion(fuente) == generacion) emit cambio();
}

void Emisor::fallaAlAire(std::uint64_t fuente, int generacion, const QString& error) {
    if (this->generacion(fuente) != generacion) return; // ya hay otro gráfico para esa fuente
    quitarVideo(fuente);
    emit fallo(tr("El gráfico no se pudo dibujar: %1").arg(error));
}

QString Emisor::seguirReloj(const QString& grafico, std::uint64_t fuente, const QJsonObject& actuales,
                            std::function<QJsonObject()> ahora) {
    if (!d_->proc.disponible()) return tr("el renderizador de gráficos no está en este bundle");
    const QSize tam = tamanoDe(fuente);
    if (!tam.isValid()) return tr("esa fuente de gráfico ya no está en ninguna escena");
    // El pedido sin valores (los de fábrica de la ficha): en cada vuelta se le ponen encima
    // los de `ahora`, como hace Catalogo::pedido.
    QString err;
    QJsonObject base = d_->catalogo.pedido(grafico, {}, &err);
    if (!err.isEmpty()) return err;
    base["ancho"] = tam.width();
    base["alto"] = tam.height();
    prepararAire();
    d_->aire->conReloj(fuente, generacion(fuente), std::move(base), msEstable(ficha(grafico)), actuales, std::move(ahora));
    return {};
}

bool Emisor::siguiendoReloj(std::uint64_t fuente) const { return d_->aire && d_->aire->conReloj(fuente); }

int Emisor::msEstable(const QJsonObject& ficha) {
    // Pasada la animación de entrada (la duración de la ficha, o 2 s), antes de
    // cualquier salida automática.
    return std::clamp(ficha.value("duracionMs").toInt(2000) - 1000 / kFps, 0, kMaxSegundos * 1000);
}

void Emisor::dibujarQuieto(const QString& grafico, const QJsonObject& valores, const QString& fuente,
                           std::function<void(QImage, QString)> listo) {
    if (!d_->proc.disponible()) return listo({}, tr("el renderizador de gráficos no está en este bundle"));
    const QJsonObject f = ficha(grafico);
    QSize tam = tamanoDe(fuentePorNombre(fuente));
    if (!tam.isValid()) tam = QSize(f.value("ancho").toInt(900), f.value("alto").toInt(220));
    // Por resolver y no por conPedido: un plugin contesta DESPUÉS y su error no es un
    // «el gráfico no salió» del aire (esto no sale).
    QPointer<Emisor> yo(this);
    resolver(grafico, valores, tam, [yo, f, tam, listo](QJsonObject p, QString e) {
        if (!yo) return;
        if (!e.isEmpty()) return listo({}, e);
        p["ancho"] = tam.width();
        p["alto"] = tam.height();
        QString err;
        const auto cuadro = yo->d_->proc.tira(p, QVector<int>{msEstable(f)}, &err);
        if (err.isEmpty() && cuadro.empty()) err = tr("el renderizador no devolvió el cuadro");
        listo(err.isEmpty() ? cuadro.front().imagen : QImage(), err);
    });
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
            encargar(fuente, ++d_->generacion[fuente], std::move(p), std::move(plan), false, true);
            return {};
        }
        // El cuadro quieto con los valores nuevos, también en el hilo del dibujo (antes se
        // esperaba al renderizador acá, en la interfaz, una vez por segundo con un reloj).
        // Es el mismo gráfico: la generación no cambia (el guion lo sigue pudiendo sacar).
        encargar(fuente, generacion(fuente), std::move(p), {msEstable(f)}, false, false);
        return {};
    });
}

QString Emisor::ocultar(std::uint64_t fuente) {
    quitarVideo(fuente); // el video del gráfico se va con él (aunque la fuente ya no esté)
    // Lo que se estaba dibujando para esa fuente (o su reloj) ya no sale: si no, la
    // próxima tanda lo volvería a poner.
    if (d_->aire) d_->aire->cancelar(fuente);
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
    const int gen = ++d_->generacion[fuente];
    // El video, desde el mismo punto (ya entrado) hasta irse; lo saca el ocultar del final.
    if (d_->video.count(fuente) > 0) prepararVideo(fuente, grafico, p, gen, kDesde, kDesde, dur - 150, 0);
    encargar(fuente, gen, std::move(p), std::move(plan), false, true);
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
