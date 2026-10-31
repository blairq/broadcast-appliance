// SPDX-License-Identifier: GPL-2.0-or-later
//
// FallasProgramadas: fallas con nombre y hora, para el banco de caos (F1-14,
// tools/caos/). «Que el codificador de la transmisión falle dentro de 3 s», «que
// el sumidero de la grabación falle dentro de 30 s».
//
// Un punto es «<salida>.<pieza>»: salida = grabacion | transmision (el tipo, no el
// id: la primera que pase por ahí se la lleva), pieza = codificador | sumidero.
// Cada falla se dispara UNA vez (tomar la consume).
//
// Solo las arma la herramienta diag_falla, que existe solo con --test: en uso
// normal el registro está vacío y lo único que cuesta es una carga atómica por tic
// de cada salida (hay()). Lo que se rompe y cómo lo hace el motor (Fallas.hpp);
// esto es solo el registro, sin GStreamer, para que lo use cualquiera.
#pragma once

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace ba::fault {

class FallasProgramadas {
   public:
    static FallasProgramadas& instancia();

    /// Programa `pieza` de `salida` para dentro de `enMs` (0 = ya). "" o por qué no.
    std::string programar(const std::string& salida, const std::string& pieza, std::int64_t enMs);
    void limpiar();
    /// ¿Hay alguna programada? (barato: lo mira cada tic de cada salida)
    bool hay() const { return cuantas_.load(std::memory_order_relaxed) > 0; }
    /// La pieza de `salida` que toca romper ya ("" si ninguna). La consume.
    std::string tomar(const std::string& salida);
    /// [{salida, pieza, faltan_ms}] lo que queda programado; y las ya disparadas.
    nlohmann::json estado() const;

    /// Para las pruebas: el reloj (ms monótonos).
    void ponerReloj(std::int64_t (*ahoraMs)()) { ahoraMs_ = ahoraMs; }

   private:
    struct Falla {
        std::string salida, pieza;
        std::int64_t cuandoMs = 0;
    };
    std::int64_t ahora() const;
    mutable std::mutex mu_;
    std::vector<Falla> pendientes_;
    nlohmann::json disparadas_ = nlohmann::json::array();
    std::atomic<int> cuantas_{0};
    std::int64_t (*ahoraMs_)() = nullptr;
};

} // namespace ba::fault
