// ============================================================================
// src/app/ui/EventoViewModel.hpp — el guion del evento, para la interfaz
//
// Le da a QML el guion (`evento.Evento`) y el recorrido en vivo
// (`evento.Guion`), y traduce cada cue a lo que hay que hacerle al sistema.
//
// El modelo de abajo no sabe ejecutar nada a propósito; acá es donde se cierra
// esa costura. Lo importante es que la traducción sea HONESTA: un cue de un
// tipo que todavía no sabemos ejecutar no se hace el que sí — lo dice, queda
// registrado, y el guion sigue. En vivo, un cue que falla en silencio es peor
// que uno que avisa.
//
// EL ENSAYO es la misma máquina con la ejecución apagada: se recorre el guion
// igual, se ven los tiempos, pero no se toca lo que sale al aire.
// ============================================================================
#pragma once

#include <memory>

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include "evento/Evento.hpp"
#include "evento/EventoStore.hpp"

namespace ba::app {

class AppRuntime;
class ConferenceViewModel;
namespace graficos { class Emisor; }

class EventoViewModel : public QObject {
    Q_OBJECT
    // --- La lista de eventos --------------------------------------------------
    Q_PROPERTY(QVariantList eventos READ eventos NOTIFY listaChanged)
    Q_PROPERTY(QString abierto READ abierto NOTIFY guionChanged)

    // --- El guion abierto -----------------------------------------------------
    /// {nombre, inicio, nota, duracionSeg, bloques: [{id, nombre, nota,
    /// duracionSeg, offsetSeg, cues: [{...}]}]}. Cada cue trae ademas `falta`
    /// (lo que le impide ejecutarse) para poder marcarlo en la lista.
    Q_PROPERTY(QVariantMap guion READ guion NOTIFY guionChanged)
    Q_PROPERTY(bool sucio READ sucio NOTIFY sucioChanged)

    // --- El recorrido en vivo -------------------------------------------------
    /// "parado" | "corriendo" | "pausado" | "terminado"
    Q_PROPERTY(QString estado READ estado NOTIFY correChanged)
    Q_PROPERTY(bool ensayo READ ensayo WRITE setEnsayo NOTIFY correChanged)
    /// El guion esta corriendo DE VERDAD (no en ensayo). Con esto prendido,
    /// cualquier cosa que se apriete por error sale al aire: la interfaz avisa
    /// antes de lo peligroso y el asistente no puede tocar la configuracion
    /// que reinicia el gateway.
    Q_PROPERTY(bool enVivo READ enVivo NOTIFY correChanged)
    Q_PROPERTY(QVariantMap actual READ actual NOTIFY correChanged)
    Q_PROPERTY(QVariantMap siguiente READ siguiente NOTIFY correChanged)
    /// {desvioSeg, enCueSeg, restanteSeg, cueNro, cuesTotal}: el reloj, que
    /// cambia cada segundo (aparte para no redibujar el guion entero).
    Q_PROPERTY(QVariantMap reloj READ reloj NOTIFY relojChanged)
    /// Lo ultimo que se ejecuto o se intento, para la vista de conduccion.
    Q_PROPERTY(QVariantList bitacora READ bitacora NOTIFY bitacoraChanged)

    // --- Revision previa ------------------------------------------------------
    Q_PROPERTY(QVariantList reparos READ reparos NOTIFY reparosChanged)

  public:
    EventoViewModel(AppRuntime& rt, ConferenceViewModel* sala,
                    graficos::Emisor* graficos, QObject* parent = nullptr);
    ~EventoViewModel() override;

    QVariantList eventos() const;
    QString abierto() const { return abierto_; }
    QVariantMap guion() const;
    bool sucio() const { return sucio_; }
    QString estado() const;
    bool ensayo() const { return ensayo_; }
    bool enVivo() const;
    QVariantMap actual() const;
    QVariantMap siguiente() const;
    QVariantMap reloj() const;
    QVariantList bitacora() const { return bitacora_; }
    QVariantList reparos() const;

    // --- La lista -------------------------------------------------------------
    Q_INVOKABLE void refrescar();
    Q_INVOKABLE QString crear(const QString& nombre);
    Q_INVOKABLE bool abrir(const QString& carpeta);
    Q_INVOKABLE void cerrar();
    Q_INVOKABLE bool borrar(const QString& carpeta);

    // --- Editar el guion ------------------------------------------------------
    Q_INVOKABLE void setNombre(const QString& nombre);
    Q_INVOKABLE void setInicio(const QString& hhmm);
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
    Q_INVOKABLE bool guardar();

    /// Lo que se puede poner en cada tipo de cue: para armar el formulario sin
    /// que QML tenga que saberse la lista de memoria.
    Q_INVOKABLE QVariantList tiposDeCue() const;

    // --- Cues de grafico ------------------------------------------------------
    /// Los graficos guardados, por nombre.
    Q_INVOKABLE QStringList graficosDisponibles() const;
    /// Las fuentes «Grafico animado» que hay en las escenas, por nombre.
    Q_INVOKABLE QStringList fuentesDeGrafico() const;
    /// Los parametros que declara ese grafico, para armar el formulario del cue.
    Q_INVOKABLE QVariantList parametrosDeGrafico(const QString& grafico) const;
    /// Cambia UN valor de los parametros de un cue de grafico, sin pisar los
    /// otros (que es lo que pasaria mandando `datos` entero).
    Q_INVOKABLE void setValorDeCue(int bloque, int cue, const QString& clave, const QVariant& valor);

    // --- Correr ---------------------------------------------------------------
    Q_INVOKABLE void arrancar();
    Q_INVOKABLE void avanzar();
    Q_INVOKABLE void retroceder();
    Q_INVOKABLE void irA(int bloque, int cue);
    Q_INVOKABLE void pausar();
    Q_INVOKABLE void reanudar();
    Q_INVOKABLE void parar();
    void setEnsayo(bool on);

    /// Ensayo acelerado: recorre el guion ENTERO de una, sin esperar las
    /// duraciones y sin tocar nada, y dice de cada cue si se podria ejecutar.
    /// A diferencia de `revisar`, que mira el guion quieto, esto camina el
    /// recorrido de verdad: encuentra los bloques que quedan colgados, el
    /// orden real y los cues que nadie sabe ejecutar todavia.
    Q_INVOKABLE void ensayoAcelerado();

    /// Lo que realmente paso, para el evento siguiente: por bloque y por cue,
    /// cuanto estaba previsto y cuanto duro de verdad.
    ///
    /// Se arma del recorrido que el guion fue guardando, asi que solo tiene
    /// sentido despues de correrlo (o de ensayarlo). Vacio si no se corrio.
    ///
    /// {previstoSeg, realSeg, desvioSeg, bloques: [{nombre, previstoSeg,
    ///  realSeg, desvioSeg, cues: [{nro, texto, tipo, previstoSeg, realSeg,
    ///  desvioSeg}]}]}
    Q_INVOKABLE QVariantMap informe() const;
    /// Guarda el informe en la carpeta del evento, con la fecha en el nombre.
    /// Devuelve el archivo escrito, o vacio si no se pudo.
    Q_INVOKABLE QString guardarInforme();

    /// Revision previa completa: la del modelo mas lo que necesita el motor
    /// (que las escenas existan, que los archivos esten, que el orador este
    /// conectado). Se corre a pedido, no sola.
    Q_INVOKABLE void revisar();

  signals:
    void listaChanged();
    void guionChanged();
    void sucioChanged();
    void correChanged();
    void relojChanged();
    void bitacoraChanged();
    void reparosChanged();
    void notify(const QString& texto, const QString& tipo);

  private:
    /// Ejecuta el cue donde esta parado el guion. En ensayo no toca nada.
    void ejecutarActual();
    void anotar(const QString& texto, const QString& tipo);
    void marcarSucio();
    evento::Cue* cuePtr(int bloque, int cue);
    const evento::Cue* cuePtrConst(int bloque, int cue) const;
    /// Ruta de un discurso, siempre DENTRO de la carpeta del evento: un
    /// guion escrito por otro no puede hacer que el programa lea cualquier
    /// archivo del disco.
    QString discursoPath(const QString& rel) const;
    /// Ruta de un medio (musica o video), siempre dentro de Medios/.
    QString medioPath(const QString& rel) const;
    /// Ruta de `rel` dentro de `sub` en la carpeta del evento, o vacia si se
    /// sale de ahi.
    QString dentroDelEvento(const QString& sub, const QString& rel) const;
    static QVariantMap cueAMap(const evento::Cue& c, int bloque, int cue);

    AppRuntime& rt_;
    ConferenceViewModel* sala_ = nullptr;
    graficos::Emisor* graficos_ = nullptr;
    evento::EventoStore store_;
    evento::Evento e_;
    std::unique_ptr<evento::Guion> guionCorriendo_;
    QString abierto_;
    bool sucio_ = false;
    bool ensayo_ = false;
    QVariantList bitacora_;
    std::vector<evento::Reparo> reparos_;
    QTimer reloj_;
};

} // namespace ba::app
