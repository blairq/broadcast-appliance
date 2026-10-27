// SPDX-License-Identifier: GPL-2.0-or-later
//
// Los votos se guardan y se comparan como clave («afirmativo», «negativo»,
// «abstencion», «ausente», «no_participa»); al acta y a lo que leen los integrantes
// va su etiqueta, en el idioma del evento.
#pragma once

#include "i18n/I18n.hpp"

#include <string>

namespace ba::app::asamblea {

/// La etiqueta de un voto en el idioma del evento. Una clave que no conoce queda como vino.
inline std::string etiquetaDeVoto(const std::string& clave) {
    //: un voto, como va en la tabla nominal del acta (en minúsculas)
    if (clave == "afirmativo") return ba::i18n::traducirEvento("asamblea", "afirmativo");
    //: un voto, como va en la tabla nominal del acta (en minúsculas)
    if (clave == "negativo") return ba::i18n::traducirEvento("asamblea", "negativo");
    //: un voto, como va en la tabla nominal del acta (en minúsculas)
    if (clave == "abstencion") return ba::i18n::traducirEvento("asamblea", "abstención");
    //: un voto, como va en la tabla nominal del acta (en minúsculas)
    if (clave == "ausente") return ba::i18n::traducirEvento("asamblea", "ausente");
    //: un voto, como va en la tabla nominal del acta (en minúsculas)
    if (clave == "no_participa") return ba::i18n::traducirEvento("asamblea", "no participa");
    return clave;
}

} // namespace ba::app::asamblea
