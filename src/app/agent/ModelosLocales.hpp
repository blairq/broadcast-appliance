// ============================================================================
// src/app/agent/ModelosLocales.hpp — el modelo chico que corre en la máquina
//
// QUE ES. El modelo reactivo (docs/modelo-local.md): una frase corta de cabina
// entra y sale la herramienta que la atiende —o `derivar`, y el pedido va
// tal cual al asistente grande—. Y, de a poco, texto corto que tiene que salir
// al toque. Corre acá: la ida y vuelta a un modelo remoto se come el tiempo.
//
// QUE HACE ESTA CLASE.
//   * El catálogo (datos/modelos-catalogo.json, viaja en el binario): los
//     modelos publicados en Hugging Face para ESTA plataforma.
//   * Bajarlos a la carpeta portable (Modelos/), fijando el commit del repo y
//     verificando cada archivo: sha256 en los grandes (LFS) y el sha1 de blob
//     de git en el resto. Nada se usa sin verificar, y una descarga a medias
//     nunca pisa un modelo que andaba (se baja aparte y se cambia al final).
//   * Lanzar el motor y hablarle: `ba-modelo` en la Mac (MLX, con la cabeza
//     MTP si el repo la trae; src/modelo-mac). En Linux y Windows el motor
//     es llama.cpp y todavía no está: se dice así, no se finge.
//
// POR QUE UN PROCESO APARTE. El motor tiene su propia memoria (gigas) y su
// propio hilo de GPU; si se cae, que se caiga él y no la app al aire. Le
// habla por stdin/stdout, una línea JSON por mensaje (ver main.swift).
//
// EL PROMPT NO SE ARMA ACA. Viaja con el modelo (prompt.json): es el mismo
// del entrenamiento, y con otras fichas sería otro modelo. Su huella se
// muestra; compararla con la del inventario actual del MCP (si cambió, el
// modelo quedó viejo y sigue contestando a otra cosa) queda pendiente: la
// calcula tools/lib_prompt.py y hay que portarla igual, byte a byte.
// ============================================================================
#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

class QProcess;

namespace ba::app::agent {

/// Un archivo de un repo de Hugging Face, como lo lista su API (?blobs=true).
struct ArchivoHf {
    std::string nombre;
    std::uint64_t tamano = 0;
    std::string sha256; ///< el de LFS (archivos grandes); vacío si no es LFS
    std::string blob;   ///< sha1 de blob de git (los chicos)
    std::string url;    ///< si no es de un repo: de dónde se baja (con sha256)
};

/// Lo que dice la API de un repo: su commit y sus archivos.
struct RepoHf {
    std::string commit;
    std::vector<ArchivoHf> archivos;
    bool privado = false;
};

/// Lee la respuesta de /api/models/<repo>?blobs=true. Vacío si no se entiende.
RepoHf leerRepoHf(const std::string& cuerpo);
/// sha1 de blob de git: sha1("blob <tamaño>\0" + contenido), en hex.
std::string shaBlobGit(const std::filesystem::path& archivo);
/// La carpeta de un repo dentro de Modelos/: "BlairQ/x" -> "BlairQ__x".
std::string carpetaDeRepo(const std::string& repo);
/// La plataforma como la nombra el catálogo: mac-arm64, linux-x64, win-x64.
std::string plataformaCatalogo();

class ModelosLocales : public QObject {
    Q_OBJECT
    /// Los modelos del catálogo para esta plataforma, con `instalado` y `activo`.
    Q_PROPERTY(QVariantList catalogo READ catalogo NOTIFY cambio)
    /// El repo cargado (o cargándose); vacío si ninguno.
    Q_PROPERTY(QString activo READ activo NOTIFY cambio)
    /// apagado | cargando | listo | error
    Q_PROPERTY(QString estado READ estado NOTIFY cambio)
    Q_PROPERTY(QString detalle READ detalle NOTIFY cambio)
    /// Si hay motor para esta plataforma (en la Mac: bin/64bit/ba-modelo).
    Q_PROPERTY(bool hayMotor READ hayMotor CONSTANT)
    Q_PROPERTY(QString motor READ motor CONSTANT)
    Q_PROPERTY(bool descargando READ descargando NOTIFY descargaCambio)
    Q_PROPERTY(double progreso READ progreso NOTIFY descargaCambio)
    Q_PROPERTY(QString textoDescarga READ textoDescarga NOTIFY descargaCambio)
    /// Lo que informó el motor al quedar listo: tiempos, MTP, frases de prueba.
    Q_PROPERTY(QVariantMap info READ info NOTIFY cambio)
    /// El dial de derivar (se suma al logit de `derivar`): + deriva más.
    Q_PROPERTY(double sesgo READ sesgo WRITE setSesgo NOTIFY cambio)
    /// La cabeza MTP: no | auto | q8 | fp16 (se aplica al próximo arranque).
    Q_PROPERTY(QString mtp READ mtp WRITE setMtp NOTIFY cambio)
    /// Arrancar el modelo activo al abrir la app.
    Q_PROPERTY(bool alArrancar READ alArrancar WRITE setAlArrancar NOTIFY cambio)
    /// Las últimas pruebas (frase, herramienta, prob, ms), la más nueva primero.
    Q_PROPERTY(QVariantList pruebas READ pruebas NOTIFY pruebasCambio)

   public:
    using json = nlohmann::json;
    /// Respuesta del motor a un pedido (o {"error": ...}).
    using Respuesta = std::function<void(const json&)>;

    /// :param raizDatos: la carpeta portable (los modelos van a Modelos/).
    /// :param dirBinarios: donde está ba-modelo (el de ba-app).
    ModelosLocales(std::filesystem::path raizDatos, QString dirBinarios, QObject* parent = nullptr);
    ~ModelosLocales() override;

    QVariantList catalogo() const;
    QString activo() const { return activo_; }
    QString estado() const { return estado_; }
    QString detalle() const { return detalle_; }
    bool hayMotor() const;
    QString motor() const;
    bool descargando() const { return descargando_; }
    double progreso() const { return progreso_; }
    QString textoDescarga() const { return textoDescarga_; }
    QVariantMap info() const { return info_; }
    double sesgo() const { return sesgo_; }
    void setSesgo(double s);
    QString mtp() const { return mtp_; }
    void setMtp(const QString& m);
    bool alArrancar() const { return alArrancar_; }
    void setAlArrancar(bool on);
    QVariantList pruebas() const { return pruebas_; }

    Q_INVOKABLE void descargar(const QString& repo);
    Q_INVOKABLE void cancelarDescarga();
    Q_INVOKABLE void borrar(const QString& repo);
    /// Carga el modelo (y apaga el que hubiera).
    Q_INVOKABLE void activar(const QString& repo);
    Q_INVOKABLE void apagar();
    /// Elegir con una frase, para ver qué hace: el resultado va a `pruebas`.
    Q_INVOKABLE void probar(const QString& frase);
    /// Preguntarle algo de una imagen (ruta o file:// URL): va a `pruebas`.
    Q_INVOKABLE void probarVision(const QString& imagen, const QString& pregunta);
    Q_INVOKABLE void abrirCarpeta() const;

    /// Para el resto de la app: qué herramienta atiende la frase. Contesta
    /// {"herramienta","derivar","prob","ms"} o {"error"}; sin modelo listo,
    /// {"error":"sin modelo"} enseguida.
    void elegir(const QString& frase, Respuesta r);
    /// Modelos v2 (formato «tc», tool calling nativo): una sola vuelta con el
    /// estado (JSON compacto; vacío = charla) y la frase. Responde {crudo,
    /// texto, tipo: llamada|charla, herramienta, prob, ms} o {error}.
    void llamar(const QString& estadoJson, const QString& frase, Respuesta r);
    /// Modelos v2: lee el estado antes de que llegue la frase (al apretar F9),
    /// para que `llamar` con ese MISMO estado solo procese la frase: el primer
    /// token pasa de ~1,1 s a ~0,3 s. Responde {preparado, ms, tokens} o {error}.
    void preparar(const QString& estadoJson, Respuesta r);
    /// v2: quien arma el estado del aparato y llama (Voz). Con esto, `probar`
    /// y `probarFrase` muestran la llamada entera, no solo el nombre.
    using Probador = std::function<void(const QString& frase, Respuesta r)>;
    void setProbadorTc(Probador p) { probadorTc_ = std::move(p); }
    /// Lo que haría el modelo con la frase, SIN hacerlo. v1: {herramienta,
    /// derivar, prob, ms}. v2: además {tipo, crudo, resumen («audio_set(name:
    /// Música, volumeDb: +6)» o la charla)}.
    void probarFrase(const QString& frase, Respuesta r);
    /// "tc" si el modelo activo es v2 (tool calling nativo), "v1" si no.
    QString formato() const { return info_.value("formato", "v1").toString(); }
    /// Texto corto (con MTP si rinde): `delta` va llegando, `fin` al final.
    void generar(const QString& texto, bool conPrefijo, int maxTokens,
                 std::function<void(const QString&)> delta, Respuesta fin);
    /// Una imagen (ruta local) y una pregunta, si el modelo trae la torre de
    /// visión (info().vision). Mismo formato que generar.
    void ver(const QString& imagen, const QString& pregunta, int maxTokens,
             std::function<void(const QString&)> delta, Respuesta fin);
    bool listo() const { return estado_ == "listo"; }
    bool conVision() const { return info_.value("vision").toBool(); }

    /// La carpeta de un modelo bajado (o vacía si no está instalado).
    std::filesystem::path carpetaInstalada(const QString& repo) const;
    /// El primer modelo instalado de un tipo del catálogo para esta plataforma
    /// ("voz", "vad"...; los de lenguaje son "reactivo"). Vacío si no hay.
    QString instaladoDeTipo(const QString& tipo) const;

    /// Arranca el activo si así se configuró. Lo llama la app al abrir.
    void arrancarSiCorresponde();

   signals:
    void cambio();
    void descargaCambio();
    void pruebasCambio();
    void notify(const QString& text, const QString& kind);

   private:
    std::filesystem::path carpetaModelos() const;
    std::filesystem::path carpetaDe(const QString& repo) const;
    bool instalado(const QString& repo) const;
    json entradaCatalogo(const QString& repo) const;
    void leerAjustes();
    void guardarAjustes() const;
    void setEstado(const QString& estado, const QString& detalle);
    void linea(const QByteArray& l);
    void pedir(json pedido, Respuesta r, std::function<void(const QString&)> delta = {});
    void fallanTodos(const QString& error);

    std::filesystem::path raiz_;
    QString dirBinarios_;
    json catalogo_;

    QString activo_;
    QString estado_ = "apagado";
    QString detalle_;
    QString detalleError_; ///< lo último que dijo el motor por stderr
    QVariantMap info_;
    double sesgo_ = 0.0;
    QString mtp_ = "no"; ///< la cabeza todavía no rinde: sin MTP hasta reentrenarla
    bool alArrancar_ = false;
    QVariantList pruebas_;
    Probador probadorTc_;

    QProcess* proc_ = nullptr;
    QByteArray pendiente_;
    std::int64_t proximoId_ = 1;
    struct Espera {
        Respuesta fin;
        std::function<void(const QString&)> delta;
    };
    std::map<std::int64_t, Espera> esperas_;

    std::thread hiloDescarga_;
    std::atomic<bool> cancelar_{false};
    bool descargando_ = false;
    double progreso_ = 0.0;
    QString textoDescarga_;
};

} // namespace ba::app::agent
