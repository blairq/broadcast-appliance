// ============================================================================
// src/app/ui/LineaViewModel.hpp — la línea de tiempo en la interfaz (L4 y L5)
//
// El plan: docs/planes/PLAN-LINEA-DE-TIEMPO.md, 3.5 A (la Línea multipista), B (la
// escaleta compacta, con su modo Storyboard), C (la conducción) y D (el teclado).
// `Studio.linea` en QML.
//
// NO ES OTRO MODELO DE LA LÍNEA. Lee lo que ya hay por la API pública de
// EventoViewModel (la línea, la corrida, `lineaEstado`, las acciones `linea*` que
// usa también el MCP) y lo ordena para la vista con evento/LineaVista (puro). Todo
// tiempo sale del resolvedor: el QML solo dibuja y le avisa a esto qué tocó la persona.
//
// CUÁNDO SE RECALCULA. Cuando el evento avisa (cambió la línea, la corrida o pasó un
// segundo corriendo) y, mientras alguien mira (`mirando`), una vez por segundo para
// que la línea de «ahora» avance. Sin nadie mirando solo se publica el estado en el
// bus (`linea/<salida>/estado`, el feedback de la Botonera y Companion).
//
// EL PRESUPUESTO DE MINIATURAS (3.5 A y la memoria «CPU ociosa»): al aire, el programa
// de la salida; el próximo, su PVW (ya existen, no cuestan nada más); las dos escenas
// que siguen, vistas vivas chicas («mini:<escena>» en FrameService, 320 de ancho, ~5
// cuadros por segundo) que se sueltan a los 10 s de no hacer falta; el resto, una foto
// cada 20 s, solo de las que se ven (cada caja avisa con `mirarFoto`).
//
// LA EDICIÓN SIN QUE SALGA (Silent Edition de Millumin): arrastrar en la Línea no
// toca el plan; se junta en `pendientes` (operaciones de linea_editar) y lo que se ve
// es la línea como quedaría. Recién `confirmar` la aplica (con su revisión para
// deshacer). Lo emitido no se arrastra: la caja dice por qué.
// ============================================================================
#pragma once

#include <QImage>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include "evento/LineaVista.hpp"

namespace ba::app {

class BroadcastEngine;
class EventoViewModel;
class FrameService;
class LineaProductor;
namespace control {
class Acciones;
class BusEventos;
} // namespace control

class LineaViewModel : public QObject {
    Q_OBJECT
    /// La vista está a la vista (la página del guion): recalcula cada segundo y pide miniaturas.
    Q_PROPERTY(bool mirando READ mirando WRITE setMirando NOTIFY mirandoChanged)
    /// La Línea multipista se ve (si no, no se arma `multipista`).
    Q_PROPERTY(bool mirandoLinea READ mirandoLinea WRITE setMirandoLinea NOTIFY mirandoChanged)
    /// Las salidas de la línea y la que se mira (y maneja el teclado: Ctrl+1…4).
    Q_PROPERTY(QStringList salidas READ salidas NOTIFY salidaChanged)
    Q_PROPERTY(QString salida READ salida WRITE setSalida NOTIFY salidaChanged)
    /// parado, corriendo, pausado o terminado (el de la corrida).
    Q_PROPERTY(QString estado READ estado NOTIFY vistaChanged)
    Q_PROPERTY(bool corriendo READ corriendo NOTIFY vistaChanged)
    /// La escaleta compacta de la salida, por bloque (ver linea::escaleta).
    Q_PROPERTY(QVariantList escaleta READ escaleta NOTIFY vistaChanged)
    /// El Storyboard: lo que viene (al aire, próximo y futuro), en fila.
    Q_PROPERTY(QVariantList storyboard READ storyboard NOTIFY vistaChanged)
    /// La conducción y la barra de abajo: al aire, próximo, HOLD, entra solo en,
    /// over/under, próximo hard start, bloque, transcurrido, hora (ver linea::conduccion).
    Q_PROPERTY(QVariantMap conduccion READ conduccion NOTIFY vistaChanged)
    /// El estante de adlibs (cue cart): [{id, texto, tipo, color, tecla, esPaso}].
    Q_PROPERTY(QVariantList estante READ estante NOTIFY vistaChanged)
    /// Los atajos de la línea que valen en la página: [{accion, tecla}] (los de 3.5 D,
    /// menos los que la persona puso en otra cosa o reasignó en Botonera → Atajos).
    Q_PROPERTY(QVariantList teclas READ teclas NOTIFY teclasChanged)
    /// La Línea multipista en la ventana (ver linea::multipista).
    Q_PROPERTY(QVariantMap multipista READ multipista NOTIFY multipistaChanged)
    /// Cuánto tiempo entra en el ancho (10 s a 4 h) y si la ventana sigue a «ahora».
    Q_PROPERTY(qint64 zoomMs READ zoomMs NOTIFY multipistaChanged)
    Q_PROPERTY(bool siguiendoAhora READ siguiendoAhora NOTIFY multipistaChanged)
    /// Los cambios sin confirmar, en palabras: [{texto}].
    Q_PROPERTY(QVariantList pendientes READ pendientes NOTIFY pendientesChanged)
    /// Las fotos de las escenas sin vista viva (escena → imagen) y cuántos segundos tienen.
    Q_PROPERTY(QVariantMap fotos READ fotos NOTIFY fotosChanged)
    Q_PROPERTY(QVariantMap edadFotos READ edadFotos NOTIFY vistaChanged)
    /// El productor (ui/LineaProductor, L9): las propuestas para llegar a horario.
    Q_PROPERTY(QObject* productor READ productorQml CONSTANT)

   public:
    LineaViewModel(EventoViewModel& evento, BroadcastEngine& engine, FrameService* frames, control::Acciones* acciones,
                   control::BusEventos* bus, QObject* parent = nullptr);
    ~LineaViewModel() override;

    bool mirando() const { return mirando_; }
    void setMirando(bool on);
    bool mirandoLinea() const { return mirandoLinea_; }
    void setMirandoLinea(bool on);
    QStringList salidas() const;
    QString salida() const { return QString::fromStdString(salidaEfectiva()); }
    void setSalida(const QString& s);
    QString estado() const;
    bool corriendo() const;
    QVariantList escaleta() const { return escaleta_; }
    QVariantList storyboard() const { return storyboard_; }
    QVariantMap conduccion() const { return conduccion_; }
    QVariantList estante() const { return estante_; }
    QVariantList teclas() const { return teclas_; }
    QVariantMap multipista() const { return multipista_; }
    qint64 zoomMs() const { return zoomMs_; }
    bool siguiendoAhora() const { return corrimientoMs_ == 0; }
    QVariantList pendientes() const { return pendientesUi_; }
    QVariantMap fotos() const { return fotos_; }
    QVariantMap edadFotos() const { return edadFotos_; }
    QObject* productorQml() const;

    // --- Vivo (por la salida que se mira): "" o por qué no ------------------------------
    Q_INVOKABLE QString go();
    Q_INVOKABLE QString hold();
    Q_INVOKABLE QString siguiente();
    Q_INVOKABLE QString anterior();
    Q_INVOKABLE QString saltar();
    Q_INVOKABLE QString repetir();
    Q_INVOKABLE QString irA(const QString& paso);
    /// Dispara un adlib: una pieza manual del paso al aire o un ítem del estante (un
    /// paso del estante va de próximo).
    Q_INVOKABLE QString adlib(const QString& id);
    Q_INVOKABLE QString insertar(const QString& item, bool alAire);
    /// Un atajo de `teclas` (go, hold, anterior, siguiente, saltar, salida:N, estante:<id>).
    Q_INVOKABLE QString atajo(const QString& accion);

    // --- La ventana de la Línea ------------------------------------------------------
    /// 1 = el bloque al aire, 2 = el evento entero, 3 = 5 min, 4 = 1 min.
    Q_INVOKABLE void zoom(int nivel);
    /// Ctrl+rueda: el ancho por `factor` (<1 acerca), dentro de 10 s y 4 h.
    Q_INVOKABLE void acercar(double factor);
    /// Corre la ventana una fracción del ancho (rueda o arrastre de la regla).
    Q_INVOKABLE void correr(double fraccion);
    /// Vuelve a seguir «ahora» (fijo a un tercio).
    Q_INVOKABLE void alAhora();

    // --- Editar el futuro (pendiente hasta confirmar): "" o por qué no ------------------
    /// Cambia el largo de un paso en `dx` (fracción del ancho).
    Q_INVOKABLE QString estirar(const QString& paso, double dx);
    /// Lleva un paso a donde se soltó (`x`, fracción del ancho): detrás del que arranca antes.
    Q_INVOKABLE QString mover(const QString& paso, double x);
    /// Corre una pieza dentro de su paso en `dx` (fracción del ancho).
    Q_INVOKABLE QString moverPieza(const QString& pieza, double dx);
    Q_INVOKABLE QString confirmar();
    Q_INVOKABLE void descartar();

    /// Una caja con foto entra (o sale) de la pantalla: solo se fotografía lo que se ve.
    Q_INVOKABLE void mirarFoto(const QString& escena, bool mirar);

   signals:
    void mirandoChanged();
    void salidaChanged();
    void vistaChanged();
    void teclasChanged();
    void multipistaChanged();
    void pendientesChanged();
    void fotosChanged();

   private:
    std::string salidaEfectiva() const;
    /// Junta los avisos del evento en un solo recálculo (en la próxima vuelta del bucle).
    void pedirRecalculo();
    void recalcular();
    /// El estado de cada salida al bus (`linea/<salida>/estado`): el feedback de las superficies.
    void publicarBus(const nlohmann::json& estado);
    /// Las vistas vivas y las fotos según el presupuesto.
    void miniaturas(const std::map<std::string, evento::linea::FuenteMini>& presupuesto, int64_t ahora);
    void tomarFoto(const std::string& escena);
    void rehacerTeclas();
    /// Prueba `ops` más lo pendiente sobre el plan; si anda, lo suma a lo pendiente.
    QString sumarPendiente(const nlohmann::json& op);
    /// El paso ya salió o está al aire: no se edita (y por qué).
    bool intocable(const std::string& paso, std::string* porque) const;
    QString resultado(const std::string& error);

    EventoViewModel& ev_;
    BroadcastEngine& engine_;
    FrameService* frames_ = nullptr;
    control::Acciones* acciones_ = nullptr;
    control::BusEventos* bus_ = nullptr;
    std::unique_ptr<LineaProductor> productor_;

    bool mirando_ = false;
    bool mirandoLinea_ = false;
    std::string salida_; ///< "" = la por omisión de la línea
    QTimer segundo_;     ///< mientras se mira: la línea de «ahora» avanza
    bool recalculoPedido_ = false;

    nlohmann::json escaletaJ_, conduccionJ_, estanteJ_, multipistaJ_;
    QVariantList escaleta_, storyboard_, estante_, teclas_, pendientesUi_;
    QVariantMap conduccion_, multipista_, fotos_, edadFotos_;
    QString estado_ = QStringLiteral("parado");

    // La ventana de la Línea.
    int64_t zoomMs_ = 10 * 60 * 1000;
    int64_t corrimientoMs_ = 0;
    evento::linea::Ventana ventana_;
    /// La línea que se ve: el plan (o lo que corre) con lo pendiente aplicado.
    evento::linea::Linea lineaVista_;
    evento::linea::Resolucion resolucionVista_;

    nlohmann::json pendientes_ = nlohmann::json::array();

    // Las miniaturas: las vistas vivas pedidas (escena → la última vez que hicieron
    // falta), las cajas con foto a la vista (cuántas) y cuándo se sacó cada foto.
    std::map<std::string, int64_t> vivas_;
    std::map<std::string, int> fotosVisibles_;
    std::map<std::string, int64_t> fotoMs_;
    bool fotoEnCurso_ = false;
    static constexpr int64_t kSoltarVivaMs = 10'000;
    static constexpr int64_t kFotoCadaMs = 20'000;
};

} // namespace ba::app
