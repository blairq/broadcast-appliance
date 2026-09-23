// ============================================================================
// src/app/ui/CalidadCara.hpp — ¿esta cara sirve para reconocer? ¿y si no, qué?
//
// PARA QUE SIRVE: para poder PEDIR algo concreto. «No te reconozco» no ayuda a
// nadie; «acercate un poco» o «mirá a la cámara» sí. Con alguien que entra por
// WebRTC —del otro lado de una cámara que no manejamos, con la luz de su
// casa—, esto es lo que convierte un fracaso en una instrucción.
//
// Lo usan las herramientas MCP del alta guiada y la baldosa del lobby, que
// muestra lo mismo sin que nadie pregunte.
// ============================================================================
#pragma once

#if BA_HAVE_ROSTROS

#include <string>

#include <QImage>

#include "rostros/Rostros.hpp"

namespace ba::app {

// Lo que se mide, y por qué así.
//
// PARA QUE SIRVE: para poder PEDIR algo concreto. «No te reconozco» no ayuda a
// nadie; «acercate un poco» o «mirá a la cámara» sí. Con un invitado que entra
// por WebRTC, el asistente mira esto y le dice qué hacer hasta que la cara
// sirva, y recién ahí la guarda.
//
// LA NITIDEZ SE MIDE AL TAMAÑO QUE VE EL MODELO (112 px), no al tamaño que
// tiene en el cuadro. Medida en crudo, una cara enorme y borrosa da más
// detalle que una chiquita y perfecta, que es al revés de lo que importa: lo
// que cuenta es cuánto detalle le llega al modelo.
struct Calidad {
    int altoPx = 0;
    double nitidez = 0;   ///< varianza del laplaciano, a 112 px
    double luz = 0;       ///< brillo medio 0..255
    double giro = 0;      ///< 0 de frente; |giro| > 0.25 ya está girada
    double inclinacion = 0; ///< grados de la línea de los ojos
    bool cortada = false; ///< el recuadro toca el borde del cuadro
    float confianza = 0;
    std::string sugerencia; ///< qué pedirle, en palabras que se le puedan decir
    bool sirve = false;
};

/// Lo que se le pide al modelo de mínima. Abajo de esto el vector sale pobre y
/// el reconocimiento se vuelve una lotería (medido: caras de menos de ~80 px
/// dan parecidos de 0.5 contra sí mismas).
constexpr int kAltoMinimo = 80;
constexpr double kNitidezMinima = 35.0;
constexpr double kLuzMinima = 45.0;
constexpr double kLuzMaxima = 218.0;
constexpr double kGiroMaximo = 0.28;

Calidad calidadDe(const QImage& img, const ba::rostros::Cara& c);

} // namespace ba::app

#endif // BA_HAVE_ROSTROS
