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
#include <QMetaObject>
#include <QPointer>

#include "graficos/GraficoSource.hpp"

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
    explicit Impl(const std::filesystem::path& raizDatos) : catalogo(raizDatos) {}
    GraficoProceso proc;
    Catalogo catalogo;
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
QStringList Emisor::nombres() const { return d_->catalogo.nombres(); }
QJsonObject Emisor::ficha(const QString& grafico) const { return d_->catalogo.ficha(grafico); }

std::uint64_t Emisor::fuentePorNombre(const QString& nombre) const {
    for (const auto& i : instancias())
        if (QString::fromStdString(i.nombre) == nombre) return i.id;
    return 0;
}

QString Emisor::mostrar(const QString& grafico, const QJsonObject& valores, std::uint64_t fuente,
                        int duracionMs, bool repetir) {
    QString err;
    QJsonObject p = d_->catalogo.pedido(grafico, valores, &err);
    if (!err.isEmpty()) return err;
    if (duracionMs <= 0) duracionMs = ficha(grafico).value("duracionMs").toInt(2000);
    return reproducir(std::move(p), grafico, valores, fuente, duracionMs, repetir);
}

QString Emisor::reproducir(QJsonObject p, const QString& grafico, const QJsonObject& valores,
                           std::uint64_t fuente, int duracionMs, bool repetir) {
    if (!d_->proc.disponible()) return "el renderizador de gráficos no está en este bundle";

    // El tamaño lo manda la fuente de escena, no la ficha: es el tamaño al que
    // se va a ver.
    bool existe = false;
    for (const auto& i : instancias())
        if (i.id == fuente) {
            p["ancho"] = i.ancho;
            p["alto"] = i.alto;
            existe = true;
        }
    if (!existe) return "esa fuente de gráfico ya no está en ninguna escena";

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
    d_->aire->pedir({fuente, gen, std::move(p), std::move(plan)});
    d_->ultimos[fuente] = {grafico, valores};
    return {};
}

void Emisor::recibirTanda(std::uint64_t fuente, int generacion, std::vector<QImage> cuadros, bool fin, const QString& error) {
    if (d_->generacion[fuente] != generacion) return; // ya hay otro gráfico para esa fuente
    if (!error.isEmpty()) {
        if (d_->empezado[fuente]) graficos::agregar(fuente, {}, true); // queda lo que llegó
        emit fallo("El gráfico no se pudo dibujar: " + error);
        return;
    }
    if (!d_->empezado[fuente]) {
        if (cuadros.empty()) return;
        if (!graficos::reproducir(fuente, std::move(cuadros), kFps, d_->repetir[fuente], !fin)) {
            emit fallo("No se pudo reproducir el gráfico: esa fuente ya no está");
            return;
        }
        d_->empezado[fuente] = true;
        emit cambio();
        return;
    }
    graficos::agregar(fuente, std::move(cuadros), fin);
}

QString Emisor::actualizar(const QString& grafico, const QJsonObject& valores, std::uint64_t fuente) {
    if (!d_->proc.disponible()) return "el renderizador de gráficos no está en este bundle";
    QString err;
    QJsonObject p = d_->catalogo.pedido(grafico, valores, &err);
    if (!err.isEmpty()) return err;
    bool existe = false;
    for (const auto& i : instancias())
        if (i.id == fuente) {
            p["ancho"] = i.ancho;
            p["alto"] = i.alto;
            existe = true;
        }
    if (!existe) return "esa fuente de gráfico ya no está en ninguna escena";
    // El cuadro estable: pasada la animación de entrada (la duración de la ficha,
    // o 2 s), antes de cualquier salida automática.
    const int ms = std::clamp(ficha(grafico).value("duracionMs").toInt(2000) - 1000 / kFps, 0, kMaxSegundos * 1000);
    const auto cuadro = d_->proc.tira(p, QVector<int>{ms}, &err);
    if (!err.isEmpty()) return err;
    if (cuadro.empty()) return "el renderizador no devolvió el cuadro";
    std::vector<QImage> uno{cuadro.front().imagen};
    if (!graficos::reproducir(fuente, std::move(uno), kFps, false)) return "no se pudo actualizar esa fuente";
    d_->ultimos[fuente] = {grafico, valores};
    return {};
}

QString Emisor::ocultar(std::uint64_t fuente) {
    if (!graficos::ocultar(fuente)) return "esa fuente de gráfico ya no está";
    emit cambio();
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
