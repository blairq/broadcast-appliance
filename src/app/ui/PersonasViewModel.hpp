// ============================================================================
// src/app/ui/PersonasViewModel.hpp — la libreta de personas, para la interfaz
//
// La misma libreta que usan las herramientas MCP (`rostro_*`): una sola verdad.
// Lo que se edita acá lo ve el asistente en el acto, y al revés.
//
// PARA QUE SIRVE LA PAGINA, que no es obvio: el reconocimiento se da de alta
// solo cuando alguien DICE quién es. Esta página es donde se dice, se corrige
// lo que quedó mal, se le suman caras a quien cuesta reconocer, y se borra a
// quien no tiene que estar. Sin eso el sistema acumula desconocidos.
//
// LA FOTO ES PARA MIRAR. El reconocimiento usa los vectores; la foto está para
// que el operador confirme de un vistazo que la ficha es de quien cree. Un
// nombre sin cara no se verifica.
// ============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#if BA_HAVE_ROSTROS
#include "ui/Reconocedor.hpp"
#endif

class QImage;

typedef struct _GstSample GstSample; // gst/gst.h

namespace ba::rostros {
struct Cara;
class Motor;
class Personas;
} // namespace ba::rostros

namespace ba::app {

class AppRuntime;
class StudioViewModel;

class PersonasViewModel : public QObject {
    Q_OBJECT
    /// [{id, nombre, cargo, nota, foto (url), caras, vistaSeg}]
    Q_PROPERTY(QVariantList personas READ personas NOTIFY cambio)
    /// Si el reconocimiento está disponible (modelos vendoreados presentes).
    Q_PROPERTY(bool disponible READ disponible CONSTANT)
    /// Lo que no se pudo hacer, para mostrarlo donde se pidió.
    Q_PROPERTY(QString aviso READ aviso NOTIFY cambio)
    /// El interruptor de ANOTAR SOLA a toda cara nueva que pase por `feedAuto`.
    /// Ver el comentario largo en el .cpp: lo que hace y lo que NO hace.
    Q_PROPERTY(bool autoalta READ autoalta WRITE setAutoalta NOTIFY autoaltaCambio)
    /// Qué cámara mira el anotado automático. La página le pone la que se está
    /// viendo; si nadie la pone, el programa.
    Q_PROPERTY(QString feedAuto READ feedAuto WRITE setFeedAuto NOTIFY autoaltaCambio)
    /// El interruptor de la PUERTA: reconoce a quien entra al lugar por
    /// `feedPuerta` y lo acredita en el padrón del evento abierto. Es la
    /// contracara del lobby —el mismo reconocimiento apuntado al mundo físico—
    /// y es lo que hace que «¿quiénes están?» sepa de las dos mitades.
    Q_PROPERTY(bool puertaAuto READ puertaAuto WRITE setPuertaAuto NOTIFY autoaltaCambio)
    /// Qué cámara mira la puerta.
    Q_PROPERTY(QString feedPuerta READ feedPuerta WRITE setFeedPuerta NOTIFY autoaltaCambio)
    /// El mismo interruptor, pero mirando EL LOBBY: cada posición de espera,
    /// por turno. Es el caso real de un evento —la gente llega antes de entrar,
    /// y ahí es cuando hay tiempo de saber quién es—.
    Q_PROPERTY(bool lobbyAuto READ lobbyAuto WRITE setLobbyAuto NOTIFY autoaltaCambio)
    /// La atención a quien se conecta está prendida: el lobby se mira siempre,
    /// esté o no el interruptor de arriba (ui/AtencionViewModel lo pone).
    Q_PROPERTY(bool lobbyPorAtencion READ lobbyPorAtencion NOTIFY autoaltaCambio)
    /// Si de verdad se está mirando el lobby (el interruptor o la atención).
    Q_PROPERTY(bool lobbyMirando READ lobbyMirando NOTIFY autoaltaCambio)
    /// ALTA AUTOMÁTICA: a todo el que dio su nombre (lo escribió al entrar o se
    /// lo corrigió quien lo atendió; ver sala::nombreDado) y tiene una cara que
    /// sirve, se lo anota en Personas con ese nombre y su cargo, sin depender
    /// de que el asistente se acuerde de `rostro_guardar`. Se guarda.
    Q_PROPERTY(bool altaAutomatica READ altaAutomatica WRITE setAltaAutomatica NOTIFY autoaltaCambio)
    /// Quién hay en cada posición del lobby: {"2": {id, nombre, cargo, foto,
    /// parecido, caras, sirve, sugerencia}}. La clave es el número de posición
    /// EN TEXTO, porque un QVariantMap no tiene claves numéricas.
    Q_PROPERTY(QVariantMap enLobby READ enLobby NOTIFY enLobbyCambio)
    /// Cuántas fichas están sin nombre esperando que alguien las complete.
    Q_PROPERTY(int sinNombre READ sinNombre NOTIFY cambio)
    /// Lo último que se vio: [{id, nombre, parecido, x, y, ancho, alto, cw, ch}].
    /// Lo llena `mirar()` cuando el otro hilo contesta.
    Q_PROPERTY(QVariantList vistos READ vistos NOTIFY vistosCambio)

   public:
    PersonasViewModel(AppRuntime& rt, StudioViewModel& vm, QObject* parent = nullptr);
    ~PersonasViewModel() override;

    QVariantList personas() const;
    bool disponible() const;
    QString aviso() const;
    bool autoalta() const;
    void setAutoalta(bool activo);
    QString feedAuto() const;
    void setFeedAuto(const QString& feed);
    int sinNombre() const;
    QVariantList vistos() const;
    bool lobbyAuto() const;
    void setLobbyAuto(bool activo);
    bool puertaAuto() const;
    void setPuertaAuto(bool activo);
    QString feedPuerta() const;
    void setFeedPuerta(const QString& feed);
    QVariantMap enLobby() const;
    bool lobbyPorAtencion() const;
    /// Lo llama la atención al prenderse o apagarse (no es un interruptor del operador).
    void setLobbyPorAtencion(bool activo);
    bool lobbyMirando() const;
    bool altaAutomatica() const;
    void setAltaAutomatica(bool activo);
    /// El parecido mínimo para dejar entrar sin preguntar (rostros.admitirSolo).
    float umbralEntrarSolo() const;
    /// Compara la cara más grande de una foto (la que manda el celular de un
    /// integrante remoto) con la ficha `personaId`. `luego(parecido, sirve,
    /// error)` en el hilo de la interfaz; parecido 0 si no hay cara o ficha.
    void compararFoto(const QByteArray& jpeg, const QString& personaId,
                      std::function<void(float parecido, bool sirve, const QString& error)> luego);
    /// Quién es la cara más grande de una foto (la que manda un tótem de
    /// registro), contra la libreta. `personaId` vacío si no se la conoce (o no
    /// hay cara); `sirve` dice si la cara alcanza para darla de alta.
    struct Identificado {
        QString personaId, nombre;
        float parecido = 0.0f;
        bool hayCara = false;
        bool sirve = false;
        QString sugerencia; ///< si no sirve, qué pedirle («acercate un poco», «mirá a la cámara»)
        QString sugerenciaEvento; ///< la misma en el idioma del evento (la que oye o lee la persona)
    };
    /// `luego(quien, error)` en el hilo de la interfaz. El trabajo pesado va al
    /// hilo del reconocimiento (Reconocedor), como compararFoto.
    void identificarFoto(const QByteArray& jpeg, std::function<void(const Identificado& quien, const QString& error)> luego);
    /// Alta automática desde una foto: ficha nueva con ese nombre y cargo, su
    /// cara y su foto. `luego(id, error)` (id vacío si no se pudo: sin cara, o
    /// la cara no sirve, o ya es de otra ficha con nombre).
    void altaConFoto(const QByteArray& jpeg, const QString& nombre, const QString& cargo,
                     std::function<void(const QString& id, const QString& error)> luego);

    Q_INVOKABLE void refrescar();
    /// Crea o corrige. `id` vacío crea. Devuelve el id, o vacío si falló.
    Q_INVOKABLE QString guardar(const QString& id, const QString& nombre, const QString& cargo,
                                const QString& nota);
    Q_INVOKABLE bool borrar(const QString& id);

    /// Le suma la cara que se vea en `feed` (clave de video).
    ///
    /// NO DEVUELVE NADA, y no es un descuido: mirar una cara pasa por otro
    /// hilo (ver Reconocedor.hpp), así que cuando esta función termina todavía
    /// no hay nada que contestar. Lo que salió sale por `notify`.
    ///
    /// `nuevaFoto` reemplaza la foto de la ficha por la de ahora.
    Q_INVOKABLE void sumarCara(const QString& id, const QString& feed, bool nuevaFoto = false);

    /// Pide mirar quién hay en `feed`. La respuesta llega en `vistos`, más
    /// tarde: el reconocimiento no corre en el hilo de la interfaz.
    Q_INVOKABLE void mirar(const QString& feed);
    /// Una cámara del estudio que se analiza o se sigue (PTZ): lleva su
    /// tarjeta «Rostros» mientras `usar` (ver Seguidor).
    void usarCamara(const QString& entrada, bool usar);

    /// Las claves de video que se pueden mirar, con su etiqueta.
    Q_INVOKABLE QVariantList fuentes() const;
    /// El nombre legible de una clave de video ("Programa", "Invitado 2").
    Q_INVOKABLE QString etiquetaDe(const QString& feed) const;

#if BA_HAVE_ROSTROS
    /// Las caras de un feed, miradas FUERA del hilo de la interfaz. `luego` se
    /// llama en el hilo de la interfaz, con el cuadro que se miró (para la
    /// foto) y el motivo si algo falló.
    ///
    /// :param descartable: true para una mirada periódica que se puede saltear
    ///        si ya hay otra en vuelo; false para lo que alguien espera.
    using Analisis = std::function<void(std::vector<Reconocedor::Visto>, const QImage&, const QString&)>;
    void pedirAnalisis(const QString& feed, bool descartable, Analisis luego);
    /// Una cámara del estudio recién pedida tarda unos cuadros en tener imagen
    /// (su tarjeta empieza a bajar con el primer pedido): se reintenta ~2 s.
    void reintentarAnalisis(const QString& feed, Analisis luego, int intento);
    /// Lo que vio la tarjeta «Rostros» de una entrada (su mensaje del bus, ya en
    /// el hilo de la interfaz): contesta a quien lo esté esperando.
    void alRostros(const QString& entrada, const std::string& json, GstSample* cuadro);

    /// La libreta EN USO, para quien tenga que trabajar sobre la misma y no
    /// sobre otra copia del archivo.
    ///
    /// POR QUE SE PRESTAN. Las herramientas MCP tenían su propia libreta leída
    /// del mismo JSON. Mientras nadie escribía, las dos decían lo mismo y
    /// parecía que no pasaba nada. Con el anotado automático escribiendo, la
    /// copia de MCP queda vieja y el primer `rostro_guardar` pisa el archivo
    /// con una libreta sin las fichas que se acababan de anotar. Una sola
    /// libreta viva; el archivo es dónde se guarda, no dónde vive.
    ba::rostros::Personas& libreta();
    /// Guarda la libreta y avisa a la vista. Lo llama quien la tocó.
    bool persistir();
    /// Dónde van las fotos de las fichas.
    QString carpetaFotos() const;
#endif

   signals:
    void cambio();
    void autoaltaCambio();
    void vistosCambio();
    void enLobbyCambio();
    void notify(const QString& texto, const QString& tipo);

   private:
    /// Pone o saca las tarjetas «Rostros» en las cámaras del estudio que se
    /// usan (anotado, puerta, la que mira la página). Ver VideoGrafos.
    void sincronizarRostros();
    /// El motor de la página: mira `feed`, devuelve quién hay, y si
    /// `registrar` anota sola a la que no conoce. Lo usan la vista (sin
    /// registrar, salvo que el interruptor esté puesto en esa misma cámara) y
    /// la ronda del anotado automático (registrando siempre).
    /// :param uid: a quién pertenece esa cámara, cuando es del lobby. Viaja
    ///        CON el pedido y no en un campo compartido: el reconocimiento
    ///        contesta más tarde y, para entonces, un campo compartido ya fue
    ///        pisado por la vuelta siguiente —y el resultado de mirar a uno se
    ///        le atribuye al otro—.
    void analizar(const QString& feed, bool registrar, const QString& uid = {});
#if BA_HAVE_ROSTROS
    /// Lo que se hace con las caras ya descritas: buscarlas en la libreta,
    /// anotarlas si corresponde y armar la lista para la vista. Todo esto sí
    /// va en el hilo de la interfaz: es la libreta, y solo ella la toca.
    void resolver(const QString& feed, const QString& uid, std::vector<Reconocedor::Visto> caras,
                  const QImage& img, bool registrar);
    /// La próxima posición del lobby que toca mirar, o "" si no hay ninguna.
    /// Va rotando: una por vuelta, así el costo no crece con la cantidad de
    /// gente esperando.
    /// :param uid,lugar: a quién le toca y en qué posición está.
    QString proximaDelLobby(QString& uid, int& lugar);
    /// Si esa persona está en la lista de auto-aceptar del evento abierto, la
    /// hace pasar y lo avisa. Ver `evento::Invitado`.
    /// Deja constancia de que alguien llegó al lugar, si está en el padrón.
    void acreditarEnLaPuerta(const QString& personaId, const QString& nombre);
    void dejarPasarSiCorresponde(const QString& uid, const QString& personaId, const QString& nombre,
                                 float parecido, bool sirve);
    /// Una cara que no cayó en ninguna ficha. Devuelve el id de la ficha nueva
    /// cuando la vio las veces suficientes como para creerle, o vacío si
    /// todavía la está mirando.
    QString anotarDesconocida(const std::vector<float>& v, const QImage& img, const ba::rostros::Cara& c,
                              std::int64_t ahora);
    /// Alta automática en el lobby: si la ficha de `uid` no tiene nombre (la
    /// anotó sola el reconocimiento) y esa persona dio el suyo, se lo pone.
    void altaSiDioSuNombre(const QString& uid, const QString& fichaId, bool sirve);
    /// Prende o apaga la ronda según lo que haya que mirar.
    void revisarRonda();
#endif
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace ba::app
