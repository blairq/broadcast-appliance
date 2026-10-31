#pragma once
// La búsqueda de un catálogo, como modelo: lo que se ve de un catálogo (los filtros de
// audio, los módulos de video, las herramientas de la Botonera) según lo escrito, la
// categoría elegida y las condiciones prendidas. Antes cada catálogo buscaba en QML con
// su propio «normal()» y su puntaje (tres copias); ésta es la única.
//
// En QML (registrado como «BA.FiltroCatalogo»):
//     FiltroCatalogo {
//         fuente: Studio.audio.catalogoLsp      // [{...}] (cada uno con su `categoria`)
//         clave: "clave"                        // para actualizar en su lugar (ui/ListaObjetos)
//         campos: ["nombre", "descripcion"]     // dónde se busca (textos o listas de textos)
//         texto: busqueda.text
//         categoria: cat.categoria              // "" = todas
//         exigir: ["vivo"]                      // los que tienen estos campos en verdadero
//     }
// La búsqueda: todas las palabras, sin tildes ni mayúsculas (y «_» como espacio); suma 3
// por palabra en `camposCabeza` y 1 en el resto. Si se le dan `puntajes` (de un buscador
// mejor: el índice del asistente, {nombre: {puntaje, porque}}), manda ese: entran solo
// ésos, con su puntaje y su `porque`; si vienen vacíos, la búsqueda simple.
// Con `agrupar`, buscando: la categoría con el mejor primero y adentro por puntaje (una
// sección no se parte en dos). Sin buscar, el orden del catálogo.
//
// Cada fila trae los campos del catálogo y además `puntos` y `porque`.

#include <QStringList>
#include <QVariantMap>

#include "ui/ListaObjetos.hpp"

namespace ba::app {

class FiltroCatalogo : public ListaObjetos {
    Q_OBJECT
    Q_PROPERTY(QVariantList fuente READ fuente WRITE setFuente NOTIFY entradaCambio)
    Q_PROPERTY(QStringList campos MEMBER campos_ NOTIFY entradaCambio)
    Q_PROPERTY(QStringList camposCabeza MEMBER camposCabeza_ NOTIFY entradaCambio)
    Q_PROPERTY(QString texto MEMBER texto_ NOTIFY entradaCambio)
    Q_PROPERTY(QString categoria MEMBER categoria_ NOTIFY entradaCambio)
    Q_PROPERTY(QString campoCategoria MEMBER campoCategoria_ NOTIFY entradaCambio)
    Q_PROPERTY(QStringList exigir MEMBER exigir_ NOTIFY entradaCambio)
    Q_PROPERTY(QVariantMap puntajes MEMBER puntajes_ NOTIFY entradaCambio)
    Q_PROPERTY(bool agrupar MEMBER agrupar_ NOTIFY entradaCambio)
    Q_PROPERTY(bool ordenarCategorias MEMBER ordenarCategorias_ NOTIFY entradaCambio)
    /// Las categorías de lo que cumple `exigir` (sin mirar lo escrito ni la categoría).
    Q_PROPERTY(QStringList categorias READ categorias NOTIFY resultadoCambio)
    /// Cuántos cumplen `exigir` (el total de «12 de 40»).
    Q_PROPERTY(int total READ total NOTIFY resultadoCambio)
    /// Buscando: cuántos encontró en cada categoría (sin acotar por la elegida).
    Q_PROPERTY(QVariantMap cuentas READ cuentas NOTIFY resultadoCambio)
    /// El color de cada categoría: el campo `color` del primero de esa categoría.
    Q_PROPERTY(QVariantMap colores READ colores NOTIFY resultadoCambio)

   public:
    explicit FiltroCatalogo(QObject* parent = nullptr);

    QVariantList fuente() const { return fuente_; }
    void setFuente(const QVariantList& f);
    QStringList categorias() const { return categorias_; }
    int total() const { return total_; }
    QVariantMap cuentas() const { return cuentas_; }
    QVariantMap colores() const { return colores_; }

    /// Sin tildes ni mayúsculas, con «_» como espacio (lo que se compara).
    static QString plano(const QString& s);

   signals:
    void entradaCambio();
    void resultadoCambio();

   private:
    void filtrar();
    QString textoDe(const QVariantMap& m, const QStringList& campos) const;

    QVariantList fuente_;
    QStringList campos_, camposCabeza_, exigir_;
    QString texto_, categoria_, campoCategoria_ = QStringLiteral("categoria");
    QVariantMap puntajes_;
    bool agrupar_ = false, ordenarCategorias_ = false;
    QStringList categorias_;
    int total_ = 0;
    QVariantMap cuentas_, colores_;
};

} // namespace ba::app
