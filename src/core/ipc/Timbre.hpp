// ============================================================================
// src/core/ipc/Timbre.hpp — el timbre entre procesos de un canal compartido (F3-13)
//
// Mirada (F2-14) dejó al lector durmiendo hasta el próximo cuadro esperado: una
// despertada por cuadro (dos o tres en macOS, que duerme por tramos) y medio ms de
// margen por salto, más el temblor del productor. Lo que falta es que el que escribe
// AVISE: con el timbre el lector duerme en el sistema hasta que el escritor toca (o
// hasta un plazo, lo que llegue primero) y se despierta en decenas de µs, sin margen
// ni sondeo. En reposo (nadie publica) no se despierta más que por el plazo.
//
// Vive en la cabecera del canal (memoria compartida): `toques` (la palabra en la que
// se duerme), `durmientes` (cuántos lectores duermen: sin ninguno el escritor no
// llama al sistema) y `vivo` (el escritor toca; uno viejo deja 0 y el lector sigue
// con Mirada). La primitiva, la de cada sistema, sin servicios:
//   - Linux: futex compartido (FUTEX_WAIT / FUTEX_WAKE sobre `toques`).
//   - macOS 14.4+: os_sync_wait_on_address con OS_SYNC_WAIT_ON_ADDRESS_SHARED (en
//     uno anterior no hay nada público que sirva entre procesos con plazo —los
//     semáforos con nombre no tienen espera con plazo—: el lector sigue con Mirada).
//   - Windows: un semáforo con nombre (`Local\<canal>.Timbre`). WaitOnAddress no
//     cruza procesos. El escritor suelta tantos permisos como durmientes hay; uno
//     que se venció justo antes deja un permiso de más, que es una despertada de más
//     para el próximo, no una perdida.
//
// Sin carreras: el lector anota `visto()` ANTES de mirar si hay algo; si el escritor
// publica entre medio, `toques` ya no es `visto` y la espera vuelve en seguida (el
// sistema compara la palabra al dormir). El escritor suma el toque y recién después
// mira si hay durmientes (los dos en orden total: si no ve a nadie, el que se iba a
// dormir ve el toque).
// ============================================================================
#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace ba::ipc {

/// Lo que va en la cabecera del canal: 16 bytes, alineado a 4 (la palabra del futex).
struct TimbreCompartido {
    std::uint32_t toques = 0;     ///< cuántas veces tocó el escritor (donde se duerme)
    std::uint32_t durmientes = 0; ///< lectores durmiendo ahora
    std::uint32_t vivo = 0;       ///< 1: el escritor toca (0: uno viejo, o sin primitiva)
    std::uint32_t reserva = 0;
};
static_assert(sizeof(TimbreCompartido) == 16, "el timbre ocupa 16 bytes de la cabecera");

class Timbre {
   public:
    Timbre() = default;
    ~Timbre();
    Timbre(Timbre&& o) noexcept;
    Timbre& operator=(Timbre&& o) noexcept;
    Timbre(const Timbre&) = delete;
    Timbre& operator=(const Timbre&) = delete;

    /// ¿Hay primitiva en este sistema? (macOS anterior a 14.4: no.)
    static bool hayPrimitiva();

    /// Se engancha a la palabra `t` del canal `canal` (el nombre del canal: en Windows,
    /// el del semáforo). El escritor la marca viva (si hay primitiva).
    void enganchar(TimbreCompartido* t, const std::wstring& canal, bool escritor);
    void soltar();
    bool enganchado() const { return t_ != nullptr; }

    /// Escritor: después de anunciar lo nuevo.
    void tocar();
    /// Lector: los toques hasta ahora; anotarlos ANTES de mirar si hay algo.
    std::uint32_t visto() const;
    /// Lector: ¿el escritor toca? (Si no, a dormir hasta el plazo de Mirada.)
    bool vivo() const;
    /// Lector: duerme hasta que toquen después de `visto` o hasta `plazo`. true si
    /// tocaron (o despertar()); false si se venció el plazo. Sin timbre vivo, duerme
    /// hasta el plazo.
    bool esperar(std::uint32_t visto, std::chrono::steady_clock::time_point plazo);
    /// Despierta a los que duermen en este canal (del mismo proceso: parar una fuente,
    /// cambiarle el canal). Es un toque más.
    void despertar() { tocar(); }

   private:
    TimbreCompartido* t_ = nullptr;
    bool escritor_ = false;
#if defined(_WIN32)
    void* sem_ = nullptr; ///< HANDLE del semáforo con nombre
#endif
};

} // namespace ba::ipc
