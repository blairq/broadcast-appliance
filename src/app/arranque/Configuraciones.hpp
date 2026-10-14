// ============================================================================
// src/app/arranque/Configuraciones.hpp — configuraciones con nombre, cada una con lo suyo
//
// Cada configuración es una carpeta `<datos>/Configuraciones/<nombre>/` con TODO lo
// suyo (Config/, Media/, Graficos/, Plugins/, Fotos/, Miniaturas/, Eventos/,
// Recordings/…; ver platform/Paths.hpp): nada se comparte entre configuraciones, así
// un cambio en una (un plugin, una escena) no rompe otra. De la máquina quedan
// Modelos/, Herramientas/, Agentes/, Credenciales/, Logs/ y Runtime/.
//
// CAMBIAR de configuración no copia nada: se anota la activa en
// `<datos>/configuraciones.json` y se reinicia (el launcher le pasa a la app y al
// gateway la carpeta nueva). CREAR una sí escribe sus archivos una vez:
//   - "vacia": la configuración de fábrica;
//   - "actual" u otra por su nombre: una copia (sin grabaciones, salvo que se pida);
//   - "demo:<nombre>": una de las demos que trae el programa (Demos/<nombre>);
//   - "evento:<carpeta>/<inicio|cierre|ultima>": la configuración con la que se hizo
//     un evento (su foto de Config/, ver evento/FotosConfig) y los medios de la actual.
// Cada una puede tener una descripción (configuracion.json en su carpeta).
//
// EVENTOS (docs/planes/PLAN-EVENTO-TODO.md): cada carpeta es UN evento con todo lo
// suyo (escenas, mezclador, medios, asamblea, orden del día, web, guion). En la
// interfaz se llaman «eventos»; nuevoEvento crea la carpeta y su evento en curso:
//   - "cero": de fábrica;   "actual": copia del abierto;   "<carpeta>": de otro;
//   - "plantilla:<nombre>": una demo del programa o una carpeta marcada como plantilla;
//   - "evento:<carpeta>/<foto>": la configuración con que se hizo un evento cerrado.
// Siempre SIN historial: ni grabaciones, ni eventos anteriores, ni el orden del día,
// el quórum o las votaciones de la sesión de donde sale.
// Las rutas que guardan son relativas a su carpeta (arranque/RutasPortables): se
// pueden copiar a otra máquina tal cual.
// ============================================================================
#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantMap>
#include <QVariantList>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace ba::app {

class Configuraciones : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList lista READ listaUi NOTIFY cambio)
    Q_PROPERTY(QString activa READ activaUi NOTIFY cambio)
    Q_PROPERTY(QVariantList demos READ demosUi CONSTANT)
    /// La copia que se está haciendo: {nombre, hechos, total} (bytes), o vacío.
    Q_PROPERTY(QVariantMap copia READ copia NOTIFY copiaCambio)

   public:
    using json = nlohmann::json;
    /// `reiniciar`: cierra la app y la vuelve a abrir (con la configuración que quedó anotada).
    explicit Configuraciones(std::function<QString()> reiniciar, QObject* parent = nullptr);

    /// [{nombre, activa, descripcion, creada_ms, desde, bytes}].
    json listar() const;
    std::string activa() const;
    /// Las demos que trae el programa: [{nombre, descripcion}].
    json demos() const;

    /// Crea `nombre` desde `desde` (ver arriba). "" o el error.
    std::string crear(const std::string& nombre, const std::string& desde, const std::string& descripcion,
                      bool conGrabaciones = false);
    /// Un evento nuevo: su carpeta. "cero": de fábrica, con un evento en curso nuevo;
    /// "actual", otra carpeta o "plantilla:<nombre>": COPIA TOTAL (web, guion, medios,
    /// historial, grabaciones; ver copiarTodo); "evento:<carpeta>/<foto>": la foto de
    /// ajustes de un evento cerrado. No la activa. "" o el error. (En este hilo.)
    std::string nuevoEvento(const std::string& nombre, const std::string& desde, const std::string& descripcion = {});
    /// Lo mismo en otro hilo (una copia puede tardar): progreso en `copia`; al terminar,
    /// nuevoEventoListo y `listo(error)` en el hilo principal; con `abrir`, pasa a él
    /// (reinicia). "" si arrancó, o el error de entrada.
    QString nuevoEventoAparte(const QString& nombre, const QString& desde, bool abrir,
                              std::function<void(QString)> listo);
    QVariantMap copia() const {
        QVariantMap c = copia_;
        if (!c.isEmpty() && copiaContador_) c["hechos"] = static_cast<double>(copiaContador_->load());
        return c;
    }
    /// Marcar una carpeta como plantilla (aparece en «Evento nuevo › desde plantilla»).
    std::string marcarPlantilla(const std::string& nombre, bool plantilla);
    /// Las plantillas VISIBLES, en su orden (las del programa y las carpetas marcadas):
    /// [{nombre (el que se ve), carpeta (para «plantilla:<carpeta>»), descripcion, demo,
    /// escenario, icono}].
    json plantillas() const;
    /// Todas, para administrarlas: [{id (= carpeta), nombre, descripcion, escenario, icono,
    /// visible, programa (viene con el programa), propia (carpeta del usuario), editada (copia
    /// propia de una del programa, que le gana), activa}]. Orden, nombres, descripciones y
    /// visibilidad: <datos>/plantillas.json (sirve igual para las del programa, que no se tocan).
    json plantillasAdmin() const;
    /// Cambia lo que venga: nombre (el que se ve; "" = el de la carpeta), descripcion, visible.
    std::string plantillaAjustar(const std::string& id, const json& cambios);
    /// Un lugar más arriba (delta < 0) o más abajo.
    std::string plantillaMover(const std::string& id, int delta);
    /// Para editarla: si es del programa, primero una copia propia (que le gana); la deja
    /// anotada como activa (quien llama reinicia). "" o el error.
    std::string plantillaParaEditar(const std::string& id);
    /// Saca la copia propia de una del programa (vuelve la original; a la papelera).
    std::string plantillaRestablecer(const std::string& id);
    /// Una plantilla nueva copiando el evento abierto (con todo lo suyo, sin historial).
    std::string plantillaDesdeActual(const std::string& nombre, const std::string& escenario);
    /// Una vez (marca en <datos>/migraciones.json): las carpetas que tenían VARIOS eventos
    /// adentro se separan, cada evento a su carpeta con una copia de los ajustes, escenas
    /// y medios de donde estaba (sin historial). Se queda el en curso (o el último tocado)
    /// y las jornadas automáticas. Antes respalda los Eventos/ en <datos>/Respaldos.
    /// Devuelve lo que hizo (vacío si nada). Antes de abrir el evento (StudioViewModel).
    static json separarEventos(const std::filesystem::path& datos);
    /// Deja anotada `nombre` como activa (sin reiniciar). "" o el error.
    std::string activar(const std::string& nombre);
    std::string renombrar(const std::string& de, const std::string& a);
    std::string describir(const std::string& nombre, const std::string& descripcion);
    /// A la papelera (Configuraciones/.papelera/): nunca la activa.
    std::string borrar(const std::string& nombre);

    QVariantList listaUi() const;
    QString activaUi() const { return QString::fromStdString(activa()); }
    QVariantList demosUi() const;
    Q_INVOKABLE QString crearUi(const QString& nombre, const QString& desde, const QString& descripcion);
    /// Evento nuevo y, si `abrir`, pasar a él (reinicia).
    Q_INVOKABLE QString nuevoEventoUi(const QString& nombre, const QString& desde, bool abrir);
    Q_INVOKABLE QString marcarPlantillaUi(const QString& nombre, bool plantilla);
    Q_INVOKABLE QVariantList plantillasUi() const;
    Q_INVOKABLE QVariantList plantillasAdminUi() const;
    Q_INVOKABLE QString plantillaAjustarUi(const QString& id, const QVariantMap& cambios);
    Q_INVOKABLE QString plantillaMoverUi(const QString& id, int delta);
    /// La abre para editarla (reinicia con ella).
    Q_INVOKABLE QString plantillaEditarUi(const QString& id);
    Q_INVOKABLE QString plantillaRestablecerUi(const QString& id);
    Q_INVOKABLE QString plantillaBorrarUi(const QString& id);
    Q_INVOKABLE QString plantillaDesdeActualUi(const QString& nombre, const QString& escenario);
    /// Activa y reinicia.
    Q_INVOKABLE QString cambiarUi(const QString& nombre);
    Q_INVOKABLE QString renombrarUi(const QString& de, const QString& a);
    Q_INVOKABLE QString describirUi(const QString& nombre, const QString& descripcion);
    Q_INVOKABLE QString borrarUi(const QString& nombre);
    /// Reinicia con la que esté anotada (después de activar por MCP).
    QString reiniciar() { return reiniciar_ ? reiniciar_() : QStringLiteral("no se puede reiniciar desde acá"); }

    /// Volver a medir cuánto ocupa cada evento (en otro hilo; avisa con cambio()).
    Q_INVOKABLE void medir();

   signals:
    void cambio();
    void copiaCambio();
    /// Terminó nuevoEventoAparte: `error` vacío si salió bien; `abriendo` si se reinicia con él.
    void nuevoEventoListo(const QString& nombre, const QString& error, bool abriendo);

   private:
    // Cuánto ocupa cada carpeta: se mide en otro hilo (con grabaciones grandes,
    // recorrerla en cada lectura de la lista trababa la interfaz).
    struct Tamano {
        std::uintmax_t total = 0;
        std::uintmax_t grabaciones = 0;
    };
    mutable std::mutex tamanosM_;
    std::map<std::string, Tamano> tamanos_;
    std::atomic<bool> midiendo_{false};
    bool otraVez_ = false;         ///< cambió algo mientras se medía
    bool avisandoTamanos_ = false; ///< el cambio() que sale de la medición (no vuelve a medir)
    std::filesystem::path datos() const;
    std::filesystem::path carpeta(const std::string& nombre) const;
    /// crear() sin avisar (lo llama otro hilo).
    std::string crearSinAvisar(const std::string& nombre, const std::string& desde, const std::string& descripcion,
                               bool conGrabaciones = false);
    /// El trabajo de nuevoEvento, sin avisar (se puede llamar desde otro hilo).
    std::string crearEvento(const std::string& nombre, const std::string& desde, const std::string& descripcion,
                            std::atomic<std::uintmax_t>* hechos);
    /// La carpeta de donde se copia `desde` ("" en `de` si no se copia nada). "" o el error.
    std::string origenDe(const std::string& desde, std::filesystem::path* de) const;
    QVariantMap copia_;
    std::shared_ptr<std::atomic<std::uintmax_t>> copiaContador_;
    QTimer progresoCopia_; ///< mientras copia, avisa el progreso
    static std::string nombreValido(const std::string& nombre);
    std::function<QString()> reiniciar_;
};

} // namespace ba::app
