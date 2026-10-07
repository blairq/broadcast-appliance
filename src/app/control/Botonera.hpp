// SPDX-License-Identifier: GPL-2.0-or-later
//
// Botonera: la superficie de control estilo Bitfocus Companion. Una tablet
// emparejada por QR (o un Stream Deck, un controlador MIDI… vía plugin) muestra
// páginas con una grilla de celdas: botones, faders, perillas, medidores,
// textos y relojes. Cada control dispara una ACCIÓN (una herramienta MCP con
// argumentos fijos o con la plantilla {{valor}}/{{delta}}, ir a otra página,
// una secuencia o una receta) y muestra ESTADO vivo del bus de eventos
// (ligaduras: «tally de Cámara 2 → fondo rojo», «grabando → titilar»).
//
// La app es la fuente de verdad: el modelo, los permisos y la ejecución viven
// acá. El cliente (la página web, el plugin) solo dice «apretaron la celda
// (x, y)», nunca nombres de herramientas ni argumentos. Cada perfil (un PUESTO:
// director, audio, gráficos, sala) tiene su lista de herramientas permitidas
// y la botonera rechaza todo lo que esté afuera. Lo que cambia el aire,
// estando al aire, pide confirmación en el mismo aparato.
//
// Config/botonera.json: {perfiles: [{id, nombre, herramientas_permitidas[],
//   puede_editar, paginas: [{id, nombre, columnas, filas, celdas: [Celda]}]}]}
// Config/botonera-dispositivos.json: los aparatos emparejados (solo el hash
//   del token, con sal), su perfil y página.
// Logs/botonera.log: la auditoría (emparejar, revocar, cada acción hecha o
//   rechazada, cada edición).
//
// Celda: {x, y, ancho, alto, tipo: boton|fader|perilla|medidor|texto|reloj,
//   estilo: {texto, icono, color, fondo, tamano_texto},
//   accion, accion_soltar, accion_mantener,
//   valor: {min, max, paso, inicial, modo: absoluto|relativo},
//   estado: [Ligadura]}
// Acción: {tipo: herramienta, herramienta, args} | {tipo: pagina, ir_a}
//   | {tipo: secuencia, pasos: [Acción], espera_ms} | {tipo: receta, id}
// Ligadura: {tema, ruta, op (== != > < >= <= en contiene existe), valor,
//   efecto: fondo|color|texto|titilar|valor|icono, dato}
//   (sin op, siempre aplica; en texto, «{{v}}» es el valor del tema).
#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <nlohmann/json.hpp>

#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>

namespace ba::app::control {

class BusEventos;

class Botonera : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList perfiles READ perfilesUi NOTIFY cambio)
    Q_PROPERTY(QVariantList dispositivos READ dispositivosUi NOTIFY dispositivosCambio)
    Q_PROPERTY(QVariantList solicitudes READ solicitudesUi NOTIFY solicitudesCambio)
   public:
    using json = nlohmann::json;
    /// Resultado de una herramienta: ok y un texto corto para el aparato.
    using Listo = std::function<void(bool ok, const std::string& texto)>;
    struct Ganchos {
        /// Ejecuta una herramienta MCP como «botonera:<dispositivo>» (persona).
        std::function<void(const std::string& herramienta, const json& args, const std::string& dispositivo,
                           const std::string& perfil, Listo listo)>
            ejecutar;
        /// ¿Esta llamada cambia el aire y ahora hay aire? (pide confirmación).
        std::function<bool(const std::string& herramienta, const json& args)> deAireAhora;
        /// Para las plantillas: escenas, fuentes de audio, bancas.
        std::function<std::vector<std::string>()> escenas;
        std::function<std::vector<std::string>()> fuentesAudio;
        /// (banca, nombre) del cuerpo, para la plantilla Asamblea.
        std::function<std::vector<std::pair<std::string, std::string>>()> bancas;
        /// La grilla del recinto (asamblea/Cuerpo::recinto), para la plantilla Recinto.
        std::function<json()> recinto;
        /// Una referencia de imagen («cuerpo:<banca>», «ptz:<cámara>/<posición>») como
        /// miniatura chica en data: URL ("" si no hay). Se llama al dibujar: con caché.
        std::function<std::string(const std::string& ref)> imagen;
        /// Las bancas que son de presidente (la central), para pedir confirmación.
        std::function<std::set<std::string>()> presidentes;
        /// Nombres de herramientas que existen (para validar y el selector).
        std::function<std::set<std::string>()> herramientas;
    };

    Botonera(const std::filesystem::path& datos, BusEventos& bus, Ganchos g, QObject* parent = nullptr);
    ~Botonera() override;
    /// Para completar los ganchos que dependen del servidor MCP (se arma después).
    Ganchos& ganchos() { return g_; }
    /// "tablet", "plugin", "satellite" o "" si no existe.
    std::string origenDe(const std::string& dispositivo) const;

    // --- Modelo ----------------------------------------------------------------
    json perfiles() const;
    json perfil(const std::string& id) const;
    /// Crea un perfil (vacío o desde una plantilla). Devuelve "" o el error.
    std::string crearPerfil(const json& p, const std::string& plantilla, std::string* idOut);
    /// Cambia nombre, herramientas_permitidas, puede_editar. `humano`: solo una
    /// persona puede AMPLIAR herramientas_permitidas o prender puede_editar.
    std::string editarPerfil(const std::string& id, const json& cambios, bool humano);
    bool borrarPerfil(const std::string& id);
    std::string crearPagina(const std::string& perfil, const std::string& nombre, int columnas, int filas,
                            std::string* idOut);
    std::string editarPagina(const std::string& perfil, const std::string& pagina, const json& cambios);
    bool borrarPagina(const std::string& perfil, const std::string& pagina);
    /// Pone (o reemplaza) la celda en (x, y). Valida tipo, acción y ligaduras.
    std::string setCelda(const std::string& perfil, const std::string& pagina, const json& celda);
    bool borrarCelda(const std::string& perfil, const std::string& pagina, int x, int y);
    /// Las celdas de una página con su estado renderizado ahora.
    json celdas(const std::string& perfil, const std::string& pagina) const;
    json plantillas() const;
    std::string importar(const json& perfil, std::string* idOut);
    json exportar(const std::string& perfil) const;

    // --- Aparatos ----------------------------------------------------------------
    /// [{id, nombre, origen: tablet|plugin|satellite, perfil, pagina, ip,
    ///   creado_ms, ultimo_uso_ms, conectado, vence_dias}] (sin hashes).
    json dispositivos() const;
    std::string asignar(const std::string& dispositivo, const std::string& perfil, const std::string& pagina);
    /// Saca un aparato (o todos con "todos"): su token deja de valer.
    int revocar(const std::string& dispositivo);
    /// Un aparato que no se empareja (plugin de hardware, satélite): se da de
    /// alta tal cual, sin token. Si no tiene perfil, toma el primero.
    void registrarLocal(const std::string& id, const std::string& nombre, const std::string& origen,
                        const json& capacidades);
    void quitarLocal(const std::string& id);
    /// La lista de {id, hash, sal} que el gateway usa para autenticar tablets.
    json credencialesParaGateway() const;
    void marcarConexion(const std::string& dispositivo, bool conectado, const std::string& ip);

    // --- Emparejamiento (lo humano pasa por la interfaz) ------------------------------
    /// Un código de un uso (128 bits, 2 min) atado a `perfil`. {codigo, vence_ms}.
    json nuevoCodigo(const std::string& perfil, const std::string& nombre, int venceDias);
    /// El gateway canjeó un código: queda una solicitud que una persona aprueba.
    /// "" o el error (código inválido, vencido o ya usado).
    std::string solicitud(const std::string& solicitud, const std::string& codigo, const std::string& nombre,
                   const std::string& ip);
    /// Aprueba (genera el token y devuelve {dispositivo, token}) o rechaza.
    json resolverSolicitud(const std::string& solicitud, bool aprobar);

    // --- Lo que llega de un aparato ----------------------------------------------------
    /// {type: presionar|soltar|valor|girar|pagina|confirmar|hola, x, y, v, delta, id}.
    void entrada(const std::string& dispositivo, const json& msg);

    // --- API remota HTTP (T59: «Generic HTTP» de Bitfocus Companion) ------------
    /// Prendida o apagada (apagada, el gateway rechaza los tokens HTTP con 401).
    bool apiRemotaActiva() const;
    void setApiRemotaActiva(bool on);
    /// Un acceso HTTP para `perfil`: un aparato más (origen tablet, vía http) con su
    /// token, que se devuelve UNA vez: {dispositivo, token, perfil, ejemplo} o {error}.
    json crearAccesoHttp(const std::string& perfil, const std::string& nombre);
    /// Los accesos HTTP: [{id, nombre, perfil, ultimo_uso_ms}].
    json accesosHttp() const;
    /// Una celda apretada por la API remota. "" o por qué no (queda en la auditoría).
    std::string entradaRemota(const std::string& dispositivo, const std::string& perfil, const std::string& pagina,
                              int x, int y, const std::string& accion, const std::string& ip);
    /// El asistente prueba una celda por el camino completo, como el aparato
    /// «asistente» (sus acciones de aire las aprueba una persona en el escritorio).
    std::string simular(const std::string& perfil, const std::string& pagina, const json& msg);
    /// Lo que el aparato tiene que mostrar ahora (página entera).
    json render(const std::string& dispositivo) const;

    // --- Auditoría -------------------------------------------------------------------
    json auditoria(std::int64_t desdeMs, const std::string& dispositivo) const;
    void auditar(const std::string& dispositivo, const std::string& que, const std::string& detalle);

    // --- Para la interfaz (QML) ----------------------------------------------------------
    QVariantList perfilesUi() const;
    QVariantList dispositivosUi() const;
    QVariantList solicitudesUi() const;
    Q_INVOKABLE QVariantMap paginaUi(const QString& perfil, const QString& pagina) const;
    Q_INVOKABLE QString crearPerfilUi(const QString& nombre, const QString& plantilla);
    Q_INVOKABLE QString editarPerfilUi(const QString& id, const QVariantMap& cambios);
    Q_INVOKABLE bool borrarPerfilUi(const QString& id);
    Q_INVOKABLE QString crearPaginaUi(const QString& perfil, const QString& nombre, int columnas, int filas);
    Q_INVOKABLE QString editarPaginaUi(const QString& perfil, const QString& pagina, const QVariantMap& cambios);
    Q_INVOKABLE bool borrarPaginaUi(const QString& perfil, const QString& pagina);
    /// La celda como texto JSON (lo edita el panel derecho). "" o el error.
    Q_INVOKABLE QString setCeldaUi(const QString& perfil, const QString& pagina, const QString& celdaJson);
    Q_INVOKABLE QString celdaJsonUi(const QString& perfil, const QString& pagina, int x, int y) const;
    Q_INVOKABLE bool borrarCeldaUi(const QString& perfil, const QString& pagina, int x, int y);
    Q_INVOKABLE QVariantList plantillasUi() const;
    Q_INVOKABLE QString exportarUi(const QString& perfil) const;
    Q_INVOKABLE QString importarUi(const QString& texto);
    /// Probar una celda desde el escritorio (como si la apretaran en el aparato «escritorio»).
    Q_INVOKABLE void probarUi(const QString& perfil, const QString& pagina, int x, int y);
    Q_INVOKABLE QString asignarUi(const QString& dispositivo, const QString& perfil, const QString& pagina);
    Q_INVOKABLE int revocarUi(const QString& dispositivo);
    Q_INVOKABLE QVariantList auditoriaUi() const;
    Q_INVOKABLE QStringList herramientasUi(const QString& filtro) const;

   signals:
    void cambio();
    void dispositivosCambio();
    void solicitudesCambio();
    /// Hay que mandarle algo a un aparato: {type: estado|confirmar|aviso, …}.
    void mensaje(const QString& dispositivo, const nlohmann::json& msg);
    /// Cambió la lista de credenciales (sumar, revocar, vencer): al gateway.
    void credencialesCambio();
    /// Un emparejamiento espera a una persona.
    void solicitudNueva(const QString& solicitud, const QString& nombre, const QString& perfil, const QString& ip);

   private:
    struct Pendiente { // acción de aire esperando confirmación en el aparato
        std::string celda;
        json accion;
        std::int64_t venceMs = 0;
    };
    void cargar();
    void guardar() const;
    void guardarDispositivos() const;
    json* buscarPerfil(const std::string& id);
    const json* buscarPerfil(const std::string& id) const;
    json* buscarPagina(const std::string& perfil, const std::string& pagina);
    const json* buscarPagina(const std::string& perfil, const std::string& pagina) const;
    json* buscarDispositivo(const std::string& id);
    const json* buscarDispositivo(const std::string& id) const;
    const json* celdaEn(const json& pagina, int x, int y) const;
    json renderCelda(const json& celda, const std::string& claveValor) const;
    json renderPagina(const json& perfil, const json& pagina, const std::string& dispositivo) const;
    std::string validarCelda(const json& c) const;
    std::string validarAccion(const json& a, int profundidad) const;
    void correr(const std::string& dispositivo, const json& perfil, const json& accion, const std::string& celda,
                double valor, double delta, bool confirmado);
    void avisar(const std::string& dispositivo, const std::string& texto, bool error);
    void refrescar(); ///< recalcula lo que muestra cada aparato y manda lo que cambió
    bool limiteOk(const std::string& dispositivo);
    json armarPlantilla(const std::string& id) const;
    std::string idNuevo(const char* prefijo) const;
    void vencerDispositivos();

    std::filesystem::path archivo_, archivoDisp_, log_;
    BusEventos& bus_;
    Ganchos g_;
    json datos_;
    json disp_ = json::array();
    std::map<std::string, std::string> paginaDe_; ///< dispositivo -> página (si no es la guardada)
    std::map<std::string, double> valores_;      ///< perfil/página/x/y -> último valor de fader/perilla
    std::map<std::string, std::pair<json, double>> faderPend_; ///< dispositivo|celda -> (acción, valor) a mandar
    std::map<std::string, std::pair<json, double>> perillaPend_; ///< dispositivo|celda -> (acción, delta acumulado)
    std::map<std::string, Pendiente> pendientes_; ///< dispositivo -> confirmación de aire
    std::map<std::string, std::int64_t> presionadoMs_; ///< dispositivo|celda -> cuándo se apretó (mantener)
    std::set<std::string> mantenidos_;
    std::map<std::string, std::deque<std::int64_t>> acciones_; ///< límite por aparato
    std::map<std::string, json> ultimoRender_;
    std::set<std::string> conectados_;
    struct Codigo {
        std::string perfil, nombre;
        int venceDias = 30;
        std::int64_t venceMs = 0;
    };
    std::map<std::string, Codigo> codigos_; ///< código -> a qué perfil
    struct Solicitud {
        std::string codigo, nombre, ip, perfil;
        int venceDias = 30;
        std::int64_t ms = 0;
    };
    std::map<std::string, Solicitud> solicitudes_;
    std::deque<json> auditoria_;
    QTimer refresco_;
    QTimer coalescer_;
    int sub_ = 0;
    bool sucio_ = true;
    bool enCoalescer_ = false; ///< faders y perillas: no cuentan para el límite de acciones
};

} // namespace ba::app::control
