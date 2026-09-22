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
// Todo lo que habla con un aparato BLOQUEA (TLS, esperas): va en un hilo
// aparte y vuelve por señales. La interfaz nunca se congela por un televisor
// apagado.
// ============================================================================
#pragma once

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

  signals:
    void cambio();
    void notify(const QString& texto, const QString& tipo);

  private:
    /// Abrir el canal y mandarle la URL: el final de todos los contenidos.
    void mandarUrl(const QString& id, const QString& url, const QString& tipo, const QString& que);

    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace ba::app
