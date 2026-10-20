// src/app/video/VideoGrafos.hpp — el grafo de video del estudio (Studio.video)
//
// UN grafo (video/GrafoVideo) con un sub-grafo por entrada de video (cámara,
// red, NDI) y nodos del estudio (el compositor). Las tarjetas de imagen de
// cada entrada (color, croma, recorte, superresolución, módulos de GStreamer)
// corren en la cadena de GStreamer de la entrada (ajuste `filtros` del motor),
// DESPUÉS de los filtros extra (Mejorar imagen y PTZ digital, FuentesApp).
// Las demás (salida, toma, rostros, compositor) todavía no corren en
// el motor: el estado lo dice por tarjeta (erroresDe).
//
// O CUALQUIER módulo de video de GStreamer (`gst`, con su elemento y sus
// propiedades): el catálogo sale del registro de GStreamer del bundle
// (video/CatalogoGst), con la documentación en castellano de
// datos/gst-modulos.json, y el formulario, de la introspección de GObject.
//
// Las entradas virtuales (señal de ajuste, color, negro, o cualquier pipeline
// de origen de GStreamer) son fuentes `ba_gst_fuente`: cuentan como entradas.
//
// Una tarjeta «Salida» publica ese punto como una fuente `ba_video_salida`
// con nombre: `aEscena` la pone en una escena (y queda en app.json).
//
// Se guarda en Config/video-grafo.json. El formato de antes
// (Config/video-grafos.json, un grafo por entrada) se migra solo.

#pragma once

#include "video/CatalogoGst.hpp"
#include "video/GrafoVideo.hpp"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>


namespace ba::app {
class AppRuntime;
}

namespace ba::app::video {

class VideoGrafos : public QObject {
    Q_OBJECT
    /// Las entradas de video que hay.
    Q_PROPERTY(QStringList entradas READ entradas NOTIFY cambio)
    /// Las mismas, agrupadas como en el audio: [{nombre, grupo («Cámaras»,
    /// «Red», «NDI»), tarjetas (cuántas tiene)}]
    Q_PROPERTY(QVariantList entradasInfo READ entradasInfo NOTIFY cambio)
    /// La que se está editando.
    Q_PROPERTY(QString entrada READ entrada WRITE seleccionar NOTIFY cambio)
    /// Su sub-grafo: {nodos:[{id,tipo,titulo,etiqueta,grupo,x,y,params,externo,
    /// entradas:[{id,tipo,pedido}],salidas:[…]}], cables:[{desde,salida,hacia,entrada}], trivial}
    Q_PROPERTY(QVariantMap grafo READ grafo NOTIFY cambio)
    /// Todo el estudio (mismo formato).
    Q_PROPERTY(QVariantMap estudio READ estudio NOTIFY cambio)
    /// Qué muestra la página: "racks" (una entrada) o "estudio" (todo).
    Q_PROPERTY(QString vista READ vista WRITE setVista NOTIFY vistaCambio)
    /// Las tarjetas propias: [{tipo,titulo,descripcion,params,ambito}]
    Q_PROPERTY(QVariantList catalogo READ catalogo CONSTANT)

   public:
    VideoGrafos(AppRuntime& rt, std::filesystem::path dataRoot, QObject* parent = nullptr);

    QString vista() const { return vista_; }
    void setVista(const QString& v) {
        if (v == vista_ || (v != "racks" && v != "estudio")) return;
        vista_ = v;
        emit vistaCambio();
    }
    QStringList entradas() const;
    QVariantList entradasInfo() const;
    QString entrada() const { return entrada_; }
    void seleccionar(const QString& entrada);
    QVariantMap grafo() const;
    QVariantMap estudio() const;
    static QVariantList catalogo();
    /// Los filtros de video de OBS que se pueden poner como tarjeta:
    /// [{id, nombre, propio (de esta app)}], por nombre.
    Q_INVOKABLE QVariantList filtrosObs() const;
    /// Los controles de una tarjeta de OBS o de GStreamer (como
    /// PropertiesModel): [{name, label, kind, value, min, max, step, options,
    /// suffix, help}].
    Q_INVOKABLE QVariantList propiedades(const QString& id) const;
    /// Los módulos de video de GStreamer (CatalogoGst::modulos).
    Q_INVOKABLE QVariantList modulosGst() const;
    /// Las propiedades de un módulo, para el detalle del catálogo.
    Q_INVOKABLE QVariantList propiedadesModulo(const QString& elemento) const;
    /// Las propiedades de un elemento con estos valores puestos (para editar un filtro ya armado).
    Q_INVOKABLE QVariantList propiedadesModuloCon(const QString& elemento, const QVariantMap& valores) const;
    /// Los filtros de audio de GStreamer (para la cadena de audio de una entrada).
    Q_INVOKABLE QVariantList modulosAudio() const;
    /// "GStreamer 1.28.7", o por qué no está.
    Q_INVOKABLE QString versionGst() const;
    /// Las entradas virtuales que se pueden crear: [{id, nombre, descripcion, pipeline}].
    Q_INVOKABLE static QVariantList virtuales();

    // --- Editar (la interfaz; la entrada elegida). "" o el error. -------------
    Q_INVOKABLE QString agregar(const QString& tipo, const QString& despues);
    Q_INVOKABLE QString agregarFiltro(const QString& filtroId, const QString& despues);
    /// Un módulo de GStreamer como tarjeta (`elemento`, p. ej. "videobalance").
    Q_INVOKABLE QString agregarModulo(const QString& elemento, const QString& despues);
    /// Una tarjeta de SUPERRESOLUCIÓN (un filtro de GStreamer que agranda x2: bafsr, bavtsuperres o
    /// baonnxsr con `modelo`) en `entrada` (vacía: la elegida), después de `despues`. "" o el error.
    std::string agregarSuperres(const std::string& entrada, const std::string& motor,
                                const std::string& modelo, const std::string& despues);
    /// La tarjeta «Superresolución» con un plugin (por id; lo traduce SuperRes):
    /// la agrega o le cambia el plugin a la que hay. "" o el error.
    std::string superresPlugin(const std::string& entrada, const std::string& plugin,
                               const nlohmann::json& extra = nlohmann::json::object());
    Q_INVOKABLE QString agregarSuperresUi(const QString& motor, const QString& modelo,
                                          const QString& despues);
    /// Crea una entrada virtual en la escena (vacía = la del aire): `preset` es
    /// un id de virtuales() o, si empieza con «pipeline:», el pipeline de origen.
    Q_INVOKABLE QString crearVirtual(const QString& nombre, const QString& preset, const QString& escena);
    Q_INVOKABLE QString quitar(const QString& id);
    /// Lleva una tarjeta a otro lugar de la cadena de su entrada: después de
    /// `despues` (vacío = justo antes de la escena). "" o el error.
    Q_INVOKABLE QString reubicar(const QString& id, const QString& despues);
    /// La corre un lugar antes (-1) o después (+1) en la cadena. "" o el error.
    Q_INVOKABLE QString correr(const QString& id, int paso);
    /// Los eventos de GStreamer «ba/*» que salen de cada tarjeta de la entrada
    /// elegida (sondas en sus pads): tarjeta -> [{evento, direccion (abajo|arriba),
    /// n, hace_ms}]. Para ver en Racks cómo se hablan las tarjetas.
    Q_INVOKABLE QVariantMap eventosDeEntrada() const;
    Q_INVOKABLE QString conectar(const QString& desde, const QString& salida, const QString& hacia,
                                 const QString& entrada);
    Q_INVOKABLE QString desconectar(const QString& desde, const QString& salida, const QString& hacia,
                                    const QString& entrada);
    Q_INVOKABLE QString mover(const QString& id, int x, int y);
    Q_INVOKABLE QString ajustar(const QString& id, const QVariantMap& params);
    /// Hace un controlador con la tarjeta `id` (la misma corrección para varias entradas).
    Q_INVOKABLE QString controlar(const QString& id);
    /// Agrega a `entrada` una tarjeta manejada por el controlador `control`.
    Q_INVOKABLE QString agregarControlada(const QString& control, const QString& entrada);
    /// Suelta la tarjeta de su controlador (se queda con los valores que tenía).
    Q_INVOKABLE QString soltarControl(const QString& id);
    /// Un ajuste de una tarjeta de OBS o de GStreamer (lo que cambia en su formulario).
    Q_INVOKABLE QString ajustarFiltro(const QString& id, const QString& clave, const QVariant& valor);
    /// Pone la salida o el compositor `id` en la escena (vacía = la que está al aire).
    Q_INVOKABLE QString aEscena(const QString& id, const QString& escena);
    /// La entrada elegida vuelve a «entrada -> escena».
    Q_INVOKABLE QString limpiar();
    /// Acomoda todas las tarjetas: una franja por entrada (en columnas según la
    /// cadena, las ramas abajo), los controladores a la izquierda, los
    /// compositores a la derecha. "" o el error.
    Q_INVOKABLE QString ordenar();
    // --- Sincronía y red, para la interfaz (controls/VistaSincronia.qml, en Latencias) ---
    Q_INVOKABLE QVariantMap relojUi() const;
    Q_INVOKABLE QString configurarRelojUi(const QVariantMap& cambios);
    /// Las entradas de GStreamer (red y plugins) con su estado y diagnóstico.
    Q_INVOKABLE QVariantList estadoRed() const;
    /// Todas las entradas de video, con su diagnóstico (las que tienen algo que decir).
    Q_INVOKABLE QVariantList diagnosticoUi() const;
    /// Mide (y con `aplicar` corrige) cuánto atrasa `b` de `a`: el resultado llega por `medicion`.
    Q_INVOKABLE void medirUi(const QString& a, const QString& b, bool aplicar);
    Q_INVOKABLE QString desfaseUi(const QString& entrada, double ms);
    /// Superresolución x2 de una entrada de red (macOS 26: bavtsuperres). "" o el error.
    Q_INVOKABLE QString superresUi(const QString& entrada, bool activa);
    /// Cómo sincroniza la entrada: "colchon" o "gstreamer" (latencia del estudio).
    Q_INVOKABLE QString sincroniaUi(const QString& entrada, const QString& modo);
    /// Si este sistema tiene superresolución (el elemento de GStreamer está registrado).
    Q_INVOKABLE bool haySuperres() const;
    /// El motor de superresolución de una entrada de red: "" (ninguno), apple,
    /// fsr u onnx (con `modelo`, la ruta del .onnx). "" o el error.
    /// Los filtros de AUDIO de una entrada de GStreamer (elementos de GStreamer
    /// en la cadena de su audio: `baplugin medio=audio` de un plugin de Python,
    /// audiodynamic, audioecho…): [{elemento, propiedades}]. Vacío: ninguno.
    /// La demora que agreguen se compensa sola (el colchón atrasa el video igual).
    std::string filtrosAudio(const std::string& entrada, const nlohmann::json& filtros);
    nlohmann::json filtrosAudioDe(const std::string& entrada) const;

    // --- La latencia del estudio (P0.6) ------------------------------------------------------
    /// {latencia_ms, tope_ms, automatico, tarde: [{nombre, pide_ms}]}. `tope`: -2 no cambia,
    /// -1 automático (250 ms), >= 0 ese (se guarda).
    nlohmann::json estudioLatencia(int tope = -2);
    Q_INVOKABLE QVariantMap estudioLatenciaUi();
    Q_INVOKABLE void ponerTopeEstudioUi(int tope);
    /// La clase de sincronía de una entrada: "sala" (sincronizada: marca la latencia del estudio),
    /// "propia" (con su propia demora: no arrastra a las demás) o "auto" (la de su tipo).
    std::string claseSincronia(const std::string& entrada, const std::string& clase);
    Q_INVOKABLE QString claseSincroniaUi(const QString& entrada, const QString& clase) {
        return QString::fromStdString(claseSincronia(entrada.toStdString(), clase.toStdString()));
    }
    /// Bajar la latencia del estudio ahora (a lo que piden las sincronizadas). "" o por qué no.
    Q_INVOKABLE QString bajarLatenciaUi();

    // --- Automixer de mesa redonda (T32) ---------------------------------------------------
    /// Pone al micrófono (o a cualquier entrada de GStreamer con audio) en la mesa
    /// `grupo`: un `baautomixer` al final de sus filtros de audio. Los de la misma mesa
    /// se reparten la ganancia (Dugan): el que habla pasa al frente. `grupo` vacío: lo
    /// saca de la mesa. "" o el error.
    std::string automixer(const std::string& entrada, const std::string& grupo);
    /// Los ajustes de la mesa (umbral_db, hold_ms, piso_hold_db, max_abiertos,
    /// ataque_ms, liberacion_ms): se guardan en todos sus micrófonos. "" o el error.
    std::string automixerAjustes(const std::string& grupo, const nlohmann::json& ajustes);
    /// [{grupo, ajustes, microfonos: [{nombre, canal, ganancia_db, nivel_db}]}], en vivo.
    nlohmann::json automixerEstado() const;
    /// {mesas: automixerEstado, microfonos: [nombres de los micrófonos por GStreamer]}.
    Q_INVOKABLE QVariantMap automixerUi() const;
    Q_INVOKABLE QString automixerPonerUi(const QString& entrada, const QString& grupo);
    Q_INVOKABLE QString automixerAjustesUi(const QString& grupo, const QVariantMap& ajustes);

    // --- Canal de control de las entradas de GStreamer ------------------------------------
    /// Manda `control` (un objeto plano) a la cadena de GStreamer de la entrada como
    /// evento estándar «ba/control» (personalizado, hacia abajo, fuera de banda): cada
    /// elemento que lo entiende lo usa y todos lo dejan pasar. La entrada lo recuerda
    /// y lo vuelve a mandar si se rearma. "" o el error.
    std::string controlEntrada(const std::string& entrada, const nlohmann::json& control);

    // --- Programador de superresolución ---------------------------------------------------
    /// Con varias cámaras, las redes de todas quedan cargadas y calientes, pero
    /// solo trabajan las que se ven grandes: el programador mira el programa y el
    /// preview (la escena armada para el próximo take) cada 250 ms y manda por el
    /// canal de control `superres` = activo (hasta `max_activas`, por prioridad:
    /// programa, después preview, más grande primero), barato (se ve grande pero no
    /// tiene turno) o reposo (no se ve, o se ve más chica que la cámara: la red no
    /// suma nada). Se prende al pasar a preview: la transición pasa fuera del aire.
    /// Configuración: {activo, max_activas, transicion_ms}; estado por entrada.
    nlohmann::json programador() const;
    std::string configurarProgramador(const nlohmann::json& cambios);
    Q_INVOKABLE QVariantMap programadorUi() const;
    Q_INVOKABLE QString configurarProgramadorUi(const QVariantMap& cambios);
    Q_INVOKABLE QVariantList filtrosAudioUi(const QString& entrada) const;
    /// Agrega uno de los de uso común (compresor, pasa_altos, eco, volumen) al final.
    Q_INVOKABLE QString agregarFiltroAudioUi(const QString& entrada, const QString& cual);
    Q_INVOKABLE QString quitarFiltroAudioUi(const QString& entrada, int indice);
    /// Reemplaza todos los filtros de audio de la entrada (editor: agregar, editar, ordenar).
    Q_INVOKABLE QString ponerFiltrosAudioUi(const QString& entrada, const QVariantList& filtros);
    std::string superresMotor(const std::string& entrada, const std::string& motor,
                              const std::string& modelo);
    Q_INVOKABLE QString superresMotorUi(const QString& entrada, const QString& motor, const QString& modelo);
    /// Qué motores hay en este sistema: [apple?, fsr?, onnx?].
    Q_INVOKABLE QStringList motoresSuperres() const;
    /// Los tamaños (ancho, alto) de las entradas de video que hay (para el banco).
    std::vector<std::pair<int, int>> tamanosDeEntradas() const;
    /// Lo que va a tardar `plugin` con entradas de ancho×alto según el banco de
    /// pruebas (0 = no se midió). El programador lo usa antes de prender una red.
    void setEstimadorSr(std::function<double(const std::string&, int, int)> fn) { estimadorSr_ = std::move(fn); }
    /// Vuelve a armar la cadena de cada entrada (p. ej. hay variantes nuevas de una red).
    void reaplicarEntradas();
    /// El legalizador broadcast-safe (balegal) al final de la cadena de cada entrada
    /// de GStreamer: "no", "pal" (compuesta +133 %), "ntsc" (+120 %) o "rango" (solo
    /// luma y croma). Rearma las cadenas si cambió.
    void setLegalizar(const std::string& modo);
    const std::string& legalizar() const { return legalizar_; }
    /// Cada entrada de GStreamer: {entrada: "andando" | "falta" | el error del elemento}.
    nlohmann::json estadoLegalizador() const;
    /// La dirección de una entrada de red (para grabar de ella al entrenar). Vacía si no es de red.
    std::string uriDe(const std::string& entrada) const;
    /// Un cuadro de la entrada tal como sale de su grafo (para entrenar con una cámara
    /// local, que no tiene dirección de red). false si no hay imagen.
    /// Un cuadro de la entrada sin que espere la interfaz: `listo` llega en el
    /// hilo de la interfaz (imagen nula si no dio).
    void capturarCuadroAparte(const std::string& entrada, std::function<void(QImage)> listo) const;
    /// Atrasa el audio de la entrada lo que demoran sus tarjetas de superresolución.
    void compensarAudio(const std::string& entrada);
    /// La demora medida de cada tarjeta de la entrada (ms), por id de nodo.
    nlohmann::json demorasDe(const std::string& entrada) const;
    /// Traduce el id de un plugin de superresolución a lo de esta máquina:
    /// {motor, modelo, escala_red, proveedor?} o "" + error (lo pone SuperRes).
    using ResolverPlugin = std::function<nlohmann::json(const std::string& id, std::string* error)>;
    void setResolverPlugin(ResolverPlugin r) { resolverPlugin_ = std::move(r); }
    Q_INVOKABLE QString nuevaEntradaRed(const QString& nombre, const QString& uri, const QString& escena,
                                        int latenciaMs, bool normalizar);
    Q_INVOKABLE QStringList escenasUi() const;

    // --- Para MCP --------------------------------------------------------------
    /// El sub-grafo de `entrada` (o todo el estudio si `entrada` es vacía), o {"error"}.
    nlohmann::json describir(const std::string& entrada) const;
    /// Edita el grafo con `f` (que devuelve "" o el error); si cambió, se
    /// guarda y se aplica.
    std::string editar(const std::function<std::string(GrafoVideo&)>& f);
    std::string ponerEnEscena(const std::string& id, const std::string& escena);
    bool esEntrada(const std::string& nombre) const;
    /// El grafo (solo para leer: títulos y a qué entrada pertenece cada tarjeta).
    const GrafoVideo& grafoVideo() const { return grafo_; }
    /// El nombre legible de un filtro de OBS ("" si no existe).
    static std::string nombreFiltro(const std::string& filtroId);
    /// El catálogo de GStreamer (vacío, con el motivo, hasta que está listo).
    const CatalogoGst& gst() const;
    /// Agrega un módulo de GStreamer a `grupo` (MCP y la interfaz): el id o "".
    std::string agregarModuloEn(GrafoVideo& g, const std::string& elemento, const std::string& grupo,
                                const std::string& despues, const nlohmann::json& propiedades,
                                std::string* error) const;
    /// Ajusta `id`; si la maneja un controlador, el ajuste va al controlador
    /// (salvo la etiqueta). "" o el error.
    static std::string ajustarEn(GrafoVideo& g, const std::string& id, nlohmann::json params);
    /// Pone en `g` las capas de la disposición en el compositor `comp` (borra
    /// antes las que tenía, así no queda un `orden` o una opacidad de otra).
    static std::string disponerEn(GrafoVideo& g, const std::string& comp, const std::string& disposicion,
                                  const std::vector<std::string>& puertos);
    // --- Rostros (lo usa Personas) ------------------------------------------
    // (Lo que ve cada tarjeta llega por el bus de la entrada: motor::escucharElementos.)
    /// A quién sigue la tarjeta Seguimiento de `entrada` (la pone después de
    /// Rostros si no está). `objetivo`: "" | id:N | persona:X | x,y | mayor | centro.
    /// "" o el error.
    std::string configurarSeguir(const std::string& entrada, const std::string& objetivo);
    /// Pone o saca las tarjetas `rostros` que Personas necesita: `quiero` es
    /// {entrada: uso}. Las que puso Personas y ya no hacen falta se van; una
    /// que puso alguien a mano se queda (con uso «mirar»). "" o el error.
    std::string configurarRostros(const std::map<std::string, std::string>& quiero);
    // --- Relojes y sincronía ---------------------------------------------------
    /// El reloj del estudio: {tipo (sistema|ntp|ptp|red), servidor, puerto,
    /// dominio, latencia_ms (la de las entradas de red), publicar_puerto}.
    nlohmann::json reloj() const;
    /// Lo cambia, lo guarda y lo aplica a todas las fuentes de GStreamer. "" o el error.
    std::string configurarReloj(const nlohmann::json& cambios);
    /// Todo lo que se sabe de una entrada: tamaño, si es asincrónica y su
    /// desentrelazado, sus ajustes, lo que mide su pipeline de GStreamer (reloj,
    /// fps, formato, colorimetría, colchón, cuadros tarde), la latencia que
    /// agrega su sub-grafo, y los errores y avisos de sus tarjetas.
    nlohmann::json estadoEntrada(const std::string& entrada) const;
    /// ¿Hay señal según el estado de una entrada (estadoEntrada)? Cuadros que llegan,
    /// con 12 s de gracia si recién se rearmó (se vuelve a abrir la cámara).
    static bool tieneSenal(const nlohmann::json& estado);
    /// Agrega al final del sub-grafo de `entrada` una tarjeta `gst` con ese
    /// módulo (p. ej. `baplugin`, la de un plugin de Python). "" o el error.
    std::string agregarTarjetaGst(const std::string& entrada, const std::string& elemento,
                                  const nlohmann::json& propiedades, nlohmann::json* hecho);
    /// Una entrada de GStreamer a partir de un pipeline de origen (video y/o
    /// audio): un dispositivo local raro, la entrada de un plugin… Con el
    /// colchón y el reloj del estudio. `extra` va a los ajustes de la fuente
    /// (p. ej. `ba_plugin`). Si ya existe y es de GStreamer, la actualiza.
    std::string crearEntradaPipeline(const std::string& nombre, const std::string& video,
                                     const std::string& audio, const std::string& escena, int latenciaMs,
                                     int normalizar, const nlohmann::json& extra = {});
    /// Una entrada de red (srt://, rtmp://, rtsp://, udp://, http://) con el
    /// reloj del estudio. `latenciaMs` < 0: la del estudio. "" o el error.
    std::string crearEntradaRed(const std::string& nombre, const std::string& uri, int latenciaMs,
                                const std::string& escena, int audio = -1, int normalizar = -1,
                                int ntpSync = -1, int superres = -1, const std::string& sincronia = {});
    // (audio, normalizar, ntpSync: 1 sí, 0 no, -1 sin decir: al crear, de
    // fábrica; al actualizar una que ya existe, no se toca.)
    /// Corre una entrada `ms` (o la lleva a `ms` si !relativo): una fuente de
    /// GStreamer por su colchón (para los dos lados); una cámara, solo para
    /// atrás, con una tarjeta retardo. "" o el error; `hecho` dice qué se tocó.
    std::string ajustarDesfase(const std::string& entrada, double ms, bool relativo, nlohmann::json* hecho);
    /// Mide durante `segundos` cuánto atrasa `b` respecto de `a` (tarjetas
    /// `sincro` puestas y sacadas solas) y, con `aplicar`, lo corrige.
    void medirDesfase(const std::string& a, const std::string& b, int segundos, bool aplicar,
                      std::function<void(nlohmann::json)> listo);
    /// Las entradas de red (GStreamer) que ya toman video de `host`: [{entrada, url}].
    nlohmann::json entradasDeHost(const std::string& host) const;
    /// Suma a `escena` una fuente que ya existe (la misma, no otra copia). "" o el error.
    std::string sumarAEscena(const std::string& nombre, const std::string& escena);
    /// Crea la escena si no existe. true si existe (o se creó).
    bool asegurarEscena(const std::string& escena, bool* creada = nullptr);
    /// Una escena que creó un pedido (asegurarEscena) y quedó vacía porque el pedido falló
    /// después: se va al salir de la función (sin dejar escenas vacías sueltas).
    struct EscenaNueva {
        VideoGrafos& grafos;
        std::string nombre;
        bool creada = false;
        ~EscenaNueva();
    };
    /// Muestra u oculta una fuente en una escena.
    bool mostrarEnEscena(const std::string& nombre, const std::string& escena, bool visible);
    /// Un mosaico (o imagen en imagen, o lado a lado) de `entradas` en `escena`,
    /// en una vez: el compositor, los cables (desde lo que llega a la escena de
    /// cada una, o sea con sus tarjetas) y la fuente en la escena. Con
    /// `ocultarSueltas`, oculta en esa escena las entradas sueltas (el
    /// compositor las sigue recibiendo). "" o el error; `hecho` dice qué quedó.
    /// La Panorámica en UNA llamada: la tarjeta (o la que se llame `nombre`), un cable de
    /// cada cámara (lo que llega a su escena; la k-ésima es la lente k) y la fuente en
    /// `escena` (las cámaras sueltas se ocultan ahí). `preset`: "" | "360" (dos ojos de
    /// pez) | "fila" (comunes de `campo` grados); encima, `params` (los de la tarjeta).
    std::string armarPanoramica(const std::vector<std::string>& camaras, const std::string& preset, double campo,
                                const std::string& nombre, const std::string& escena, const nlohmann::json& params,
                                nlohmann::json* hecho);
    /// Un atajo sobre una Panorámica que ya está: "360" o "fila" (con las cámaras que tiene).
    std::string presetPanoramica(const std::string& id, const std::string& preset, double campo);
    Q_INVOKABLE QString presetPanoramicaUi(const QString& id, const QString& preset, double campo) {
        return QString::fromStdString(presetPanoramica(id.toStdString(), preset.toStdString(), campo));
    }
    /// Los parámetros de un atajo para `cuantas` cámaras.
    static nlohmann::json paramsPreset(const std::string& preset, int cuantas, double campo);
    std::string armarMosaico(const std::string& escena, const std::vector<std::string>& entradas,
                             const std::string& disposicion, const std::string& nombre, bool ocultarSueltas,
                             nlohmann::json* hecho);
    /// Las capas de una disposición del compositor para sus `puertos`, en orden
    /// (el primero es el fondo en «pip»): {puerto: {x, y, ancho, alto, orden?} | null}
    /// (null = en la grilla). `disposicion`: "mosaico" ("" también; con 3, una
    /// arriba al centro y dos abajo), "pip" (la primera de fondo y las demás
    /// chicas arriba a la derecha, una debajo de otra) o "lado" (cada una en su
    /// franja de 1/n de ancho y toda la altura: el encaje la centra sin
    /// estirarla). Vacío y `error` si no se puede.
    static nlohmann::json capasDeDisposicion(const std::string& disposicion, const std::vector<std::string>& puertos,
                                             std::string* error);
    /// Rearma el compositor `compositor` (su id) en una disposición, con las
    /// entradas que ya tiene conectadas: borra las capas que tenía y pone las de
    /// capasDeDisposicion. La usan la página Video y video_mosaico. "" o el error.
    std::string aplicarDisposicion(const std::string& compositor, const std::string& disposicion);
    Q_INVOKABLE QString aplicarDisposicionUi(const QString& compositor, const QString& disposicion) {
        return QString::fromStdString(aplicarDisposicion(compositor.toStdString(), disposicion.toStdString()));
    }
    /// Crea una entrada virtual; "" o el error.
    std::string crearEntradaVirtual(const std::string& nombre, const std::string& preset,
                                    const std::string& escena);

   public slots:
    /// Revisa entradas y filtros (al cambiar las entradas, y cada 1,5 s).
    void sincronizar();

   signals:
    void cambio();
    void vistaCambio();
    /// El catálogo de GStreamer quedó listo.
    void gstListo();
    /// El resultado de medirUi.
    void medicion(const QVariantMap& resultado);
    /// La superresolución de una entrada no llega a tiempo (ms por cuadro sobre el
    /// umbral durante 2 s). `bypass`: se la pasó sola al modo barato un rato.
    void superresTarde(const QString& entrada, double ms, double cuadroMs, bool bypass);

   private:
    ResolverPlugin resolverPlugin_;
    void evaluarSuperres();
    QTimer programadorTimer_;
    nlohmann::json programador_ = {{"activo", true},          {"capacidad", "auto"},      {"max_activas", 3},
                                   {"uso_pct", 85},            {"transicion_ms", 400},     {"en_preview", true},
                                   {"sin_turno", "barato"},    {"umbral_aumento", 1.1},    {"forzar", nlohmann::json::object()},
                                   {"bypass_auto", true},      {"umbral_pct", 80}};
    /// La alarma «no llega»: desde cuándo está lenta, hasta cuándo está en bypass y
    /// cuántas veces seguidas pasó (cada vez espera el doble antes de volver a probar).
    struct Tarde {
        qint64 desde = 0;
        qint64 bypassHasta = 0;
        qint64 ultimaVez = 0;
        int veces = 0;
        bool avisado = false;
    };
    std::map<std::string, Tarde> tarde_;
    nlohmann::json estadoProgramador_ = nlohmann::json::object();
    nlohmann::json presupuesto_ = nlohmann::json::object();
    std::map<std::string, std::string> modoSr_; ///< lo último mandado a cada entrada
    /// Lo medido de cada red (cada ~1 s): ms por cuadro y en qué acelerador corre.
    std::map<std::string, std::pair<double, std::string>> medidasSr_;
    int ticsProgramador_ = 0;
    qint64 ultimoCambioSr_ = 0;
    void aplicar(const std::string& entrada);
    /// Las tarjetas que cuelgan en fila de la entrada y son de imagen (color,
    /// croma, recorte, superresolución, módulos de GStreamer) pasan a la cadena
    /// de GStreamer de la fuente (`filtros` del motor): las saca de `para`
    /// (lo que queda es lo que el motor todavía no corre) y devuelve los filtros
    /// {"video": [...], "alfa"}. Anota cuáles corren en la entrada.
    nlohmann::json pasarAEntrada(const std::string& entrada, nlohmann::json& para);
    /// El productor de una Salida (vacío = el final de la cadena de su entrada).
    std::string productorDe(const std::string& salida) const;
    /// Lo que informa la cadena de GStreamer de la entrada sobre sus filtros.
    nlohmann::json filtrosDeEntrada(const std::string& entrada) const;
    /// Lo que dura un cuadro del lienzo (ms).
    double cuadroMsLienzo() const;
    /// Los compositores publicados al día, y las entradas que alimentan otra
    /// parte del estudio, activas.
    void actualizarEstudio();
    /// A las salidas sin nombre les pone uno (único entre las fuentes).
    /// "" o el error: dos salidas (o compositores) con el mismo nombre, o dos tomas iguales en una entrada.
    static std::string nombresRepetidos(const GrafoVideo& g);
    void nombrarSalidas(GrafoVideo& g) const;
    nlohmann::json conPublicadas(nlohmann::json d) const;
    /// Los errores de las tarjetas de `entrada` (de su filtro): {id: motivo}.
    nlohmann::json erroresDe(const std::string& entrada) const;
    nlohmann::json avisosDe(const std::string& entrada) const;
    /// Los ajustes de reloj para una fuente de GStreamer nueva.
    nlohmann::json relojParaFuente() const;
    void guardar() const;
    void cargar();

    AppRuntime& rt_;
    std::filesystem::path archivo_, archivoViejo_;
    GrafoVideo grafo_;
    /// Por entrada: las tarjetas que el motor todavía no corre → por qué.
    std::map<std::string, std::map<std::string, std::string>> sinCorrer_;
    QStringList entradas_;
    QString entrada_;
    QString vista_ = QStringLiteral("racks");
    QTimer revision_;
    std::string erroresVistos_;       ///< los errores de las tarjetas, la última vez
    nlohmann::json reloj_ = {{"tipo", "sistema"},
                             {"latencia_ms", 200}}; ///< el del estudio (video-reloj.json)
    /// El catálogo se arma en un hilo al arrancar (GStreamer revisa sus
    /// plugins; la primera vez tarda): mientras tanto, uno vacío.
    std::shared_ptr<const CatalogoGst> gst_ = std::make_shared<CatalogoGst>();
    std::map<std::string, int> compAudio_; ///< entrada -> ms de audio agregados por las tarjetas
    std::map<std::string, std::set<std::string>> enEntrada_; ///< entrada -> tarjetas que corren en GStreamer
    /// Salida (su nombre) -> el productor que lee su escena: "" = el final de la
    /// cadena de su entrada; si cuelga del medio, el de su basalida.
    std::map<std::string, std::string> productorSalida_;
    std::function<double(const std::string&, int, int)> estimadorSr_;
    std::string legalizar_ = "no";
    std::set<std::string> ptzHabilitado_; ///< entradas con el PTZ digital prendido (Cámaras PTZ)
};

} // namespace ba::app::video
