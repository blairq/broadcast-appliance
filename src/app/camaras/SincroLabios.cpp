// ============================================================================
// src/app/camaras/SincroLabios.cpp — ver SincroLabios.hpp
// ============================================================================
#include "camaras/SincroLabios.hpp"

#include "i18n/I18n.hpp"

#include <algorithm>
#include <cmath>

namespace ba::app::camaras {

int mediana(std::vector<int> v) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    // Con cantidad par se promedia el par del medio, como corresponde.
    return n % 2 == 1 ? v[n / 2] : static_cast<int>((v[n / 2 - 1] + v[n / 2]) / 2);
}

namespace {

/// El cuartil `q` (0..1) de una lista YA ORDENADA.
int cuartil(const std::vector<int>& v, double q) {
    if (v.empty()) return 0;
    const auto i = static_cast<std::size_t>(q * static_cast<double>(v.size() - 1) + 0.5);
    return v[std::min(i, v.size() - 1)];
}

} // namespace

std::vector<Golpe> buscarGolpes(const std::vector<Muestra>& s, double sobrePiso, int ventanaMs,
                                int separacionMs) {
    std::vector<Golpe> out;
    if (s.size() < 3 || sobrePiso <= 0) return out;

    std::size_t inicio = 0; // primera muestra dentro de la ventana
    for (std::size_t i = 1; i < s.size(); ++i) {
        while (inicio < i && s[i].ms - s[inicio].ms > ventanaMs)
            ++inicio;
        // Hace falta algo de historia para saber que es "lo normal": con dos
        // muestras cualquier subida parece un golpe.
        if (i - inicio < 3) continue;

        // El piso es la MEDIANA de la ventana, no el promedio: el promedio lo
        // levanta el propio golpe anterior y entonces el siguiente no se ve.
        std::vector<double> ventana;
        ventana.reserve(i - inicio);
        for (std::size_t k = inicio; k < i; ++k)
            ventana.push_back(s[k].nivel);
        std::sort(ventana.begin(), ventana.end());
        const double piso = ventana[ventana.size() / 2];

        const double salto = s[i].nivel - piso;
        if (salto < sobrePiso) continue;
        // Solo el FLANCO de subida: mientras sigue subiendo no es el golpe
        // todavia, y una vez que baja ya paso.
        if (s[i].nivel < s[i - 1].nivel) continue;
        if (i + 1 < s.size() && s[i + 1].nivel > s[i].nivel) continue;

        if (!out.empty() && s[i].ms - out.back().ms < separacionMs) {
            // Es el mismo golpe (o su eco): queda el mas fuerte, que es el
            // directo, no el que rebota en la pared.
            if (salto > out.back().fuerza) out.back() = Golpe{s[i].ms, salto};
            continue;
        }
        out.push_back(Golpe{s[i].ms, salto});
    }
    return out;
}

Medicion medirRetardo(const std::vector<Golpe>& audio, const std::vector<Golpe>& video, int maxRetardoMs,
                      int dispersionMaxMs) {
    Medicion m;
    if (audio.empty() || video.empty()) {
        m.problema = audio.empty() ? ba::i18n::traducir("camaras", "no se escuchó ningún aplauso")
                                   : ba::i18n::traducir("camaras", "no se vio ningún aplauso");
        return m;
    }

    // A cada golpe del audio se le busca el golpe de video mas cercano DESPUES
    // (el video llega tarde, no antes). Cada golpe de video se usa una sola
    // vez: si no, dos aplausos juntos se emparejarian con el mismo cuadro.
    std::vector<int> retardos;
    std::vector<bool> usado(video.size(), false);
    for (const Golpe& a : audio) {
        std::size_t mejor = video.size();
        int64_t mejorDif = 0;
        for (std::size_t i = 0; i < video.size(); ++i) {
            if (usado[i]) continue;
            const int64_t dif = video[i].ms - a.ms;
            // Un margen chico hacia atras: el audio puede llegar unos pocos ms
            // despues que el video si la placa tiene mas buffer que la camara.
            if (dif < -100 || dif > maxRetardoMs) continue;
            if (mejor == video.size() || dif < mejorDif) {
                mejor = i;
                mejorDif = dif;
            }
        }
        if (mejor == video.size()) continue;
        usado[mejor] = true;
        retardos.push_back(static_cast<int>(mejorDif));
    }

    m.aplausos = static_cast<int>(retardos.size());
    if (retardos.empty()) {
        m.problema = ba::i18n::traducir("camaras", "se oyeron aplausos y se vieron aplausos, pero ninguno coincide: "
                                                   "¿la persona estaba frente a esta cámara?");
        return m;
    }

    m.ok = true;
    m.retardoMs = mediana(retardos);
    std::sort(retardos.begin(), retardos.end());
    // Mitad del rango intercuartil: no se la come un solo aplauso mal
    // detectado, que es justo lo que pasa cuando alguien tose.
    m.dispersionMs = (cuartil(retardos, 0.75) - cuartil(retardos, 0.25)) / 2;

    // Tres aplausos es el minimo para que la mediana signifique algo: con dos,
    // cualquiera de los dos manda.
    if (m.aplausos < 3) {
        m.problema = ba::i18n::traducir("camaras", "solo %n aplauso: pedí unos cuantos más antes de confiar en el número",
                                        nullptr, m.aplausos);
    } else if (m.dispersionMs > dispersionMaxMs) {
        m.problema = ba::i18n::llenar(ba::i18n::traducir("camaras", "las mediciones varían ±%1 ms entre sí: la cámara tiene "
                                                                    "jitter y este número es aproximado"),
                                      {std::to_string(m.dispersionMs)});
    } else {
        m.confiable = true;
    }
    return m;
}

} // namespace ba::app::camaras
