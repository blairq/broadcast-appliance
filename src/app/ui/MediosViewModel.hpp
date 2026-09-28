// ============================================================================
// src/app/ui/MediosViewModel.hpp — la pestaña Medios
//
// `medios` era una de las DOS unicas categorias del MCP sin una sola linea de
// interfaz (la otra es `recetas`). O sea: el operador no tenia donde ver sus
// propios archivos. Para poner un video de fondo habia que pedirselo al
// asistente o saberse la ruta de memoria, en un aparato donde todo lo demas se
// puede hacer con las manos.
//
// EL FILTRO ES LO QUE HACE UTIL LA PESTAÑA. Con doscientos archivos, una lista
// es tan inservible como no tenerla: lo que sirve es escribir dos palabras y
// que queden tres. Por eso filtra MIENTRAS SE ESCRIBE y sin pedirle nada a
// nadie: la lista ya esta en memoria (ver `medios::filtrar`).
//
// SE RECORRE EL DISCO UNA VEZ Y SE GUARDA. Recorrer en cada tecla haria que
// escribir rapido congele la interfaz; y el disco no cambia solo, asi que
// refrescar es un boton (y pasa despues de traer o convertir algo).
// ============================================================================
#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include "medios/Medios.hpp"

namespace ba::app {

class AppRuntime;

class MediosViewModel : public QObject {
    Q_OBJECT
    /// Lo que se muestra AHORA: ya filtrado por `consulta` y por `tipo`.
    /// [{ruta, relativa, nombre, carpeta, tipo, bytes, modificadoMs, url}]
    Q_PROPERTY(QVariantList archivos READ archivos NOTIFY cambio)
    /// Lo que se escribio en el cuadro de busqueda.
    Q_PROPERTY(QString consulta READ consulta WRITE setConsulta NOTIFY cambio)
    /// "" (todos), "imagen", "video", "audio", "documento".
    Q_PROPERTY(QString tipo READ tipo WRITE setTipo NOTIFY cambio)
    /// Cuantos hay de cada tipo, SIN filtrar: es lo que deja decir «no hay
    /// ningun video» en vez de mostrar una grilla vacia sin explicacion.
    Q_PROPERTY(QVariantMap cuenta READ cuenta NOTIFY cambio)
    /// Cuantos hay en total y cuantos pasan el filtro.
    Q_PROPERTY(int total READ total NOTIFY cambio)
    Q_PROPERTY(int mostrados READ mostrados NOTIFY cambio)

  public:
    explicit MediosViewModel(AppRuntime& rt, QObject* parent = nullptr);
    ~MediosViewModel() override;

    QVariantList archivos() const;
    QString consulta() const;
    void setConsulta(const QString& q);
    QString tipo() const;
    void setTipo(const QString& t);
    QVariantMap cuenta() const;
    int total() const;
    int mostrados() const;

    /// Vuelve a recorrer el disco. Despues de traer o convertir algo.
    Q_INVOKABLE void refrescar();
    /// La carpeta de medios, para abrirla o para decirle al operador donde
    /// dejar los archivos.
    Q_INVOKABLE QString carpetaDeMedios() const;
    /// true si de ese archivo se puede sacar un carrusel (pdf, pptx).
    Q_INVOKABLE bool sePuedeDesarmar(const QString& ruta) const;

  signals:
    void cambio();
    void notify(const QString& texto, const QString& tipo);

  private:
    void recalcular();

    AppRuntime& rt_;
    QVector<medios::Archivo> todos_;
    QVector<medios::Archivo> visibles_;
    QString consulta_;
    QString tipo_;
};

} // namespace ba::app
