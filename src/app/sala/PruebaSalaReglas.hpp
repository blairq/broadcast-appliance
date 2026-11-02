// ============================================================================
// src/app/sala/PruebaSalaReglas.hpp — la prueba de sala: las reglas puras
//
// La prueba de sala (docs/funciones/PRUEBA-SALA.md) mete N participantes «de prueba»
// por el camino real de los invitados para ver si la máquina y la red aguantan
// antes del evento. Acá va lo que no depende de Qt, del gateway ni del reloj, para
// probarlo solo (test_prueba_sala_reglas):
//
//   - la GUARDIA: si la máquina aprieta no se arranca (memoria disponible, carga
//     y lo que haría falta para N), y durante la prueba se corta sola si se pasa
//     de los umbrales (memoria < 3 GB, hilos de la app, carga). El 2026-11-01 una
//     prueba de carga agotó la memoria y tiró el kernel: esto no es opcional;
//   - quién es «de prueba» y cómo se lo saca de lo que cuenta (padrón, quórum,
//     votación, el acta);
//   - el INFORME: lo que se mide cada segundo (ancho de banda, CPU de la app y del
//     gateway, la máquina), quién entró y quién falló, cuánto tardó la HD de cada
//     uno y los cortes; en vivo y al final (va al chequeo del evento).
// ============================================================================
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::app::sala::prueba {

/// Cómo está la máquina (lo que mide MedidaMaquina). -1 = no se sabe.
struct Medida {
    double memLibreMb = -1; ///< la que el sistema puede dar sin presión (libre + inactiva + purgable)
    double carga = -1;      ///< promedio de carga de 1 minuto
    int hilosApp = -1;      ///< hilos de ba-app
};

/// Los umbrales (los mismos de la guardia de las pruebas de carga).
struct Umbrales {
    double arrancarMemMb = 6000;    ///< para arrancar: el doble del de cortar
    double arrancarCarga = 8;       ///< para arrancar: la máquina quieta
    double cortarMemMb = 3000;      ///< se corta por debajo
    int cortarHilos = 1200;         ///< se corta por encima (ba-app)
    double cortarCarga = 40;        ///< se corta por encima
    double mbPorParticipante = 80;  ///< lo que suma cada uno (app + gateway + el probador local)
    int maxParticipantes = 64;      ///< el cupo más grande de la sala (ipc/LugaresSala)
};

/// "" si se puede arrancar con `n` participantes; si no, por qué (para el operador).
/// `generadorLocal`: el probador corre en esta máquina (suma su memoria).
std::string puedeArrancar(const Medida& m, const Umbrales& u, int n, bool generadorLocal);
/// "" si sigue; si no, por qué se corta.
std::string debeCortar(const Medida& m, const Umbrales& u);

/// ¿Es un participante de la prueba? (el campo `prueba` que pone el gateway)
bool esDePrueba(const nlohmann::json& participante);
/// Los participantes sin los de prueba: lo que cuenta para el padrón, el quórum,
/// la votación y el acta.
nlohmann::json sinPrueba(const nlohmann::json& participantes);
/// El nombre para mostrar: «nombre · prueba» (el rótulo en la grilla y las listas).
std::string nombreConRotulo(const std::string& nombre, bool prueba);

/// Una muestra del segundo.
struct Muestra {
    int64_t t = 0;
    double kbpsSubida = -1; ///< lo que llega al gateway (lo que suben los participantes)
    double kbpsBajada = -1; ///< lo que sale del gateway hacia ellos
    double cpuApp = -1;     ///< % de la máquina
    double cpuGateway = -1; ///< % de la máquina
    double memLibreMb = -1;
    double carga = -1;
    int hilosApp = -1;
};

class Informe {
   public:
    /// Empieza otra prueba (olvida la anterior).
    void empezar(int64_t t, int pedidos, const std::string& generador, const std::string& evento);
    void muestra(const Muestra& m);
    /// Los participantes de prueba como los da la sala (snapshot: id, name, state,
    /// connected, mediaUp, joinedMs). Se llama con cada foto de la sala.
    void participantes(int64_t t, const nlohmann::json& deLaSala);
    /// La HD de alguien llegó `ms` después de pedirla.
    void hd(const std::string& id, int ms);
    /// `porGuardia`: la cortó la guardia (la máquina no aguantó).
    void terminar(int64_t t, const std::string& motivo, bool porGuardia = false);
    bool porGuardia() const { return porGuardia_; }

    bool empezado() const { return inicio_ > 0; }
    bool terminado() const { return fin_ > 0; }
    /// Lo de ahora: {pedidos, entraron, conectados, conVideo, fallidos, cortes,
    /// hd: {cuantos, p50, p95, max}, ultima: muestra, picos: {...}, segundos}.
    nlohmann::json vivo(int64_t t) const;
    /// Todo: lo de vivo + las series (de a 5 s) + cada persona + el motivo.
    nlohmann::json final() const;
    /// Una línea para la lista de chequeo del evento.
    std::string nota() const;
    /// ¿Salió bien? (todos entraron con video, sin cortes de la guardia, sin fallidos)
    bool bien() const;

   private:
    struct Persona {
        std::string nombre;
        int64_t llegoMs = 0;
        int64_t videoMs = 0; ///< cuándo tuvo los medios arriba (0 = nunca)
        bool conectado = false;
        bool vio = false; ///< se lo vio alguna vez (para contar los cortes: conectado → caído)
        int cortes = 0;
        std::vector<int> hd;
    };
    nlohmann::json resumen(int64_t t) const;

    int64_t inicio_ = 0, fin_ = 0;
    int pedidos_ = 0;
    std::string generador_, evento_, motivo_;
    bool porGuardia_ = false;
    std::map<std::string, Persona> gente_;
    std::vector<Muestra> serie_;
};

} // namespace ba::app::sala::prueba
