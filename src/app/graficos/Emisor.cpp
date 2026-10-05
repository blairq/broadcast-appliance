// ============================================================================
// src/app/graficos/Emisor.cpp — ver Emisor.hpp
// ============================================================================
#include "graficos/Emisor.hpp"

#include <algorithm>
#include <map>
#include <vector>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

#include "graficos/GraficoSource.hpp"

namespace ba::app::graficos {
namespace {

QJsonObject leerJson(const QString& ruta) {
    QFile f(ruta);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

/// Cuántos cuadros por segundo se pre-renderizan. 30 alcanza para un gráfico:
/// no es video, es un movimiento corto.
constexpr int kFps = 30;
/// De a cuántos se le piden al renderizador. Viajan en base64, así que una
/// tanda muy grande hincha el mensaje sin ganar nada.
constexpr int kPorTanda = 12;

} // namespace

struct Emisor::Impl {
    GraficoProceso proc;
    QString raiz;
    /// Lo último que se disparó en cada fuente, para poder cambiarle un texto
    /// sin tener que volver a escribir todos los valores.
    struct Ultimo {
        QString grafico;
        QJsonObject valores;
    };
    std::map<std::uint64_t, Ultimo> ultimos;
};

Emisor::Emisor(const std::filesystem::path& raizDatos, QObject* padre)
    : QObject(padre), d_(std::make_unique<Impl>()) {
    d_->raiz = QString::fromStdString((raizDatos / "Graficos").string());
    QDir().mkpath(d_->raiz);
}

Emisor::~Emisor() = default;

bool Emisor::disponible() const { return d_->proc.disponible(); }
GraficoProceso& Emisor::proceso() { return d_->proc; }
QString Emisor::raiz() const { return d_->raiz; }
QString Emisor::carpeta(const QString& grafico) const { return QDir(d_->raiz).filePath(grafico); }

QStringList Emisor::nombres() const {
    QStringList out;
    for (const QString& n : QDir(d_->raiz).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
        if (QFile::exists(QDir(carpeta(n)).filePath("grafico.json"))) out << n;
    return out;
}

QJsonObject Emisor::ficha(const QString& grafico) const {
    if (grafico.isEmpty()) return {};
    return leerJson(QDir(carpeta(grafico)).filePath("grafico.json"));
}

QJsonObject Emisor::valoresPorDefecto(const QString& grafico) const {
    QJsonObject out;
    for (const QJsonValue& v : ficha(grafico).value("parametros").toArray()) {
        const QJsonObject p = v.toObject();
        const QString clave = p.value("clave").toString();
        if (!clave.isEmpty()) out[clave] = p.value("porDefecto");
    }
    return out;
}

QJsonObject Emisor::pedido(const QString& grafico, const QJsonObject& valores, QString* error) const {
    const auto falla = [error](const QString& m) {
        if (error != nullptr) *error = m;
        return QJsonObject{};
    };
    const QJsonObject f = ficha(grafico);
    if (f.isEmpty()) return falla("no existe el gráfico «" + grafico + "»");

    // Los valores que no vengan se completan con los de la ficha: el guion
    // suele traer solo el nombre del orador, no los cinco parámetros.
    QJsonObject v = valoresPorDefecto(grafico);
    for (auto it = valores.begin(); it != valores.end(); ++it)
        v[it.key()] = it.value();

    QJsonObject p{{"parametros", v}};
    if (f.contains("declarativo")) {
        p["declarativo"] = f.value("declarativo").toObject();
    } else {
        QFile qml(QDir(carpeta(grafico)).filePath("grafico.qml"));
        if (!qml.open(QIODevice::ReadOnly))
            return falla("el gráfico «" + grafico + "» no tiene ni descripción ni grafico.qml");
        p["fuente"] = QString::fromUtf8(qml.readAll());
    }
    p["ancho"] = f.value("ancho").toInt(900);
    p["alto"] = f.value("alto").toInt(220);
    if (error != nullptr) error->clear();
    return p;
}

std::uint64_t Emisor::fuentePorNombre(const QString& nombre) const {
    for (const auto& i : instancias())
        if (QString::fromStdString(i.nombre) == nombre) return i.id;
    return 0;
}

QString Emisor::mostrar(const QString& grafico, const QJsonObject& valores, std::uint64_t fuente,
                        int duracionMs, bool repetir) {
    QString err;
    QJsonObject p = pedido(grafico, valores, &err);
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

    duracionMs = std::clamp(duracionMs, 100, kMaxSegundos * 1000);
    const int total = std::max(1, duracionMs * kFps / 1000);
    std::vector<QImage> cuadros;
    cuadros.reserve(static_cast<std::size_t>(total));
    for (int desde = 0; desde < total;) {
        QVector<int> momentos;
        for (int k = 0; k < kPorTanda && desde < total; ++k, ++desde)
            momentos.append(desde * 1000 / kFps);
        QString err;
        const auto parte = d_->proc.tira(p, momentos, &err);
        if (!err.isEmpty()) return err;
        for (const auto& c : parte)
            cuadros.push_back(c.imagen);
    }
    if (!graficos::reproducir(fuente, std::move(cuadros), kFps, repetir))
        return "no se pudo reproducir en esa fuente";

    d_->ultimos[fuente] = {grafico, valores};
    emit cambio();
    return {};
}

QString Emisor::actualizar(const QString& grafico, const QJsonObject& valores, std::uint64_t fuente) {
    if (!d_->proc.disponible()) return "el renderizador de gráficos no está en este bundle";
    QString err;
    QJsonObject p = pedido(grafico, valores, &err);
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
