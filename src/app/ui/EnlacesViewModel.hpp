// ============================================================================
// src/app/ui/EnlacesViewModel.hpp — todos los enlaces del aparato, en un lugar
//
// Cada servicio web que el aparato le ofrece a alguien (los invitados, el
// público, el teleprompter, los rótulos desde el celular, la botonera, la
// sesión remota, el túnel...) tenía su enlace en SU página, con su propio
// diálogo de QR. Para pasar un enlace había que acordarse dónde estaba. Acá
// están todos: con su estado (el LED), el botón para activarlo si se puede, el
// enlace de la red local y el de afuera (el túnel), para copiar, abrir o
// mostrar en QR.
//
// El catálogo se declara UNA vez (en el .cpp) y lo leen la página Enlaces y el
// MCP (enlaces_listar / enlace_activar). Los enlaces los da la sala
// (EnlacesSala) y son los mismos que dan las otras páginas: acá no se arma
// ninguno a mano. Los que son por persona (cada integrante, cada televisor,
// cada invitación) no se listan uno por uno: la fila lleva a la página donde
// se dan.
//
// No conoce a las clases de cada servicio: StudioViewModel le pasa, en
// `Fuentes`, cómo preguntar y cómo prender cada cosa.
//
// Los servicios que piden la aprobación de una persona por aparato (la tablet
// de la botonera, el control web, Companion Satellite, el lobby de invitados)
// traen en su fila los pedidos que esperan (`solicitudes`) y se aprueban o
// rechazan acá mismo (`resolver`), sin ir a otra página: lo hace la persona que
// está en la consola. El asistente no aprueba por acá (enlaces_listar solo los
// muestra); lo suyo pasa por sus herramientas, con la aprobación del operador.
// ============================================================================
#pragma once

#include <functional>
#include <map>
#include <string>

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace ba::app {

namespace sala {
class EnlacesSala;
class Direcciones;
}
struct ServicioEnlace; ///< una fila del catálogo (EnlacesViewModel.cpp)

class EnlacesViewModel : public QObject {
    Q_OBJECT
    /// [{id, grupo, nombre, para, led ("ok"|"aviso"|"apagado"|"error"), estado,
    ///   activable, activo, local, remoto, nota, pagina, porPersona, accion,
    ///   aprueba (pide aprobación por aparato), pendientes (cuántos esperan),
    ///   solicitudes [{id, nombre, ip, ms, perfil, detalle}]}].
    Q_PROPERTY(QVariantList servicios READ servicios NOTIFY cambio)
    /// Hay túnel andando: los enlaces de afuera valen.
    Q_PROPERTY(bool hayTunel READ hayTunel NOTIFY cambio)
    /// El catálogo, que no cambia: [{id, grupo, indice (en `servicios`)}]. La página
    /// arma las filas con esto (no se rehacen con cada cambio de estado: se pierde
    /// lo que la persona estaba escribiendo o eligiendo) y lee el estado de `servicios`.
    Q_PROPERTY(QVariantList catalogo READ catalogoUi CONSTANT)
    /// Los grupos, en orden.
    Q_PROPERTY(QStringList grupos READ grupos CONSTANT)
    /// Los pedidos que esperan aprobación, por servicio: {id: {solicitudes, perfilModo,
    /// perfiles, perfilAyuda, textoAprobar}}. Avisa solo cuando cambian (no con cada
    /// cambio de estado), para que la fila no pierda el perfil que se está eligiendo.
    Q_PROPERTY(QVariantMap aprobaciones READ aprobaciones NOTIFY aprobacionesCambio)

   public:
    /// Un servicio que pide la aprobación de una persona por aparato.
    struct Aprobaciones {
        /// Los que esperan: [{id, nombre, ip, ms (cuándo pidió, 0 = no se sabe),
        /// perfil (el que se propone: id en "lista", texto en "texto"), detalle?}].
        std::function<QVariantList()> pendientes;
        /// Aprueba o rechaza `solicitud`, con `perfil` (si el servicio lo usa). "" o el error.
        std::function<QString(const QString& solicitud, bool aprobar, const QString& perfil)> resolver;
        /// Cómo se elige el perfil al aprobar: "" (no hay), "lista" (de `perfiles`) o "texto".
        QString perfilModo;
        /// En "lista": [{id, nombre}].
        std::function<QVariantList()> perfiles;
        /// En "texto": la ayuda del campo.
        QString perfilAyuda;
        /// El texto del botón de aprobar («Aprobar», «Admitir», «Permitir»).
        QString textoAprobar = "Aprobar";
        /// El estado de la fila mientras esperan (%1 = cuántos, si son varios).
        QString unoEspera = "Un pedido esperando: aprobalo acá abajo";
        QString variosEsperan = "%1 pedidos esperando: aprobalos acá abajo";
    };

    /// Lo que el modelo necesita saber y poder hacer, sin conocer a cada clase.
    struct Fuentes {
        std::function<sala::EnlacesSala*()> enlaces;
        std::function<bool()> salaEnLinea;
        std::function<bool()> salaCerrada;
        std::function<void(bool)> cerrarSala;
        /// {activo, via, estado ("listo"|"conectando"|"error"|"apagado"), url, texto}
        std::function<QVariantMap()> tunel;
        /// "" si arrancó; si no, el motivo.
        std::function<QString(bool)> activarTunel;
        /// {activo, url, aviso}
        std::function<QVariantMap()> apiRemota;
        std::function<void(bool)> activarApiRemota;
        /// {activo, puerto, error}
        std::function<QVariantMap()> satellite;
        std::function<QString(bool)> activarSatellite;
        /// Las direcciones de los servicios (la única fuente de los enlaces).
        std::function<const sala::Direcciones*()> direcciones;
        /// {hay (la web generada), evento (el abierto)}
        std::function<QVariantMap()> webEvento;
        /// El control web (/control/): {conectados, aparatos, tunel}.
        std::function<QVariantMap()> controlWeb;
        /// Genera el código de un uso y muestra el QR para emparejar un aparato.
        std::function<void()> emparejarControlWeb;
        /// id del servicio → sus pedidos de aprobación (los que los tienen).
        std::map<std::string, Aprobaciones> aprobaciones;
    };

    explicit EnlacesViewModel(Fuentes f, QObject* parent = nullptr);

    QVariantList servicios() const;
    bool hayTunel() const;
    QVariantList catalogoUi() const;
    QStringList grupos() const;
    QVariantMap aprobaciones() const { return aprobaciones_; }

    /// Vuelve a pedirle a la sala los enlaces (los de afuera, si hay túnel).
    Q_INVOKABLE void refrescar();
    /// Prende o apaga el servicio `id` (los que tienen `activable`). "" o el motivo.
    Q_INVOKABLE QString activar(const QString& id, bool activo);
    /// La acción propia de una fila (`accion`).
    Q_INVOKABLE void accion(const QString& id);
    /// Una PERSONA aprueba o rechaza, desde la página, un pedido de `servicio`
    /// (`perfil`: el elegido, si el servicio lo usa). "" o el error.
    Q_INVOKABLE QString resolver(const QString& servicio, const QString& solicitud, bool aprobar,
                                 const QString& perfil = QString());

    /// Algo cambió en un servicio (lo llama quien conoce las señales).
    void avisarCambio();

   signals:
    void cambio();
    void aprobacionesCambio();

   private:
    QVariantMap fila(const ServicioEnlace& s) const;
    void pedir(const std::string& id, bool remoto);
    /// Vuelve a leer los pedidos de cada servicio; avisa si cambiaron.
    void releerAprobaciones();

    Fuentes f_;
    /// Lo que contestó la sala: id → {local, remoto, nota}.
    std::map<std::string, QVariantMap> urls_;
    bool tuvoTunel_ = false;
    bool tuvoSala_ = false;
    QVariantMap aprobaciones_;
};

} // namespace ba::app
