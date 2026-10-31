// ============================================================================
// src/core/diag/Recursos.hpp — cuánto se usa de la máquina, para ver venir el límite
//
// Una vez por segundo, en un hilo propio (nunca en el de la interfaz), mide:
//   - CPU: de toda la máquina y la de nuestros procesos (la app, el gateway, el
//     launcher), en % de la máquina y en núcleos;
//   - RAM: usada y total del sistema, y la residente de cada proceso nuestro;
//   - GPU: uso en % y memoria de video, donde el sistema lo dice (macOS:
//     IOAccelerator; Windows: los contadores «GPU Engine» y DXGI; Linux: amdgpu
//     en /sys). Si no lo dice, queda DESCONOCIDO (-1): nunca un número inventado;
//   - red: lo que entra y sale por cada interfaz física (Mbps) y el total, y si
//     se sabe la velocidad del enlace, qué parte de ella;
//   - disco: lo que se escribe (MB/s, todos los discos físicos);
//   - el motor (lo pasa la app): cuadros por segundo, cuadros perdidos y lo que
//     sale por la transmisión;
//   - la térmica (platform/Termica.hpp): el nivel común a los tres sistemas.
// Solo con lo que trae el sistema operativo (portable: nada nuevo en runtime; en
// Windows pdh.dll, iphlpapi.dll y dxgi.dll se cargan al medir, no se importan).
//
// Guarda un historial (por defecto 10 minutos, de a un segundo) con sus picos y
// da un ESTADO por recurso con umbrales (aviso ≥80 %, crítico ≥92 %) que tienen
// que SOSTENERSE unos segundos: un pico de un segundo no es «está por explotar».
// El margen es lo que queda del recurso más cargado (100 − promedio de 10 s):
// con varias salidas a la vez, es lo que dice cuántas más aguanta la máquina.
//
// Lo leen la tarjeta «Sistema» (RecursosViewModel), la herramienta MCP
// `sistema_recursos` y el disparador de alarmas `recursos` (AlertEngine).
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ba::diag {

class Recursos {
   public:
    /// Lo que no se sabe (el sistema no lo dice): nunca un 0 que parezca medido.
    static constexpr double kDesconocido = -1;

    struct Proceso {
        std::string nombre; ///< app, gateway, launcher
        std::uint32_t pid = 0;
        double cpu = kDesconocido;     ///< % de toda la máquina
        double nucleos = kDesconocido; ///< 1,0 = un núcleo entero
        double rssMb = kDesconocido;   ///< memoria residente
    };
    struct Interfaz {
        std::string nombre;
        double entradaMbps = 0, salidaMbps = 0;
        double velocidadMbps = kDesconocido; ///< la del enlace, si se sabe
    };
    struct Muestra {
        std::int64_t t = 0; ///< ms desde 1970
        double cpu = kDesconocido; ///< % de toda la máquina
        int nucleos = 0;
        double ramUsadaMb = kDesconocido, ramTotalMb = kDesconocido;
        double gpu = kDesconocido; ///< % de uso (el motor más cargado de la placa)
        double vramUsadaMb = kDesconocido, vramTotalMb = kDesconocido;
        std::string gpuNombre;
        bool memoriaUnificada = false; ///< la GPU usa la RAM (Apple Silicon): sin total propio
        double redEntradaMbps = kDesconocido, redSalidaMbps = kDesconocido;
        double redPct = kDesconocido; ///< de la velocidad del enlace más cargado (si se sabe)
        double discoEscrituraMBs = kDesconocido;
        // Del motor (la app los pasa con `motor`).
        double fps = kDesconocido;
        double perdidosPct = kDesconocido; ///< cuadros perdidos en el último segundo, en %
        double salidaKbps = kDesconocido;  ///< lo que sale por la transmisión
        /// Qué tan caliente está (platform/Termica.hpp, F4-05): nivel, temperatura y
        /// tope de frecuencia (lo que se sepa en este sistema).
        std::string termica = "desconocido";
        double temperaturaC = kDesconocido;
        double limiteFrecuenciaPct = kDesconocido;
        std::string termicaFuente;
        std::vector<Proceso> procesos;
        std::vector<Interfaz> interfaces;

        double ramPct() const { return ramUsadaMb >= 0 && ramTotalMb > 0 ? ramUsadaMb / ramTotalMb * 100.0 : kDesconocido; }
        double vramPct() const {
            return vramUsadaMb >= 0 && vramTotalMb > 0 ? vramUsadaMb / vramTotalMb * 100.0 : kDesconocido;
        }
    };
    struct Umbrales {
        double aviso = 80, critico = 92;            ///< % de CPU, GPU, RAM, VRAM y red
        double motorAviso = 1, motorCritico = 5;    ///< % de cuadros perdidos
        int sostenidoS = 5;                         ///< segundos seguidos antes de cambiar de estado
    };
    struct Motor {
        double fps = kDesconocido;
        double perdidosPct = kDesconocido;
        double salidaKbps = kDesconocido;
    };
    enum class Estado { Desconocido, Normal, Aviso, Critico };

    /// Los recursos con estado, en el orden en que se muestran.
    static const std::vector<std::string>& claves();
    static const char* nombre(Estado e);

    explicit Recursos(int historialS = 600);
    ~Recursos();
    Recursos(const Recursos&) = delete;
    Recursos& operator=(const Recursos&) = delete;

    /// Arranca el hilo que mide cada `periodoMs`; `alMedir` se llama desde ESE
    /// hilo después de cada muestra (quien lo use pasa a su hilo).
    void arrancar(std::function<void()> alMedir = {}, int periodoMs = 1000);
    void parar();

    /// Qué procesos medir aparte (nombre, pid). El propio se agrega solo como «app».
    void procesos(std::vector<std::pair<std::string, std::uint32_t>> quienes);
    /// Lo que sabe el motor y el sistema no (fps, perdidos, lo que sale).
    void motor(const Motor& m);
    void umbrales(const Umbrales& u);
    Umbrales umbrales() const;

    /// Una medición YA (contra la anterior; la primera no tiene CPU ni red: no hay
    /// con qué comparar). La usa el hilo; los tests también.
    Muestra medir();
    /// Suma una muestra al historial y recalcula picos y estados (la usa el hilo;
    /// los tests la llaman con valores inventados).
    void agregar(const Muestra& m);

    Muestra ultima() const;
    Estado estado(const std::string& clave) const;
    /// Los últimos `segundos` de un recurso (cpu, gpu, ram, vram, red, red_entrada,
    /// red_salida, motor, disco), del más viejo al más nuevo; -1 donde no se sabía.
    std::vector<double> serie(const std::string& clave, int segundos) const;
    /// El máximo de un recurso en el historial (-1 si nunca se supo).
    double pico(const std::string& clave) const;
    int muestras() const;
    int capacidad() const;

    /// Todo junto (lo que da `sistema_recursos`): valores, picos, estados, márgenes,
    /// umbrales, procesos e interfaces; con `historialS` > 0, el historial
    /// (agrupado de a `pasoS` segundos, tomando el máximo de cada grupo).
    nlohmann::json informe(int historialS = 0, int pasoS = 1) const;
    /// Solo los estados: {cpu: {estado, valor, sostenido_s}, …} (para las alarmas).
    nlohmann::json niveles() const;

    /// El pid de este proceso y el del que lo lanzó (con su nombre de ejecutable).
    static std::uint32_t pidPropio();
    static std::pair<std::uint32_t, std::string> padre();

   private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace ba::diag
