// ============================================================================
// src/app/ui/GuionViewModel.hpp — editar el guion del evento abierto
//
// La página Guion y `guion_*`: el guion de cues (la lectura de la línea, ver
// EventoAbierto), sus datos (nombre, fecha, hora, huso, idioma, lo que usa), la revisión
// previa, la lista de chequeo manual y lo que hace falta para armar el formulario de un
// cue (los tipos y sus campos, y las opciones de cada campo para elegirlas en vez de
// escribirlas).
//
// LO QUE SE ELIGE NO SE LEE AL DIBUJAR. Las opciones de los campos salen de cosas que
// viven en otro lado: las escenas y las fuentes de audio (la foto del motor), los medios
// del evento (Media/), los discursos (Discursos/), los teleprompters (prompters.json), los
// gráficos, los carruseles y quién está en la sala. Lo del disco se lee en un hilo de
// trabajo al abrir el evento y cuando cambia la carpeta (QFileSystemWatcher); lo demás se
// toma de su dueño cuando avisa. `opciones` es un mapa {"<tipo>/<campo>": [{valor, texto}]}
// que avisa cuando cambia cualquiera de esas cosas: el formulario se enlaza a él.
// ============================================================================
#pragma once

#include <functional>
#include <map>
#include <memory>

#include <QFileSystemWatcher>
#include <QObject>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include "evento/Evento.hpp"
#include "sala/ListaPrompters.hpp"

namespace ba::app {

class AppRuntime;
class CarruselViewModel;
class ConferenceViewModel;
class EventoAbierto;
class VentanillaMotor;
namespace graficos {
class Emisor;
}

class GuionViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString abierto READ abierto NOTIFY guionChanged)
    /// {nombre, inicio, nota, duracionSeg, bloques: [{id, nombre, nota, duracionSeg,
    /// offsetSeg, cues: [{...}]}]}. Cada cue trae además `falta` (lo que le impide
    /// ejecutarse) para poder marcarlo en la lista.
    Q_PROPERTY(QVariantMap guion READ guion NOTIFY guionChanged)
    Q_PROPERTY(bool sucio READ sucio NOTIFY sucioChanged)
    Q_PROPERTY(QVariantList reparos READ reparos NOTIFY reparosChanged)
    /// La lista de chequeo manual del evento abierto (Eventos/<id>/chequeo.json):
    /// [{item, texto, hecho, nota, cuando}].
    Q_PROPERTY(QVariantList chequeo READ chequeo NOTIFY chequeoChanged)
    /// Lo que se puede poner en cada tipo de cue: [{tipo, titulo, ayuda, campos}].
    Q_PROPERTY(QVariantList tiposDeCue READ tiposDeCue CONSTANT)
    /// El idioma del evento ("" = el del operador) y los que se pueden elegir ([{value, label}]).
    Q_PROPERTY(QVariantList idiomas READ idiomas CONSTANT)
    /// Los tipos de evento con guía para prepararlos con el asistente: [{id, titulo, icono, resumen}].
    Q_PROPERTY(QVariantList escenarios READ escenarios CONSTANT)
    /// {"<tipo>/<campo>": [{valor, texto}]} (ver la cabecera). Las láminas, por carrusel:
    /// "carrusel/lamina:<carrusel>"; los parámetros de cada gráfico: "parametros:<gráfico>".
    Q_PROPERTY(QVariantMap opciones READ opciones NOTIFY opcionesChanged)
    /// La página de cada teleprompter del evento en esta máquina, para ver si un cue anda:
    /// {"<id>": url}; "" es el principal (y el de un id que no está).
    Q_PROPERTY(QVariantMap enlacesPrompter READ enlacesPrompter NOTIFY opcionesChanged)

   public:
    GuionViewModel(EventoAbierto& abierto, AppRuntime& rt, VentanillaMotor& ventanilla, ConferenceViewModel* sala,
                   graficos::Emisor* graficos, QObject* parent = nullptr);
    ~GuionViewModel() override;

    QString abierto() const;
    QVariantMap guion() const;
    bool sucio() const;
    QVariantList reparos() const;
    const std::vector<evento::Reparo>& reparosModelo() const { return reparos_; }
    void limpiarReparos();
    QVariantList chequeo() const { return chequeo_; }
    QVariantList tiposDeCue() const;
    QVariantList idiomas() const;
    QVariantList escenarios() const { return escenarios_; }
    QVariantMap opciones() const;
    QVariantMap enlacesPrompter() const;

    void setCarruseles(CarruselViewModel* c);
    /// Lo que la revisión mira del aparato (disco, destino, entradas, pantallas):
    /// [{grave, texto}]. Lo pone la app.
    void setChequeosAparato(std::function<QVariantList()> fn) { chequeosAparato_ = std::move(fn); }

    // --- Editar el guion ------------------------------------------------------------
    Q_INVOKABLE void setNombre(const QString& nombre);
    Q_INVOKABLE void setInicio(const QString& hhmm);
    Q_INVOKABLE void setFecha(const QString& aaaammdd);
    Q_INVOKABLE void setHuso(const QString& iana);
    Q_INVOKABLE void setIdioma(const QString& codigo);
    Q_INVOKABLE void setInvitados(bool si);
    Q_INVOKABLE void setPreguntas(bool si);
    Q_INVOKABLE void setAsamblea(bool si);
    Q_INVOKABLE QString agregarBloque(const QString& nombre);
    Q_INVOKABLE void renombrarBloque(int bloque, const QString& nombre);
    Q_INVOKABLE void quitarBloque(int bloque);
    Q_INVOKABLE void moverBloque(int desde, int hasta);
    /// `tipo` es el nombre del tipo ("escena", "orador"...).
    Q_INVOKABLE QString agregarCue(int bloque, const QString& tipo);
    Q_INVOKABLE void editarCue(int bloque, int cue, const QVariantMap& campos);
    Q_INVOKABLE void quitarCue(int bloque, int cue);
    /// Mueve un cue, incluso a otro bloque (arrastrar y soltar).
    Q_INVOKABLE void moverCue(int deBloque, int deCue, int aBloque, int aCue);
    /// Cambia UN valor de los parámetros de un cue de gráfico, sin pisar los otros.
    Q_INVOKABLE void setValorDeCue(int bloque, int cue, const QString& clave, const QVariant& valor);
    Q_INVOKABLE bool guardar();

    /// El arranque como instante absoluto, ISO-8601 con desfasaje
    /// ("2026-09-24T19:00:00-03:00"). Vacío si falta la fecha o la hora.
    QString cuando() const;
    /// El huso del aparato ahora mismo, en nombre IANA.
    Q_INVOKABLE static QString husoDelAparato();

    // --- La revisión previa y el chequeo ----------------------------------------------
    /// Revisión previa completa: la del modelo más lo que necesita el motor (que las
    /// escenas existan, que los archivos estén…). Se corre a pedido, no sola.
    Q_INVOKABLE void revisar();
    /// Cuántos reparos graves dejó la última revisión.
    int graves() const;
    /// Marca un ítem (o lo agrega si no existe). false si no hay evento abierto.
    Q_INVOKABLE bool marcarChequeo(const QString& item, bool hecho, const QString& nota, const QString& texto = {});

    // --- Para armar un cue ----------------------------------------------------------
    QStringList graficosDisponibles() const;
    QStringList fuentesDeGrafico() const;
    QVariantList parametrosDeGrafico(const QString& grafico) const;
    /// Las opciones de un campo (de `opciones`): [{valor, texto}]. Vacía = se escribe.
    QVariantList opcionesDeCampo(const QString& tipo, const QString& campo, const QVariantMap& datos = {}) const;
    /// La vista web para ver si un cue anda (el teleprompter), en esta máquina; "" si no tiene.
    Q_INVOKABLE QString enlaceDeCue(const QString& tipo, const QVariantMap& datos = {}) const;
    /// Los teleprompters del evento con su nombre (de la caché de prompters.json).
    const sala::ListaPrompters& listaPrompters() const { return prompters_; }
    /// El pedido para el asistente: preparar un evento de ese tipo siguiendo su guía.
    Q_INVOKABLE QString pedidoDeEscenario(const QString& id, const QString& notas, const QString& plantilla = {}) const;

    // --- Las carpetas del evento -------------------------------------------------------
    QString carpetaDeDiscursos() const;
    QString rutaDeDiscurso(const QString& rel) const;
    /// El texto de un discurso, de la caché (se lee en el fondo al abrir y cuando cambia la
    /// carpeta). `ok` false si no está.
    QString textoDeDiscurso(const QString& rel, bool* ok) const;
    /// Ruta de un medio (música o video), siempre dentro de Media/ (o Medios/ del evento).
    QString medioPath(const QString& rel) const;

   signals:
    void guionChanged();
    void sucioChanged();
    void reparosChanged();
    void chequeoChanged();
    void opcionesChanged();
    /// Se guardó el abierto (la lista y la web se ponen al día).
    void guardado();
    void notify(const QString& texto, const QString& tipo);

   private:
    void editado();
    /// Lo del disco de este evento (medios, discursos, prompters, chequeo), en el fondo.
    void leerCarpetas();
    void cambioOpciones();

    EventoAbierto& ab_;
    AppRuntime& rt_;
    VentanillaMotor& ventanilla_;
    ConferenceViewModel* sala_ = nullptr;
    graficos::Emisor* graficos_ = nullptr;
    CarruselViewModel* carruseles_ = nullptr;
    std::function<QVariantList()> chequeosAparato_;
    std::vector<evento::Reparo> reparos_;

    // La caché (ver la cabecera).
    QVariantList escenarios_;
    QVariantList chequeo_;
    nlohmann::json chequeoGuardado_ = nlohmann::json::object();
    QStringList audios_, videos_;               ///< Media/ (rutas relativas)
    QStringList discursos_;                     ///< Discursos/ (rutas relativas)
    std::map<QString, QString> textos_;         ///< el texto de cada discurso
    sala::ListaPrompters prompters_;
    mutable QVariantMap opciones_;
    mutable bool opcionesSucias_ = true;
    QThreadPool fondo_;
    int genCarpetas_ = 0;
    QFileSystemWatcher vigia_;
    QTimer juntar_;
};

} // namespace ba::app
