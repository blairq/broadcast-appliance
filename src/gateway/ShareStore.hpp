// ============================================================================
// src/gateway/ShareStore.hpp — Archivos que comparten los invitados
//
// Proposito:
//   Guarda en disco lo que un invitado sube para mostrar (imagen, video o
//   audio) y que el operador puede "tomar" como fuente. Reglas:
//     - streaming directo a disco (nunca el archivo entero en memoria), con
//       tope de tamaño;
//     - el TIPO se decide por el contenido (firmas/magic bytes), no por el
//       nombre ni por el Content-Type que manda el navegador: un .exe
//       renombrado a .jpg se rechaza;
//     - el nombre en disco lo elige el servidor (aleatorio + extension segun
//       el tipo detectado): el nombre del invitado solo se muestra;
//     - se escribe a un temporal y se renombra al terminar: nunca queda un
//       archivo a medias con nombre final.
//
//   A (la app) copia el archivo a su carpeta de medios al tomarlo, asi que
//   este directorio es de B y se limpia en cada arranque.
//
// Threading: sin estado compartido mutable; save() es reentrante.
// ============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace ba::gateway {

/// Ruta desde UTF-8 (u8path esta deprecado en C++20).
inline std::filesystem::path pathFromUtf8(const std::string& s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

class ShareStore {
   public:
    struct Kind {
        std::string kind; ///< "image" | "video" | "audio"
        std::string mime;
        std::string ext; ///< con punto: ".png"
    };

    enum Err : int { kOk = 0, kTooLarge = 1, kUnsupported = 2, kIo = 3, kShort = 4 };

    struct Result {
        int err = kOk;
        std::filesystem::path path;
        Kind kind;
        uint64_t bytes = 0;
    };

    /// :param dir: directorio donde se guardan (se crea si falta).
    /// :param maxBytes: tope por archivo.
    ShareStore(std::filesystem::path dir, uint64_t maxBytes);

    /// Borra todo lo que haya quedado de un arranque anterior.
    void clear();

    /// Detecta el tipo por las primeras bytes. :returns: false si no es un
    /// formato de imagen/video/audio soportado.
    static bool sniff(const uint8_t* p, size_t n, Kind& out);

    /// Guarda `total` bytes leidos con `read` (llena el buffer y devuelve lo
    /// leido; <= 0 es EOF/error).
    Result save(uint64_t total, const std::function<int(uint8_t*, size_t)>& read);

    uint64_t maxBytes() const { return maxBytes_; }
    const std::filesystem::path& dir() const { return dir_; }

   private:
    std::filesystem::path dir_;
    uint64_t maxBytes_;
};

} // namespace ba::gateway
