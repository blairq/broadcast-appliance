// ============================================================================
// src/app/carrusel/Carrusel.hpp — una tira de imágenes en una escena
//
// El caso real: un orador manda su presentación y en medio de la charla dice
// «volvé a la del gráfico de barras». Con veinte diapositivas y un botón de
// siguiente, eso son veinte clics y diez segundos de aire perdido.
//
// EL CARRUSEL ES NUESTRO, Y NO EL DE libobs. La fuente `slideshow_v2` existe y
// rota sola, pero NO SE PUEDE CONTROLAR: se le pidieron sus propiedades al
// aparato y son `bool`, `int` y `list` —ningún botón—, y su propio texto dice
// «Manual (Use hotkeys to control slideshow)». Los atajos no están expuestos.
//
// Así que la escena lleva un `image_source` común y el aparato le cambia el
// archivo, que eso SÍ anda en vivo. De ahí sale lo único que importa: saltar a
// cualquier imagen al instante.
//
// ESTE ARCHIVO NO DIBUJA NI TOCA NINGUNA ESCENA. Dice qué imagen toca y cuándo;
// quien sabe ponerla vive arriba. Esa costura es la que permite probarlo entero
// sin prender una cámara.
// ============================================================================
#pragma once

#include <cstdint>
#include "ajustes/Ajustes.hpp"
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ba::carrusel {

/// Una imagen de la tira.
struct Lamina {
    /// Ruta del archivo. Es la identidad: dos láminas con la misma ruta son la
    /// misma, y es lo que se le pasa a la fuente de la escena.
    std::string archivo;
    /// Cómo llamarla en la grilla. Vacío = el nombre del archivo.
    std::string titulo;
    /// Lo que dijo de ella el que mira imágenes, para poder buscarla. Se
    /// calcula UNA vez (ver `analizada`) y viaja con el carrusel.
    std::string etiquetas;
    /// true cuando ya se le pidieron las etiquetas, aunque hayan salido
    /// vacías: sin esto, una imagen que no dio nada se reintenta para siempre.
    bool analizada = false;
};

class Carrusel {
   public:
    /// Cuántas láminas se admiten. Una presentación larga tiene cincuenta; mil
    /// es que alguien apuntó a la carpeta equivocada.
    static std::size_t kMax() { return static_cast<std::size_t>(ba::ajustes::entero("limites.carrusel", 500)); }

    const std::vector<Lamina>& laminas() const { return laminas_; }
    std::size_t tamano() const { return laminas_.size(); }
    bool vacio() const { return laminas_.empty(); }

    /// Dónde está parado. -1 si está vacío.
    int actual() const { return laminas_.empty() ? -1 : actual_; }
    /// La lámina de ahora, o nullptr.
    const Lamina* lamina() const;

    /// Agrega al final. Una ruta repetida NO entra dos veces: la misma imagen
    /// dos veces en la tira es casi siempre un descuido al armarla.
    /// :returns: false si ya estaba o si no entra.
    bool agregar(Lamina l);
    /// Saca una por su posición. Si era la actual, queda parado en la que
    /// ocupó su lugar.
    bool quitar(std::size_t i);
    /// La mueve de lugar (arrastrar para reordenar).
    bool mover(std::size_t de, std::size_t a);
    void vaciar();

    /// Salta a una posición. Es lo que hace el operador tocando una tarjeta, y
    /// la razón de que este carrusel exista.
    bool ir(int i);
    /// La que sigue. Da la vuelta al llegar al final.
    void siguiente();
    void anterior();

    /// Cada cuántos milisegundos avanza solo. 0 = no avanza (lo pasa alguien).
    int avanceMs() const { return avanceMs_; }
    void setAvanceMs(int ms);

    /// Guarda las etiquetas de una lámina y la marca analizada.
    bool etiquetar(const std::string& archivo, const std::string& etiquetas);
    /// La primera sin analizar, o nullptr. Es por donde va el que las pide.
    const Lamina* proximaSinAnalizar() const;

    /// El nombre de la fuente de la escena que muestra esta tira.
    const std::string& fuente() const { return fuente_; }
    void setFuente(std::string f) { fuente_ = std::move(f); }

    const std::string& nombre() const { return nombre_; }
    void setNombre(std::string n) { nombre_ = std::move(n); }

    nlohmann::json aJson() const;
    static Carrusel deJson(const nlohmann::json& j);

   private:
    std::string nombre_;
    std::string fuente_;
    std::vector<Lamina> laminas_;
    int actual_ = 0;
    int avanceMs_ = 0;
};

} // namespace ba::carrusel
