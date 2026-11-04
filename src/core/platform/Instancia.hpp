// ============================================================================
// src/core/platform/Instancia.hpp — Una instancia por carpeta de datos
//
// Propósito:
//   Dos copias del programa con CARPETAS DE DATOS distintas tienen que poder
//   correr a la vez en la misma máquina (la del operador y una de prueba, o dos
//   eventos en dos carpetas); dos con la MISMA carpeta, no (pelearían por los
//   mismos archivos). Todo recurso con nombre de la máquina (el lock de
//   instancia única, los canales locales, la memoria compartida, los
//   semáforos con nombre) lleva la CLAVE de la carpeta de datos, y los puertos
//   TCP/UDP se corren según el LUGAR que toma cada launcher.
//
// La clave:
//   10 dígitos hex del hash (FNV-1a) de la ruta canónica de la carpeta de
//   datos, en minúsculas (macOS y Windows no distinguen mayúsculas; en Linux
//   dos carpetas que difieren solo en eso quedan como una: del lado seguro).
//   Sale de BA_DATA_ROOT, que el launcher pone antes de tomar nada y hereda a
//   todos sus hijos. Sin esa variable (un proceso lanzado a mano, los tests)
//   la clave es vacía y los nombres quedan como antes.
//
// El lugar:
//   El launcher toma el primer lugar libre (lock de la MÁQUINA, sin clave) y lo
//   pasa a sus hijos en BA_LUGAR. Lugar 0 = los puertos configurados; lugar N =
//   cada puerto + N×100 (el rango UDP de WebRTC, + N×1600). El modo prueba
//   (--test) nunca toma el 0: así la app de pruebas no le quita sus puertos a la
//   del operador aunque arranque antes.
//
// Threading: todo es seguro desde cualquier hilo. No lanza.
//
// La clave y los puertos van EN LÍNEA (sin .cpp): los compilan también los plugins de
// GStreamer y los tests que toman fuentes sueltas del núcleo (SharedMemoryRegion,
// Timbre). LugarDeInstancia (el lock) está en Instancia.cpp, en ba_core.
// ============================================================================

#pragma once

#include "platform/Paths.hpp"
#include "platform/SingleInstance.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>

namespace ba::platform {

/// Clave de una carpeta de datos (función pura salvo por resolver enlaces).
inline std::string claveDeDatos(const std::filesystem::path& datos) {
    if (datos.empty()) return {};
    std::error_code ec;
    std::filesystem::path c = std::filesystem::weakly_canonical(datos, ec);
    if (ec || c.empty()) c = datos.lexically_normal();
    const std::u8string u = c.generic_u8string();
    std::string s(u.begin(), u.end());
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    std::uint64_t h = 1469598103934665603ULL; // FNV-1a 64
    for (char ch : s) {
        const unsigned char b =
            (ch >= 'A' && ch <= 'Z') ? static_cast<unsigned char>(ch - 'A' + 'a') : static_cast<unsigned char>(ch);
        h ^= b;
        h *= 1099511628211ULL;
    }
    char hex[11];
    std::snprintf(hex, sizeof(hex), "%010llx", static_cast<unsigned long long>(h & 0xFFFFFFFFFFULL));
    return hex;
}

/// La clave de este proceso: la de BA_DATA_ROOT; "" sin esa variable.
inline std::string claveInstancia() {
    const char* env = std::getenv(kDataRootEnv);
    const std::string raiz = env != nullptr ? env : "";
    if (raiz.empty()) return {};
    // Resolver la ruta toca el disco: se recuerda la última (los tests cambian la variable).
    static std::mutex m;
    static std::string ultimaRaiz;
    static std::string ultimaClave;
    std::lock_guard<std::mutex> lock(m);
    if (raiz != ultimaRaiz || ultimaClave.empty()) {
        ultimaClave = claveDeDatos(std::filesystem::path(std::u8string(raiz.begin(), raiz.end())));
        ultimaRaiz = raiz;
    }
    return ultimaClave;
}

/// `nombre` para un recurso con nombre de la máquina: tal cual sin clave;
/// `nombre.<clave>` con clave.
inline std::string nombreDeInstancia(const std::string& nombre) {
    const std::string clave = claveInstancia();
    return clave.empty() ? nombre : nombre + "." + clave;
}
inline std::wstring nombreDeInstancia(const std::wstring& nombre) {
    const std::string clave = claveInstancia();
    return clave.empty() ? nombre : nombre + L"." + std::wstring(clave.begin(), clave.end());
}

// --- Lugar (puertos) -----------------------------------------------------------

/// Variable con la que el launcher les pasa el lugar a sus hijos.
inline constexpr const char* kLugarEnv = "BA_LUGAR";
/// Cuántas instancias a la vez (con puertos propios) en una máquina.
inline constexpr int kLugares = 8;
/// Corrimiento de los puertos TCP/UDP sueltos por lugar.
inline constexpr int kPasoPuertos = 100;
/// Corrimiento del rango UDP de WebRTC por lugar (su ancho de fábrica).
inline constexpr int kPasoRtc = 1600;

/// El lugar de este proceso (BA_LUGAR; 0 sin la variable o si no se entiende).
inline int lugarInstancia() {
    const char* env = std::getenv(kLugarEnv);
    if (env == nullptr || *env == '\0') return 0;
    char* fin = nullptr;
    const long n = std::strtol(env, &fin, 10);
    if (fin == env || *fin != '\0' || n < 0 || n >= kLugares) return 0;
    return static_cast<int>(n);
}

namespace detail {
inline std::uint16_t corrido(std::uint16_t p, int lugar, int paso) {
    if (p == 0 || lugar <= 0) return p;
    const long q = static_cast<long>(p) + static_cast<long>(lugar) * paso;
    return q > 65535 ? p : static_cast<std::uint16_t>(q);
}
} // namespace detail

/// El puerto que usa este proceso para uno configurado: `p + lugar×100`.
/// 0 queda 0 (= apagado / el que dé el sistema); si se pasa de 65535, el configurado.
inline std::uint16_t puertoDeInstancia(std::uint16_t p, int lugar) { return detail::corrido(p, lugar, kPasoPuertos); }
inline std::uint16_t puertoDeInstancia(std::uint16_t p) { return puertoDeInstancia(p, lugarInstancia()); }
/// Lo mismo para un extremo del rango UDP de WebRTC (`p + lugar×1600`).
inline std::uint16_t puertoRtcDeInstancia(std::uint16_t p, int lugar) { return detail::corrido(p, lugar, kPasoRtc); }
inline std::uint16_t puertoRtcDeInstancia(std::uint16_t p) { return puertoRtcDeInstancia(p, lugarInstancia()); }

/// El lugar que tiene tomado un launcher; se suelta al destruirse.
class LugarDeInstancia {
   public:
    /// Toma el primer lugar libre de la máquina: desde 0, o desde 1 con `prueba`.
    ///
    /// :returns: true y el lugar en `out`; false si están todos tomados (`err`).
    static bool tomar(bool prueba, LugarDeInstancia& out, std::string* err = nullptr);

    /// El lugar tomado (-1 si ninguno).
    int lugar() const { return lugar_; }

   private:
    SingleInstance lock_;
    int lugar_ = -1;
};

} // namespace ba::platform
