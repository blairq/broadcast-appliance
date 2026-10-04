// src/app/agent/Traductor.hpp — traducción en vivo de las salidas de texto (Studio.traductor)
//
// Una TRADUCCIÓN toma una salida de texto (lo que transcribe una «Voz a texto»:
// «Orador») y publica otra salida con lo mismo en otro idioma («Orador · en»).
// Esa salida es como cualquier otra: la lee una tarjeta «Texto a voz» con una
// voz de ese idioma, va a subtítulos en un rótulo, a archivo .txt/.srt, a los
// disparadores. Idiomas: castellano, inglés, portugués (Brasil), francés,
// alemán, ruso y chino mandarín.
//
// EL MOTOR. Modelos OPUS-MT empaquetados por Argos Translate (CTranslate2), sin
// red, en el procesador: 0,1 a 0,6 s por frase. Corren en un proceso aparte (el
// Python del programa, assets/traductor/traductor.py): si se cae, la emisión
// sigue y se vuelve a lanzar. Castellano <-> inglés y castellano <-> portugués
// son directos; el resto pasa por el inglés (dos pasos). Los paquetes están en
// el catálogo de modelos (tipo «traduccion»: URL y sha256) y se bajan a Modelos/.
//
// EL GLOSARIO. Antes de traducir, los términos conocidos se reemplazan por su
// traducción ya hecha (medido: el modelo los respeta; sin esto «cuarto
// intermedio» sale «middle room»). El de fábrica (datos/glosario-traduccion.json:
// cabina y Legislatura del Chubut) más el del operador, que se suma o lo pisa:
// Datos/Config/glosario-traduccion.json. Los grupos se pueden apagar.
//
// ORACIONES ENTERAS. La transcripción continua manda un pedazo en cada pausa
// del orador, a veces a mitad de una idea; traducido suelto sale mal. Se juntan
// los pedazos hasta que uno termina en punto (., ?, !, …) o pasa `espera_ms`
// (0,6 s por defecto) sin que llegue otro. Si el orador encadena sin punto, a
// los `maximo_ms` de juntar (8 s por defecto) o a los 280 caracteres se traduce
// lo que hay, hasta la última coma si la hay.
//
// CADA TRADUCCIÓN ES UNA TARJETA «Traducir» del rack de audio (AudioRack, sin
// audio: va al lado de la voz a texto cuya salida traduce) y se guarda con el
// rack. Sin racks (compilado sin CLAP) se guarda en Datos/Config/traducciones.json;
// lo que hubiera ahí pasa a tarjetas la primera vez (el archivo queda como
// traducciones.json.migrado).
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>

class QTimer;

namespace ba::app::plugins {
class Hijo;
}

namespace ba::app::agent {

class ModelosLocales;
class FlujosTexto;

class Traductor : public QObject {
    Q_OBJECT
    /// [{id, desde, idioma_desde, a, nombre, activa, rotulo, archivo, estado, faltan, ultima}]
    Q_PROPERTY(QVariantList traducciones READ traduccionesQml NOTIFY cambio)
    /// [{codigo, nombre}]
    Q_PROPERTY(QVariantList idiomas READ idiomasQml CONSTANT)
    /// [{es: [...], en, pt, fr, de, ru, zh, grupo, origen: fabrica|operador, activo}]
    Q_PROPERTY(QVariantList glosario READ glosarioQml NOTIFY glosarioCambio)
    /// [{grupo, nombre, activo, cuantos}]
    Q_PROPERTY(QVariantList grupos READ gruposQml NOTIFY glosarioCambio)
    /// listo | arrancando | apagado | error: …
    Q_PROPERTY(QString motor READ motor NOTIFY cambio)
    Q_PROPERTY(QString archivoGlosario READ archivoGlosarioTexto CONSTANT)

   public:
    using json = nlohmann::json;
    using Listo = std::function<void(json)>;

    Traductor(std::filesystem::path dataRoot, ModelosLocales& modelos, FlujosTexto& flujos,
              QObject* parent = nullptr);
    ~Traductor() override;

    static const std::map<std::string, std::string>& nombresIdiomas();

    /// Dónde viven las traducciones: las tarjetas «Traducir» de los racks. Los
    /// ids son los de las tarjetas («<fuente>#<nodo>»).
    struct Tarjetas {
        std::function<json()> listar; ///< [{id, fuente, nodo, desde, idioma_desde, a, nombre, rotulo,
                                      ///< archivo, activa, espera_ms}]
        /// Agrega una (en `t["fuente"]` o al lado de la voz a texto de `t["desde"]`). "" o el error.
        std::function<std::string(const json& t, std::string* id)> agregar;
        std::function<std::string(const std::string& id, const json& cambios)> ajustar;
        std::function<std::string(const std::string& id)> quitar;
    };
    void setTarjetas(Tarjetas t);
    /// Vuelve a leer las tarjetas (cuando cambia un rack).
    void sincronizar();

    // --- Traducciones (configuración) --------------------------------------------------
    json lista() const;
    /// Traduce la salida `desde` (en `idiomaDesde`) a `a`; publica en `nombre`
    /// ("" = «<desde> · <a>»). `opciones`: {rotulo, archivo, activa}. "" o el error.
    std::string agregar(const std::string& desde, const std::string& idiomaDesde, const std::string& a,
                        const std::string& nombre, const json& opciones, json* hecha);
    std::string ajustar(const std::string& id, const json& cambios);
    std::string quitar(const std::string& id);
    /// Para FlujosTexto: las salidas que publica el traductor, con su configuración.
    json salidas() const;
    json salidaDe(const std::string& flujo) const;

    // --- Traducir una frase ---------------------------------------------------------------
    /// {texto, ms, pasos} o {error}. `de` vacío = castellano.
    void traducir(const QString& texto, const std::string& de, const std::string& a, Listo listo);
    /// Los paquetes que faltan bajar para ir de `de` a `a` (repos del catálogo).
    json faltan(const std::string& de, const std::string& a) const;
    /// Baja lo que falte (de a uno; sigue solo). "" o el error.
    std::string bajar(const std::string& de, const std::string& a);

    // --- Glosario ---------------------------------------------------------------------------
    /// El glosario entero (fábrica + operador), cada término con su origen y si está activo.
    json glosario() const;
    /// Suma o pisa un término (el operador): {es: [..] o "...", en, pt, fr, de, ru, zh, grupo?}.
    std::string glosarioAgregar(const json& entrada);
    /// Saca un término del operador o apaga uno de fábrica (por su primer `es`).
    std::string glosarioQuitar(const std::string& es);
    std::string glosarioGrupo(const std::string& grupo, bool activo);
    std::filesystem::path archivoGlosario() const;

    // --- Para la interfaz -----------------------------------------------------------------
    QVariantList traduccionesQml() const;
    QVariantList idiomasQml() const;
    QVariantList glosarioQml() const;
    QVariantList gruposQml() const;
    QString motor() const { return motor_; }
    QString archivoGlosarioTexto() const;
    /// Las salidas de texto que se pueden traducir (las de las tarjetas y las ya traducidas).
    Q_INVOKABLE QStringList salidasDisponibles() const;
    Q_INVOKABLE QString agregarUi(const QString& desde, const QString& a, const QString& nombre);
    Q_INVOKABLE QString ajustarUi(const QString& id, const QVariantMap& cambios);
    Q_INVOKABLE QString quitarUi(const QString& id);
    Q_INVOKABLE QString bajarUi(const QString& de, const QString& a);
    /// Prueba una frase: el resultado llega por `probado`.
    Q_INVOKABLE void probarUi(const QString& texto, const QString& a, const QString& de = QString());
    Q_INVOKABLE QString glosarioAgregarUi(const QVariantMap& entrada);
    Q_INVOKABLE QString glosarioQuitarUi(const QString& es);
    Q_INVOKABLE QString glosarioGrupoUi(const QString& grupo, bool activo);

   signals:
    void cambio();
    void glosarioCambio();
    void probado(const QString& a, const QString& texto, bool ok);

   private:
    struct Pedido {
        json mensaje;
        Listo listo;
    };
    json pasos(const std::string& de, const std::string& a, std::string* error) const;
    json glosarioActivo() const;
    void leer();
    void guardar() const;
    void leerGlosario();
    void guardarGlosario() const;
    void arrancarMotor();
    void alSalir();
    void leerSalida();
    void mandar();
    void alFrase(const QString& flujo, const QString& texto);
    void seguirBajando();

    std::filesystem::path root_;
    ModelosLocales& modelos_;
    FlujosTexto& flujos_;
    json traducciones_ = json::array(); ///< las tarjetas (o el archivo, sin racks), con `nombre` ya resuelto
    Tarjetas tarjetas_;
    /// Las del archivo viejo que todavía no pasaron a tarjeta (su rack no está
    /// montado): siguen traduciendo y se reintenta en cada cambio de los racks.
    json porMigrar_ = json::array();
    bool migrando_ = false;
    void migrar();
    json fabrica_ = json::array();   ///< datos/glosario-traduccion.json
    json operador_ = json::object(); ///< {terminos: [...], apagados: [es...], grupos_apagados: [...]}
    std::unique_ptr<plugins::Hijo> hijo_;
    QByteArray pendiente_;
    QString motor_ = "apagado";
    std::deque<Pedido> cola_;
    std::map<long long, Listo> enCurso_;
    long long n_ = 0;
    int caidas_ = 0;
    std::map<std::string, json> ultima_; ///< id de traducción -> {texto, ms, hora}
    std::deque<std::string> porBajar_;
    /// Lo que llegó de cada traducción y todavía no cierra una oración (la
    /// transcripción corta en cada pausa, a veces a mitad de idea).
    struct Junta {
        QString texto;
        QTimer* timer = nullptr;
        qint64 desde = 0; ///< cuándo llegó el primer pedazo de lo que se está juntando
    };
    std::map<std::string, Junta> juntas_;
    void soltar(const std::string& id);
};

} // namespace ba::app::agent
