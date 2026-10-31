// ============================================================================
// src/app/emision/FotoMotor.hpp — la foto del motor que lee la interfaz (F3-03)
//
// docs/arquitectura/05-motor-del-plano.md. El motor es del plano de control: la interfaz
// no lo lee directo, lee ESTA foto, que arma su dueño (AppRuntime::publicarFoto, con el
// turno, en el hilo de control) y le llega por la ventanilla (emision/VentanillaMotor.hpp).
// Es un valor: se copia entera, no tiene punteros al motor y la interfaz la lee sin
// candados ni turno (la que tiene la ventanilla no cambia hasta que la interfaz la cambia
// en su hilo, al avisar).
//
// POR PARTES: cada parte se rearma cuando su dueño sabe que cambió (los avisos del runtime:
// AppRuntime::Cambio) o en cada vuelta del reloj del motor (lo que se mueve solo: lo que
// sale, la salud, los niveles), y se compara con la anterior: la ventanilla avisa qué
// partes cambiaron (VentanillaMotor::cambio) y cada view model escucha las suyas.
//
//   Escenas  las escenas en el orden del operador, con su carpeta, si está protegida y sus
//            fuentes (de abajo hacia arriba: tipo, visible, lugar, efectos); todas las
//            fuentes; los tipos que se pueden crear; las transiciones de entrada.
//   Salidas  cada salida (la por omisión primera): nombre, color, codificador, la escena al
//            aire y la armada, lo que sale (transmite, graba, el archivo, sus destinos), si
//            sale algo, el negro y la placa, el AFV, la T-bar, las capas DSK; el tope.
//   Estado   lo que sale de la por omisión (las pastillas: grabando, transmitiendo,
//            reconectando, cuadros, el codificador) y si el último corte se pidió.
//   Emision  los destinos, la grabación (y la repetición), la señal limpia, las ISO, el
//            disco en apuros, el remux, lo que oye cada invitado.
//   Ajustes  el lienzo, la transición global, el tema, los invitados, el servidor y la clave.
//   Medidas  lo que se mueve siempre (los medios que suenan y cuánto les queda, la salud de
//            cada destino, el loudness de cada salida): se lee cuando se lo necesita.
//   Configuracion  la configuración guardada entera (app.json, gateway.json, launcher.json),
//            una copia que no cambia: la leen los formularios de Configuración, la voz, el
//            asistente. Se rearma cuando se aplica (AppRuntime::Cambio::Ajustes) o alguien la
//            ensucia (cambiar el micrófono del operador).
//   Equipo   lo que vigilan los chequeos del equipo (F4-07), los recursos y las alarmas:
//            el reloj del estudio, la foto de las salidas del motor (sesiones de
//            codificador, el colchón del disco, cada salida), el pico de cada entrada de
//            audio y lo del motor que lee el MCP (el último error, el codificador de
//            respaldo, cada salida como la ve status()); dos veces por segundo.
//   Sala     lo que leen la asamblea, la sala y la línea de tiempo: lo guardado de la sala
//            (gateway.json: el puerto, el túnel, pinggy, el proxy, la atención automática),
//            con qué aparece el operador ante el lobby, la carpeta de grabación, las
//            fuentes con audio y la fuente «Banca con la palabra» (si hay y en qué escenas).
// ============================================================================
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::app {

struct AppConfig;
struct GatewaySettings;
struct LauncherSettings;

struct FotoMotor {
    /// Las partes (bits): lo que avisa la ventanilla.
    enum Parte : unsigned {
        Escenas = 1u << 0,
        Salidas = 1u << 1,
        Estado = 1u << 2,
        Emision = 1u << 3,
        Ajustes = 1u << 4,
        Medidas = 1u << 5,
        Sala = 1u << 12, // (los bits de cada área: I 9, M 10, C 11, S 12, R 13 y 14; el 8 es de AppRuntime)
        Configuracion = 1u << 13,
        Equipo = 1u << 14,
        Todas = Escenas | Salidas | Estado | Emision | Ajustes | Medidas | Sala | Configuracion | Equipo,
    };

    // --- Escenas ---------------------------------------------------------------------
    struct Lugar {
        float x = 0, y = 0, w = 1, h = 1; ///< fracciones del lienzo
        bool operator==(const Lugar&) const = default;
    };
    struct Fuente {
        std::string nombre, tipo;
        bool visible = true;
        Lugar lugar;
        nlohmann::json efectos = nlohmann::json::object(); ///< EfectosCapa::aJson
        bool operator==(const Fuente&) const = default;
    };
    struct Escena {
        std::string nombre, carpeta;
        bool protegida = false;
        std::vector<Fuente> fuentes; ///< de abajo hacia arriba (el orden del motor)
        bool operator==(const Escena&) const = default;
    };
    struct Tipo {
        std::string id, nombre;
        bool video = false, audio = false;
        bool operator==(const Tipo&) const = default;
    };
    std::vector<Escena> escenas; ///< en el orden del operador
    /// Todas las fuentes que existen (en alguna escena o globales): {nombre, tipo}.
    std::vector<std::pair<std::string, std::string>> fuentes;
    std::vector<Tipo> tipos;
    /// {escena: {tipo, ms}}: con qué transición entra cada una (escenas::Conmutadores).
    nlohmann::json transicionesEntrada = nlohmann::json::object();

    // --- Salidas -----------------------------------------------------------------------
    struct Salida {
        std::string id, nombre, color, codificador;
        bool porOmision = false;
        std::string alAire, armada; ///< las escenas de su programa y de su preview
        std::string archivo;        ///< lo que graba ("" si no graba)
        bool transmitiendo = false; ///< su principal o algún destino
        bool grabando = false, reconectando = false;
        bool sale = false; ///< sale algo de ella (BroadcastEngine::haySalidaEn)
        nlohmann::json destinos = nlohmann::json::array(); ///< los suyos: [{id, nombre, activo, reconectando, respaldo}]
        nlohmann::json placa = nlohmann::json::object();   ///< {ftb, ftb_audio, placa, placa_texto, placa_imagen}
        std::vector<std::string> afv;  ///< las fuentes que siguen al video en ella
        std::vector<std::string> dsk;  ///< las fuentes de sus capas DSK
        double tbar = 0;               ///< por dónde va la T-bar (0 = quieta)
        std::string manualHacia;       ///< la escena hacia la que va la T-bar
        bool operator==(const Salida&) const = default;
    };
    std::vector<Salida> salidas; ///< la por omisión primera
    int tope = 1;

    // --- Estado (la por omisión) -----------------------------------------------------
    struct EstadoAire {
        bool transmite = false, graba = false, grabacionCaida = false, reconectando = false;
        std::string problema, codificador;
        double fps = 0;
        int descartados = 0, cuadros = 0;
        std::uint64_t bytes = 0;
        bool haySalida = false;     ///< sale algo de alguna salida (grabación incluida)
        bool hayTransmision = false; ///< sale algo de alguna salida, sin contar la grabación
        bool corteGrabacionPedido = true, corteTransmisionPedido = true;
        bool operator==(const EstadoAire&) const = default;
    };
    EstadoAire estado;

    // --- Emisión -----------------------------------------------------------------------
    nlohmann::json destinos = nlohmann::json::array(); ///< AppRuntime::destinos
    nlohmann::json grabacion = nlohmann::json::object(); ///< grabacionEstado + {replay}
    nlohmann::json limpia = nlohmann::json::object();    ///< cleanFeedEstado
    nlohmann::json isos = nlohmann::json::array();       ///< [{fuente, archivo, mb, audioPropio, activa, pausada}]
    nlohmann::json apurosDisco = nlohmann::json::object(); ///< discoEstado
    nlohmann::json remux = nlohmann::json::object();     ///< remuxEstado
    nlohmann::json retornos = nlohmann::json::object();  ///< {slot: "mix_minus" | "programa"}

    // --- Ajustes -----------------------------------------------------------------------
    struct AjustesVideo {
        int baseAncho = 1920, baseAlto = 1080, salidaAncho = 1920, salidaAlto = 1080;
        int fpsNum = 30, fpsDen = 1;
        std::string transicion;
        int transicionMs = 300;
        std::string tema;
        int invitados = 0;
        std::string servidor;          ///< el de la transmisión de la configuración
        bool claveEnEntorno = false;   ///< la variable de la clave tiene algo
        bool claveGuardada = false;    ///< hay una clave guardada (cifrada)
        std::string sala;              ///< el nombre de la sala (gateway)
        bool operator==(const AjustesVideo&) const = default;
    };
    AjustesVideo ajustes;

    // --- Medidas -----------------------------------------------------------------------
    nlohmann::json medios = nlohmann::json::array();   ///< [{fuente, estado, duracion_ms, tiempo_ms}]
    nlohmann::json salud = nlohmann::json::object();   ///< BroadcastEngine::saludSalidas
    nlohmann::json loudness = nlohmann::json::object(); ///< {salida: LoudnessPrograma::json}

    // --- Configuracion -------------------------------------------------------------------
    /// Copias que nadie cambia (el dueño arma otras cuando cambia la configuración): se
    /// comparan por puntero. Nunca nulas después de la primera foto.
    struct Guardada {
        std::shared_ptr<const AppConfig> app;
        std::shared_ptr<const GatewaySettings> gateway;
        std::shared_ptr<const LauncherSettings> launcher;
        bool operator==(const Guardada&) const = default;
    };
    Guardada configuracion;

    // --- Equipo ----------------------------------------------------------------------
    struct DelEquipo {
        std::string relojTipo, relojError; ///< BroadcastEngine::relojEstado
        nlohmann::json salidas = nlohmann::json::object(); ///< BroadcastEngine::fotoSalidas
        nlohmann::json audio = nlohmann::json::array();    ///< [{fuente, pico_db, mudo}] (audioSources)
        /// {ultimo_error, respaldo_codificador, salidas: [{id, nombre, por_omision, escena,
        /// streaming, reconectando, recording, destinos, limpia, repeticion}]} (EngineStatus).
        nlohmann::json motor = nlohmann::json::object();
        bool operator==(const DelEquipo&) const = default;
    };
    DelEquipo equipo;
    // --- Sala (la asamblea, la sala y la línea) ---------------------------------------
    struct SalaYEvento {
        // Lo guardado de la sala (gateway.json), lo que se lee de este lado.
        std::string sala;           ///< el nombre (room)
        int puerto = 8787;          ///< el de su página (port)
        bool atencionAutomatica = false; ///< autoIntake
        nlohmann::json proxy = nlohmann::json::object();
        bool tunel = false;         ///< Cloudflare
        std::string tunelHost;
        bool pinggyActivo = false, pinggyTurn = true, pinggyTurnUdp = false;
        std::string pinggyRegion, pinggyDominioTipo, pinggyHost, pinggyCnameDestino, pinggyListaBlanca;
        int pinggyPuertoTunel = 8786;
        // Lo del operador y lo que se graba.
        std::string operadorVideo, operadorAudio; ///< con qué aparece ante el lobby
        std::string carpetaGrabacion;             ///< cfg rec.directory
        std::vector<std::string> audio;           ///< las fuentes con audio (BroadcastEngine::audioSources)
        // La fuente «Banca con la palabra».
        bool hayFuenteBanca = false;
        std::vector<std::string> escenaDeRanura; ///< [ranura - 1]: la primera escena que la muestra
        bool operator==(const SalaYEvento&) const = default;
    };
    SalaYEvento sala;

    // --- Para leerla -----------------------------------------------------------------
    const Escena* escena(const std::string& nombre) const {
        for (const Escena& e : escenas)
            if (e.nombre == nombre) return &e;
        return nullptr;
    }
    /// La salida `id` ("" = la por omisión), o nullptr.
    const Salida* salida(const std::string& id) const {
        for (const Salida& s : salidas)
            if (id.empty() ? s.porOmision : s.id == id) return &s;
        return nullptr;
    }
    /// El tipo de la fuente `nombre` ("" si no existe).
    std::string tipoDe(const std::string& nombre) const {
        for (const auto& [n, t] : fuentes)
            if (n == nombre) return t;
        return {};
    }
    /// Las escenas en el orden del operador (sus nombres).
    std::vector<std::string> nombresDeEscenas() const {
        std::vector<std::string> out;
        for (const Escena& e : escenas) out.push_back(e.nombre);
        return out;
    }
    /// ¿Existe esa fuente (en alguna escena o global)?
    bool hayFuente(const std::string& nombre) const {
        for (const auto& [n, t] : fuentes)
            if (n == nombre) return true;
        return false;
    }
    /// Las fuentes de una escena (sus nombres, de abajo hacia arriba).
    std::vector<std::string> fuentesDe(const std::string& escena) const {
        std::vector<std::string> out;
        if (const Escena* e = this->escena(escena))
            for (const Fuente& f : e->fuentes) out.push_back(f.nombre);
        return out;
    }
};

} // namespace ba::app
