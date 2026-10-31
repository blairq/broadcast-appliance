// ============================================================================
// src/app/ui/EscenasViewModel.hpp — el área de las escenas: armarlas y cortarlas (F3-03, partición)
//
// Sale de StudioViewModel: los mandos sobre las escenas, sobre sus modelos (el runtime,
// que tiene las escenas y las guarda; el conmutador de la salida activa, que las pone al
// aire; escenas::AjustesEscenas, Config/escenas.json):
//
//   - crear, copiar, renombrar, proteger, ordenar y llevar a una carpeta (la lista de la
//     pestaña Escenas y el árbol por carpetas de la tarjeta);
//   - el bus de preview y el TAKE de la salida activa (armar, take, take con otra
//     transición, la T-bar, cortar directo), la transición con la que entra cada escena y
//     el audio que sigue al video (AFV);
//   - la repetición: guardar los últimos segundos y armarlos en la escena «Repetición».
//
// EL MOTOR ES DEL PLANO DE CONTROL (F3-03, docs/arquitectura/05-motor-del-plano.md): lo
// que se hace se le PIDE por la ventanilla (emision/VentanillaMotor: la orden corre en el
// dueño, con el turno, y el error vuelve como `notify`) y lo que se muestra se LEE de la
// foto del motor (las escenas, sus carpetas y protecciones, la armada y el AFV de la
// activa, las transiciones de entrada), con avisos cuando cambia (listaCambio,
// escenaCambio, afvCambio, transicionesCambio). Con la interfaz trabada no se pierde nada:
// lo pedido corre igual y la foto llega cuando vuelve.
//
// La escena al aire y la armada (currentScene, escenaArmada) siguen en la raíz: las mira
// toda la interfaz. En QML: Studio.escenas.
// ============================================================================
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <string>

namespace ba::app {
class AppRuntime;
class SalidasViewModel;
class SourcesModel;
class VentanillaMotor;
} // namespace ba::app
namespace ba::app::escenas {
class AjustesEscenas;
class Conmutador;
} // namespace ba::app::escenas

namespace ba::app {

class EscenasViewModel : public QObject {
    Q_OBJECT
    /// El botón de cada escena del editor corta directo (si no, la arma en preview).
    Q_PROPERTY(bool corteDirectoEditor READ corteDirectoEditor WRITE setCorteDirectoEditor NOTIFY ajustesCambio)
    /// Las fuentes con AFV en la salida activa, para enlazar en la vista (afv() es una
    /// llamada: no se reevalúa y el botón no se enteraba de un cambio hecho en otra vista o
    /// por MCP).
    Q_PROPERTY(QStringList afvFuentes READ afvFuentes NOTIFY afvCambio)
    /// Las escenas protegidas (para enlazar: «¿está protegida la que edito?»).
    Q_PROPERTY(QStringList protegidas READ protegidas NOTIFY listaCambio)
    /// {escena: {tipo, ms}}: con qué transición entra cada una (vacía = la global).
    Q_PROPERTY(QVariantMap transicionesEntrada READ transicionesEntrada NOTIFY transicionesCambio)
   public:
    struct Partes {
        AppRuntime& rt;
        VentanillaMotor& ventanilla; ///< pedir y leer la foto del motor
        SalidasViewModel& salidas;   ///< la activa (y los conmutadores, para las órdenes)
        SourcesModel& fuentes;       ///< el editor de fuentes (sigue a la escena que se toca)
    };
    explicit EscenasViewModel(Partes p, QObject* parent = nullptr);
    ~EscenasViewModel() override;

    bool corteDirectoEditor() const;
    void setCorteDirectoEditor(bool si);
    QStringList afvFuentes() const;
    QStringList protegidas() const;
    QVariantMap transicionesEntrada() const;

    /// Las escenas AGRUPADAS POR CARPETA, en el orden que puso el operador:
    /// `[{carpeta: "", escenas: ["a", "b"]}, {carpeta: "Bloque 1", ...}]`.
    ///
    /// Esto se arma acá y no en QML porque desde QML no se puede recorrer un
    /// QAbstractListModel. Devuelve SOLO la agrupación, sin decir cuál está al
    /// aire ni cuál armada: esas dos salen de `currentScene` y `escenaArmada`,
    /// que ya avisan cuando cambian. Metiéndolas acá habría que reconstruir la
    /// lista entera en cada corte, y la lista no cambió.
    Q_INVOKABLE QVariantList escenasPorCarpeta() const;
    Q_INVOKABLE void addScene(const QString& name);
    Q_INVOKABLE void removeScene(const QString& name);
    Q_INVOKABLE void renameScene(const QString& from, const QString& to);
    /// Copia una escena entera con sus fuentes. Devuelve el nombre que le
    /// va a quedar (le agrega un numero si el que se pidio ya existia), o vacio si
    /// no se puede pedir; si después falla, lo dice `notify`.
    Q_INVOKABLE QString copyScene(const QString& from, const QString& to = {});
    Q_INVOKABLE void setSceneProtected(const QString& name, bool protegida);
    Q_INVOKABLE bool sceneProtected(const QString& name) const;
    /// Mueve la escena a una carpeta ("" la saca). Solo ordena la lista.
    Q_INVOKABLE void setSceneFolder(const QString& name, const QString& folder);
    /// Carpetas que ya existen (para ofrecerlas al mover una escena).
    Q_INVOKABLE QStringList sceneFolders() const;
    /// Reordena: deja `name` justo antes de `before` (vacio = al final), en la
    /// carpeta de `before`.
    Q_INVOKABLE void moveScene(const QString& name, const QString& before);
    /// Escena que muestra el lienzo del editor ("" = ninguna). Se renderiza
    /// aunque no este al aire, asi una escena vacia se ve vacia.
    Q_INVOKABLE void setScenePreview(const QString& name);

    // --- Bus de preview y TAKE ------------------------------------------------
    // Una mesa de switcher tiene DOS buses: lo que esta al aire (programa) y lo
    // que va a salir (preview). El operador arma el proximo plano, lo MIRA, y
    // recien ahi hace TAKE. Sin eso, cada cambio de plano es a ciegas y en
    // vivo: lo que esta roto se descubre cuando ya salio.
    //
    // `takeScene` sigue estando y sigue yendo directo al aire: hay momentos en
    // los que hay que cortar YA y pedir dos pasos seria peor.
    Q_INVOKABLE void takeScene(const QString& name);
    /// Arma una escena en el bus de preview (NO la pone al aire).
    Q_INVOKABLE void armarEscena(const QString& name);
    /// Pone al aire la escena armada, con la transicion configurada.
    ///
    /// Y deja armada la que SALIO del aire, como cualquier mesa: despues de un
    /// take, el bus de preview queda con el plano anterior, listo para volver.
    Q_INVOKABLE void take();
    /// AUTO con otra transición solo esta vez («cut» = CUT). `ms` < 0: la duración global.
    Q_INVOKABLE void takeCon(const QString& tipo, int ms);
    /// T-bar hacia la escena armada: `t` de 0 a 1; `soltar` termina (pasada la mitad, queda al aire).
    Q_INVOKABLE void tbar(double t, bool soltar);
    /// La transición con la que ENTRA una escena (vacío = la global). {tipo, ms}.
    Q_INVOKABLE QVariantMap transicionEntrada(const QString& escena) const;
    /// La pide; "" (si después falla, lo dice `notify`).
    Q_INVOKABLE QString setTransicionEntrada(const QString& escena, const QString& tipo, int ms);
    /// Audio que sigue al video (AFV): la fuente sube cuando su escena entra al aire y baja cuando sale.
    Q_INVOKABLE bool afv(const QString& fuente) const;
    Q_INVOKABLE void setAfv(const QString& fuente, bool sigue);

    /// «Repetir»: guarda los últimos segundos y, cuando el clip está escrito, lo arma
    /// en la escena «Repetición» (lista para el take). "" (lo que pase después lo dice
    /// `notify`).
    Q_INVOKABLE QString repetirUi();
    /// Pone `archivo` en la escena «Repetición» (la crea si falta) y la deja armada en la
    /// `salida` ("" = la por omisión); `listo` con "" o el error, en este hilo.
    void armarRepeticion(const QString& archivo, const std::string& salida, std::function<void(const QString&)> listo);
    /// Espera (hasta `ms`) a que haya un clip nuevo de la `salida` distinto de `antes`
    /// y llama a `listo` con él ("" si no llegó). Pregunta por la ventanilla.
    void esperarReplay(const std::string& antes, int ms, std::function<void(const QString&)> listo,
                       const std::string& salida = {});

   signals:
    void ajustesCambio();
    void afvCambio();
    void transicionesCambio();
    /// La LISTA de escenas cambió (se agregó, borró, renombró o cambió de carpeta o protección).
    void listaCambio();
    /// Cambió algo de lo que muestra la escena (sus fuentes, su carpeta, su orden).
    void escenaCambio();
    /// Para el operador (ya traducido).
    void notify(const QString& texto, const QString& nivel);

   private:
    QString escenaAlAire() const;
    /// Nombre, carpeta y protección de cada escena, para ver si la lista cambió.
    QStringList firmaLista() const;
    /// Lo que cambió en la foto del motor (ver arriba).
    void alCambiar(unsigned partes);
    /// Pide algo que devuelve "" o el error (y `alVolver`, si salió bien).
    void pedir(const char* que, std::function<std::string(AppRuntime&)> orden, const QString& nivelError = "error",
               std::function<void()> alVolver = {});
    /// Pide al conmutador de la activa (el de la por omisión si se fue) algo que devuelve ""
    /// o el error.
    void pedirAlConmutador(const char* que, std::function<std::string(escenas::Conmutador&)> orden,
                           const QString& nivelError = "error");

    Partes p_;
    std::unique_ptr<escenas::AjustesEscenas> ajustes_; ///< Config/escenas.json (modelo)
    /// Lo que se avisó la última vez (para avisar solo cuando cambia).
    QStringList listaAntes_, afvAntes_;
    QVariantMap transicionesAntes_;
};

} // namespace ba::app
