// ============================================================================
// src/gateway/Prompter.hpp — el teleprompter
//
// Tres vistas de la misma cosa:
//   /prompter          la tablet del orador: solo el texto, grande.
//   /prompter/control  el celular del operador: arrancar, velocidad, avisos.
//   (y el cue `teleprompter` del guion, que carga el discurso desde A)
//
// POR QUE LA POSICION VA EN PALABRAS. Podria ir en pixeles, pero entonces
// dependeria del tamano de letra y del tamano de la pantalla, y el control no
// sabe como es la tablet. En palabras, las dos vistas hablan del mismo lugar
// del texto aunque una sea un celular y la otra un televisor. Ademas sale
// gratis la estimacion de duracion, que es lo que el guion necesita para
// calcular los tiempos (130-150 palabras por minuto en castellano).
//
// POR QUE LA TABLET DESPLAZA SOLA. Si el servidor mandara la posicion cuadro a
// cuadro, cualquier hipo de la red se veria como un tiron en el texto, justo
// cuando alguien lo esta leyendo en vivo. En vez de eso el servidor manda
// DESDE DONDE y A QUE VELOCIDAD, con el momento en que eso empezo, y cada
// vista calcula sola donde va. Si se corta la red, el texto sigue andando.
//
// El estado es del servidor (no de la tablet) para que el orador pueda
// recargar la pagina, o que se le apague, y vuelva al mismo lugar.
// ============================================================================
#pragma once

#include <cstdint>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

namespace ba::gateway {

class Prompter {
  public:
    /// Palabras por minuto de una lectura normal en castellano. Es el valor
    /// con el que se estima cuanto dura un discurso.
    static constexpr int kPpmPorDefecto = 140;

    /// Carga un discurso. Reinicia la posicion y lo deja PARADO: que un texto
    /// nuevo arranque a correr solo seria la peor sorpresa posible.
    void cargar(const std::string& titulo, const std::string& texto);

    /// Aplica los campos presentes de `cmd`. Devuelve false si no habia
    /// ninguno conocido.
    ///
    /// Campos: corriendo (bool), posicion (palabra, double), ppm (int),
    /// tamano (int, 1..10), espejo (bool), aviso (string), parrafo (int:
    /// saltar al principio de ese parrafo).
    bool comandar(const nlohmann::json& cmd);

    /// El boton de panico: para en seco donde este. No mueve la posicion, para
    /// que se pueda retomar exactamente ahi.
    void pararEnSeco();

    /// El estado, con `atMs` para que cada vista calcule su propia posicion.
    nlohmann::json estado() const;

    /// Cuanto dura leer lo que esta cargado, a la velocidad actual.
    int duracionSeg() const;

    /// Cuantas palabras tiene un texto (la misma cuenta que usa el prompter).
    static int contarPalabras(const std::string& texto);

  private:
    nlohmann::json estadoLocked() const;
    /// Congela la posicion calculada hasta ahora (antes de cambiar velocidad,
    /// parar o saltar): si no, cambiar la velocidad moveria el texto.
    void fijarPosicion(int64_t ahora);

    mutable std::mutex m_;
    std::string titulo_;
    std::string texto_;
    int palabras_ = 0;
    /// Indices de la primera palabra de cada parrafo (para saltar).
    std::vector<int> parrafos_;
    double posicion_ = 0; ///< palabra en la que va la linea de lectura
    int ppm_ = kPpmPorDefecto;
    bool corriendo_ = false;
    int64_t desdeMs_ = 0; ///< cuando empezo a correr desde `posicion_`
    int tamano_ = 5;      ///< 1..10, lo elige el orador en su tablet
    bool espejo_ = false; ///< para el vidrio del teleprompter
    std::string aviso_;   ///< "redondea", "te escuchamos bajo"
    int64_t avisoMs_ = 0;
    uint64_t version_ = 0; ///< sube con cada cambio: las vistas detectan saltos
};

} // namespace ba::gateway
