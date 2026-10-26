// src/core/platform/Maquina.cpp — ver Maquina.hpp (lo común a los sistemas)

#include "platform/Maquina.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace ba::platform {

std::string huellaDe(const std::string& crudo) {
    // Dos pasadas de FNV-1a con sales distintas, para que dos ids parecidos no
    // den huellas parecidas; se muestran 16 hex (64 bits alcanzan de sobra para
    // distinguir las máquinas por las que pasa un disco).
    auto fnv = [](const std::string& s, std::uint64_t h) {
        for (unsigned char c : s) {
            h ^= c;
            h *= 1099511628211ULL;
        }
        return h;
    };
    const std::uint64_t a = fnv("ba-maquina:" + crudo, 14695981039346656037ULL);
    const std::uint64_t b = fnv(crudo + ":ba-maquina", a ^ 0x9e3779b97f4a7c15ULL);
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(a ^ (b >> 1)));
    return buf;
}

std::string huellaDeMaquina() {
    if (const char* f = std::getenv("BA_HUELLA_MAQUINA"); f != nullptr && *f != '\0') return f;
    static const std::string h = [] {
        std::string id = idCrudoDeMaquina();
        // Sin id del sistema (un Linux sin machine-id): el modelo solo. Dos PC iguales
        // se confundirían, pero eso es mejor que una huella nueva en cada arranque.
        if (id.empty()) id = "sin-id";
        return huellaDe(id + "|" + modeloDeMaquina());
    }();
    return h;
}

} // namespace ba::platform
