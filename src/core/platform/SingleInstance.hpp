// ============================================================================
// src/core/platform/SingleInstance.hpp — Instancia unica por carpeta de datos
//
// Proposito:
//   El launcher no debe correr dos veces sobre la misma carpeta de datos (spec
//   §3.1.1): dos supervisores lanzarian dos A y dos B peleando por los mismos
//   archivos y canales. Con carpetas distintas si pueden convivir: el nombre
//   lleva la clave de la carpeta (platform/Instancia.hpp). `Alcance::Maquina`
//   es para lo que se reparte entre todas (los lugares de puertos).
//
// Implementaciones:
//   - Windows: mutex con nombre en el namespace `Local\` (por sesion). Si el
//     proceso muere, el SO lo libera.
//   - Linux/macOS: `flock(LOCK_EX|LOCK_NB)` sobre `runtimeDir()/<nombre>.lock`.
//     El lock es del descriptor: si el proceso muere, el kernel lo suelta. No
//     se usa "existe el archivo" (quedaria trabado tras un crash).
//
// Estrategia de errores:
//   No lanza. `acquire` devuelve false si otra instancia ya lo tiene o si no
//   se pudo crear el lock (el motivo va a `err`).
// ============================================================================

#pragma once

#include <string>

namespace ba::platform {

/// Lock de instancia unica; se libera al destruirse.
class SingleInstance {
   public:
    SingleInstance() = default;
    ~SingleInstance();
    SingleInstance(SingleInstance&& other) noexcept;
    SingleInstance& operator=(SingleInstance&& other) noexcept;
    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    /// De quien es el nombre: de esta carpeta de datos (lo normal) o de toda la
    /// maquina (para el usuario actual).
    enum class Alcance { Datos, Maquina };

    /// Intenta tomar el lock `name` ([A-Za-z0-9._-]).
    ///
    /// :returns: true si esta instancia es la unica; false si otra ya lo tiene
    ///           (o por error, con `err`).
    static bool acquire(const std::string& name, SingleInstance& out, std::string* err = nullptr,
                        Alcance alcance = Alcance::Datos);

    /// true si este objeto tiene el lock.
    bool held() const;

    /// Suelta el lock. Idempotente.
    void release();

   private:
#if defined(_WIN32)
    void* mutex_ = nullptr;
#else
    int fd_ = -1;
#endif
};

} // namespace ba::platform
