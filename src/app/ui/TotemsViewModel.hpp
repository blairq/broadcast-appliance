// ============================================================================
// src/app/ui/TotemsViewModel.hpp — el registro de personas por tótem (Studio.totems)
//
// docs/funciones/REGISTRO-TOTEM.md. Una página web (/registro/, la sirve el
// gateway) en un televisor con cámara y micrófono, una tablet o el celular de
// quien escanea el QR: la persona pone la cara, el asistente la saluda con voz
// y le pide hablando los datos que se configuraron acá. Lo que contesta va al
// PADRÓN del evento (Asistente::datos) y queda acreditada; si la cara no se
// conocía, se la da de alta en la libreta con su foto.
//
// (Se llama «Totems» y no «Registro» porque Studio.registro ya es el registro
// de lo que pasa —los logs—: dos cosas distintas con el mismo nombre confunden
// al que lee y al asistente.)
//
// LAS PIEZAS, todas las que ya había, sin un sistema aparte:
//   - la configuración: registro/Totems (registro.json en la carpeta del evento
//     abierto) y el token de cada url en el almacén de secretos, como los links
//     de la sesión remota (security/CredentialStore);
//   - el guion: registro/Conversacion (puro, con test), uno por conexión;
//   - el oído: agent::Voz::abrirOido (el reconocedor del dictado), con la voz
//     que sube por WebRTC a BA.Audio.Registro.<k> (OperatorFeed::escucharRegistro);
//   - la boca: audio/Habla con «la voz del atendedor» (la de AtencionViewModel),
//     que baja por BA.Audio.RegistroVoz.<k> (emision/VozAlLobby) SOLO a esa
//     conexión. Mientras habla, al oído le llegan ceros: si no, se
//     transcribiría a sí mismo;
//   - la cara: la cámara del tótem sube por WebRTC (video, como el lobby) a
//     BA.Registro.<k>; sus cuadros (FrameService, clave "registro:<k>") van al
//     reconocimiento (PersonasViewModel::identificarFoto, fuera de este hilo). La
//     misma clave sirve para mirarla en la app, en un monitor o en una escena;
//   - el gateway: la API de control /api/v1/registro/* y el evento SSE
//     «registro» (ConferenceClient::registroEvento);
//   - el documento (un campo `documento`): los mismos cuadros de "registro:<k>"
//     van al lector de documentos (ui/LectorDocumentos: zxing-cpp fuera de este
//     hilo, el DNI de fábrica y los tipos de los plugins). docs/funciones/DOCUMENTOS.md.
//     Si el código del frente no sale, la conversación pide la parte de atrás y las
//     fotos enteras que manda la página van también a un modelo de visión (el local con
//     su proyector, en 127.0.0.1; si no, el propio), que lee la zona de lectura mecánica
//     (LectorDocumentos::leerDorso), una por vez por conexión.
//
// Hasta ipc::kCanalesRegistro conversaciones a la vez (los canales de audio del gateway).
// ============================================================================
#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <array>
#include <filesystem>
#include <map>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "ipc/CanalesRegistro.hpp"
#include "registro/Totems.hpp"
#include "ui/LectorDocumentos.hpp"

namespace ba::registro {
class Conversacion;
class AgenteTotem;
} // namespace ba::registro

namespace ba::app {

class AppRuntime;
class StudioViewModel;
class VozAlLobby;

namespace audio {
class Habla;
}
namespace agent {
class Voz;
class ModelosLocales;
class Roles;
struct FuenteMirada;
} // namespace agent

class TotemsViewModel : public QObject {
    Q_OBJECT
    /// Hay un evento abierto (los tótems son de un evento).
    Q_PROPERTY(bool hayEvento READ hayEvento NOTIFY cambio)
    /// El nombre del evento abierto (lo que va en {evento}).
    Q_PROPERTY(QString evento READ evento NOTIFY cambio)
    /// Los tótems: [{id, nombre, activo, titulo, subtitulo, saludo, despedida,
    /// yaRegistrado, confirmar, reconocer, campos: [...], url, urlPantalla, conectados}].
    Q_PROPERTY(QVariantList totems READ totems NOTIFY cambio)
    /// Las conexiones en vivo: [{conn, id, totem, canal, audio, ip, lugar, desdeMs,
    /// paso, quien, oido, dice, campos}].
    Q_PROPERTY(QVariantList conexiones READ conexiones NOTIFY vivoCambio)
    /// Los registrados por tótem en este evento: [{id, nombre, personaId, registro,
    /// totem, llegoMs, datos}].
    Q_PROPERTY(QVariantList registrados READ registrados NOTIFY registradosCambio)
    /// Los tipos de campo: [{id, etiqueta}].
    Q_PROPERTY(QVariantList tipos READ tipos CONSTANT)
    /// Qué falta para que el tótem hable y escuche ("" = nada): lo mismo que la atención hablada.
    Q_PROPERTY(QString hablarFalta READ hablarFalta NOTIFY cambio)
    /// La sala (el gateway) responde.
    Q_PROPERTY(bool enLinea READ enLinea NOTIFY cambio)
    /// Los tipos de documento que puede aceptar un campo `documento` (los de fábrica
    /// y los de los plugins que corren): [{id, titulo, origen, plugin?, formatos}].
    Q_PROPERTY(QVariantList tiposDocumento READ tiposDocumento NOTIFY tiposDocumentoCambio)
    /// Esta compilación trae el lector de códigos de barras (zxing-cpp).
    Q_PROPERTY(bool lectorDisponible READ lectorDisponible CONSTANT)
    /// Si el tótem puede leer el dorso cuando el código no sale, en palabras: «con el
    /// modelo local» / «con el modelo propio», o qué falta. Cambia al bajar o borrar la
    /// visión del modelo local (ModelosLocales).
    Q_PROPERTY(QString dorso READ dorso NOTIFY dorsoCambio)
    Q_PROPERTY(bool dorsoDisponible READ dorsoDisponible NOTIFY dorsoCambio)
    // --- La atención remota (docs/funciones/REGISTRO-TOTEM.md, «Atención remota») ---
    /// El link de la página de atención del evento (/registro-atiende/#t=…): quien lo abre ve
    /// todas las conversaciones, mira y escucha una, la atiende y completa datos. "" sin evento.
    Q_PROPERTY(QString urlAtencion READ urlAtencion NOTIFY cambio)
    /// Quienes tienen abierta la página de atención: [{aconn, nombre, lugar, ip, atiende}]
    /// (`atiende`: el conn de la conversación que atiende, o "").
    Q_PROPERTY(QVariantList atienden READ atienden NOTIFY vivoCambio)
    // --- El modelo que entiende (el rol «registro»; registro/AgenteRegistro) ---
    /// Con qué entienden los tótems lo que dice la gente, en palabras: «con el modelo local»,
    /// «con OpenRouter (los datos salen de esta máquina)», o por qué el guion solo.
    Q_PROPERTY(QString agente READ agente NOTIFY cambio)
    /// El modelo del rol «registro» es de la nube (o el propio): la voz y los datos salen.
    Q_PROPERTY(bool agenteAfuera READ agenteAfuera NOTIFY cambio)

   public:
    TotemsViewModel(AppRuntime& rt, StudioViewModel& vm, agent::Voz* voz, agent::ModelosLocales* modelos,
                    QObject* parent = nullptr);
    ~TotemsViewModel() override;

    bool hayEvento() const { return !carpeta_.empty(); }
    QString evento() const { return QString::fromStdString(eventoNombre_); }
    QVariantList totems() const;
    QVariantList conexiones() const;
    QVariantList registrados() const;
    QVariantList tipos() const;
    QString hablarFalta() const;
    bool enLinea() const;
    QVariantList tiposDocumento() const;
    bool lectorDisponible() const;
    QString dorso() const;
    bool dorsoDisponible() const { return lector_ && lector_->dorsoDisponible(); }
    /// El lector de documentos (lo usa también el MCP: documento_leer, documento_tipos).
    LectorDocumentos* lector() const { return lector_.get(); }

    // --- Configurar -------------------------------------------------------------------
    /// Un tótem nuevo con valores de fábrica. Devuelve su id, o "" (y avisa).
    Q_INVOKABLE QString crear(const QString& nombre);
    /// Cambia las claves que vengan (las de `totems`; `campos` reemplaza la lista). "" o el error.
    Q_INVOKABLE QString cambiar(const QString& id, const QVariantMap& cambios);
    Q_INVOKABLE bool borrar(const QString& id);
    /// Los campos, de a uno (para el editor). "" o el error.
    Q_INVOKABLE QString agregarCampo(const QString& id, const QString& tipo);
    Q_INVOKABLE QString quitarCampo(const QString& id, int indice);
    Q_INVOKABLE QString moverCampo(const QString& id, int desde, int hasta);
    Q_INVOKABLE QString cambiarCampo(const QString& id, int indice, const QVariantMap& cambios);
    /// La url del tótem (con su token en el fragmento) y la de la TV (?pantalla=1:
    /// muestra el QR de la otra). "" si no hay ese tótem.
    Q_INVOKABLE QString url(const QString& id) const;
    Q_INVOKABLE QString urlPantalla(const QString& id) const;
    /// Token nuevo: la url vieja deja de andar. Devuelve la nueva.
    Q_INVOKABLE QString renovar(const QString& id);
    /// Corta una conexión en vivo.
    Q_INVOKABLE void cortar(const QString& conn);

    // --- La atención remota (también desde la app y el MCP) --------------------------------
    QString urlAtencion() const;
    QVariantList atienden() const;
    /// Token nuevo para el link de atención: el viejo deja de andar (y se corta a quien entró
    /// con él). Devuelve el nuevo.
    Q_INVOKABLE QString renovarAtencion();
    /// Atender una conversación desde la app (sin voz): el asistente se calla y deja de
    /// preguntar; la pantalla del tótem dice «Te atiende una persona del equipo». "" o el error.
    Q_INVOKABLE QString atender(const QString& conn);
    /// Terminar la atención: `como` = "asistente" (sigue el guion), "guardar" (guarda ya) o
    /// "cancelar". "" o el error.
    Q_INVOKABLE QString terminarAtencion(const QString& conn, const QString& como);
    /// Completar o corregir un dato (atendida o no), como si lo hubiera tipeado. Vacío = borrarlo.
    Q_INVOKABLE QString ponerDato(const QString& conn, const QString& clave, const QString& valor);
    /// Lo mismo, diciendo quién (`quien`, para la pantalla del tótem) y desde qué página de
    /// atención (`aconn`; "" = la app o el MCP, que pueden siempre).
    std::string atenderConn(const QString& conn, const QString& quien, const QString& aconn);
    std::string terminarConn(const QString& conn, const QString& como, const QString& aconn);
    std::string ponerDatoConn(const QString& conn, const QString& clave, const QString& valor, const QString& aconn);
    /// {url, atienden: [...]} para el MCP.
    nlohmann::json atencionJson() const;

    // --- El modelo que entiende (región propia al final de TotemsViewModel.cpp) -------------
    /// Los roles (para el del tótem, «registro»): lo engancha StudioViewModel.
    void setRoles(agent::Roles* roles);
    QString agente() const;
    bool agenteAfuera() const;

    // --- Para el MCP, el catálogo de pantallas y los QR ---------------------------------
    /// El tótem como JSON (con url y urlPantalla), o null.
    nlohmann::json totemJson(const QString& id) const;
    /// {totems: [...]}
    nlohmann::json listarJson() const;
    /// {conexiones: [...], hablarFalta, enLinea}
    nlohmann::json estadoJson() const;
    nlohmann::json registradosJson() const;
    /// Crea con lo que venga en `j` (nombre y, si se quiere, el resto). "" o el error; `id` el creado.
    std::string crearJson(const nlohmann::json& j, std::string& id);
    std::string cambiarJson(const QString& id, const nlohmann::json& cambios);
    /// [{id, nombre, url, urlPantalla}] de los activos.
    QVariantList paraElegir() const;

   signals:
    void cambio();
    void vivoCambio();
    void registradosCambio();
    void tiposDocumentoCambio();
    void dorsoCambio();
    void notify(const QString& texto, const QString& tipo);

   private:
    struct Conexion;
    struct Secreto {
        std::string token, sal;
    };

    void recargar();
    std::string guardarYPublicar();
    void publicar();
    void reconocerConexiones();
    Secreto secreto(const std::string& id, bool renovar = false) const;
    std::string nombreSecreto(const std::string& id) const;
    std::string origen() const;
    /// Mandarle a una conexión (o a todas las de un tótem, con `porTotem`).
    void enviar(const QString& a, const nlohmann::json& msg, bool porTotem = false);
    void mandarPantalla(Conexion& c);
    QVariantMap filaDe(const Conexion& c) const;

    void alEvento(const nlohmann::json& ev);
    Conexion& conexion(const QString& conn, const QString& totem);
    void soltar(const QString& conn);
    void abrirAudio(Conexion& c);
    void cerrarAudio(Conexion& c);
    void cargarVoz();
    void mirarCara(Conexion& c);
    void abrirOido(Conexion& c);
    void alCara(const QString& conn, const QByteArray& jpeg, const QString& personaId, const QString& nombre,
                bool hayCara, bool sirve, const QString& sugerencia);
    void empezar(Conexion& c, const QString& personaId, const QString& nombre);
    void alOido(const QString& conn, const QString& texto);
    void alToque(Conexion& c, const nlohmann::json& ev);
    /// Hace lo que dice la conversación: decir, mostrar, pedir, guardar, terminar, y
    /// `foto`: buscar una toma de frente en la cámara cuando termine de decirlo.
    void aplicar(Conexion& c, const std::string& decir, bool campos, const std::string& pedir, bool guardar,
                 bool terminar, bool foto = false);
    void decir(Conexion& c, const std::string& texto);
    /// El paso del documento: busca el código en el último cuadro de la cámara (más
    /// seguido que la cara) y, si lo lee, se lo pasa a la conversación.
    void mirarDocumento(Conexion& c);
    /// El código que leyó la página (BarcodeDetector) o la foto entera que mandó (respaldo).
    void alCodigoDePagina(Conexion& c, const nlohmann::json& ev);
    void alCuadroDePagina(Conexion& c);
    /// Lo leído (de la cámara, del navegador o de la foto), al paso «documento» de la conexión.
    void alLeerDocumento(const QString& conn, LectorDocumentos::Resultado r);
    /// Lo que leyó el modelo propio del dorso (la zona de lectura mecánica).
    void alLeerDorso(const QString& conn, LectorDocumentos::Resultado r);
    void guardarRegistro(Conexion& c);
    void tic();

    // La atención remota (región propia al final de TotemsViewModel.cpp).
    struct Atiende {
        QString aconn, nombre, ip, lugar;
    };
    void conectarAtencion();
    bool alEventoAtencion(const nlohmann::json& ev);
    void publicarAtencion();
    /// El asistente se calla en el acto (lo que estaba diciendo y lo que faltaba sintetizar).
    void callarAsistente(Conexion& c);
    /// Deja de estar atendida: la pantalla del tótem y el gateway (su voz ya no baja ahí).
    void liberarAtencion(Conexion& c);
    nlohmann::json conversacionesJson() const;
    /// Las conversaciones a las páginas de atención ("" = a todas).
    void enviarAtencion(const QString& aconn = {});

    // El modelo que entiende (región propia al final de TotemsViewModel.cpp).
    /// Si los tótems lo van a usar con el modelo local, que el llama-server vaya cargando.
    void prepararModelo();
    void soltarModelo();
    /// El servidor y el prompt del rol «registro». false y `falta` si no hay.
    bool fuenteAgente(agent::FuenteMirada& f, std::string& prompt, std::string& falta) const;
    /// Uno nuevo para la conversación de `c` (o ninguno: el guion solo).
    void crearAgente(Conexion& c);
    /// Lo oído, al modelo. false = lo contesta el guion (como sin modelo).
    bool alOidoAgente(Conexion& c, const std::string& texto);
    /// Solo la voz (sin la pantalla): de a una frase, mientras el modelo sigue escribiendo.
    void hablar(Conexion& c, const std::string& texto);

    AppRuntime& rt_;
    StudioViewModel& vm_;
    agent::Voz* voz_;
    agent::ModelosLocales* modelos_;

    std::filesystem::path carpeta_;
    std::string eventoNombre_;
    registro::Totems totems_;
    mutable std::map<std::string, Secreto> secretos_;

    std::map<QString, std::unique_ptr<Conexion>> conns_;
    std::array<std::unique_ptr<VozAlLobby>, ba::ipc::kCanalesRegistro + 1> voces_; ///< por canal (1..n; el 0 no se usa)
    std::unique_ptr<audio::Habla> habla_;
    QString vozCargada_;
    bool vozLista_ = false;
    bool cargandoVoz_ = false;
    QTimer tic_;
    std::unique_ptr<LectorDocumentos> lector_;
    // La atención remota.
    std::map<QString, Atiende> atienden_;
    QTimer atencionTimer_; ///< junta los cambios: a las páginas de atención, a lo sumo cada 150 ms
    // El modelo que entiende.
    agent::Roles* roles_ = nullptr;
    quint16 puertoLocal_ = 0;  ///< el llama-server del programa, listo
    bool pidiendoLocal_ = false;
    QString faltaLocal_;       ///< por qué no hay modelo local
};

} // namespace ba::app
