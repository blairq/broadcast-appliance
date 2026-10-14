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
#include <vector>

#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

#include "graficos/Catalogo.hpp"
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

    /// Los gráficos guardados (dónde están, sus fichas, su pedido): el mismo
    /// catálogo para el editor, el MCP y todo lo que dispara gráficos.
    Catalogo& catalogo();
    const Catalogo& catalogo() const;
    /// Atajos al catálogo: los gráficos guardados, por nombre de carpeta…
    QStringList nombres() const;
    /// …y el `grafico.json` de uno (vacío = no existe).
    QJsonObject ficha(const QString& grafico) const;

    /// La fuente de escena que se llama así, o 0 si no hay ninguna. El guion y
    /// el asistente nombran la fuente, no la numeran.
    std::uint64_t fuentePorNombre(const QString& nombre) const;

    /// Lo pone al aire en esa fuente. Se dibuja en otro hilo y sale con la primera tanda
    /// de cuadros (la interfaz no espera); si algo falla después, avisa con `fallo`.
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
    /// Lo saca CON su animación de salida (no de golpe, como ocultar): lo dibuja ya entrado
    /// y desde ahí se va; al terminar, lo oculta. Un gráfico que no declara «cuándo se va»
    /// (salidaEn) no sabe irse: se oculta. Lo usa el guion para los que se van al avanzar.
    /// :param siSigue: la generación que se quiere sacar (generacion()); si en esa fuente ya
    ///        se puso otra cosa, no se toca. -1 = lo que haya.
    QString sacar(std::uint64_t fuente, int siSigue = -1);
    /// Cuántos gráficos se pusieron en esa fuente (cambia con cada mostrar/sacar).
    int generacion(std::uint64_t fuente) const;
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
    /// Un gráfico que se estaba dibujando para el aire no salió (o se cortó).
    void fallo(const QString& motivo);

   private:
    /// Crea el dibujo para el aire (una vez).
    void prepararAire();
    /// Una tanda del dibujo para el aire (en el hilo de la interfaz).
    void recibirTanda(std::uint64_t fuente, int generacion, std::vector<QImage> cuadros, bool fin, const QString& error);
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace ba::app::graficos
