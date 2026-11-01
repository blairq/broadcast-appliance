// ============================================================================
// src/app/registro/ConexionesTotem.hpp — las conexiones en vivo de los tótems (modelo)
//
// Cada página del tótem que está conectada (la TV y cada celular que escaneó su QR) es
// una CONEXIÓN, y cada una lleva su conversación entera: quién tiene adelante (la cara),
// el turno de lo que se oye, lo que dice el asistente, la foto de frente, el documento,
// el modelo que entiende y la atención remota (una persona del equipo que la toma). Acá
// están las REGLAS de todo eso, y el TIC que las hace andar.
//
// Sin Qt, sin audio, sin red y sin reloj propio: lo de afuera lo hace quien lo usa
// (ui/TotemsViewModel) por los `Puertos` —mandarle algo a la página, sintetizar una frase,
// pedir el último cuadro de la cámara al reconocimiento o al lector, guardar en el padrón—
// y lo que vuelve (lo oído, la cara, el documento leído, lo que escribe el modelo) entra por
// los métodos de abajo. La hora viene siempre como argumento.
//
// EL TIC. Las esperas (el fin del turno, la voz que termina, la foto, el documento, el
// silencio, las gracias, el arranque con canal y sin cara) las decide registro/RitmoTotem en
// cada vuelta de `tic`. El tic corre SOLO mientras alguna conexión está activa (con canal de
// audio, conversando, hablando o con la pantalla de gracias): el modelo lo pide y lo suelta
// por `Puertos::pedirTic` y quien lo usa pone el reloj. Una TV conectada esperando, sin
// canal, no despierta a nadie.
//
// Con test (test_registro_conexiones), con unos Puertos de mentira y la hora a mano.
// ============================================================================
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "registro/Conversacion.hpp"
#include "registro/RitmoTotem.hpp"

namespace ba::documentos {
class ConfirmadorMrz;
}

namespace ba::registro {

class Totems;

/// El modelo que entiende de UNA conversación (registro/AgenteTotem, con Qt), visto desde
/// acá: lo que se le pide y lo que se le pregunta.
class Entendedor {
   public:
    virtual ~Entendedor() = default;
    /// Lo que dijo la persona. false = no es para el modelo: lo contesta el guion.
    virtual bool oido(const std::string& dicho) = 0;
    /// Un turno en curso (pensando o escribiendo).
    virtual bool ocupado() const = 0;
    virtual bool caido() const = 0;
    virtual std::string motivoCaida() const = 0;
    /// El nombre del servidor con que entiende («modelo local», el de la nube).
    virtual std::string nombre() const = 0;
    virtual void cancelar() = 0;
};

class ConexionesTotem {
   public:
    // Cada cuánto se mira (los tiempos de la conversación son de registro/RitmoTotem).
    static constexpr std::int64_t kEntreFotosMs = 600; ///< a lo sumo una foto al reconocimiento por conexión
    static constexpr std::int64_t kEntreFotosFrenteMs = 250; ///< en el paso de la foto, más seguido: se busca el momento
    static constexpr std::int64_t kEntreDocumentosMs = 300; ///< mostrando el documento: un cuadro al lector cada tanto

    /// Lo que le pide al mundo (lo hace ui/TotemsViewModel). `conn` es la conexión.
    class Puertos {
       public:
        virtual ~Puertos() = default;
        // --- Los avisos para la vista ---
        virtual void vivoCambio() = 0;        ///< cambió algo de lo vivo (filas, lo oído, quién atiende)
        virtual void conectadosCambio() = 0;  ///< cambió cuántas conexiones tiene un tótem
        virtual void registradosCambio() = 0; ///< alguien quedó en el padrón
        /// Que el reloj llame a `tic` (cada 200 ms) o que pare: hay o no conexiones activas.
        virtual void pedirTic(bool si) = 0;

        // --- La página y el gateway ---
        /// Un mensaje a la página de una conexión.
        virtual void enviar(const std::string& conn, const nlohmann::json& msg) = 0;
        /// Un pedido POST a la API de control del registro (/api/v1/registro/<ruta>), sin respuesta.
        virtual void alGateway(const std::string& ruta, const nlohmann::json& cuerpo) = 0;

        // --- El audio ---
        /// Hay canal: el oído (reconocedor) y la voz que baja por ese canal.
        virtual void abrirAudio(const std::string& conn, int canal) = 0;
        virtual void cerrarAudio(const std::string& conn, int canal) = 0;
        /// El reconocedor puede no estar cuando llega el canal (carga en segundo plano): en
        /// cada vuelta se vuelve a probar abrir el oído.
        virtual void revisarOido(const std::string& conn, int canal) = 0;
        /// Sintetiza `texto` y lo baja por el canal; al terminar esa frase, quien sintetiza
        /// llama a `alTerminarFrase(conn)`. false = no hay voz (la página lo muestra escrito).
        virtual bool sintetizar(const std::string& conn, int canal, const std::string& texto) = 0;
        /// El asistente se calla en el acto: lo que falta del canal (y, con `sintesis`, lo
        /// que se estaba sintetizando para todos). Lo que llegue tarde no sale.
        virtual void callar(const std::string& conn, int canal, bool sintesis) = 0;
        /// Terminó de hablar: al oído le vuelve a llegar lo que se oye (no ceros).
        virtual void escuchar(const std::string& conn) = 0;
        /// El oído sigue oyendo mientras habla el asistente (Nemotron y «puede interrumpir»).
        virtual bool oyeMientrasHabla(const std::string& conn) const = 0;
        /// El VAD oye voz ahora.
        virtual bool oyendoVoz(const std::string& conn) const = 0;
        /// Lo dicho todavía no salió entero por el canal.
        virtual bool vozPendiente(int canal) const = 0;

        // --- La cara y el documento (los cuadros de la cámara del tótem) ---
        /// El último cuadro de la cámara del canal, si es nuevo, al reconocimiento: la cara
        /// vuelve por `alCara` (o `sinCara` si falló). false = no había uno nuevo (o no hay
        /// reconocimiento).
        virtual bool pedirCara(const std::string& conn, int canal) = 0;
        /// Lo mismo, al lector de documentos: vuelve por `alLeerDocumento`.
        virtual bool pedirDocumento(const std::string& conn, int canal, const std::vector<std::string>& aceptados) = 0;
        /// El código que leyó la página (BarcodeDetector), al lector: vuelve por `alLeerDocumento`.
        virtual void traducirCodigo(const std::string& conn, const std::string& formato, const std::string& texto,
                                    const std::vector<std::string>& aceptados) = 0;
        /// La foto entera que mandó la página (sin BarcodeDetector): se la pide al gateway y
        /// se la pasa a `alCuadroDePagina`.
        virtual void pedirCuadroDePagina(const std::string& conn) = 0;
        /// Hay o no alguien mostrando un documento: el modelo con visión (el dorso) se carga
        /// antes de que haga falta o se suelta.
        virtual void prepararDorso() = 0;
        virtual void soltarDorso() = 0;
        virtual bool dorsoDisponible() const = 0;

        // --- El padrón, el modelo que entiende ---
        /// Ya está en el padrón de este evento por un registro.
        virtual bool yaRegistrado(const std::string& personaId) const = 0;
        /// Terminó bien: al padrón (y, si la cara no se conocía, el alta con `foto`).
        virtual void guardarRegistro(const std::string& conn, const std::string& totem, const std::string& nombre,
                                     const std::string& personaId, const nlohmann::json& datos,
                                     const std::string& foto) = 0;
        /// El modelo que entiende para la conversación de `conn` (nullptr: el guion solo).
        /// `avisar`: al empezar (que lo diga en el registro y lo precaliente).
        virtual std::unique_ptr<Entendedor> crearEntendedor(const std::string& conn, const std::string& totem,
                                                            Conversacion& conv, bool avisar) = 0;
        /// Que el modelo local vaya cargando (se conectó un tótem); y soltarlo (no queda nadie).
        virtual void prepararModelo() = 0;
        virtual void soltarModelo() = 0;
    };

    /// Lo que vio el reconocimiento en un cuadro.
    struct Cara {
        std::string personaId, nombre;
        bool hayCara = false;
        bool sirve = false;          ///< alcanza para el alta (y para la foto de frente)
        std::string sugerencia;      ///< si no sirve, qué corregir (en el idioma del evento)
        std::string foto;            ///< el cuadro en JPEG (el de la ficha, si hay alta)
    };
    /// Lo que leyó el lector (el frente o el dorso).
    struct Lectura {
        bool leido = false;
        nlohmann::json datos;        ///< los normalizados (documentos::Datos::aJson)
        bool hayCodigos = false;     ///< vio un código, pero no de un documento aceptado
        // Del dorso:
        std::string con;             ///< con qué se leyó (para el registro)
        std::string error;           ///< por qué no (el modelo no contestó…)
        bool dudosa = false;         ///< cerró casi todo: la próxima foto que coincida la confirma
        bool noSeVe = false;         ///< no se ven los renglones
        bool hayDetalle = false;     ///< se ven, pero no cierran
    };

    explicit ConexionesTotem(Puertos& p);
    ~ConexionesTotem();
    ConexionesTotem(const ConexionesTotem&) = delete;
    ConexionesTotem& operator=(const ConexionesTotem&) = delete;

    /// Los tótems del evento abierto (los tiene quien los configura; tienen que vivir más
    /// que esto o cambiarse antes) y el nombre y el idioma del evento ("" = el del operador).
    void ponerEvento(const Totems* totems, const std::string& nombre, const std::string& idioma);
    /// La pantalla de cada conexión, con lo de ahora (cambió el tótem o el nombre del evento).
    void mandarPantallas();
    /// Otro evento: a cada página «reiniciar» y se sueltan todas.
    void reiniciarTodas(std::int64_t ahora);
    /// El gateway se fue: no hay nadie conectado (ni páginas de atención).
    void soltarTodas(std::int64_t ahora);

    // --- Lo que llega del gateway -------------------------------------------------------
    /// Un evento «registro» (conexion, canal, audio, toque, codigo, cuadro, intento y los de
    /// la atención remota).
    void alEvento(const nlohmann::json& ev, std::int64_t ahora);
    /// Lo que contestó /api/v1/registro/estado (el gateway volvió): quién está.
    void reconocer(const nlohmann::json& estado, std::int64_t ahora);

    // --- Lo que vuelve de los Puertos -------------------------------------------------------
    void alOido(const std::string& conn, const std::string& texto, std::int64_t ahora);
    /// La persona habló mientras el asistente hablaba (solo con «interrumpir»).
    void interrumpido(const std::string& conn, std::int64_t ahora);
    void alTerminarFrase(const std::string& conn);
    void alCara(const std::string& conn, const Cara& cara, std::int64_t ahora);
    void sinCara(const std::string& conn);
    void alLeerDocumento(const std::string& conn, const Lectura& r, std::int64_t ahora);
    void alLeerDorso(const std::string& conn, const Lectura& r, std::int64_t ahora);
    /// La foto de la página llegó (`hay`) o no. Lo que hay que hacer con ella: leer el
    /// dorso (una por vez), leer el código del frente, con qué tipos.
    struct QueLeer {
        bool dorso = false, frente = false;
        std::vector<std::string> aceptados;
        /// Las lecturas dudosas del dorso de esta conexión (hasta que dos coinciden).
        std::shared_ptr<documentos::ConfirmadorMrz> lecturas;
    };
    QueLeer alCuadroDePagina(const std::string& conn, bool hay);
    /// El alta de la cara terminó: desde ahora esa cara es conocida (no «otra persona»).
    void conocida(const std::string& conn, const std::string& personaId);
    // El modelo que entiende, mientras contesta.
    void alTextoModelo(const std::string& conn, const std::string& acumulado, std::int64_t ahora);
    void alFraseModelo(const std::string& conn, const std::string& frase, std::int64_t ahora);
    void alListoModelo(const std::string& conn, const Conversacion::Respuesta& r, std::int64_t ahora);
    void alCaerModelo(const std::string& conn, const std::string& motivo);

    /// Una vuelta del reloj: la cara, el documento y las esperas de cada conexión.
    void tic(std::int64_t ahora);
    /// Hay alguna conexión activa (el tic anda).
    bool activo() const { return ticPedido_; }

    // --- La atención remota (docs/funciones/REGISTRO-TOTEM.md, «Atención remota») ----------
    /// Atenderla (`aconn`: la página de atención; "" = la app o el MCP). "" o por qué no.
    std::string atender(const std::string& conn, const std::string& quien, const std::string& aconn, std::int64_t ahora);
    /// `como`: asistente, guardar o cancelar.
    std::string terminar(const std::string& conn, const std::string& como, const std::string& aconn, std::int64_t ahora);
    std::string ponerDato(const std::string& conn, const std::string& clave, const std::string& valor,
                          const std::string& aconn, std::int64_t ahora);
    /// Quienes tienen abierta la página de atención: [{aconn, nombre, lugar, ip, atiende}].
    nlohmann::json atiendenJson() const;
    bool hayAtienden() const { return !atienden_.empty(); }
    /// Las conversaciones como las ve la página de atención (con lo que hace falta para editar).
    nlohmann::json conversacionesJson() const;
    /// Lo que se manda a las páginas de atención ("" = a todas): {atiende|aconn, msg}.
    nlohmann::json paraAtencion(const std::string& aconn = {}) const;

    // --- Lo que se lee -------------------------------------------------------------------
    /// Las filas: [{conn, id, totem, canal, audio, ip, lugar, desdeMs, estado, paso, quien,
    /// oido, dice, atendida, atiende, campos, modo, modelo, modoPorQue}].
    nlohmann::json filas() const;
    /// Lo mismo, para afuera (MCP): sin los datos de un documento.
    nlohmann::json filasParaAfuera() const;
    /// Cuántas conexiones tiene un tótem, y cuántas conversan.
    int conectados(const std::string& totem) const;
    int conversando() const;
    bool hayConexiones() const { return !conns_.empty(); }

   private:
    struct Conexion;
    Conexion* buscar(const std::string& conn);
    const Conexion* buscar(const std::string& conn) const;
    Conexion& conexion(const std::string& conn, const std::string& totem, std::int64_t ahora);
    void soltar(const std::string& conn);
    nlohmann::json fila(const Conexion& c) const;
    ba::i18n::En idioma() const;

    void mandarPantalla(Conexion& c);
    void mirarCara(Conexion& c, std::int64_t ahora);
    void mirarDocumento(Conexion& c, std::int64_t ahora);
    void empezar(Conexion& c, const std::string& personaId, const std::string& nombre, std::int64_t ahora);
    void alToque(Conexion& c, const nlohmann::json& ev, std::int64_t ahora);
    void alCodigoDePagina(Conexion& c, const nlohmann::json& ev);
    void cerrarTurno(Conexion& c, std::int64_t ahora);
    bool alOidoAgente(Conexion& c, const std::string& texto);
    void crearAgente(Conexion& c, bool avisar);
    /// Hace lo que dice la conversación: decir, mostrar, pedir, guardar, terminar, y `foto`:
    /// buscar una toma de frente en la cámara cuando termine de decirlo.
    void aplicar(Conexion& c, const Conversacion::Respuesta& r, std::int64_t ahora);
    void decir(Conexion& c, const std::string& texto);
    void hablar(Conexion& c, const std::string& texto);
    void callarAsistente(Conexion& c);
    void liberarAtencion(Conexion& c);
    bool alEventoAtencion(const nlohmann::json& ev, std::int64_t ahora);
    /// ¿La conexión necesita el reloj?
    static bool activa(const Conexion& c);
    /// Pide o suelta el tic según haya conexiones activas (al final de cada entrada).
    void revisarTic();

    Puertos& p_;
    const Totems* totems_ = nullptr;
    std::string evento_, idioma_;
    std::map<std::string, std::unique_ptr<Conexion>> conns_;
    struct Atiende {
        std::string nombre, ip, lugar;
    };
    std::map<std::string, Atiende> atienden_;
    bool ticPedido_ = false;
};

} // namespace ba::registro
