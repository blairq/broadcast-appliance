// ============================================================================
// src/app/graficos/Catalogo.hpp — los gráficos guardados, en disco
//
// Un gráfico es una carpeta en <datos>/Graficos/<nombre>/ con su `grafico.json`
// (la ficha: título, parámetros con su porDefecto, tamaño, acciones) y, o una
// descripción declarativa adentro de la ficha, o un `grafico.qml` escrito a mano.
//
// Los medios de un gráfico (GIF, secuencias de cuadros) van en su carpeta, en
// `medios/`, y el gráfico los nombra relativos («medios/bandera.gif»): por eso
// el pedido lleva la `carpeta` aunque el texto venga del editor sin guardar.
//
// Un gráfico de un PLUGIN (ver docs/funciones/GRAFICOS.md, «Gráficos de un
// plugin») también es una carpeta acá, con su ficha y `"plugin"`: así lo ven
// igual el editor, Rótulos, el guion y el asistente. Lo que no tiene es texto:
// lo dibuja el plugin, y su pedido se arma con Emisor::resolver.
//
// Lo leían tres por su cuenta —el editor (GraficoViewModel), el Emisor y el
// MCP—, cada uno con su lista de carpetas, su lectura de la ficha y su forma de
// armar el pedido para el renderizador, y no daban lo mismo (el MCP probaba el
// QML por archivo y el aire lo mandaba como texto; uno llenaba los valores que
// faltaban con null y otro no). Esto es lo único que sabe dónde están, cómo se
// llaman, cómo se arma su pedido y cómo se crean y se guardan.
// ============================================================================
#pragma once

#include <filesystem>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace ba::app::graficos {

class Catalogo {
   public:
    /// Los gráficos viven en `<raizDatos>/Graficos/` (la crea si no está).
    explicit Catalogo(const std::filesystem::path& raizDatos);

    QString raiz() const;
    /// La carpeta de un gráfico que existe. Vacío y el motivo si el nombre no
    /// sirve (un nombre con ".." o barras no tiene por qué llegar a leer el
    /// disco) o si no está.
    QString carpeta(const QString& nombre, QString* error = nullptr) const;
    /// Los gráficos guardados (carpetas con su grafico.json), por nombre.
    QStringList nombres() const;
    /// Todos, para mostrarlos: [{nombre, titulo, descripcion, categoria,
    /// declarativo, parametros: [{clave, titulo, tipo, ayuda}], acciones:
    /// [{titulo, prompt, icono}]}], agrupados por categoría («General» primero).
    QJsonArray lista() const;
    /// El `grafico.json` de uno. Vacío = no existe (o el nombre no sirve).
    QJsonObject ficha(const QString& nombre) const;
    /// Cuándo se va una cue del guion que no lo dice: `seVa` de su ficha («al-avanzar»,
    /// «sola», «queda»); si la ficha no dice (la copia de un evento de antes), el de la
    /// ficha de fábrica con ese nombre; si tampoco, «queda».
    QString seVaDe(const QString& nombre) const;
    /// Los valores por defecto que declara una ficha (solo los que tienen
    /// `porDefecto`: un null en un parámetro de un QML escrito a mano falla).
    /// Un parámetro de color que se llama como un valor del evento (`acento`), o que lo
    /// nombra con `"tema": "<clave>"`, toma el del evento abierto en vez de su porDefecto.
    static QJsonObject valoresPorDefecto(const QJsonObject& ficha);
    /// Los valores del evento abierto para eso ({"acento": "#00a86b"}; vacío = ninguno).
    /// Los pone el tema del evento al abrirlo o cambiarlo (TemaEventoViewModel). Cualquier hilo.
    static void ponerValoresDelEvento(const QJsonObject& valores);
    static QJsonObject valoresDelEvento();
    /// Lo que se edita de un gráfico: la descripción (JSON) si es declarativo,
    /// o el texto de su grafico.qml. Vacío si no tiene ninguno.
    QString texto(const QString& nombre) const;

    /// El pedido para el renderizador de un gráfico guardado, con `valores`
    /// encima de los por defecto. Vacío y el motivo en `error` si no se puede
    /// (también si lo dibuja un plugin: ese se pide con Emisor::resolver).
    QJsonObject pedido(const QString& nombre, const QJsonObject& valores, QString* error) const;
    /// Ídem, con un texto que puede no estar guardado (el del editor): la
    /// descripción en JSON si la ficha es declarativa, o el QML. El tamaño de
    /// un QML sale de la ficha (900x220 si no dice); una descripción trae el suyo.
    /// :param carpeta: la del gráfico, para sus rutas relativas (medios/…).
    static QJsonObject pedidoCon(const QJsonObject& ficha, const QString& texto, const QJsonObject& valores,
                                 QString* error, const QString& carpeta = {});

    /// El plugin que dibuja este gráfico ("" si no es de un plugin).
    static QString pluginDe(const QJsonObject& ficha) { return ficha.value("plugin").toString(); }
    /// Sabe animar un CAMBIO de valores al aire (tortas, barras, o un QML que lo
    /// declara con `"cambio"` en su ficha): con el gráfico en pantalla, cambiarle
    /// los valores no lo hace entrar de nuevo.
    static bool sabeCambiar(const QJsonObject& ficha);
    /// Cuánto dura la animación de un cambio (ms): `cambio.duracion` de la
    /// ficha, o 700.
    static int duracionCambio(const QJsonObject& ficha);
    /// Anota (o pone al día) el gráfico que declaró un plugin: una carpeta
    /// `plugin-<id>-<nombre>` con su ficha. No reescribe si no cambió (un plugin
    /// se redeclara en cada arranque). "" o el error; en `nombre`, la carpeta.
    QString guardarDePlugin(const QString& plugin, const QJsonObject& declaracion, QString* nombre = nullptr);

    /// Crea uno nuevo de ejemplo (declarativo o QML). "" o el error; en
    /// `creado`, el nombre con el que quedó (sin los caracteres que no sirven).
    QString crear(const QString& nombre, bool declarativo, QString* creado);
    /// Guarda el texto editado (la descripción en la ficha, o el grafico.qml).
    /// "" o el error.
    QString guardar(const QString& nombre, const QString& texto);

   private:
    QString raiz_;
};

} // namespace ba::app::graficos
