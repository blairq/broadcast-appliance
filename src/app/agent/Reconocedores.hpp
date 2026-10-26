// ============================================================================
// src/app/agent/Reconocedores.hpp — qué reconocedor de voz usa cada uno, y cuáles están cargados
//
// HAY DOS MOTORES de voz a texto (audio/Reconocedor.hpp): Parakeet (offline,
// frase por frase; lo de siempre) y Nemotron (streaming: muchas voces a la vez,
// texto parcial, se lo puede interrumpir). Ninguno reemplaza al otro: CADA
// CONSUMIDOR elige el suyo en la pestaña Agentes —el dictado del operador, la
// transcripción continua (subtítulos y traducción en tiempo real), la recepción
// que atiende hablando y el tótem de registro—. De fábrica, todos Parakeet: nadie
// cambia de comportamiento sin pedirlo.
//
// UNA COPIA DE CADA MODELO EN MEMORIA. Cada uso abierto (una corriente con su
// clave: «registro:<conn>», «atencion:<id>», el nombre de una salida de texto,
// «dictado») cuenta para su motor. El motor se carga con el primer uso y se
// descarga cuando no queda ninguno, pasado un MARGEN (`escucha.descargarTrasS`,
// 2 min): un tótem que se reconecta no recarga 700 MB.
//
// LA ALERTA. Si los dos motores están en uso a la vez por consumidores
// distintos, hay dos modelos en memoria (~1 GB cada uno): la pestaña Agentes, el
// MCP (`voz_reconocedores`) e `ia_estado` lo dicen, con quién usa cada uno,
// sugiriendo unificar.
//
// Sin Qt y sin sherpa-onnx (las acciones de cargar y descargar las pone
// agent/Voz): se prueba solo, con motores falsos (test_reconocedores).
// Lo elegido va en Config/reconocedores.json.
// ============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::app::agent {

enum class MotorOido { Parakeet, Nemotron };

class Reconocedores {
   public:
    struct Consumidor {
        std::string id;
        std::string titulo;
        std::string descripcion;
        /// Se lo puede interrumpir hablando (hay cancelación de eco y la voz del
        /// asistente baja por el mismo canal): el tótem.
        bool interrumpible = false;
    };
    /// Lo que hace cargar o descargar un motor (agent/Voz). `cargar` es
    /// asíncrono: el resultado vuelve por `informarCarga`.
    struct Acciones {
        std::function<void(MotorOido)> cargar;
        std::function<void(MotorOido)> descargar;
    };

    explicit Reconocedores(std::filesystem::path archivo);

    static const std::vector<Consumidor>& consumidores();
    static const Consumidor* consumidor(const std::string& id);
    static const std::vector<MotorOido>& motores();
    static std::string nombre(MotorOido m);  ///< "parakeet" / "nemotron"
    static std::string titulo(MotorOido m);  ///< para mostrar
    static std::optional<MotorOido> deNombre(const std::string& s);
    /// Lo que ocupa en memoria un motor cargado (aprox., MB).
    static int memoriaMB(MotorOido m);
    /// De quién es una clave de corriente: «registro:…» el tótem, «atencion:…» la
    /// recepción, «dictado» el dictado y lo demás (salidas de texto) la transcripción.
    static std::string consumidorDeClave(const std::string& clave);

    // --- Lo elegido -------------------------------------------------------------------
    MotorOido motorDe(const std::string& consumidor) const;
    /// Cambia el motor de un consumidor y lo guarda. "" o el error. No mueve los
    /// usos abiertos: eso lo hace quien los tiene (Voz los cierra y los vuelve a abrir).
    std::string elegir(const std::string& consumidor, const std::string& motor);
    /// Si la persona puede interrumpir al asistente (efectivo: la opción y que el
    /// motor sea Nemotron; Parakeet no oye mientras habla el asistente).
    bool interrumpir(const std::string& consumidor) const;
    /// La opción tal cual (de fábrica: prendida).
    bool interrumpirElegido(const std::string& consumidor) const;
    std::string setInterrumpir(const std::string& consumidor, bool on);

    // --- Los usos y la memoria -----------------------------------------------------------
    void setAcciones(Acciones a) { acciones_ = std::move(a); }
    void setMargenMs(std::int64_t ms) { margenMs_ = ms < 0 ? 0 : ms; }
    std::int64_t margenMs() const { return margenMs_; }
    /// Abre un uso: cuenta para el motor de su consumidor (y lo carga si hace
    /// falta). Si la clave ya estaba abierta con otro consumidor o motor, la mueve.
    MotorOido usar(const std::string& clave, const std::string& consumidor, std::int64_t ahoraMs);
    void soltar(const std::string& clave, std::int64_t ahoraMs);
    std::optional<MotorOido> motorDeUso(const std::string& clave) const;
    /// Las claves abiertas de un consumidor.
    std::vector<std::string> usosDe(const std::string& consumidor) const;
    /// Descarga lo que quedó sin uso pasado el margen. true si descargó algo.
    bool tic(std::int64_t ahoraMs);
    /// Lo que contestó el cargador.
    void informarCarga(MotorOido m, bool ok, const std::string& error = {});
    /// Los archivos del modelo cambiaron (se bajó o se borró): si está en uso, se vuelve a cargar.
    void recargar(MotorOido m);
    bool cargado(MotorOido m) const;
    bool cargando(MotorOido m) const;
    bool enUso(MotorOido m) const;
    std::string error(MotorOido m) const;

    /// "" o el aviso de los dos motores en uso a la vez (quién usa cada uno y la memoria).
    std::string alerta() const;
    /// Todo junto, para la interfaz y el MCP:
    /// {motores: [{id, titulo, cargado, cargando, error, usos, consumidores, memoria_mb}],
    ///  consumidores: [{id, titulo, descripcion, motor, usos, interrumpible, interrumpir}],
    ///  alerta, mezcla, memoria_mb, margen_s}.
    nlohmann::json estado() const;

   private:
    struct EstadoMotor {
        bool cargado = false;
        bool cargando = false;
        std::string error;
        std::int64_t sinUsoDesde = -1; ///< ms; -1: en uso (o nunca usado)
    };
    struct Uso {
        std::string consumidor;
        MotorOido motor = MotorOido::Parakeet;
    };
    int usos(MotorOido m) const;
    std::set<std::string> consumidoresDe(MotorOido m) const;
    void pedirCarga(MotorOido m);
    void leer();
    void guardar() const;

    std::filesystem::path archivo_;
    std::map<std::string, MotorOido> elegidos_;  ///< solo los que no son de fábrica
    std::map<std::string, bool> interrumpir_;    ///< solo los que no son de fábrica
    std::map<std::string, Uso> usos_;            ///< clave -> uso
    std::map<MotorOido, EstadoMotor> motores_;
    Acciones acciones_;
    std::int64_t margenMs_ = 120000;
};

} // namespace ba::app::agent
