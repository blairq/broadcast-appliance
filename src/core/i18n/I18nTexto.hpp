// ============================================================================
// src/core/i18n/I18nTexto.hpp — traducir un texto (lo que usa casi todo el programa)
//
// Lo mínimo para marcar y traducir textos, sin nada pesado: solo <string> e
// <initializer_list>. Lo incluyen cientos de archivos, así que acá no entra nada más
// (ni el perfil de un idioma, ni json, ni <filesystem>): eso está en I18nIdioma.hpp y en
// I18nPerfiles.hpp. El cuadro completo del sistema de idiomas, en I18n.cpp.
//
// Marcar un texto para traducir (lo encuentra `make i18n-actualizar`, con lupdate):
//   ba::i18n::traducir("contexto", "Texto en español")
//   ba::i18n::traducir("contexto", "Se conectó %n invitado", nullptr, n)   // plural
//   ba::i18n::llenar(ba::i18n::traducir("ctx", "Grabando %1 en %2"), {nombre, disco})
// Nunca una frase armada con pedazos: el orden de las palabras cambia con el idioma.
// ============================================================================
#pragma once

#include <initializer_list>
#include <string>

// Marca un texto de una tabla que se arma una vez sin traducirlo (lo encuentra lupdate) y se
// traduce al mostrarlo con traducir(contexto, texto). La misma definición que Qt: el código
// sin Qt la usa igual y no choca si después se incluye Qt.
#ifndef QT_TRANSLATE_NOOP
#define QT_TRANSLATE_NOOP(scope, x) x
#endif

namespace ba::i18n {

/// El texto en el idioma del operador. `desambiguacion` es la de lupdate (dos
/// textos iguales con sentidos distintos: «Salida» de audio y de la sala). Con
/// `n >= 0` elige la forma del plural y reemplaza %n por n.
std::string traducir(const char* contexto, const char* texto, const char* desambiguacion = nullptr, int n = -1);

/// El código del idioma del operador, el de traducir(): `actual().codigo` (I18nPerfiles.hpp)
/// sin traer el perfil entero.
std::string idiomaDelOperador();

/// El idioma del EVENTO abierto en este proceso: lo que ven y escuchan el público y los
/// invitados (gráficos, placas, tótems, voz, la web, el acta). Lo fija la app al abrir el
/// evento (y al cambiarle el idioma) y el gateway con el estado que le manda la app.
/// Vacío o sin fijar = el del operador.
void usarEnEvento(const std::string& codigo);
std::string idiomaDelEvento();
/// traducir() en el idioma del evento abierto (lupdate la reconoce igual):
/// `ba::i18n::traducirEvento("graficos", "Volvemos enseguida")`.
std::string traducirEvento(const char* contexto, const char* texto, const char* desambiguacion = nullptr, int n = -1);

/// Llena %1, %2… (hasta %9) con los valores, en el orden en que los diga la
/// traducción. Un %n que quedó se deja (lo llena traducir).
std::string llenar(const std::string& plantilla, std::initializer_list<std::string> valores);

} // namespace ba::i18n
