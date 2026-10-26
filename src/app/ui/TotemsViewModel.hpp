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
//
// Hasta cuatro conversaciones a la vez (los canales de audio 1..4 del gateway).
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

#include "registro/Totems.hpp"

namespace ba::registro {
class Conversacion;
}

namespace ba::app {

class AppRuntime;
class LectorDocumentos;
class StudioViewModel;
class VozAlLobby;

namespace audio {
class Habla;
}
namespace agent {
class Voz;
class ModelosLocales;
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
    void guardarRegistro(Conexion& c);
    void tic();

    AppRuntime& rt_;
    StudioViewModel& vm_;
    agent::Voz* voz_;
    agent::ModelosLocales* modelos_;

    std::filesystem::path carpeta_;
    std::string eventoNombre_;
    registro::Totems totems_;
    mutable std::map<std::string, Secreto> secretos_;

    std::map<QString, std::unique_ptr<Conexion>> conns_;
    std::array<std::unique_ptr<VozAlLobby>, 5> voces_; ///< por canal (1..4; el 0 no se usa)
    std::unique_ptr<audio::Habla> habla_;
    QString vozCargada_;
    bool vozLista_ = false;
    bool cargandoVoz_ = false;
    QTimer tic_;
    std::unique_ptr<LectorDocumentos> lector_;
};

} // namespace ba::app
