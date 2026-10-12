// ============================================================================
// src/core/rostros/Rostros.hpp — reconocer a alguien que ya estuvo
//
// PARA QUE. Que el lobby sepa quién llegó, que el rótulo salga con su nombre y
// su cargo sin tipearlo de nuevo, que el guion sepa si el orador que sigue ya
// está en la sala. Todo eso hoy se hace mirando y escribiendo.
//
// CORRE EN EL APARATO, y esa es la decisión de fondo. Un modelo remoto no
// sirve para esto: tarda segundos, cuesta en cada consulta y no contesta igual
// dos veces. Acá son milisegundos, sin red, y el mismo rostro da siempre el
// mismo número. Lo que se paga son unos megas vendoreados (ver `make
// deps-rostros`).
//
// NO SE GUARDA LA CARA. De cada persona queda un VECTOR de 128 números —el
// "embedding"— con el que se puede comparar, no reconstruir. Quien se lleve el
// archivo no se lleva las fotos de nadie. La foto, si se quiere una para la
// ficha, se guarda aparte y porque alguien lo pidió, no como efecto de esto.
//
// DOS PASOS, DOS MODELOS, a propósito:
//   1. DETECTAR dónde hay caras en el cuadro (YuNet, 228 KB). Devuelve además
//      cinco puntos —ojos, nariz, comisuras— que hacen falta para el paso 2.
//   2. DESCRIBIR una cara como números (SFace). Antes la endereza usando esos
//      cinco puntos: sin eso, la misma persona de perfil y de frente da
//      vectores distintos y no se reconoce a sí misma.
//
// COMPARAR ES UN COSENO. Dos vectores de la misma persona apuntan casi al
// mismo lado. `kMismaPersona()` es dónde se corta, y está elegido para errar
// hacia "no sé" antes que hacia "es fulano": confundir a dos personas en un
// rótulo al aire es peor que no reconocer a ninguna.
// ============================================================================
#pragma once

#include <cstdint>
#include "ajustes/Ajustes.hpp"
#include <memory>
#include <string>
#include <vector>

namespace ba::rostros {

/// Una cara encontrada en un cuadro.
struct Cara {
    int x = 0, y = 0, ancho = 0, alto = 0;
    float confianza = 0;
    /// Ojo der., ojo izq., nariz, comisura der., comisura izq. (x, y).
    float puntos[10] = {};
};

/// Cuánto se parecen dos vectores: 1 = idénticos, 0 = nada que ver.
float parecido(const std::vector<float>& a, const std::vector<float>& b);

/// Por encima de esto se considera la MISMA persona.
///
/// SFace da ~0.6 para la misma persona en condiciones normales. Se usa 0.45,
/// que es conservador: con menos entran parecidos de familia y gente con
/// anteojos parecidos. Confundir a dos personas en un rótulo al aire es peor
/// que no reconocer a ninguna, así que el error se paga del lado de "no sé".
inline float kMismaPersona() { return static_cast<float>(ba::ajustes::numero("rostros.mismaPersona", 0.45)); }

/// El motor: carga los dos modelos una vez y los reusa.
///
/// No lanza. `abrir` devuelve false y deja el motivo en `error()`; con el
/// motor cerrado, `detectar` y `describir` devuelven vacío.
class Motor {
   public:
    Motor();
    ~Motor();
    Motor(const Motor&) = delete;
    Motor& operator=(const Motor&) = delete;

    /// :param carpetaModelos: donde están `yunet.onnx` y `sface.onnx`.
    bool abrir(const std::string& carpetaModelos);
    bool abierto() const;
    const std::string& error() const;

    /// Las caras que hay en un cuadro BGRA (ancho*4 bytes por fila).
    ///
    /// :param minConfianza: descarta lo dudoso. Una cara mal detectada da un
    ///        vector que no sirve y ensucia la búsqueda.
    ///
    /// EL 0.7 SALE DE MEDIR, no del ejemplo del modelo. Con 0.9 —que es lo
    /// que trae la demo— una cara grande y de frente, al aire y perfectamente
    /// visible, daba 0.87 y se descartaba: el cuadro real llega escalado y
    /// recomprimido, no como la foto de prueba. Con 0.7 esa misma cara entra
    /// y un cuadro sin caras sigue dando cero.
    std::vector<Cara> detectar(const std::uint8_t* bgra, int ancho, int alto,
                               float minConfianza = 0.7f) const;

    /// El vector de 128 números de UNA cara ya detectada. Vacío si falla.
    std::vector<float> describir(const std::uint8_t* bgra, int ancho, int alto, const Cara& cara) const;

   private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace ba::rostros
