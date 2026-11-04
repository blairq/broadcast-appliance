// ============================================================================
// src/core/i18n/I18n.hpp — los idiomas del sistema (docs/planes/PLAN-I18N.md)
//
// Un idioma es una carpeta de recursos, nunca código: su PERFIL
// (`assets/idiomas/<código>.json` → `data/idiomas/`) y su CATÁLOGO
// (`i18n/ba_<código>.ts` → `tools/i18n/ts-a-json.py` → `data/i18n/<código>.json`).
// Los idiomas son los perfiles que hay: sumar uno no toca este archivo.
//
// El texto ORIGINAL es el español con el que está escrito el código: lo que no
// tiene traducción en el idioma pedido (ni en sus respaldos) sale tal cual. Por eso
// el español no tiene catálogo.
//
// Lo usan los tres programas (launcher, ba-app, ba-gateway), sin Qt: el adaptador
// de Qt (`qsTr`, `QObject::tr`) está en la app (arranque/TraductorQt) y le pregunta
// a esto. La web pide los mismos catálogos al gateway (/comun/i18n/).
//
// Dos idiomas a la vez en el mismo proceso: el del OPERADOR (`usar`, `traducir`) y el
// del EVENTO o el del invitado (`En`), que puede ser otro.
//
// Este header es el PARAGUAS: junta las partes. Cada archivo incluye solo la que usa, así
// tocar una no recompila a todos (cada una trae la de arriba):
//   - I18nTexto.hpp     traducir, traducirEvento, llenar, QT_TRANSLATE_NOOP, el idioma del
//                       evento y el del operador (lo de casi todos; solo <string>);
//   - I18nIdioma.hpp    elegir el idioma del proceso (usar, resolver, idiomaDeLaMaquina,
//                       carpetas), En (otro idioma), los archivos de un idioma, los plurales
//                       y el pseudo-idioma;
//   - I18nPerfiles.hpp  Perfil, Cobertura, idiomas, perfil, cobertura, actual.
// tools/dev/migrar_includes.py elige la parte para cada archivo.
// ============================================================================
#pragma once

#include "i18n/I18nIdioma.hpp"
#include "i18n/I18nPerfiles.hpp"
#include "i18n/I18nTexto.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>
