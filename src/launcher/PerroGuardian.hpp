// PerroGuardian.hpp
//
// Proposito:
//   El watchdog de HARDWARE de Linux (/dev/watchdog), opcional y APAGADO de
//   fábrica (04-plan-unificado.md, F4-08; 03 D8). El supervisor ya relanza A y B
//   si se caen o se cuelgan; lo que no puede arreglar es un cuelgue del sistema
//   operativo entero (un controlador trabado, la máquina sin memoria). Para eso
//   está el perro de la placa: si el launcher deja de «darle de comer» durante
//   `segundos`, la placa reinicia la máquina, y al volver todo arranca solo (corte
//   de luz = todo vuelve como estaba).
//
//   Se le da de comer desde la vuelta del supervisor (cada 250 ms; acá se limita a
//   una vez por segundo): si el launcher se traba, deja de comer y la máquina se
//   reinicia. Al cerrar en orden se lo DESARMA (la «V» mágica antes de cerrar),
//   así salir del programa no reinicia la máquina.
//
//   El riesgo es un reinicio no deseado en pleno evento: por eso es opcional
//   (Configuración → Sistema y datos, «launcher.perroGuardian»), solo Linux, y
//   el dispositivo tiene que ser del usuario (una regla de udev: el programa no
//   toca el sistema).
//
// Modelo de threading:
//   Lo usa solo el hilo del supervisor.
//
// Estrategia de errores:
//   No lanza. Si no se puede abrir (no es Linux, no hay dispositivo, sin
//   permisos) se registra por qué y el launcher sigue sin él. El ioctl del
//   tiempo puede fallar (un dispositivo que no lo admite, o un archivo de prueba):
//   se registra y queda el tiempo que tenga la placa.

#pragma once

#include <cstdint>
#include <string>

namespace ba::launcher {

class PerroGuardian {
   public:
    PerroGuardian() = default;
    ~PerroGuardian();
    PerroGuardian(const PerroGuardian&) = delete;
    PerroGuardian& operator=(const PerroGuardian&) = delete;

    /// Abre `dispositivo` y le pide `segundos` de plazo. false (y `error`) si no se pudo.
    bool abrir(const std::string& dispositivo, int segundos, std::string* error = nullptr);
    bool abierto() const { return fd_ >= 0; }
    /// Le da de comer (como mucho una vez por segundo: `ahoraMs` lo dice quien llama).
    void latir(std::uint64_t ahoraMs);
    /// Cierre ordenado: lo desarma y lo cierra (la máquina no se reinicia).
    void desarmar();
    /// Cuántas veces comió (para las métricas del launcher).
    std::uint64_t latidos() const { return latidos_; }

   private:
    int fd_ = -1;
    std::uint64_t ultimoMs_ = 0;
    std::uint64_t latidos_ = 0;
};

} // namespace ba::launcher
