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
#include <QSet>
#include <QStringList>
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

    /// El arbol de carpetas, en orden de dibujo:
    /// [{relativa, nombre, nivel, propios, total, raiz, abierta, visible}]
    /// `visible` ya viene resuelto —una carpeta cuya madre esta cerrada viene
    /// en false— para que la vista no tenga que recorrer el arbol por cada
    /// fila, que con cien carpetas se nota.
    Q_PROPERTY(QVariantList arbol READ arbol NOTIFY cambio)
    /// La carpeta que se esta mirando. "" = todo.
    Q_PROPERTY(QString carpeta READ carpeta WRITE setCarpeta NOTIFY cambio)
    /// Si se muestran tambien los de las subcarpetas. Prendido: ver "todo lo
    /// que hay de aca para abajo" es lo que uno quiere al elegir Media.
    Q_PROPERTY(bool conSubcarpetas READ conSubcarpetas WRITE setConSubcarpetas NOTIFY cambio)
    /// nombre | fecha | tamano | tipo, y para que lado.
    Q_PROPERTY(QString orden READ orden WRITE setOrden NOTIFY cambio)
    Q_PROPERTY(bool ordenDescendente READ ordenDescendente WRITE setOrdenDescendente NOTIFY cambio)

    /// Las rutas relativas elegidas. Las acciones de abajo actuan sobre ESTO.
    Q_PROPERTY(QStringList seleccion READ seleccion NOTIFY seleccionCambio)
    Q_PROPERTY(int cuantosElegidos READ cuantosElegidos NOTIFY seleccionCambio)
    /// Cuanto pesa lo elegido, para saber antes de mover o borrar.
    Q_PROPERTY(qint64 bytesElegidos READ bytesElegidos NOTIFY seleccionCambio)

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

    QVariantList arbol() const;
    QString carpeta() const;
    void setCarpeta(const QString& c);
    bool conSubcarpetas() const;
    void setConSubcarpetas(bool v);
    QString orden() const;
    void setOrden(const QString& o);
    bool ordenDescendente() const;
    void setOrdenDescendente(bool v);
    QStringList seleccion() const;
    int cuantosElegidos() const;
    qint64 bytesElegidos() const;

    /// Abre o cierra una carpeta del arbol.
    Q_INVOKABLE void plegar(const QString& relativa);
    Q_INVOKABLE void abrirHasta(const QString& relativa);

    /// Elegir. `modo`: "solo" (reemplaza), "sumar" (ctrl), "hasta" (shift, desde
    /// la ultima elegida). Son los tres gestos que cualquiera espera de una
    /// grilla de archivos; sin el tercero, elegir cuarenta es cuarenta clics.
    Q_INVOKABLE void elegir(const QString& relativa, const QString& modo = "solo");
    Q_INVOKABLE void elegirTodo();
    Q_INVOKABLE void noElegirNada();
    Q_INVOKABLE bool estaElegido(const QString& relativa) const;

    /// Mueve lo elegido adentro de `destino` (una carpeta relativa). Es lo que
    /// pasa al soltar la seleccion sobre una carpeta del arbol.
    /// :returns: cuantos se movieron.
    Q_INVOKABLE int moverElegidos(const QString& destino);
    Q_INVOKABLE bool nuevaCarpeta(const QString& padre, const QString& nombre);
    Q_INVOKABLE bool renombrar(const QString& relativa, const QString& nombre);
    /// A la papelera, no al vacio: un archivo puede estar en una escena.
    Q_INVOKABLE int borrarElegidos();

    /// Vuelve a recorrer el disco. Despues de traer o convertir algo.
    Q_INVOKABLE void refrescar();
    /// La carpeta de medios, para abrirla o para decirle al operador donde
    /// dejar los archivos.
    Q_INVOKABLE QString carpetaDeMedios() const;
    /// La ruta absoluta de una relativa, o "" si se sale de la carpeta del
    /// sistema. La vista NO arma rutas pegando texto: el unico lugar donde se
    /// resuelve una ruta es el que tiene el guardia (`resolverAdentro`).
    Q_INVOKABLE QString rutaAbsoluta(const QString& relativa) const;
    /// true si de ese archivo se puede sacar un carrusel (pdf, pptx).
    Q_INVOKABLE bool sePuedeDesarmar(const QString& ruta) const;

   signals:
    void cambio();
    void seleccionCambio();
    void notify(const QString& texto, const QString& tipo);

   private:
    void recalcular();

    AppRuntime& rt_;
    QVector<medios::Archivo> todos_;
    QVector<medios::Archivo> visibles_;
    QVector<medios::Carpeta> carpetas_;
    QString consulta_;
    QString tipo_;
    QString carpeta_;
    bool conSub_ = true;
    QString orden_ = "nombre";
    bool ordenDesc_ = false;
    /// Las carpetas cerradas, no las abiertas: un arbol nuevo se ve entero, y
    /// guardar lo cerrado hace que una carpeta que aparece despues no nazca
    /// escondida.
    QSet<QString> cerradas_;
    QStringList seleccion_;
    /// Desde donde cuenta el shift. Vacio si no hay ninguna elegida.
    QString ancla_;

    QString raiz() const;
};

} // namespace ba::app
