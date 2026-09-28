// ============================================================================
// src/app/ui/CarruselViewModel.hpp — la biblioteca de carruseles, para la vista
//
// DONDE SE ARMAN. En la pestaña Medios, no adentro de una escena. Un carrusel
// se arma una vez —con las placas institucionales, con la presentación del
// orador— y se aplica donde haga falta: en la apertura y en el cierre. Si
// viviera adentro de una escena habría que copiarlo a mano para la siguiente.
//
// LA CONVERSION AVISA. Desarmar un .pptx de cincuenta láminas tarda. Sin
// `convirtiendo`, `hecho` y `total` la ventana se queda muda y el operador no
// sabe si está trabajando o se colgó, que es justo lo que no se puede permitir
// diez minutos antes de empezar.
//
// LAS IMAGENES VAN AL DISCO, NO A LA MEMORIA. Cada lámina es un PNG en
// Media/Carruseles/<nombre>/ y la vista lo dibuja desde ahí. Guardarlas en
// memoria sería llenar la RAM con lo que el disco ya tiene, y además se
// perderían al cerrar.
// ============================================================================
#pragma once

#include <memory>

#include <QObject>
#include <QStringList>
#include <QVariantList>

namespace ba::carrusel {
class Store;
class Conversor;
} // namespace ba::carrusel

namespace ba::app {

class AppRuntime;

class CarruselViewModel : public QObject {
    Q_OBJECT
    /// Todos los de la biblioteca: [{nombre, laminas, tapa}]. `tapa` es la URL
    /// de la primera lámina, que es como se reconoce un carrusel de un vistazo.
    Q_PROPERTY(QVariantList carruseles READ carruseles NOTIFY cambio)
    /// El que se está editando, "" si ninguno.
    Q_PROPERTY(QString actual READ actual WRITE setActual NOTIFY cambio)
    /// Las láminas del actual: [{archivo, url, titulo, etiquetas, analizada}].
    Q_PROPERTY(QVariantList laminas READ laminas NOTIFY cambio)
    /// Cuántas láminas del actual todavía no tienen etiquetas. Es lo que deja
    /// ofrecer «etiquetar» sólo cuando hay algo que etiquetar, en vez de un
    /// botón que a veces no hace nada.
    Q_PROPERTY(int sinAnalizar READ sinAnalizar NOTIFY cambio)
    /// Lo que se escribió para filtrar las láminas del actual.
    Q_PROPERTY(QString consulta READ consulta WRITE setConsulta NOTIFY cambio)

    /// La barra de trabajo. `total` en 0 con `convirtiendo` en true significa
    /// que no se sabe cuánto falta (LibreOffice no informa progreso): la barra
    /// va indeterminada en vez de fingir un número.
    Q_PROPERTY(bool convirtiendo READ convirtiendo NOTIFY progreso)
    Q_PROPERTY(QString etapa READ etapa NOTIFY progreso)
    Q_PROPERTY(int hecho READ hecho NOTIFY progreso)
    Q_PROPERTY(int total READ total NOTIFY progreso)

  public:
    explicit CarruselViewModel(AppRuntime& rt, QObject* parent = nullptr);
    ~CarruselViewModel() override;

    QVariantList carruseles() const;
    QString actual() const;
    void setActual(const QString& nombre);
    QVariantList laminas() const;
    int sinAnalizar() const;
    QString consulta() const;
    void setConsulta(const QString& q);

    bool convirtiendo() const;
    QString etapa() const;
    int hecho() const;
    int total() const;

    /// Crea uno vacío y lo deja como actual. Devuelve false si el nombre ya
    /// estaba: dos carruseles con el mismo nombre no se pueden distinguir.
    Q_INVOKABLE bool crear(const QString& nombre);
    Q_INVOKABLE bool borrar(const QString& nombre);
    Q_INVOKABLE bool renombrar(const QString& de, const QString& a);

    /// Agrega una imagen suelta al actual (arrastrada o elegida de la grilla).
    Q_INVOKABLE bool agregar(const QString& archivo);
    Q_INVOKABLE bool quitar(int i);
    Q_INVOKABLE bool mover(int de, int a);
    /// Salta a una lámina. Es lo que hace el operador tocando una tarjeta.
    Q_INVOKABLE bool ir(int i);
    Q_INVOKABLE int posicion() const;
    /// Guarda las etiquetas de una lámina de CUALQUIER carrusel (no hace falta
    /// abrirlo): lo llama el que mira imágenes, que trabaja de fondo mientras
    /// el operador tiene otro abierto.
    Q_INVOKABLE bool etiquetar(const QString& carrusel, int i, const QString& etiquetas);

    /// Desarma un PDF o un PowerPoint en láminas y arma un carrusel con ellas.
    /// :param nombre: cómo llamarlo. Vacío = el nombre del archivo.
    Q_INVOKABLE void desarmar(const QString& documento, const QString& nombre = {});
    Q_INVOKABLE void cancelar();
    /// true si este equipo puede desarmar ESE archivo. Un PDF sí siempre; un
    /// .pptx sólo si el bundle trae LibreOffice.
    Q_INVOKABLE bool sePuedeDesarmar(const QString& archivo) const;

  signals:
    void cambio();
    void progreso();
    void notify(const QString& texto, const QString& tipo);

  private:
    QString carpetaDe(const QString& nombre) const;

    AppRuntime& rt_;
    std::unique_ptr<carrusel::Store> store_;
    std::unique_ptr<carrusel::Conversor> conv_;
    QString actual_;
    QString consulta_;
    QString etapa_;
    bool convirtiendo_ = false;
    int hecho_ = 0;
    int total_ = 0;
    /// A qué carrusel van las láminas que salgan de la conversión en curso.
    QString destinoConversion_;
};

} // namespace ba::app
