// src/app/video/CatalogoGst.hpp — los módulos de GStreamer que hay, y sus propiedades
//
// Le pregunta al registro de GStreamer (el del bundle: ba::platform::prepararGst)
// qué módulos de VIDEO hay, y arma el catálogo del botón «Agregar módulo»:
// nombre del elemento, nombre largo, clase («Filter/Effect/Video»…),
// descripción, plugin, paquete (base/good/bad/ugly/libav/rs) y licencia. La
// documentación en castellano (para qué sirve, uso en vivo) sale de
// datos/gst-modulos.json; lo que no está ahí muestra la descripción original.
//
// Las propiedades de un módulo salen de GObject (introspección): tipo, rango,
// valor de fábrica, opciones de un enum o flags, y si se pueden cambiar con el
// pipeline andando. Con eso la interfaz arma el formulario de cada tarjeta.
//
// Sin GStreamer en el bundle (BA_HAVE_GST apagado o el framework ausente),
// `disponible()` es false y el catálogo está vacío.
#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>

namespace ba::app::video {

class CatalogoGst {
   public:
    /// Inicia GStreamer (una vez) y lee la documentación de `docs`
    /// (datos/gst-modulos.json; puede no estar).
    /// Uno vacío mientras el de verdad se arma (`error()` lo explica).
    CatalogoGst();
    explicit CatalogoGst(const std::filesystem::path& docs);
    /// Lo mismo, con la documentación ya leída ({"modulos": {…}}).
    explicit CatalogoGst(const nlohmann::json& docs);

    bool disponible() const { return disponible_; }
    const std::string& error() const { return error_; }
    std::string version() const;

    /// Los módulos de video: [{elemento, nombre, clase, rol (filtro | fuente |
    /// mezcla | salida | analisis | conversor), categoria, descripcion,
    /// titulo?, para?, uso?, tarjeta, plugin, paquete, licencia}], por
    /// categoría y nombre. `tarjeta`: si va como tarjeta de filtro (imagen
    /// cruda entra y sale). Se arma una vez (el registro no cambia).
    const nlohmann::json& modulos() const;
    /// Uno (o {} si no está).
    nlohmann::json modulo(const std::string& elemento) const;
    /// Las propiedades que se pueden ajustar: [{nombre, tipo (bool | int |
    /// double | string | enum | flags), descripcion, min, max, defecto, valor,
    /// opciones:[{valor, nick, descripcion}], enVivo}]. `actuales` pisa los
    /// valores de fábrica.
    nlohmann::json propiedades(const std::string& elemento, const nlohmann::json& actuales) const;

   private:
    void iniciar();

    bool disponible_ = false;
    mutable nlohmann::json modulos_;
    std::string error_;
    nlohmann::json docs_ = nlohmann::json::object();
};

} // namespace ba::app::video
