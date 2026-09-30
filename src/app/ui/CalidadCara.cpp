// ============================================================================
// src/app/ui/CalidadCara.cpp — ver CalidadCara.hpp
// ============================================================================
#include "ui/CalidadCara.hpp"

#if BA_HAVE_ROSTROS

#include <cmath>

namespace ba::app {

Calidad calidadDe(const QImage& img, const ba::rostros::Cara& c) {
    Calidad q;
    q.altoPx = c.alto;
    q.confianza = c.confianza;
    q.cortada = c.x <= 1 || c.y <= 1 || c.x + c.ancho >= img.width() - 1 || c.y + c.alto >= img.height() - 1;

    // Giro e inclinación, de los cinco puntos: ojo der., ojo izq., nariz.
    const double ojoDx = c.puntos[2] - c.puntos[0];
    const double ojoDy = c.puntos[3] - c.puntos[1];
    const double entreOjos = std::sqrt(ojoDx * ojoDx + ojoDy * ojoDy);
    if (entreOjos > 1) {
        const double medioX = (c.puntos[0] + c.puntos[2]) / 2.0;
        // La nariz corrida respecto del medio de los ojos: de frente cae en el
        // medio, de perfil se va para un lado. Dividido por la distancia entre
        // ojos para que no dependa de a qué distancia esté la persona.
        q.giro = (c.puntos[4] - medioX) / entreOjos;
        q.inclinacion = std::atan2(ojoDy, ojoDx) * 180.0 / 3.14159265;
    }

    const QRect r = QRect(c.x, c.y, c.ancho, c.alto).intersected(img.rect());
    if (r.width() >= 8 && r.height() >= 8) {
        const QImage cara = img.copy(r)
                                .scaled(112, 112, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                .convertToFormat(QImage::Format_Grayscale8);
        double suma = 0;
        double suma2 = 0;
        int n = 0;
        for (int y = 1; y < cara.height() - 1; ++y) {
            const uchar* f0 = cara.constScanLine(y - 1);
            const uchar* f1 = cara.constScanLine(y);
            const uchar* f2 = cara.constScanLine(y + 1);
            for (int x = 1; x < cara.width() - 1; ++x) {
                const double lap = 4.0 * f1[x] - f1[x - 1] - f1[x + 1] - f0[x] - f2[x];
                suma += lap;
                suma2 += lap * lap;
                q.luz += f1[x];
                ++n;
            }
        }
        if (n > 0) {
            const double media = suma / n;
            q.nitidez = suma2 / n - media * media;
            q.luz /= n;
        }
    }

    // El orden importa: se pide UNA cosa por vez, la más gruesa primero. Una
    // lista de cinco correcciones no la sigue nadie.
    if (q.cortada)
        q.sugerencia = "Pedile que se ponga entero en el cuadro: la cara sale cortada por el borde.";
    else if (q.altoPx < kAltoMinimo)
        q.sugerencia = "Pedile que se acerque a la camara: la cara se ve demasiado chica.";
    else if (q.luz < kLuzMinima)
        q.sugerencia = "Hay poca luz sobre la cara. Pedile que se ponga de frente a una ventana o "
                       "que prenda una luz.";
    else if (q.luz > kLuzMaxima)
        q.sugerencia = "La cara esta quemada de luz (contraluz). Pedile que se corra de la ventana "
                       "o que la deje a su espalda.";
    else if (std::abs(q.giro) > kGiroMaximo)
        q.sugerencia = "Pedile que mire de frente a la camara: la cara esta girada.";
    else if (q.nitidez < kNitidezMinima)
        q.sugerencia = "La imagen sale borrosa. Pedile que se quede quieto un momento o que revise "
                       "el enfoque.";
    else {
        q.sirve = true;
        q.sugerencia = "Asi sirve.";
    }
    return q;
}

} // namespace ba::app

#endif // BA_HAVE_ROSTROS
