// ============================================================================
// src/app/ui/QrViewModel.hpp — la pestaña de códigos QR
//
// Una biblioteca de códigos guardados, con su estudio de diseño. Dos ámbitos,
// a propósito:
//
//   GLOBAL   Config/qr.json — el WiFi de la sala, el teléfono de producción,
//            el enlace del teleprompter. Sirven para todos los eventos y no
//            tiene sentido rehacerlos cada vez.
//   EVENTO   Eventos/<x>/QR/qr.json — los de ESTE evento: el formulario de
//            inscripción, la encuesta del cierre. Viajan con él a otra máquina.
//
// LOS DEL APARATO SE RESUELVEN AL MOSTRARSE, no al guardarse. El enlace de la
// sala lleva su PIN y el del teleprompter su clave, y eso cambia (la clave del
// prompter es nueva en cada arranque). Guardar el texto ya armado daría un QR
// que funciona hoy y manda a ningún lado mañana; se guarda QUE es, y el texto
// se pide cuando hace falta.
//
// El dibujo lo hace ba::qr (núcleo), que es el mismo que usa el gateway para
// los televisores: lo que se diseña acá es lo que sale por todos lados.
// ============================================================================
#pragma once

#include <memory>

#include <QObject>
#include <QQuickImageProvider>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace ba::app {

class AppRuntime;
class ConferenceViewModel;
class EventoViewModel;

class QrViewModel : public QObject {
    Q_OBJECT
    /// Los códigos guardados del ámbito que se está mirando:
    /// [{id, nombre, tipo, tipoTitulo, resumen}].
    Q_PROPERTY(QVariantList codigos READ codigos NOTIFY bibliotecaCambio)
    /// "global" o "evento".
    Q_PROPERTY(QString ambito READ ambito WRITE setAmbito NOTIFY bibliotecaCambio)
    /// Vacío si se puede usar el ámbito «evento»; si no, por qué no.
    Q_PROPERTY(QString sinEvento READ sinEvento NOTIFY bibliotecaCambio)

    /// Qué se puede codificar: [{id, titulo, ayuda, campos, delAparato}].
    Q_PROPERTY(QVariantList tipos READ tipos CONSTANT)
    /// Los íconos que pueden ir al centro: [{id, titulo}].
    Q_PROPERTY(QVariantList iconos READ iconos CONSTANT)

    /// El código abierto en el editor (vacío = ninguno).
    Q_PROPERTY(QString abierto READ abierto NOTIFY editorCambio)
    /// Sus campos: {nombre, tipo, datos{...}, estilo{...}}.
    Q_PROPERTY(QVariantMap actual READ actual NOTIFY editorCambio)
    /// El texto que va DENTRO del código, ya armado. Es lo que conviene
    /// mostrarle al operador: un QR es opaco, y ver qué dice evita mandar a
    /// imprimir mil folletos con la dirección vieja.
    Q_PROPERTY(QString contenido READ contenido NOTIFY editorCambio)
    /// Vacío si todo bien; si no, por qué el código no se puede generar.
    Q_PROPERTY(QString aviso READ aviso NOTIFY editorCambio)
    /// Cuántos módulos de lado tiene (0 si no se puede). Más de 80 y conviene
    /// avisar que va a costar leerlo de lejos.
    Q_PROPERTY(int modulos READ modulos NOTIFY editorCambio)
    /// Sube con cada cambio: la vista previa lo usa para no mostrar lo viejo.
    Q_PROPERTY(int revision READ revision NOTIFY editorCambio)
    Q_PROPERTY(bool sucio READ sucio NOTIFY editorCambio)

   public:
    QrViewModel(AppRuntime& rt, EventoViewModel* evento, ConferenceViewModel* sala,
                QObject* parent = nullptr);
    ~QrViewModel() override;

    QVariantList codigos() const;
    QString ambito() const;
    void setAmbito(const QString& a);
    QString sinEvento() const;
    QVariantList tipos() const;
    QVariantList iconos() const;
    QString abierto() const;
    QVariantMap actual() const;
    QString contenido() const;
    QString aviso() const;
    int modulos() const;
    int revision() const;
    bool sucio() const;

    // --- La biblioteca --------------------------------------------------------
    Q_INVOKABLE void refrescar();
    /// Empieza uno nuevo (no lo guarda hasta que se le ponga nombre).
    Q_INVOKABLE void nuevo(const QString& tipo);
    Q_INVOKABLE void abrir(const QString& id);
    Q_INVOKABLE QString guardar(const QString& nombre);
    Q_INVOKABLE QString borrar(const QString& id);
    /// Lo copia al otro ámbito (del evento a la biblioteca general o al revés).
    Q_INVOKABLE QString copiarA(const QString& id, const QString& ambito);

    // --- El editor ------------------------------------------------------------
    Q_INVOKABLE void setTipo(const QString& tipo);
    Q_INVOKABLE void setDato(const QString& clave, const QString& valor);
    Q_INVOKABLE void setEstilo(const QString& clave, const QVariant& valor);
    /// Vuelve a pedir el contenido de los tipos del aparato (cambió el PIN,
    /// se reinició la sala).
    Q_INVOKABLE void resolver();

    // --- Salidas --------------------------------------------------------------
    /// Guarda el código en un archivo. `formato`: "svg" o "png". Devuelve la
    /// ruta donde quedó, o vacío y el motivo en la señal `notify`.
    Q_INVOKABLE QString exportar(const QString& formato, int px = 1024);
    /// El SVG de lo que está en el editor (para copiarlo, o para pruebas).
    Q_INVOKABLE QString svgActual() const;
    /// El SVG de un código guardado, por su id.
    QString svgDe(const QString& id) const;
    /// El texto que iría adentro de un código de ese tipo con esos datos, sin
    /// tocar el editor. Lo usa el MCP para generar sin pisar lo que el
    /// operador esté diseñando.
    QString contenidoDe(const QString& tipo, const QVariantMap& datos) const;
    /// Un código suelto (ni guardado ni abierto en el editor) como SVG.
    QString svgSuelto(const QString& tipo, const QVariantMap& datos, const QVariantMap& estilo) const;
    /// Un código guardado, listo para PROYECTAR: `{texto, estilo}`, donde
    /// `estilo` ya viene como parámetros de consulta (ba::qr::aConsulta). Vacío
    /// si ese id no existe.
    ///
    /// Devuelve las dos cosas juntas porque van juntas: proyectar el texto sin
    /// el estilo es lo que pasaba antes, y no se nota —sale un QR válido, pero
    /// no el que se diseñó—.
    QVariantMap paraProyectar(const QString& id) const;
    /// Lo guardado de los DOS ámbitos, para listarlo de una.
    QVariantList todos() const;
    /// Escribe un SVG en un archivo de Exportado/. Devuelve la ruta o vacío.
    QString escribir(const QString& svgTexto, const QString& nombre, const QString& formato, int px) const;

   signals:
    void bibliotecaCambio();
    void editorCambio();
    void notify(const QString& texto, const QString& tipo);

   private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

/// Dibuja la vista previa: `image://qr/actual?rev=N` o `image://qr/<id>?rev=N`.
/// Rasteriza el MISMO SVG que se exporta, así lo que se ve es lo que sale.
class QrImageProvider : public QQuickImageProvider {
   public:
    explicit QrImageProvider(QrViewModel& vm) : QQuickImageProvider(QQuickImageProvider::Image), vm_(vm) {}
    QImage requestImage(const QString& id, QSize* size, const QSize& pedido) override;

   private:
    QrViewModel& vm_;
};

} // namespace ba::app
