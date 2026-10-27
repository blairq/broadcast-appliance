// ============================================================================
// src/app/ui/CalidadCara.cpp — ver CalidadCara.hpp
// ============================================================================
#include "ui/CalidadCara.hpp"
#include "i18n/I18n.hpp"

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
    // Cada caso, dicho de dos formas: al operador («Pedile que…», en su idioma) y a la persona
    // misma (en segunda persona, en el idioma del evento: el tótem, el invitado).
    const char* k;
    const char* p;
    if (q.cortada) {
        k = QT_TRANSLATE_NOOP("caras", "Pedile que se ponga entero en el cuadro: la cara sale cortada por el borde.");
        p = QT_TRANSLATE_NOOP("caras-publico", "Ponete entero en el cuadro: la cara sale cortada.");
    } else if (q.altoPx < kAltoMinimo()) {
        k = QT_TRANSLATE_NOOP("caras", "Pedile que se acerque a la camara: la cara se ve demasiado chica.");
        p = QT_TRANSLATE_NOOP("caras-publico", "Acercate un poco a la cámara.");
    } else if (q.luz < kLuzMinima()) {
        k = QT_TRANSLATE_NOOP("caras", "Hay poca luz sobre la cara. Pedile que se ponga de frente a una "
                                       "ventana o que prenda una luz.");
        p = QT_TRANSLATE_NOOP("caras-publico", "Hay poca luz: ponete de frente a una ventana o a una luz.");
    } else if (q.luz > kLuzMaxima()) {
        k = QT_TRANSLATE_NOOP("caras", "La cara esta quemada de luz (contraluz). Pedile que se corra de "
                                       "la ventana o que la deje a su espalda.");
        p = QT_TRANSLATE_NOOP("caras-publico", "Hay mucha luz atrás: corrite un poco de la ventana.");
    } else if (std::abs(q.giro) > kGiroMaximo()) {
        k = QT_TRANSLATE_NOOP("caras", "Pedile que mire de frente a la camara: la cara esta girada.");
        p = QT_TRANSLATE_NOOP("caras-publico", "Mirá de frente a la cámara.");
    } else if (q.nitidez < kNitidezMinima()) {
        k = QT_TRANSLATE_NOOP("caras", "La imagen sale borrosa. Pedile que se quede quieto un momento o "
                                       "que revise el enfoque.");
        p = QT_TRANSLATE_NOOP("caras-publico", "Quedate quieto un momento.");
    } else {
        q.sirve = true;
        k = QT_TRANSLATE_NOOP("caras", "Asi sirve.");
        p = QT_TRANSLATE_NOOP("caras-publico", "Así está bien.");
    }
    q.sugerencia = ba::i18n::traducir("caras", k);
    q.sugerenciaEvento = ba::i18n::traducirEvento("caras-publico", p);
    return q;
}

} // namespace ba::app

#endif // BA_HAVE_ROSTROS
