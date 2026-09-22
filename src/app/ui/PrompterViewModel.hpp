// ============================================================================
// src/app/ui/PrompterViewModel.hpp — el teleprompter, desde la cabina
//
// El teleprompter ya existía entero, pero repartido: el estado y las órdenes
// viven en el gateway (src/gateway/Prompter.hpp), las dos vistas son páginas
// web (/prompter para la tablet del orador, /prompter/#control para el celular
// del operador), y la app lo único que hacía era el cue `teleprompter` del
// guion. O sea: se podía usar, pero no había dónde escribir los discursos, ni
// forma de saber si la tablet del atril estaba prendida, ni de dónde sacar el
// enlace sin ir a buscarlo. Esto es esa parte.
//
// LOS DOCUMENTOS VIVEN EN EL EVENTO. `Eventos/<evento>/Discursos/`, que es de
// donde los lee el cue. Podría haber una biblioteca aparte, pero entonces
// habría dos carpetas parecidas y el cue leería de una mientras el operador
// escribe en la otra. Con carpetas adentro: un evento largo tiene discursos de
// varias personas y "Apertura/intendente.txt" se encuentra; veinte archivos
// sueltos, no.
//
// EL ESTADO SE CONSULTA, NO SE GUARDA. El dueño del teleprompter es el
// gateway: si la app se reinicia en medio de un discurso, el texto sigue
// corriendo en la tablet y al volver la app lo ve andando. Por eso acá no hay
// copia del estado: se pregunta cada medio segundo mientras la página está a
// la vista, y nada más (preguntar cuando nadie mira es gastar).
//
// LA CLAVE ES DEL GATEWAY. La genera él al arrancar (aleatoria, 16 bytes) y
// no se puede cambiar desde acá: cambiarla dejaría afuera al control que ya
// está en la mano de alguien. Se muestra para poder dictarla.
// ============================================================================
#pragma once

#include <memory>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace ba::app {

class ConferenceViewModel;
class EventoViewModel;

class PrompterViewModel : public QObject {
    Q_OBJECT
    // --- Documentos ----------------------------------------------------------
    /// Las carpetas que hay dentro de Discursos/ ("" es la raíz), ordenadas.
    Q_PROPERTY(QStringList carpetas READ carpetas NOTIFY documentosCambio)
    /// La carpeta que se está mirando.
    Q_PROPERTY(QString carpeta READ carpeta WRITE setCarpeta NOTIFY documentosCambio)
    /// Los documentos de esa carpeta: [{nombre, rel, palabras, duracionSeg,
    /// modificado, cargado}].
    Q_PROPERTY(QVariantList documentos READ documentos NOTIFY documentosCambio)
    /// El documento abierto en el editor (rel), o vacío.
    Q_PROPERTY(QString abierto READ abierto NOTIFY editorCambio)
    /// Su texto, tal como está en el editor (puede diferir del disco).
    Q_PROPERTY(QString texto READ texto NOTIFY editorCambio)
    /// true si el editor tiene cambios sin guardar.
    Q_PROPERTY(bool sucio READ sucio NOTIFY editorCambio)
    /// Cuántas palabras y cuánto dura leer lo que hay en el editor.
    Q_PROPERTY(int palabras READ palabras NOTIFY editorCambio)
    Q_PROPERTY(int duracionSeg READ duracionSeg NOTIFY editorCambio)
    /// Vacío si hay un evento abierto; si no, por qué no se puede trabajar.
    Q_PROPERTY(QString sinEvento READ sinEvento NOTIFY documentosCambio)

    // --- El teleprompter -----------------------------------------------------
    /// El estado que manda el gateway: titulo, texto, palabras, posicion, ppm,
    /// corriendo, tamano, espejo, aviso, duracionSeg. Vacío si no contesta.
    Q_PROPERTY(QVariantMap estado READ estado NOTIFY estadoCambio)
    /// true si el gateway está contestando.
    Q_PROPERTY(bool enLinea READ enLinea NOTIFY estadoCambio)
    /// Qué documento está cargado en el teleprompter, por su título.
    Q_PROPERTY(QString cargado READ cargado NOTIFY estadoCambio)
    /// Por dónde va la lectura, de 0 a 1.
    Q_PROPERTY(double avance READ avance NOTIFY estadoCambio)
    /// Cuánto falta para terminar, a la velocidad actual, en segundos.
    Q_PROPERTY(int faltaSeg READ faltaSeg NOTIFY estadoCambio)
    /// Los aparatos conectados: [{id, vista, nombre, ip, haceMs}].
    Q_PROPERTY(QVariantList conectados READ conectados NOTIFY estadoCambio)
    /// Cuántos de cada clase, para el resumen de arriba.
    Q_PROPERTY(int oradores READ oradores NOTIFY estadoCambio)
    Q_PROPERTY(int controles READ controles NOTIFY estadoCambio)
    /// La clave del control (para dictarla si el QR va sin ella).
    Q_PROPERTY(QString clave READ clave NOTIFY estadoCambio)

  public:
    PrompterViewModel(EventoViewModel* evento, ConferenceViewModel* sala, QObject* parent = nullptr);
    ~PrompterViewModel() override;

    QStringList carpetas() const;
    QString carpeta() const;
    void setCarpeta(const QString& c);
    QVariantList documentos() const;
    QString abierto() const;
    QString texto() const;
    bool sucio() const;
    int palabras() const;
    int duracionSeg() const;
    QString sinEvento() const;

    QVariantMap estado() const;
    bool enLinea() const;
    QString cargado() const;
    double avance() const;
    int faltaSeg() const;
    QVariantList conectados() const;
    int oradores() const;
    int controles() const;
    QString clave() const;

    // --- Documentos ----------------------------------------------------------
    /// Relee la carpeta. Se llama sola al abrir la página y al cambiar de evento.
    Q_INVOKABLE void refrescar();
    /// Abre un documento en el editor. Si hay cambios sin guardar, se pierden:
    /// quien llama tiene que preguntar antes (la página lo hace).
    Q_INVOKABLE void abrir(const QString& rel);
    /// Escribe en el editor (no toca el disco).
    Q_INVOKABLE void editar(const QString& texto);
    /// Guarda lo del editor. Devuelve el error, o vacío si salió bien.
    Q_INVOKABLE QString guardar();
    /// Crea un documento vacío en la carpeta que se está mirando y lo abre.
    Q_INVOKABLE QString crear(const QString& nombre);
    Q_INVOKABLE QString crearCarpeta(const QString& nombre);
    Q_INVOKABLE QString renombrar(const QString& rel, const QString& nombre);
    /// Mueve un documento a otra carpeta ("" es la raíz).
    Q_INVOKABLE QString mover(const QString& rel, const QString& aCarpeta);
    Q_INVOKABLE QString borrar(const QString& rel);

    // --- El teleprompter -----------------------------------------------------
    /// Manda un documento al teleprompter. Queda LISTO pero parado.
    Q_INVOKABLE QString cargar(const QString& rel);
    /// Manda lo que hay en el editor, sin pasar por el disco (para probar).
    Q_INVOKABLE QString cargarDelEditor();
    Q_INVOKABLE void arrancar(bool si);
    Q_INVOKABLE void setPpm(int ppm);
    Q_INVOKABLE void setTamano(int t);
    Q_INVOKABLE void setEspejo(bool si);
    /// Salta a una fracción del texto (0 a 1).
    Q_INVOKABLE void irA(double fraccion);
    /// Salta al principio del párrafo `n` (0 = el primero).
    Q_INVOKABLE void irAParrafo(int n);
    /// Le escribe un aviso al orador ("Redondeá"); vacío lo saca.
    Q_INVOKABLE void avisar(const QString& texto);
    /// Para en seco donde esté, sin mover la posición.
    Q_INVOKABLE void panico();
    /// Empieza o deja de preguntar el estado (lo llama la página al mostrarse).
    Q_INVOKABLE void mirar(bool si);

    /// Pide el enlace y el QR. `vista` es "control" u "orador"; `target`,
    /// "local" o "remote". Contesta por `qrListo`.
    Q_INVOKABLE void pedirQr(const QString& target, const QString& vista, bool conClave);

  signals:
    void documentosCambio();
    void editorCambio();
    void estadoCambio();
    void qrListo(const QString& url, const QStringList& modulos, const QString& mensaje,
                 const QString& clave);
    void notify(const QString& texto, const QString& tipo);

  private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace ba::app
