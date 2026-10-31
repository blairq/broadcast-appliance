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
// No guarda estado de lo que está al aire: eso lo sabe la fuente gráfica
// (GraficoSource: `instancias()`), que es la única verdad. Lo que sí recuerda es con qué
// valores se disparó cada fuente por última vez, porque el rotulador necesita
// mostrar lo que se está viendo para poder cambiarlo.
// ============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

#include <nlohmann/json.hpp>

#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>

#include "graficos/Catalogo.hpp"
#include "graficos/GraficoProceso.hpp"
#include "graficos/Medios.hpp"

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

    /// Los GIF, imágenes y videos con alfa de los gráficos (Graficos/<x>/medios).
    Medios& medios();

    // --- Gráficos de plugins ------------------------------------------------------
    // Un plugin de Python declara un gráfico (@plugin.grafico, ver
    // docs/funciones/GRAFICOS.md): queda en el catálogo como cualquier otro y,
    // cuando hace falta dibujarlo, se le pide al plugin la descripción, el QML
    // o los cuadros, y de ahí sigue el camino de siempre (ba-grafico, la tira,
    // la fuente de escena).
    /// Cómo pedirle algo a un plugin (lo arma quien tiene los plugins): `args`
    /// es {nombre, valores, ancho, alto, fps, duracion_ms}; `listo` con lo que
    /// contestó ({declarativo} | {qml} | {secuencia}) o el error.
    using ProveedorPlugin = std::function<void(const QString& plugin, const QJsonObject& args,
                                               std::function<void(QJsonObject resultado, QString error)> listo)>;
    void setProveedorPlugin(ProveedorPlugin p);
    /// Un plugin declaró un gráfico: lo anota en el catálogo. "" o el error.
    QString registrarDePlugin(const QString& plugin, const QJsonObject& declaracion);

    /// El pedido para el renderizador de un gráfico guardado, sea de una ficha
    /// o de un plugin (que contesta después). `listo` puede llamarse antes de
    /// volver (un gráfico normal) o más tarde (uno de plugin). `tam`: al que se
    /// va a ver (vacío = el de la ficha).
    void resolver(const QString& grafico, const QJsonObject& valores, QSize tam,
                  std::function<void(QJsonObject pedido, QString error)> listo);

    // --- Video en vivo dentro del gráfico (VideoEnGrafico.hpp) ------------------------
    // La cámara dentro del zócalo: el gráfico declara capas de video («escena» en su
    // ficha) y el Emisor le pide al motor de la app que las ponga en las escenas del
    // gráfico (al mostrar, quietas en su primera clave), que arranquen con el primer
    // cuadro que sale del gráfico, y que se vayan cuando el gráfico se va.
    struct CapasDeVideo {
        /// Pone (o cambia) las capas que acompañan al gráfico de la fuente `grafico`
        /// (VideoEnGrafico::aJson). "" o el error.
        std::function<std::string(const std::string& grafico, const nlohmann::json& capas)> poner;
        /// Su ms 0 (g_get_monotonic_time en ns).
        std::function<void(const std::string& grafico, std::int64_t desdeNs)> empezar;
        /// Las saca (las fuentes vuelven a como estaban).
        std::function<void(const std::string& grafico)> quitar;
    };
    void setCapasDeVideo(CapasDeVideo c);

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
    /// Si el gráfico sabe animar un cambio (Catalogo::sabeCambiar: tortas, barras)
    /// y lo que está al aire es ese mismo gráfico, en vez del cuadro quieto dibuja
    /// la animación del cambio: de los valores de antes a los nuevos.
    QString actualizar(const QString& grafico, const QJsonObject& valores, std::uint64_t fuente);
    /// «Al aire» con un gráfico que ya está en pantalla en esa fuente y sabe
    /// cambiar: lo actualiza (sin volver a entrar). Si no, lo muestra de cero.
    /// Es lo que hacen Rótulos y grafico_actualizar con una votación en vivo.
    QString mostrarOCambiar(const QString& grafico, const QJsonObject& valores, std::uint64_t fuente,
                            bool repetir = false);

    /// El cuadro estable de un gráfico (ms): pasada la entrada, antes de la salida.
    static int msEstable(const QJsonObject& ficha);
    /// El gráfico con esos valores en su cuadro estable, dibujado aparte (no sale al
    /// aire): la miniatura «con gráficos» de la línea de tiempo. Al tamaño de la fuente
    /// de escena `fuente` (la que lo va a mostrar; si no está, el de la ficha). ESPERA al
    /// renderizador: quien llama se cuida de no pedirlo seguido. `listo` con la imagen
    /// (nula y el error si no): en el momento, o más tarde si el gráfico es de un plugin.
    void dibujarQuieto(const QString& grafico, const QJsonObject& valores, const QString& fuente,
                       std::function<void(QImage imagen, QString error)> listo);

    /// Con qué valores se disparó esa fuente por última vez (vacío si nunca).
    QJsonObject ultimosValores(std::uint64_t fuente) const;
    /// Qué gráfico se disparó en esa fuente por última vez.
    QString ultimoGrafico(std::uint64_t fuente) const;

   signals:
    /// Algo salió o dejó de estar al aire. El rotulador y la web se refrescan.
    void cambio();
    /// Un gráfico que se estaba dibujando para el aire no salió (o se cortó).
    void fallo(const QString& motivo);
    /// Cambió la lista de gráficos (un plugin declaró o puso al día uno).
    void catalogoCambio();

   private:
    /// Crea el dibujo para el aire (una vez).
    void prepararAire();
    /// Una tanda del dibujo para el aire (en el hilo de la interfaz).
    void recibirTanda(std::uint64_t fuente, int generacion, std::vector<QImage> cuadros, bool fin, const QString& error);
    /// Resuelve el pedido y llama a `usar`. Si se resolvió en el momento, devuelve
    /// lo que devolvió `usar` (o el error); si queda esperando a un plugin, ""
    /// y lo que falle después sale por `fallo`.
    QString conPedido(const QString& grafico, const QJsonObject& valores, QSize tam,
                      std::function<QString(QJsonObject pedido)> usar);
    /// El tamaño de esa fuente de escena (inválido si ya no está).
    static QSize tamanoDe(std::uint64_t fuente);
    /// Hay algo en pantalla en esa fuente.
    static bool enPantalla(std::uint64_t fuente);
    /// El nombre de esa fuente de escena ("" si ya no está).
    static std::string nombreDe(std::uint64_t fuente);
    /// Las capas de video del gráfico que se va a dibujar con `pedido` en esa fuente (o
    /// ninguna: saca las que hubiera). `inicioMs`: dónde arranca lo que se dibuja en su
    /// línea de tiempo; `finMs`: cuándo termina (0 = queda), para sacarlas después.
    void prepararVideo(std::uint64_t fuente, const QString& grafico, const QJsonObject& pedido, int generacion,
                       int inicioMs, int salidaEn, int salidaDur, int finMs);
    /// Saca las capas de video de esa fuente (si tenía).
    void quitarVideo(std::uint64_t fuente);
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace ba::app::graficos
