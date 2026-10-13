// ============================================================================
// src/app/graficos/Emisor.cpp — ver Emisor.hpp
// ============================================================================
#include "graficos/Emisor.hpp"

#include <algorithm>
#include <map>
#include <vector>

#include "graficos/GraficoSource.hpp"

namespace ba::app::graficos {
namespace {

/// Cuántos cuadros por segundo se pre-renderizan. 30 alcanza para un gráfico:
/// no es video, es un movimiento corto.
constexpr int kFps = 30;
/// De a cuántos se le piden al renderizador. Viajan en base64, así que una
/// tanda muy grande hincha el mensaje sin ganar nada.
constexpr int kPorTanda = 12;

} // namespace

struct Emisor::Impl {
    explicit Impl(const std::filesystem::path& raizDatos) : catalogo(raizDatos) {}
    GraficoProceso proc;
    Catalogo catalogo;
    /// Lo último que se disparó en cada fuente, para poder cambiarle un texto
    /// sin tener que volver a escribir todos los valores.
    struct Ultimo {
        QString grafico;
        QJsonObject valores;
    };
    std::map<std::uint64_t, Ultimo> ultimos;
};

Emisor::Emisor(const std::filesystem::path& raizDatos, QObject* padre)
    : QObject(padre), d_(std::make_unique<Impl>(raizDatos)) {}

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
