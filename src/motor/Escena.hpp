// ============================================================================
// src/motor/Escena.hpp — una escena del motor: sus capas (qué entrada, dónde, cómo)
//
// Los valores que la app le da al motor (Motor::definirEscena, las escenas internas) y que
// arma una Composicion (Composicion.hpp: cómo se componen, las transiciones). Aparte de
// Composicion.hpp para que quien solo habla de escenas (Motor.hpp, la app) no traiga
// GStreamer ni el mezclador. Lo que hacen (desdeJson, aJson, capaEn…) está en Composicion.cpp.
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace ba::motor {

/// Sombra (o resplandor) y borde difuso de una capa, en píxeles del lienzo (ver basombra).
struct SombraCapa {
    double alfa = 0;            ///< opacidad de la sombra (0 = sin sombra)
    std::uint32_t color = 0xFF000000; ///< ARGB
    double difuminado = 20, distancia = 10, angulo = 45, tamano = 0;
    double borde = 0;           ///< borde difuso de la imagen (aparte de la sombra)
    double radio = 0;           ///< esquinas redondeadas
    bool hace() const { return alfa > 0 || borde > 0 || radio > 0; }
    /// Lo que se agranda la caja de cada lado para que la sombra entre (de a 8: que
    /// mover un deslizador no renegocie en cada paso).
    int margen() const;
    bool operator==(const SombraCapa&) const = default;
    static SombraCapa desdeJson(const nlohmann::json& j);
    nlohmann::json aJson() const;
};

/// Un punto de la animación de una capa (Capa::claves): a los `ms` la capa está ahí y así.
/// Entre dos claves, lo de en medio (con `suave`, arrancando y frenando despacio).
struct ClaveCapa {
    int ms = 0;
    double x = 0, y = 0, ancho = 0, alto = 0; ///< en el lienzo (ancho/alto 0 = el del lienzo)
    double alfa = 1.0;
    double recIzq = 0, recArr = 0, recDer = 0, recAba = 0; ///< recorte, en píxeles de la entrada
    bool suave = true; ///< el tramo que LLEGA a esta clave
    bool operator==(const ClaveCapa&) const = default;
};

struct Capa {
    std::string entrada;           ///< la entrada del motor (lee <entrada>:video); también el nombre de la capa
    int x = 0, y = 0;
    int ancho = 0, alto = 0;       ///< 0 = el del lienzo
    double alfa = 1.0;
    int orden = 0;                 ///< más alto = más arriba
    int recIzq = 0, recArr = 0, recDer = 0, recAba = 0; ///< recorte en píxeles de la entrada
    bool visible = true;
    /// true: la imagen entra en su recuadro sin deformarse, centrada (como hacía OBS);
    /// false: se estira al recuadro.
    bool contener = false;
    std::string productor;         ///< si no está vacío, lee este productor (p. ej. «video:programa» en el multiview)
    SombraCapa sombra;
    /// Cómo se la reconoce en la escena (su rama y sus elementos): la entrada; si la
    /// misma entrada está más de una vez (dos bancas de una cámara recortada distinto),
    /// «entrada#2», «entrada#3»... (Escena::desdeJson). Vacía = la entrada.
    std::string clave;
    const std::string& claveCapa() const { return clave.empty() ? entrada : clave; }
    /// Con otro mezclador (Escena::mezclador): el pad al que va («sink_1», «video_sink»;
    /// vacío = uno nuevo a pedido) y sus propiedades ({"xpos": 10, "alpha": 0.5}).
    std::string pad;
    nlohmann::json propiedadesPad = nlohmann::json::object();
    /// ANIMADA POR CLAVES: la capa va de clave en clave (ordenadas por ms) con el reloj del
    /// motor, y pasada la última queda ahí. `clavesDesdeNs` es el ms 0, en el reloj monótono
    /// (g_get_monotonic_time en ns: el de la app y el del motor son el mismo); 0 = todavía no
    /// arrancó (el gráfico no dio su primer cuadro): queda en la primera clave.
    std::vector<ClaveCapa> claves;
    std::int64_t clavesDesdeNs = 0;
};

/// La capa a los `ms` de su animación (Capa::claves): dónde, de qué tamaño, con qué alfa y
/// recorte; lo demás, como está. Sin claves, la misma.
Capa capaEn(const Capa& c, double ms);

struct Escena {
    std::string nombre;
    std::vector<Capa> capas;
    /// Solo las escenas internas (un compositor, la Cámara 360): un lienzo propio
    /// (0 = el del estudio) y filtros después del mezclador (la lista de Piezas:
    /// [{id, elemento, propiedades}]; en la GPU, con su par de GL o D3D12).
    int lienzoAncho = 0, lienzoAlto = 0;
    nlohmann::json filtros = nlohmann::json::array();
    /// Solo las internas: otro módulo de varias entradas en vez del mezclador de siempre
    /// ({"elemento", "propiedades"}; vacío = el de siempre). Ver Capa::pad.
    nlohmann::json mezclador = nlohmann::json::object();
    static Escena desdeJson(const nlohmann::json& j);
    /// Una clave distinta para cada capa (Capa::clave): la misma entrada dos veces, o una
    /// entrada que se llama justo como la clave de otra («cam#2»). desdeJson ya la llama;
    /// quien arma una Escena a mano, también.
    void asignarClaves();
    nlohmann::json aJson() const;
};

} // namespace ba::motor
