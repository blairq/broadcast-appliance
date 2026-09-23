// ============================================================================
// src/core/qr/Iconos.hpp — los íconos que pueden ir en el centro de un QR
//
// Un set propio y chico, vendoreado como todo lo demás. No es un capricho de
// no usar una librería: un ícono que viene de una fuente del sistema se ve
// distinto en Windows que en Linux, o no se ve, y este aparate tiene que dar
// el mismo resultado en las tres plataformas. Acá son trazos, no letras.
//
// El formato es el de Lucide (MIT), que es de donde salen: un lienzo de 24x24
// y trazos sin relleno, de grosor 2 y puntas redondeadas. Eso permite que
// cualquier ícono de ese set se agregue pegando su `d`, sin tocar nada más.
//
// Se guardan POR NOMBRE con el código, no por su dibujo: así cambiar el ícono
// de un QR guardado es cambiar una palabra, y si algún día se mejora el
// dibujo, los códigos ya guardados lo heredan.
// ============================================================================
#pragma once

#include <string>
#include <vector>

namespace ba::qr {

struct Icono {
    std::string id;     ///< el nombre con el que se guarda ("wifi")
    std::string titulo; ///< como se lee en la interfaz ("Red WiFi")
    std::string trazos; ///< uno o varios caminos SVG, sobre un lienzo de 24x24
};

/// Todos los que hay, en orden de utilidad para un evento.
const std::vector<Icono>& iconos();
/// El de ese id, o nullptr.
const Icono* icono(const std::string& id);

} // namespace ba::qr
