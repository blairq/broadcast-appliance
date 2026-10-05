// ============================================================================
// src/app/graficos/Emisor.hpp — poner un gráfico al aire, desde donde sea
//
// El editor de gráficos (GraficoViewModel) sabe sacar al aire lo que hay en el
// editor. Pero un gráfico lo dispara también el guion del evento, el rotulador
// de la vista principal, el celular por la web y el asistente por MCP, y
// ninguno de esos tiene un editor abierto: tienen un NOMBRE y unos VALORES.
//
// Eso es el Emisor: dado el nombre de un gráfico guardado, sus valores y la
// fuente de escena donde va, lo dibuja entero y lo reproduce. Es uno solo para
// toda la app —el editor también lo usa— así que hay UN `ba-grafico` y no
// cuatro.
//
// No guarda estado de lo que está al aire: eso lo sabe la fuente de libobs
// (`instancias()`), que es la única verdad. Lo que sí recuerda es con qué
// valores se disparó cada fuente por última vez, porque el rotulador necesita
// mostrar lo que se está viendo para poder cambiarlo.
// ============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

#include "graficos/GraficoProceso.hpp"

namespace ba::app::graficos {

class Emisor : public QObject {
    Q_OBJECT

   public:
    /// :param raizDatos: el directorio de datos; los gráficos viven en
    ///        `<raizDatos>/Graficos/<nombre>/`.
    explicit Emisor(const std::filesystem::path& raizDatos, QObject* padre = nullptr);
    ~Emisor() override;

    bool disponible() const;
    /// El renderizador, para quien necesite dibujar algo que no está guardado
    /// (el editor, que dibuja lo que se está escribiendo).
    GraficoProceso& proceso();

    QString raiz() const;
    QString carpeta(const QString& grafico) const;
    /// Los gráficos guardados, por nombre de carpeta.
    QStringList nombres() const;
    /// El `grafico.json` de uno. Vacío = no existe.
    QJsonObject ficha(const QString& grafico) const;
    /// Los valores por defecto que declara su ficha.
    QJsonObject valoresPorDefecto(const QString& grafico) const;
    /// El pedido para el renderizador, leído del disco. Deja el motivo en
    /// `error` si el gráfico no está o no se puede leer.
    QJsonObject pedido(const QString& grafico, const QJsonObject& valores, QString* error) const;

    /// La fuente de escena que se llama así, o 0 si no hay ninguna. El guion y
    /// el asistente nombran la fuente, no la numeran.
    std::uint64_t fuentePorNombre(const QString& nombre) const;

    /// Lo dibuja entero y lo pone al aire en esa fuente.
    ///
    /// :param duracionMs: cuánto se pre-renderiza. 0 = lo que diga la ficha.
    /// :returns: vacío si salió bien; si no, el motivo, en castellano.
    QString mostrar(const QString& grafico, const QJsonObject& valores, std::uint64_t fuente,
                    int duracionMs = 0, bool repetir = false);
    /// Ídem, pero con un pedido ya armado (lo usa el editor con su texto sin
    /// guardar). `grafico` es solo para poder decir después qué se mostró.
    QString reproducir(QJsonObject pedido, const QString& grafico, const QJsonObject& valores,
                       std::uint64_t fuente, int duracionMs, bool repetir);

    QString ocultar(std::uint64_t fuente);
    /// Campos vivos (T43): con el gráfico ya al aire en `fuente`, lo vuelve a dibujar
    /// con `valores` SOLO en su cuadro estable (sin la animación de entrada): un
    /// reloj con segundos o un cronómetro que cambia cada segundo sin «volver a entrar».
    QString actualizar(const QString& grafico, const QJsonObject& valores, std::uint64_t fuente);

    /// Con qué valores se disparó esa fuente por última vez (vacío si nunca).
    QJsonObject ultimosValores(std::uint64_t fuente) const;
    /// Qué gráfico se disparó en esa fuente por última vez.
    QString ultimoGrafico(std::uint64_t fuente) const;

   signals:
    /// Algo salió o dejó de estar al aire. El rotulador y la web se refrescan.
    void cambio();

   private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace ba::app::graficos
