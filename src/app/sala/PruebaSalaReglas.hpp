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

#include "ipc/LugaresSala.hpp"

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
    int maxParticipantes = ba::ipc::kMaxInvitados; ///< el cupo más grande de la sala (ipc/LugaresSala)
};

/// "" si se puede arrancar con `n` participantes; si no, por qué (para el operador).
/// `generadorLocal`: el probador corre en esta máquina (suma su memoria).
std::string puedeArrancar(const Medida& m, const Umbrales& u, int n, bool generadorLocal);
/// Lo mismo cuando el probador genera `nLocal` de los `n` en esta máquina (el resto, en
/// otras): cada uno pesa la mitad (app + gateway) y los locales, la otra mitad.
std::string puedeArrancarRepartido(const Medida& m, const Umbrales& u, int n, int nLocal);

// --- El probador (hinchada): cuánto decodifica y entre cuántas máquinas ---------------------

/// Cuánto video que VUELVE (el retorno que la sala le manda a cada uno) decodifica el
/// probador (`hinchada --decodificar`). Decodificar es lo que más le cuesta: con «todo» y
/// 200 participantes la máquina que genera llega al 100 % de CPU y manda sus cuadros a
/// destiempo, y eso ensucia la medición de la sala. Con «muestra», unos pocos
/// decodifican de verdad (se ve que el retorno llega entero y a su ritmo: cuadros
/// decodificados, congelamientos) y el resto lo recibe y lo cuenta sin decodificar (bytes,
/// cuadros, pérdida, jitter). «nada» es lo más liviano pero no comprueba que el retorno
/// se pueda ver.
struct Decodificar {
    std::string modo = "muestra"; ///< "nada" | "muestra" | "todo"
    int pct = 5;                  ///< el de la muestra (kMuestraMinPct..kMuestraMaxPct)
};
constexpr int kMuestraMinPct = 1;
constexpr int kMuestraMaxPct = 50;
/// Con la muestra, decodifican al menos estos (con 10, el 5 % no sería ninguno).
constexpr int kMuestraMinima = 3;
/// "" si `modo` y `pct` valen (y los deja en `d`); si no, por qué.
std::string leerDecodificar(const std::string& modo, int pct, Decodificar& d);
/// El % que se le pide a hinchada para `n` participantes del mismo proceso: 0 (nada),
/// 100 (todo) o la muestra, subida para que decodifiquen al menos kMuestraMinima.
double pctEfectivo(const Decodificar& d, int n);
/// El argumento de `--decodificar`: "nada", "todo" o el % ("30", "12.5").
std::string argDecodificar(const Decodificar& d, int n);
/// Para el informe: {modo, pct, pctEfectivo}.
nlohmann::json aJson(const Decodificar& d, int n);

/// Repartir los N entre esta máquina y otras (hinchada `coordinar` / `trabajar`).
/// hinchada reparte entre los trabajadores conectados según lo que aguanta cada uno
/// (`--por-proceso`); en macOS un proceso no pasa de 150 (libwebrtc espera los sockets con
/// select()), así que a una máquina que le tocan más se le pide la línea más de una vez.
constexpr int kMaxOtrasMaquinas = 16;
constexpr int kPorProcesoMax = 150;
struct Reparto {
    int n = 0;
    int otras = 0;            ///< otras máquinas que generan
    bool estaGenera = true;   ///< esta máquina también genera (si no, solo coordina)
    int maquinas = 1;         ///< las que generan
    int porMaquina = 0;       ///< los que le tocan a cada una (el último puede tener menos)
    int local = 0;            ///< los que genera esta máquina
    int procesosLocales = 0;  ///< los `hinchada trabajar` que lanza la app acá
    int porProcesoLocal = 0;
    int procesosPorOtra = 0;  ///< cuántas veces se corre la línea en cada otra máquina
    int porProcesoOtra = 0;
    int trabajadores = 0;     ///< los que espera el coordinador (todos los procesos)
    bool varias() const { return otras > 0; }
};
/// "" si se puede repartir así; si no, por qué.
std::string puedeRepartir(int n, int otras, bool estaGenera);
Reparto repartir(int n, int otras, bool estaGenera);
/// La línea para cada otra máquina: `HINCHADA_CLAVE=… ./hinchada trabajar --coordinador
/// host:puerto --por-proceso K` (en Windows, con PowerShell: `$env:HINCHADA_CLAVE=…;
/// .\hinchada.exe …`). La clave de coordinación es de esta prueba sola (el coordinador
/// rechaza a quien no la trae).
std::string comandoTrabajador(const Reparto& r, const std::string& host, int puerto, const std::string& clave, bool windows);
/// Los trabajadores que se sumaron, según la consola del coordinador («se sumó el
/// trabajador «nombre» (…)» / «se cortó el trabajador «nombre»»). `otras`: los que no son
/// de esta máquina (los de acá se llaman `kPrefijoLocal`N).
constexpr const char* kPrefijoLocal = "esta-maquina-";
int trabajadoresSumados(const std::string& consola, int* otras = nullptr);

/// El CÓDIGO para sumar otra PC como generadora desde SU app (Sala → Prueba de sala →
/// «Generar para otro equipo»): «BAGEN1.» + base64url de la dirección del coordinador, su
/// clave, cuántos procesos corre y de cuántos cada uno, y el nombre de la máquina medida.
/// La otra app lanza esos `hinchada trabajar` (sin línea de comandos).
struct CodigoGenerador {
    std::string coordinador; ///< host:puerto
    std::string clave;       ///< la clave de coordinación (de esta prueba sola)
    int procesos = 1;
    int porProceso = kPorProcesoMax;
    std::string maquina;     ///< la que se mide (para mostrar)
};
std::string armarCodigoGenerador(const CodigoGenerador& c);
/// false si no es un código de generador (o le falta algo, o los números no valen).
bool leerCodigoGenerador(const std::string& texto, CodigoGenerador& c);

/// La CPU (% de la máquina) de cada máquina GENERADORA en una línea de muestras.jsonl del
/// coordinador de hinchada (`maquinas`: una por máquina). No cuenta la sonda ni la máquina
/// medida (la que vigila ba-gateway o ba-app: la del coordinador, que corre al lado de la app).
std::map<std::string, double> generadorasDeMuestra(const nlohmann::json& linea);
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
    double cpuGenerador = -1; ///< % de la máquina: los procesos de hinchada de ESTA máquina
    double cpuMaquina = -1;   ///< % de la máquina entera
    double redEntraMbps = -1, redSaleMbps = -1; ///< sus placas de red
    double memLibreMb = -1;
    double carga = -1;
    int hilosApp = -1;
};

class Informe {
   public:
    /// Empieza otra prueba (olvida la anterior).
    void empezar(int64_t t, int pedidos, const std::string& generador, const std::string& evento);
    /// Cómo se generó (va al informe como `probador`): {decodificar: aJson(Decodificar),
    /// maquinas: {otras, estaGenera, sumadas}, …}. Se puede volver a llamar (las sumadas).
    void probador(const nlohmann::json& p);
    /// La CPU de las máquinas generadoras en un segundo (generadorasDeMuestra).
    void generadoras(const std::map<std::string, double>& cpuPorMaquina);
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
    nlohmann::json probador_ = nlohmann::json::object();
    bool porGuardia_ = false;
    std::map<std::string, Persona> gente_;
    std::vector<Muestra> serie_;
    struct Generadora {
        double suma = 0, pico = -1;
        int n = 0;
    };
    std::map<std::string, Generadora> generadoras_;
};

} // namespace ba::app::sala::prueba
