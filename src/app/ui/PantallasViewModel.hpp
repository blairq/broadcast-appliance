// ============================================================================
// src/app/ui/PantallasViewModel.hpp — pantallas externas
//
// Un televisor en la sala, el proyector del escenario, la pantalla del hall.
// La idea es que sean TODAS lo mismo: un lugar donde mostrar algo. Hoy el
// único tipo de destino que existe es el Chromecast; mañana puede haber otro
// (una ventana en un segundo monitor, un televisor por DIAL) y la pantalla de
// esta página no tendría que cambiar.
//
// DOS COSAS SEPARADAS, A PROPOSITO:
//
//   DESTINO   dónde se muestra. Se descubre en la red (mDNS) o se agrega a
//             mano por su dirección. Lo suyo es el protocolo: encender,
//             mostrar una URL, parar, volumen.
//   CONTENIDO qué se muestra. Y acá está la parte que hace que esto crezca
//             solo: TODO lo que se proyecta es una página que ya sirve el
//             gateway. Un mensaje, un QR, la hora y el teleprompter son la
//             misma página con distintos parámetros. Agregar algo nuevo para
//             mostrar no es tocar el protocolo ni el aparato: es una entrada
//             más en el catálogo y, si hace falta, una página.
//
// POR QUE HTTP Y NO HTTPS. El gateway tiene HTTPS con certificado propio, y un
// televisor no tiene dónde aceptarlo: se planta y muestra un error. Por la red
// local va HTTP, que es lo que estos aparatos esperan.
//
// POR QUE SE ELIGE LA IP. Esta máquina puede tener seis direcciones (Docker,
// VPN, WireGuard...). Se le pasa al televisor la que está en SU misma red; si
// no, le mandaríamos una URL que él no puede alcanzar y la pantalla quedaría
// en negro sin decir por qué.
//
// LO QUE CADA PANTALLA TIENE QUE ESTAR MOSTRANDO. Un televisor de sala se
// apaga de noche, se reinicia solo con una actualización, o el router le
// cambia la dirección. Si lo que proyecta viviera solo en la memoria de la
// app, cada mañana habría que volver a mandárselo a mano. Por eso se puede
// FIJAR un contenido: queda guardado con la pantalla, y cuando el aparato
// vuelve a aparecer se le manda solo.
//
// Se guarda contra el id del servicio mDNS, no contra la IP: ese id no cambia
// cuando el DHCP le da otra dirección, que es justo el caso que hay que
// sobrevivir.
//
// CARRUSELES. Una pantalla también puede mostrar VARIAS cosas, rotando: el QR
// de la sala, un mensaje, la hora. Un carrusel es una lista de pasos, cada uno
// con su contenido y cuántos segundos dura.
//
// Lo que se le manda al televisor es UNA sola dirección (`/pantalla/carrusel/
// ?id=X`), y la lista vive en el gateway. Así cambiar un paso no obliga a
// mandarle una URL nueva —o sea, a reiniciarle la página y que parpadee—: se
// republica la lista y el televisor la toma solo.
//
// Todo lo que habla con un aparato BLOQUEA (TLS, esperas): va en un hilo
// aparte y vuelve por señales. La interfaz nunca se congela por un televisor
// apagado.
// ============================================================================
#pragma once

#include <functional>

#include <memory>

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace ba::app {

class AppRuntime;
class ConferenceViewModel;

class PantallasViewModel : public QObject {
    Q_OBJECT
    /// Los destinos: [{id, nombre, modelo, host, puerto, fija, estado,
    /// contenido, que, error, volumen, vistaHace}].
    Q_PROPERTY(QVariantList pantallas READ pantallas NOTIFY cambio)
    /// Qué se puede mostrar: [{id, titulo, ayuda, campos, disponible, porque}].
    Q_PROPERTY(QVariantList contenidos READ contenidos CONSTANT)
    Q_PROPERTY(bool buscando READ buscando NOTIFY cambio)
    /// Vacío si todo bien; si no, por qué no se puede buscar en la red.
    Q_PROPERTY(QString aviso READ aviso NOTIFY cambio)
    /// Cuántas están mostrando algo.
    Q_PROPERTY(int mostrando READ mostrando NOTIFY cambio)
    /// Los carruseles: [{id, nombre, pasos:[{tipo, datos, segundos, que}],
    /// transicion}].
    Q_PROPERTY(QVariantList carruseles READ carruseles NOTIFY carruselesCambio)

   public:
    PantallasViewModel(AppRuntime& rt, ConferenceViewModel* sala, QObject* parent = nullptr);
    ~PantallasViewModel() override;

    QVariantList pantallas() const;
    QVariantList contenidos() const;
    bool buscando() const;
    QString aviso() const;
    int mostrando() const;

    /// Busca en la red. Las que aparezcan se suman a las que ya había.
    Q_INVOKABLE void buscar();
    /// Agrega una a mano (cuando el mDNS no llega, que pasa entre VLANs).
    /// Devuelve el error, o vacío.
    Q_INVOKABLE QString agregar(const QString& nombre, const QString& host, int puerto);
    /// La saca de la lista. Si estaba mostrando algo, no se la toca: sacarla
    /// de la lista es olvidarla, no apagarla.
    Q_INVOKABLE QString olvidar(const QString& id);
    /// Le cambia el nombre. El que pone el operador gana sobre el que anuncia
    /// el aparato: "TV del hall" dice más que "Percee TV".
    Q_INVOKABLE QString renombrar(const QString& id, const QString& nombre);
    /// Fija lo que esta pantalla tiene que mostrar SIEMPRE. Se le manda ahora
    /// y cada vez que el aparato vuelva a aparecer.
    Q_INVOKABLE QString fijar(const QString& id, const QString& tipo, const QVariantMap& datos);
    /// Fija lo que ya está mostrando (el caso común: se probó y quedó bien).
    Q_INVOKABLE QString fijarLoQueMuestra(const QString& id);
    /// Deja de tener algo fijo. No apaga lo que esté mostrando.
    Q_INVOKABLE QString desfijar(const QString& id);
    /// Le manda un contenido. `datos` son los campos de ese tipo.
    Q_INVOKABLE QString mostrar(const QString& id, const QString& tipo, const QVariantMap& datos);
    /// Deja de mostrar (vuelve a su fondo de pantalla).
    Q_INVOKABLE void parar(const QString& id);
    /// Vuelve a preguntarle cómo está.
    Q_INVOKABLE void refrescar(const QString& id);
    Q_INVOKABLE void setVolumen(const QString& id, double nivel);
    Q_INVOKABLE void silenciar(const QString& id, bool si);
    /// La URL que se le mandaría: para mostrarla en la interfaz y para probar
    /// desde un navegador sin tener el televisor a mano.
    Q_INVOKABLE QString urlDe(const QString& tipo, const QVariantMap& datos, const QString& idDestino = {});

    // --- Carruseles -----------------------------------------------------------
    /// Crea uno vacío y devuelve su id.
    Q_INVOKABLE QString carruselNuevo(const QString& nombre);
    Q_INVOKABLE QString carruselBorrar(const QString& id);
    Q_INVOKABLE QString carruselRenombrar(const QString& id, const QString& nombre);
    Q_INVOKABLE QString carruselTransicion(const QString& id, const QString& transicion);
    /// Agrega un paso al final.
    Q_INVOKABLE QString pasoAgregar(const QString& carrusel, const QString& tipo, const QVariantMap& datos,
                                    int segundos);
    Q_INVOKABLE QString pasoQuitar(const QString& carrusel, int indice);
    Q_INVOKABLE QString pasoMover(const QString& carrusel, int indice, int aIndice);
    Q_INVOKABLE QString pasoSegundos(const QString& carrusel, int indice, int segundos);
    /// Lo manda a una pantalla (y lo publica en el gateway si hace falta).
    Q_INVOKABLE QString mostrarCarrusel(const QString& idPantalla, const QString& carrusel);
    /// Vuelve a publicar la lista: el televisor la toma sin reiniciarse.
    Q_INVOKABLE void publicarCarrusel(const QString& id);

    /// Publica el TALLY en el gateway: que escena esta al aire, cual armada, y
    /// que fuentes tiene cada una.
    ///
    /// Lo llama quien sabe de escenas (StudioViewModel) en cada cambio. Acá
    /// vive porque acá estan las pantallas y el cliente del gateway; esta clase
    /// no mira escenas por su cuenta.
    void publicarTally(const QString& aire, const QString& armada, const QStringList& fuentesAire,
                       const QStringList& fuentesArmadas, bool transmitiendo, bool grabando);

    // --- El canal de control --------------------------------------------------
    /// Las superficies web vivas: [{id, tipo, que, ip, desdeSeg}]. No es lo
    /// mismo que `pantallas`: esto es lo que está MOSTRÁNDOSE de verdad, dicho
    /// por la propia página. Una pantalla puede figurar «mostrando» y no tener
    /// ninguna superficie viva: eso significa que no cargó.
    Q_INVOKABLE void pedirSuperficies();
    /// Igual, pero avisando cuando llego la respuesta: lo usa MCP, que tiene
    /// que contestar con datos frescos y no con los de la ultima vez que
    /// alguien miro la pagina.
    void pedirSuperficies(std::function<void(QVariantList)> luego);
    Q_PROPERTY(QVariantList superficies READ superficies NOTIFY superficiesCambio)
    QVariantList superficies() const;
    /// Le cambia a una superficie lo que muestra SIN volver a castearla.
    /// `a` vacío = a todas las de `tipo` (o a todas, si también está vacío).
    Q_INVOKABLE void empujar(const QString& a, const QString& tipo, const QString& url);
    /// `recargar`: en vez de irse a otra direccion, que se recargue sola.
    void empujar(const QString& a, const QString& tipo, const QString& url, bool recargar,
                 std::function<void(bool, int)> luego);

    QVariantList carruseles() const;
    /// ¿Alguna pantalla está mostrando el programa? Mientras tanto la vista que
    /// lo baja al gateway no puede descansar (ver BroadcastEngine::retornoEnUso).
    bool muestraPrograma() const;

   signals:
    void cambio();
    void carruselesCambio();
    void superficiesCambio();
    void notify(const QString& texto, const QString& tipo);

   private:
    /// Resuelve los pasos de un carrusel a URLs y lo publica en el gateway.
    void publicar(const QString& id, const QString& hostDestino);
    /// Pasada de mantenimiento: a cada pantalla con algo fijo que no lo esté
    /// mostrando, se lo manda. Con espera creciente por pantalla, para no
    /// martillar a una que está apagada.
    void restaurar();
    /// Abrir el canal y mandarle la URL: el final de todos los contenidos.
    void mandarUrl(const QString& id, const QString& url, const QString& tipo, const QString& que);
    /// Una escena (no la del aire): le da un canal de escena y pide el enlace.
    QString mostrarEscena(const QString& id, const QString& escena);
    /// Suelta los canales de escena que ninguna pantalla está usando.
    void revisarCanales();

    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace ba::app
