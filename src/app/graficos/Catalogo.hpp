// ============================================================================
// src/app/graficos/Catalogo.hpp — los gráficos guardados, en disco
//
// Un gráfico es una carpeta en <datos>/Graficos/<nombre>/ con su `grafico.json`
// (la ficha: título, parámetros con su porDefecto, tamaño, acciones) y, o una
// descripción declarativa adentro de la ficha, o un `grafico.qml` escrito a mano.
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
    /// Los valores por defecto que declara una ficha (solo los que tienen
    /// `porDefecto`: un null en un parámetro de un QML escrito a mano falla).
    static QJsonObject valoresPorDefecto(const QJsonObject& ficha);
    /// Lo que se edita de un gráfico: la descripción (JSON) si es declarativo,
    /// o el texto de su grafico.qml. Vacío si no tiene ninguno.
    QString texto(const QString& nombre) const;

    /// El pedido para el renderizador de un gráfico guardado, con `valores`
    /// encima de los por defecto. Vacío y el motivo en `error` si no se puede.
    QJsonObject pedido(const QString& nombre, const QJsonObject& valores, QString* error) const;
    /// Ídem, con un texto que puede no estar guardado (el del editor): la
    /// descripción en JSON si la ficha es declarativa, o el QML. El tamaño de
    /// un QML sale de la ficha (900x220 si no dice); una descripción trae el suyo.
    static QJsonObject pedidoCon(const QJsonObject& ficha, const QString& texto, const QJsonObject& valores,
                                 QString* error);

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
