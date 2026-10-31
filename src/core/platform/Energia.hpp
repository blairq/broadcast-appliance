// ============================================================================
// src/core/platform/Energia.hpp — que la máquina no se duerma con el programa abierto
//
// Un estudio no puede apagar la pantalla del multiview a los diez minutos ni
// suspender la notebook en medio de una transmisión. Mientras el programa está
// abierto se le pide al sistema, con sus propias funciones (nada instalado
// aparte), que no se duerma:
//
//   Windows  PowerCreateRequest + PowerSetRequest (sistema y pantalla): se ve en
//            `powercfg /requests` con el motivo. Y a cada proceso pesado,
//            fuera del «modo eficiencia» (EcoQoS), que en las notebooks le baja
//            la velocidad a lo que no tiene la ventana adelante.
//   macOS    una aserción de IOPM (sin reposo del sistema ni de la pantalla) y
//            una actividad de NSProcessInfo (sin App Nap).
//   Linux    el bloqueo «sleep:idle» de systemd-logind por D-Bus (libdbus-1, que
//            es de la base del sistema: se carga al usarla, no se enlaza).
//
// Lo mantiene el lanzador, que vive mientras el programa está abierto; al
// cerrarse, el sistema lo suelta solo (los pedidos son del proceso).
// Ajustes: energia.mantenerDespierta (true), energia.mantenerPantalla (true).
//
// EL CORTE DE LUZ (F1-11). De dónde viene la energía, leído de lo que el sistema
// da directo (nada de servicios aparte: ni upower, ni NUT, ni el demonio del UPS):
//
//   macOS    IOPSCopyPowerSourcesInfo (IOKit): la batería y los UPS USB HID, que
//            macOS reconoce solo.
//   Windows  GetSystemPowerStatus: AC o batería (un UPS USB HID aparece como batería).
//   Linux    /sys/class/power_supply: «Mains»/«USB» online, «Battery»/«UPS» con
//            capacity, status y time_to_empty (el núcleo ve los UPS USB HID).
//
// Y la «preparación»: qué le falta a la máquina para volver sola después de un
// corte (arrancar al volver la energía, iniciar sesión sola, sistema de archivos
// con diario, sin reposo, sin suspensión selectiva de USB). Solo se LEE y se
// explica cómo arreglarlo: nada se cambia sin que la persona lo pida.
// ============================================================================
#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ba::platform {

/// Mientras exista, el sistema no se duerme (y la pantalla tampoco, con `pantalla`).
class Despierta {
   public:
    Despierta(const std::string& motivo, bool pantalla);
    ~Despierta();
    Despierta(const Despierta&) = delete;
    Despierta& operator=(const Despierta&) = delete;

    /// Si el sistema aceptó el pedido.
    bool activa() const;
    /// Qué se pidió o por qué no se pudo, para el registro.
    const std::string& detalle() const;

    struct Impl;

   private:
    std::unique_ptr<Impl> d_;
};

/// Para el proceso que llama: fuera del ahorro de energía por proceso (EcoQoS en
/// Windows, App Nap en macOS; en Linux no hay). Devuelve qué hizo, para el registro.
std::string sinAhorroDeEnergia();

// --- El corte de luz -----------------------------------------------------------------------

/// De dónde viene la energía ahora.
struct EstadoEnergia {
    bool conocido = false;   ///< el sistema dijo algo
    bool hayBateria = false; ///< hay una batería o un UPS que el sistema ve
    bool enBateria = false;  ///< sin red eléctrica: se está gastando la batería
    int porcentaje = -1;     ///< 0–100; -1 = no se sabe
    int minutos = -1;        ///< lo que queda según el sistema; -1 = no se sabe (o calculando)
    std::string fuente;      ///< "bateria" | "ups" | "" (red eléctrica sin nada)
};

/// Lo que dice el sistema (Energia_mac/_win/_linux).
EstadoEnergia leerEnergia();
/// Linux: /sys/class/power_supply (aparte para probarlo en cualquier sistema con carpetas falsas).
EstadoEnergia leerPowerSupply(const std::filesystem::path& dir);

/// Qué hacer cuando queda poco (ajustes energia.corte.*).
struct PoliticaEnergia {
    int porcentaje = 20;           ///< a este % o menos…
    int minutos = 10;              ///< …o a estos minutos o menos (lo que llegue primero)
    bool cerrarGrabaciones = true; ///< EOS ordenado de las grabaciones (la transmisión sigue)
};
enum class AccionEnergia { Nada, Avisar, CerrarGrabaciones };
/// Con red: nada. En batería: avisar; por debajo del umbral: cerrar las grabaciones.
AccionEnergia decidirEnergia(const EstadoEnergia& e, const PoliticaEnergia& p);

/// Un punto de la tarjeta de preparación (traducido).
struct ItemPreparacion {
    std::string id;      ///< arranque_al_volver, sesion_automatica, sistema_archivos, reposo, usb, red
    std::string estado;  ///< "ok" | "falta" | "revisar" (no se puede leer: hay que mirarlo) | "no_aplica"
    std::string titulo;
    std::string detalle; ///< lo que se leyó
    std::string como;    ///< cómo arreglarlo (lo hace la persona: suele pedir administrador)
};
/// Lo que se puede leer del sistema para volver solo tras un corte. `datos`: la
/// carpeta de los datos (su sistema de archivos es el que importa).
std::vector<ItemPreparacion> preparacion(const std::filesystem::path& datos);

/// El sistema de archivos de una carpeta ("apfs", "ext4", "NTFS", "exfat"…; "" = no se sabe).
std::string sistemaDeArchivos(const std::filesystem::path& ruta);
/// El punto de la preparación para ese sistema de archivos (común a los tres sistemas).
ItemPreparacion itemSistemaDeArchivos(const std::filesystem::path& datos);
/// El punto «hay batería o UPS» (común): sin nada, el corte apaga en seco.
ItemPreparacion itemRespaldo(const EstadoEnergia& e);

} // namespace ba::platform
